// PocketPlug - V.R. Technology VT1682 core
// SPDX-License-Identifier: BSD-3-Clause
//
// Standalone port of MAME's VT1682 driver and its helper devices
// (vt1682.cpp, vt1682_io.cpp, vt1682_uio.cpp, vt1682_alu.cpp, vt1682_timer.cpp),
// original code copyright David Haywood, BSD-3-Clause. Register layout notes,
// banking tables and rendering logic follow the MAME implementation; the
// scheduling, memory maps and device glue are rewritten for this app.

#include "vt1682.h"
#include <cstring>
#include <cmath>
#include <cstdio>
#include <cstdlib>

static uint8_t bus_main_r(void* c, uint16_t a) { return static_cast<Vt1682Core*>(c)->mainRead(a); }
static void bus_main_w(void* c, uint16_t a, uint8_t d) { static_cast<Vt1682Core*>(c)->mainWrite(a, d); }
static uint8_t bus_sound_r(void* c, uint16_t a) { return static_cast<Vt1682Core*>(c)->soundRead(a); }
static void bus_sound_w(void* c, uint16_t a, uint8_t d) { static_cast<Vt1682Core*>(c)->soundWrite(a, d); }

static inline uint8_t bitswap_d2_d7(uint8_t v) { uint8_t b2 = (v >> 2) & 1, b7 = (v >> 7) & 1; return (v & ~0x84) | (b2 << 7) | (b7 << 2); }
static inline uint8_t bitswap_d5_d6(uint8_t v) { uint8_t b5 = (v >> 5) & 1, b6 = (v >> 6) & 1; return (v & ~0x60) | (b5 << 6) | (b6 << 5); }

Vt1682Core::Vt1682Core(const Vt1682Config& cfg) : m_cfg(cfg)
{
    if (cfg.pal) {
        m_master = 26601712.0; m_mainDiv = 5; m_refresh = 50.0070; m_lines = 312;
    } else {
        m_master = 21477272.0; m_mainDiv = 4; m_refresh = 60.0; m_lines = 262;
    }
    m_lineLen = m_master / m_refresh / m_lines;
    m_sampleStep = m_master / 44100.0;

    for (int i = 0; i < 256; i++) {
        uint8_t v = (uint8_t)i;
        if (cfg.opcodeSwap == 1) v = bitswap_d2_d7(v);
        else if (cfg.opcodeSwap == 2) v = bitswap_d5_d6(v);
        m_optable[i] = v;
    }
    m_maincpu.setBus(this, bus_main_r, bus_main_w);
    if (cfg.opcodeSwap) m_maincpu.setOpcodeTable(m_optable);
    m_soundcpu.setBus(this, bus_sound_r, bus_sound_w);
    memset(m_frame, 0, sizeof(m_frame));
}

bool Vt1682Core::load(const std::vector<RomFile>& roms, std::string& error)
{
    const RomFile* rf = nullptr;
    for (auto& r : roms) if (r.name == m_cfg.romName) rf = &r;
    if (!rf && roms.size() == 1) rf = &roms[0];
    if (!rf) { error = "Missing " + m_cfg.romName; return false; }
    if (rf->data.empty()) { error = "ROM is empty"; return false; }

    // ROM region is 0x2000000 bytes; mirror smaller dumps across it
    m_rom.assign(0x2000000, 0);
    size_t n = rf->data.size();
    for (size_t off = 0; off < 0x2000000; off += n)
        memcpy(&m_rom[off], rf->data.data(), std::min(n, (size_t)0x2000000 - off));
    m_romMask = 0x1ffffff;
    reset();
    return true;
}

uint8_t Vt1682Core::rand8()
{
    m_rng = m_rng * 1103515245u + 12345u;
    return (uint8_t)(m_rng >> 16);
}

/************************************************************************
 Reset
************************************************************************/

void Vt1682Core::resetState()
{
    memset(m_mainram, 0, sizeof(m_mainram));
    memset(m_soundShare, 0, sizeof(m_soundShare));
    memset(m_spriteram, 0, sizeof(m_spriteram));
    memset(m_vram, 0, sizeof(m_vram));

    m_2000 = m_2001 = 0;
    m_2002_sprramaddr_2_0 = m_2003_sprramaddr_10_3 = 0;
    m_2005_vramaddr_7_0 = m_2006_vramaddr_15_8 = 0;
    m_201a_sp_segment_7_0 = m_201b_sp_segment_11_8 = 0;
    for (int i = 0; i < 2; i++) {
        m_segment_7_0_bk[i] = m_segment_11_8_bk[i] = 0;
        m_main_control_bk[i] = m_scroll_control_bk[i] = 0;
        m_xscroll_7_0_bk[i] = m_yscroll_7_0_bk[i] = 0;
    }
    m_200e_blend_pal_sel = m_200f_bk_pal_sel = 0;
    m_2008_lcd_vs_delay = m_2009_lcd_hs_delay_7_0 = m_200a_lcd_fr_delay_7_0 = 0;
    m_200d_misc_vregs2 = m_200c_misc_vregs1 = m_200b_misc_vregs0 = 0;
    m_2018_spregs = m_2019_bkgain = 0;
    m_2020_bk_linescroll = m_2021_lum_offset = m_2022_saturation_misc = 0;
    m_2023_lightgun_reset = m_2024_lightgun1_y = m_2025_lightgun1_x = m_2026_lightgun2_y = m_2027_lightgun2_x = 0;
    m_2031_red_dac = m_2032_green_dac = m_2033_blue_dac = 0;
    m_2028 = m_2029 = m_202a = m_202b = m_202e = m_2030 = 0;

    m_scu_to_main_irq_active = 0;
    m_timera_to_main_irq_active = 0;
    m_current_main_vector = 0xfffe;
    m_new_main_vector = 0xfffe;
    m_timera_to_sound_irq_active = 0;
    m_timerb_to_sound_irq_active = 0;
    m_current_sound_vector = 0x0ff8;
    m_new_sound_vector = 0x0ff8;

    m_prgbank1_r0 = m_prgbank1_r1 = m_210c_prgbank1_r2 = m_2100_prgbank1_r3 = m_2118_prgbank1_r4_r5 = 0;
    m_2107_prgbank0_r0 = 0x3f;
    m_2108_prgbank0_r1 = m_2109_prgbank0_r2 = m_210a_prgbank0_r3 = m_prgbank0_r4 = m_prgbank0_r5 = 0;
    m_210b_misc_cs_prg0_bank_sel = 0;
    m_2105_vt1682_2105_comr6_tvmodes = 0;
    m_211c_regs_ext2421 = 0;
    m_2122_dma_dt_addr_7_0 = m_2123_dma_dt_addr_15_8 = 0;
    m_2124_dma_sr_addr_7_0 = m_2125_dma_sr_addr_15_8 = 0;
    m_2126_dma_sr_bank_addr_22_15 = m_2128_dma_sr_bank_addr_24_23 = 0;
    m_2106_enable_reg = 0;

    m_soundcpu_2118_dacleft_7_0 = m_soundcpu_2119_dacleft_15_8 = 0;
    m_soundcpu_211a_dacright_7_0 = m_soundcpu_211b_dacright_15_8 = 0;
    m_dacLeft = m_dacRight = 0;

    memset(m_alu, 0, sizeof(m_alu));

    double mainClock = m_master / m_mainDiv;
    for (int i = 0; i < 3; i++) {
        m_timer[i].preload_7_0 = m_timer[i].preload_15_8 = m_timer[i].enable = 0;
        m_timer[i].period = 0;
        m_timer[i].next = 0;
        m_timer[i].clock = (i == 2) ? mainClock : m_master;
    }

    m_210d_ioconfig = m_211e_adconfig = m_214c_ioefconfig = 0;
    m_io_ab_o = m_io_cd_o = m_io_ef_o = 0;
    m_2129_uio_a_data = m_212a_uio_a_direction = m_212b_uio_a_attribute = 0;
    m_2149_uio_b_data = m_214a_uio_b_direction = m_214b_uio_b_attribute = 0;
}

void Vt1682Core::reset()
{
    resetState();
    m_mainTime = m_soundTime = m_now = 0;
    m_nextSample = 0;
    m_vpos = 0;
    m_lineStart = 0;
    m_audio.clear();

    m_maincpu.a = m_maincpu.x = m_maincpu.y = 0; m_maincpu.s = 0; m_maincpu.p = 0x24;
    m_maincpu.setIrq(false);
    m_maincpu.reset();

    m_soundResetLine = 1;
    m_scpu_is_in_reset = 1;
    m_soundcpu.setIrq(false);

    update_main_interrupts();
    update_sound_interrupts();
}

/************************************************************************
 Address translation (see MAME driver for the full tables)
************************************************************************/

uint8_t Vt1682Core::translate_prg0select(uint8_t tp20_tp13)
{
    uint8_t bank = m_210b_misc_cs_prg0_bank_sel & 0x07;
    uint8_t ret = 0x00;
    switch (bank) {
    case 0x0: ret = (m_210a_prgbank0_r3 & 0xc0) | (tp20_tp13 & 0x3f); break;
    case 0x1: ret = (m_210a_prgbank0_r3 & 0xe0) | (tp20_tp13 & 0x1f); break;
    case 0x2: ret = (m_210a_prgbank0_r3 & 0xf0) | (tp20_tp13 & 0x0f); break;
    case 0x3: ret = (m_210a_prgbank0_r3 & 0xf8) | (tp20_tp13 & 0x07); break;
    case 0x4: ret = (m_210a_prgbank0_r3 & 0xfc) | (tp20_tp13 & 0x03); break;
    case 0x5: ret = (m_210a_prgbank0_r3 & 0xfe) | (tp20_tp13 & 0x01); break;
    case 0x6: ret = m_210a_prgbank0_r3; break;
    case 0x7: ret = tp20_tp13; break;
    }
    return ret;
}

uint32_t Vt1682Core::translate_address_4000_to_7fff(uint16_t address)
{
    uint8_t prgbank1_r4 = (m_2118_prgbank1_r4_r5 & 0x0f);
    uint8_t prgbank1_r5 = (m_2118_prgbank1_r4_r5 & 0xf0) >> 4;
    int tp20_tp13 = 0, pa24_pa21 = 0;
    switch (address & 0x6000) {
    case 0x4000: tp20_tp13 = m_prgbank0_r4; pa24_pa21 = prgbank1_r4; break;
    case 0x6000: tp20_tp13 = m_prgbank0_r5; pa24_pa21 = prgbank1_r5; break;
    default: break;
    }
    int pa20_pa13 = translate_prg0select(tp20_tp13);
    uint32_t realaddress = address & 0x1fff;
    realaddress |= pa20_pa13 << 13;
    realaddress |= pa24_pa21 << 21;
    return realaddress;
}

uint32_t Vt1682Core::translate_address_8000_to_ffff(uint16_t address)
{
    int tp20_tp13 = 0, pa24_pa21 = 0;
    const int pq2en = (m_210b_misc_cs_prg0_bank_sel & 0x40) >> 6;
    const int comr6 = (m_2105_vt1682_2105_comr6_tvmodes & 0x40) >> 6;
    const int a14_a13 = (address & 0x6000) >> 13;
    const int lookup = a14_a13 | (comr6 << 2) | (pq2en << 3);

    switch (lookup) {
    case 0x0: tp20_tp13 = m_2107_prgbank0_r0; pa24_pa21 = m_prgbank1_r0; break;
    case 0x1: tp20_tp13 = m_2108_prgbank0_r1; pa24_pa21 = m_prgbank1_r1; break;
    case 0x2: tp20_tp13 = 0xfe;               pa24_pa21 = m_2100_prgbank1_r3; break;
    case 0x3: tp20_tp13 = 0xff;               pa24_pa21 = m_2100_prgbank1_r3; break;
    case 0x4: tp20_tp13 = 0xfe;               pa24_pa21 = m_2100_prgbank1_r3; break;
    case 0x5: tp20_tp13 = m_2108_prgbank0_r1; pa24_pa21 = m_prgbank1_r1; break;
    case 0x6: tp20_tp13 = m_2107_prgbank0_r0; pa24_pa21 = m_prgbank1_r0; break;
    case 0x7: tp20_tp13 = 0xff;               pa24_pa21 = m_2100_prgbank1_r3; break;
    case 0x8: tp20_tp13 = m_2107_prgbank0_r0; pa24_pa21 = m_prgbank1_r0; break;
    case 0x9: tp20_tp13 = m_2108_prgbank0_r1; pa24_pa21 = m_prgbank1_r1; break;
    case 0xa: tp20_tp13 = m_2109_prgbank0_r2; pa24_pa21 = m_210c_prgbank1_r2; break;
    case 0xb: tp20_tp13 = 0xff;               pa24_pa21 = m_2100_prgbank1_r3; break;
    case 0xc: tp20_tp13 = m_2109_prgbank0_r2; pa24_pa21 = m_210c_prgbank1_r2; break;
    case 0xd: tp20_tp13 = m_2108_prgbank0_r1; pa24_pa21 = m_prgbank1_r1; break;
    case 0xe: tp20_tp13 = m_2107_prgbank0_r0; pa24_pa21 = m_prgbank1_r0; break;
    case 0xf: tp20_tp13 = 0xff;               pa24_pa21 = m_2100_prgbank1_r3; break;
    }

    const int ext2421 = (m_211c_regs_ext2421 & 0x20) >> 5;
    if (ext2421)
        pa24_pa21 = m_2100_prgbank1_r3;

    const int pa20_pa13 = translate_prg0select(tp20_tp13);
    uint32_t realaddress = address & 0x1fff;
    realaddress |= pa20_pa13 << 13;
    realaddress |= (pa24_pa21 & 0x0f) << 21;
    return realaddress;
}

/************************************************************************
 Interrupts
************************************************************************/

void Vt1682Core::update_main_interrupts()
{
    if (m_timera_to_main_irq_active || m_scu_to_main_irq_active) {
        if (m_timera_to_main_irq_active) m_new_main_vector = 0xfff8;
        else m_new_main_vector = 0xfff6;
        m_maincpu.setIrq(true);
    } else {
        m_new_main_vector = 0xfffe;
        m_maincpu.setIrq(false);
    }
}

void Vt1682Core::update_sound_interrupts()
{
    if (m_timera_to_sound_irq_active || m_timerb_to_sound_irq_active) {
        if (m_timera_to_sound_irq_active) m_new_sound_vector = 0x0ff8;
        else m_new_sound_vector = 0x0ff6;
        m_soundcpu.setIrq(true);
    } else {
        m_new_sound_vector = 0x0ffe;
        m_soundcpu.setIrq(false);
    }
}

void Vt1682Core::timerIrq(int i, bool state)
{
    switch (i) {
    case 0: m_timera_to_sound_irq_active = (state && !m_scpu_is_in_reset) ? 1 : 0; update_sound_interrupts(); break;
    case 1: m_timerb_to_sound_irq_active = (state && !m_scpu_is_in_reset) ? 1 : 0; update_sound_interrupts(); break;
    case 2: m_timera_to_main_irq_active = state ? 1 : 0; update_main_interrupts(); break;
    }
}

/************************************************************************
 Timers (vt1682_timer.cpp)
************************************************************************/

void Vt1682Core::timerUpdate(int i)
{
    Timer& t = m_timer[i];
    if (t.enable & 0x01) {
        uint16_t preload = (t.preload_15_8 << 8) | t.preload_7_0;
        int realpreload = 65536 - preload;
        t.period = realpreload * (m_master / t.clock);
        t.next = m_now + t.period;
    }
}

void Vt1682Core::timerEnableW(int i, uint8_t data)
{
    m_timer[i].enable = data;
    if (data & 0x01) timerUpdate(i);
    else m_timer[i].period = 0;
}

/************************************************************************
 Sound CPU reset control
************************************************************************/

void Vt1682Core::soundcpuSetReset(bool asserted)
{
    if (asserted) {
        m_soundResetLine = 1;
    } else if (m_soundResetLine) {
        m_soundResetLine = 0;
        m_soundTime = m_now;
        m_soundcpu.reset();
    }
}

void Vt1682Core::clear_sound_reset_line()
{
    soundcpuSetReset(false);
    m_scpu_is_in_reset = 0;
}

void Vt1682Core::set_2106(uint8_t data)
{
    m_2106_enable_reg = data;
    if (data & 0x20) {
        if (m_scpu_is_in_reset) clear_sound_reset_line();
    } else {
        soundcpuSetReset(true);
        m_scpu_is_in_reset = 1;
    }
}

void Vt1682Core::set_210b(uint8_t data)
{
    m_210b_misc_cs_prg0_bank_sel = data;
    // system timer can be switched to an alternate (line-rate) clock
    if (data & 0x80) m_timer[2].clock = m_cfg.pal ? 15602.0 : 15746.0;
    else m_timer[2].clock = m_master / m_mainDiv;
}

/************************************************************************
 DMA
************************************************************************/

void Vt1682Core::set_dma_sr_addr(uint16_t addr)
{
    addr &= 0x7fff;
    m_2124_dma_sr_addr_7_0 = addr & 0xff;
    m_2125_dma_sr_addr_15_8 = (m_2125_dma_sr_addr_15_8 & 0x80) | (addr >> 8);
}

void Vt1682Core::set_dma_dt_addr(uint16_t addr)
{
    m_2122_dma_dt_addr_7_0 = addr & 0xff;
    m_2123_dma_dt_addr_15_8 = (m_2123_dma_dt_addr_15_8 & 0x80) | (addr >> 8);
}

bool Vt1682Core::get_dma_dt_is_video()
{
    if (get_dma_dt_isext()) return false;
    uint16_t a = get_dma_dt_addr();
    return a == 0x2004 || a == 0x2007;
}

void Vt1682Core::do_dma_external_to_internal(int data, bool is_video)
{
    int count = data * 2;
    if (count == 0) count = 0x200;
    int srcbank = get_dma_sr_bank_ddr();
    for (int i = 0; i < count; i++) {
        int srcaddr = get_dma_sr_addr();
        uint16_t dstaddr = get_dma_dt_addr();
        uint8_t dat = romRead(srcaddr | (srcbank << 15));
        srcaddr++;
        mainWrite(dstaddr, dat);
        if (!is_video) dstaddr++;
        set_dma_dt_addr(dstaddr);
        set_dma_sr_addr(srcaddr);
    }
}

void Vt1682Core::do_dma_internal_to_internal(int data, bool is_video)
{
    int count = data * 2;
    if (count == 0) count = 0x200;
    for (int i = 0; i < count; i++) {
        uint16_t dstaddr = get_dma_dt_addr();
        int srcaddr = get_dma_sr_addr();
        uint8_t dat = mainRead(srcaddr);
        srcaddr++;
        mainWrite(dstaddr, dat);
        if (!is_video) dstaddr++;
        set_dma_dt_addr(dstaddr);
        set_dma_sr_addr(srcaddr);
    }
}

void Vt1682Core::dma_trigger(uint8_t data)
{
    if (get_dma_sr_isext()) {
        if (get_dma_dt_isext()) return; // ext->ext invalid
        uint16_t dstaddr = get_dma_dt_addr();
        int srcaddr = get_dma_sr_addr();
        if ((srcaddr & 1) || ((dstaddr & 1) && (!get_dma_dt_is_video()))) return;
        do_dma_external_to_internal(data, get_dma_dt_is_video());
    } else {
        if (get_dma_dt_isext()) return; // internal->external not supported (as MAME)
        uint16_t srcaddr = get_dma_sr_addr();
        uint16_t dstaddr = get_dma_dt_addr();
        if ((srcaddr & 1) || ((dstaddr & 1) && (!get_dma_dt_is_video()))) return;
        do_dma_internal_to_internal(data, get_dma_dt_is_video());
    }
}

/************************************************************************
 Video data ports
************************************************************************/

void Vt1682Core::inc_spriteram_addr()
{
    int addr = get_spriteram_addr();
    addr++;
    if ((addr & 0x07) >= 0x6) {
        addr += 0x8;
        addr &= ~0x7;
    }
    set_spriteram_addr(addr);
}

/************************************************************************
 ALU (vt1682_alu.cpp)
************************************************************************/

uint8_t Vt1682Core::alu_read(int w, int reg) { return (reg < 6) ? m_alu[w][reg] : 0; }

void Vt1682Core::alu_write(int w, int reg, uint8_t data)
{
    uint8_t* o = m_alu[w];
    switch (reg) {
    case 0: case 1: case 2: case 3: case 4: o[reg] = data; break;
    case 5: {
        o[5] = data;
        uint32_t param1 = o[4] | (o[5] << 8);
        uint32_t param2 = o[0] | (o[1] << 8);
        uint32_t result = param1 * param2;
        o[0] = result; o[1] = result >> 8; o[2] = result >> 16; o[3] = result >> 24;
        o[4] = result; o[5] = result >> 8;
        break;
    }
    case 6: o[6] = data; break;
    case 7: {
        o[7] = data;
        uint32_t param1 = o[0] | (o[1] << 8) | (o[2] << 16) | ((uint32_t)o[3] << 24);
        uint16_t param2 = o[6] | (o[7] << 8);
        if (param2 != 0) {
            uint32_t quotient = param1 / param2;
            uint16_t remainder = (uint16_t)(param1 - param2 * (quotient & ~uint32_t(1)));
            o[0] = quotient; o[1] = quotient >> 8; o[2] = quotient >> 16; o[3] = quotient >> 24;
            uint16_t r = (uint16_t)((remainder >> 1) | (remainder << 15));
            o[4] = r; o[5] = r >> 8;
        }
        break;
    }
    }
}

/************************************************************************
 Main CPU memory map
************************************************************************/

uint8_t Vt1682Core::mainRead(uint16_t a)
{
    if (a < 0x1000) return m_mainram[a];
    if (a < 0x2000) return m_soundShare[a & 0xfff];
    if (a >= 0x8000) {
        if (a == 0xfffe) {
            uint16_t vector = m_new_main_vector;
            m_current_main_vector = vector;
            return romRead(translate_address_8000_to_ffff(vector));
        }
        if (a == 0xffff)
            return romRead(translate_address_8000_to_ffff(m_current_main_vector + 1));
        return romRead(translate_address_8000_to_ffff(a));
    }
    if (a >= 0x4000) return romRead(translate_address_4000_to_7fff(a));

    switch (a) {
    case 0x2000: return m_2000;
    case 0x2001: return (m_vpos > 239) ? 0x80 : 0x00;
    case 0x2002: return m_2002_sprramaddr_2_0;
    case 0x2003: return m_2003_sprramaddr_10_3;
    case 0x2004: return m_spriteram[get_spriteram_addr() & 0x7ff];
    case 0x2005: return m_2005_vramaddr_7_0;
    case 0x2006: return m_2006_vramaddr_15_8;
    case 0x2007: return vramRead(get_vram_addr());
    case 0x2008: return m_2008_lcd_vs_delay;
    case 0x2009: return m_2009_lcd_hs_delay_7_0;
    case 0x200a: return m_200a_lcd_fr_delay_7_0;
    case 0x200b: return m_200b_misc_vregs0;
    case 0x200c: return m_200c_misc_vregs1;
    case 0x200d: return m_200d_misc_vregs2;
    case 0x200e: return m_200e_blend_pal_sel;
    case 0x200f: return m_200f_bk_pal_sel;
    case 0x2010: return m_xscroll_7_0_bk[0];
    case 0x2011: return m_yscroll_7_0_bk[0];
    case 0x2012: return m_scroll_control_bk[0];
    case 0x2013: return m_main_control_bk[0];
    case 0x2014: return m_xscroll_7_0_bk[1];
    case 0x2015: return m_yscroll_7_0_bk[1];
    case 0x2016: return m_scroll_control_bk[1];
    case 0x2017: return m_main_control_bk[1];
    case 0x2018: return m_2018_spregs;
    case 0x2019: return m_2019_bkgain;
    case 0x201a: return m_201a_sp_segment_7_0;
    case 0x201b: return m_201b_sp_segment_11_8;
    case 0x201c: return m_segment_7_0_bk[0];
    case 0x201d: return m_segment_11_8_bk[0];
    case 0x201e: return m_segment_7_0_bk[1];
    case 0x201f: return m_segment_11_8_bk[1];
    case 0x2020: return m_2020_bk_linescroll;
    case 0x2021: return m_2021_lum_offset;
    case 0x2022: return m_2022_saturation_misc;
    case 0x2023: return m_2023_lightgun_reset;
    case 0x2024: return m_2024_lightgun1_y;
    case 0x2025: return m_2025_lightgun1_x;
    case 0x2026: return m_2026_lightgun2_y;
    case 0x2027: return m_2027_lightgun2_x;
    case 0x2028: return m_2028;
    case 0x2029: return m_2029;
    case 0x202a: return m_202a;
    case 0x202b: return m_202b;
    case 0x202e: return m_202e;
    case 0x2030: return m_2030;
    case 0x2031: return m_2031_red_dac;
    case 0x2032: return m_2032_green_dac;
    case 0x2033: return m_2033_blue_dac;

    case 0x2100: return m_2100_prgbank1_r3;
    case 0x2101: return m_timer[2].preload_7_0;
    case 0x2102: return m_timer[2].enable;
    case 0x2104: return m_timer[2].preload_15_8;
    case 0x2106: return m_2106_enable_reg;
    case 0x2107: return m_2107_prgbank0_r0;
    case 0x2108: return m_2108_prgbank0_r1;
    case 0x2109: return m_2109_prgbank0_r2;
    case 0x210a: return m_210a_prgbank0_r3;
    case 0x210b: return m_210b_misc_cs_prg0_bank_sel;
    case 0x210c: return m_210c_prgbank1_r2;
    case 0x2110: return m_prgbank0_r4;   // reads/writes appear swapped on 2110-2113 (as MAME)
    case 0x2111: return m_prgbank0_r5;
    case 0x2112: return m_prgbank1_r0;
    case 0x2113: return m_prgbank1_r1;
    case 0x2118: return m_2118_prgbank1_r4_r5;
    case 0x2122: return m_2122_dma_dt_addr_7_0;
    case 0x2123: return m_2123_dma_dt_addr_15_8;
    case 0x2124: return m_2124_dma_sr_addr_7_0;
    case 0x2125: return m_2125_dma_sr_addr_15_8;
    case 0x2126: return m_2126_dma_sr_bank_addr_22_15;
    case 0x2127: return 0x00; // DMA never busy
    case 0x2128: return m_2128_dma_sr_bank_addr_24_23;
    case 0x2129: return (uint8_t)~m_inputs;  // UIO port A: dance mat (active low)
    case 0x212a: return m_212a_uio_a_direction;
    case 0x212b: return m_212b_uio_a_attribute;
    case 0x212c: return rand8();
    case 0x2130: case 0x2131: case 0x2132: case 0x2133: case 0x2134: case 0x2135:
        return alu_read(0, a - 0x2130);
    case 0x2149: return 0x00; // UIO port B (unused here)
    case 0x214a: return m_214a_uio_b_direction;
    case 0x214b: return m_214b_uio_b_attribute;
    case 0x210d: case 0x210e: case 0x210f: case 0x211e: case 0x214c: case 0x214d:
        return io_read(a);
    }
    return 0x00;
}

void Vt1682Core::mainWrite(uint16_t a, uint8_t d)
{
#ifdef PP_DEBUG
    { static int w = getenv("WATCH") ? (int)strtol(getenv("WATCH"), 0, 16) : -1; if (a == w) printf("W %04x=%02x pc=%04x line=%d\n", a, d, m_maincpu.pc, m_vpos); }
#endif
    if (a < 0x1000) { m_mainram[a] = d; return; }
    if (a < 0x2000) {
        m_soundShare[a & 0xfff] = d;
        // writing the sound CPU vectors holds it in reset (MAME 'sound_reset_hack')
        if (a >= 0x1ff4) soundcpuSetReset(true);
        return;
    }
    if (a >= 0x4000) return; // ROM

    switch (a) {
    case 0x2000: m_2000 = d; break;
    case 0x2001: m_2001 = d; break;
    case 0x2002: m_2002_sprramaddr_2_0 = d & 0x07; break;
    case 0x2003: m_2003_sprramaddr_10_3 = d; break;
    case 0x2004: m_spriteram[get_spriteram_addr() & 0x7ff] = d; inc_spriteram_addr(); break;
    case 0x2005: m_2005_vramaddr_7_0 = d; break;
    case 0x2006: m_2006_vramaddr_15_8 = d; break;
    case 0x2007: {
        uint16_t vram_address = get_vram_addr();
        vramWrite(vram_address, d);
        if (m_2000 & 0x4) {
            if (vram_address & 0x01) { vram_address += 0x40; vram_address &= ~0x01; }
            else vram_address++;
        } else vram_address++;
        set_vram_addr(vram_address);
        break;
    }
    case 0x2008: m_2008_lcd_vs_delay = d; break;
    case 0x2009: m_2009_lcd_hs_delay_7_0 = d; break;
    case 0x200a: m_200a_lcd_fr_delay_7_0 = d; break;
    case 0x200b: m_200b_misc_vregs0 = d; break;
    case 0x200c: m_200c_misc_vregs1 = d; break;
    case 0x200d: m_200d_misc_vregs2 = d; break;
    case 0x200e: m_200e_blend_pal_sel = d; break;
    case 0x200f: m_200f_bk_pal_sel = d; break;
    case 0x2010: m_xscroll_7_0_bk[0] = d; break;
    case 0x2011: m_yscroll_7_0_bk[0] = d; break;
    case 0x2012: m_scroll_control_bk[0] = d; break;
    case 0x2013: m_main_control_bk[0] = d; break;
    case 0x2014: m_xscroll_7_0_bk[1] = d; break;
    case 0x2015: m_yscroll_7_0_bk[1] = d; break;
    case 0x2016: m_scroll_control_bk[1] = d; break;
    case 0x2017: m_main_control_bk[1] = d; break;
    case 0x2018: m_2018_spregs = d; break;
    case 0x2019: m_2019_bkgain = d; break;
    case 0x201a: m_201a_sp_segment_7_0 = d; break;
    case 0x201b: m_201b_sp_segment_11_8 = d & 0x0f; break;
    case 0x201c: m_segment_7_0_bk[0] = d; break;
    case 0x201d: m_segment_11_8_bk[0] = d & 0x0f; break;
    case 0x201e: m_segment_7_0_bk[1] = d; break;
    case 0x201f: m_segment_11_8_bk[1] = d & 0x0f; break;
    case 0x2020: m_2020_bk_linescroll = d; break;
    case 0x2021: m_2021_lum_offset = d; break;
    case 0x2022: m_2022_saturation_misc = d; break;
    case 0x2023: m_2023_lightgun_reset = d; break;
    case 0x2024: m_2024_lightgun1_y = d; break;
    case 0x2025: m_2025_lightgun1_x = d; break;
    case 0x2026: m_2026_lightgun2_y = d; break;
    case 0x2027: m_2027_lightgun2_x = d; break;
    case 0x2028: m_2028 = d; break;
    case 0x2029: m_2029 = d; break;
    case 0x202a: m_202a = d; break;
    case 0x202b: m_202b = d; break;
    case 0x202e: m_202e = d; break;
    case 0x2030: m_2030 = d; break;
    case 0x2031: m_2031_red_dac = d; break;
    case 0x2032: m_2032_green_dac = d; break;
    case 0x2033: m_2033_blue_dac = d; break;

    case 0x2100: m_2100_prgbank1_r3 = d; break;
    case 0x2101: m_timer[2].preload_7_0 = d; break;
    case 0x2102: timerEnableW(2, d); break;
    case 0x2103: timerIrq(2, false); break;
    case 0x2104: m_timer[2].preload_15_8 = d; timerUpdate(2); break;
    case 0x2105: m_2105_vt1682_2105_comr6_tvmodes = d; break;
    case 0x2106: set_2106(d); break;
    case 0x2107: m_2107_prgbank0_r0 = d; break;
    case 0x2108: m_2108_prgbank0_r1 = d; break;
    case 0x2109: m_2109_prgbank0_r2 = d; break;
    case 0x210a: m_210a_prgbank0_r3 = d; break;
    case 0x210b: set_210b(d); break;
    case 0x210c: m_210c_prgbank1_r2 = d; break;
    case 0x2110: m_prgbank1_r0 = d; break;
    case 0x2111: m_prgbank1_r1 = d; break;
    case 0x2112: m_prgbank0_r4 = d; break;
    case 0x2113: m_prgbank0_r5 = d; break;
    case 0x2118: m_2118_prgbank1_r4_r5 = d; break;
    case 0x211c: m_211c_regs_ext2421 = d; break;
    case 0x2122: m_2122_dma_dt_addr_7_0 = d; break;
    case 0x2123: m_2123_dma_dt_addr_15_8 = d; break;
    case 0x2124: m_2124_dma_sr_addr_7_0 = d; break;
    case 0x2125: m_2125_dma_sr_addr_15_8 = d; break;
    case 0x2126: m_2126_dma_sr_bank_addr_22_15 = d; break;
    case 0x2127: dma_trigger(d); break;
    case 0x2128: m_2128_dma_sr_bank_addr_24_23 = d & 0x03; break;
    case 0x2129: m_2129_uio_a_data = d; break;
    case 0x212a: m_212a_uio_a_direction = d; break;
    case 0x212b: m_212b_uio_a_attribute = d; break;
    case 0x212c: break; // PRNG seed
    case 0x212e: m_scu_to_main_irq_active = 0; update_main_interrupts(); break;
    case 0x2130: case 0x2131: case 0x2132: case 0x2133: case 0x2134: case 0x2135: case 0x2136: case 0x2137:
        alu_write(0, a - 0x2130, d); break;
    case 0x2149: m_2149_uio_b_data = d; break;
    case 0x214a: m_214a_uio_b_direction = d; break;
    case 0x214b: m_214b_uio_b_attribute = d; break;
    case 0x210d: case 0x210e: case 0x210f: case 0x211e: case 0x214c: case 0x214d:
        io_write(a, d); break;
    default: break;
    }
}

/************************************************************************
 IO (vt1682_io.cpp) - no external hookups on the dance mat
************************************************************************/

uint8_t Vt1682Core::io_read(uint16_t a)
{
    switch (a) {
    case 0x210d: return m_210d_ioconfig;
    case 0x210e: {
        uint8_t ret = (m_210d_ioconfig & 0x01) ? (m_io_ab_o & 0x0f) : 0;
        if (m_210d_ioconfig & 0x04) ret |= m_io_ab_o & 0xf0;
        return ret;
    }
    case 0x210f: {
        uint8_t ret = (m_210d_ioconfig & 0x10) ? (m_io_cd_o & 0x0f) : 0;
        if (m_210d_ioconfig & 0x40) ret |= m_io_cd_o & 0xf0;
        return ret;
    }
    case 0x211e: return 0x00;
    case 0x214c: return m_214c_ioefconfig;
    case 0x214d: {
        uint8_t ioe_imask = ((m_214c_ioefconfig & 0x01) ? 0 : 0x07) | ((m_211e_adconfig & 0x10) ? 0 : 0x08);
        return m_io_ef_o & ((~ioe_imask & 0x0f) | ((m_211e_adconfig & 0x0f) << 4));
    }
    }
    return 0;
}

void Vt1682Core::io_write(uint16_t a, uint8_t d)
{
    switch (a) {
    case 0x210d: m_210d_ioconfig = d; break;
    case 0x210e: m_io_ab_o = d; break;
    case 0x210f: m_io_cd_o = d; break;
    case 0x211e: m_211e_adconfig = d; break;
    case 0x214c: m_214c_ioefconfig = d; break;
    case 0x214d: m_io_ef_o = d; break;
    }
}

/************************************************************************
 Sound CPU memory map
************************************************************************/

uint8_t Vt1682Core::soundRead(uint16_t a)
{
    if (a < 0x2000) return m_soundShare[a & 0xfff];
    if (a >= 0xf000) {
        // vectors are really fetched from 0x0ff0-0x0fff
        if (a == 0xfffe) {
            uint16_t vector = m_new_sound_vector;
            m_current_sound_vector = vector;
            return m_soundShare[vector & 0xfff];
        }
        if (a == 0xffff) return m_soundShare[(m_current_sound_vector + 1) & 0xfff];
        return m_soundShare[a & 0xfff];
    }
    switch (a) {
    case 0x2100: return m_timer[0].preload_7_0;
    case 0x2101: return m_timer[0].preload_15_8;
    case 0x2102: return m_timer[0].enable;
    case 0x2110: return m_timer[1].preload_7_0;
    case 0x2111: return m_timer[1].preload_15_8;
    case 0x2112: return m_timer[1].enable;
    case 0x2118: return m_soundcpu_2118_dacleft_7_0;
    case 0x2119: return m_soundcpu_2119_dacleft_15_8;
    case 0x211a: return m_soundcpu_211a_dacright_7_0;
    case 0x211b: return m_soundcpu_211b_dacright_15_8;
    case 0x2130: case 0x2131: case 0x2132: case 0x2133: case 0x2134: case 0x2135:
        return alu_read(1, a - 0x2130);
    case 0x2140: return rand8() & 0x8a;
    case 0x2144: return rand8();
    }
    return 0x00;
}

void Vt1682Core::soundWrite(uint16_t a, uint8_t d)
{
    if (a < 0x2000) { m_soundShare[a & 0xfff] = d; return; }
    if (a >= 0xf000) { m_soundShare[a & 0xfff] = d; return; }
    switch (a) {
    case 0x2100: m_timer[0].preload_7_0 = d; break;
    case 0x2101: m_timer[0].preload_15_8 = d; timerUpdate(0); break;
    case 0x2102: timerEnableW(0, d); break;
    case 0x2103: timerIrq(0, false); break;
    case 0x2110: m_timer[1].preload_7_0 = d; break;
    case 0x2111: m_timer[1].preload_15_8 = d; timerUpdate(1); break;
    case 0x2112: timerEnableW(1, d); break;
    case 0x2113: timerIrq(1, false); break;
    case 0x2118: m_soundcpu_2118_dacleft_7_0 = d; break;
    case 0x2119:
        m_soundcpu_2119_dacleft_15_8 = d;
        m_dacLeft = (uint16_t)(((m_soundcpu_2119_dacleft_15_8 << 8) | m_soundcpu_2118_dacleft_7_0) >> 4);
        break;
    case 0x211a: m_soundcpu_211a_dacright_7_0 = d; break;
    case 0x211b:
        m_soundcpu_211b_dacright_15_8 = d;
        m_dacRight = (uint16_t)(((m_soundcpu_211b_dacright_15_8 << 8) | m_soundcpu_211a_dacright_7_0) >> 4);
        break;
    case 0x211c:
        if (d & 0x10) { m_scu_to_main_irq_active = 1; update_main_interrupts(); }
        break;
    case 0x2130: case 0x2131: case 0x2132: case 0x2133: case 0x2134: case 0x2135: case 0x2136: case 0x2137:
        alu_write(1, a - 0x2130, d); break;
    default: break;
    }
}

/************************************************************************
 Scheduling
************************************************************************/

void Vt1682Core::checkTimer(int i, double now)
{
    Timer& tm = m_timer[i];
    while (tm.period > 0 && tm.next <= now) {
        tm.next += tm.period;
        if (tm.enable & 0x02) timerIrq(i, true);
    }
}

void Vt1682Core::produceAudio(double now)
{
    while (m_nextSample <= now) {
        float l = (m_dacLeft - 2048) / 2048.0f, r = (m_dacRight - 2048) / 2048.0f;
        float in = 0.5f * l + 0.5f * r;
        float out = in - m_hpPrevIn + 0.995f * m_hpPrevOut;   // DC-blocking high-pass
        m_hpPrevIn = in; m_hpPrevOut = out;
        float smp = out * 30000.0f;
        if (smp > 32767) smp = 32767;
        if (smp < -32768) smp = -32768;
        if (m_audio.size() < 44100) m_audio.push_back((int16_t)smp);
        m_nextSample += m_sampleStep;
    }
}

// The two CPUs run in short alternating time slices (MAME uses 1/6000 s; we use
// 1/40 of a scanline). The system timer is clocked against the main CPU and the
// two sound timers against the sound CPU.
void Vt1682Core::runUntil(double t)
{
    // 1/40 of a line keeps audio bit-identical to per-instruction interleaving;
    // coarser slices (MAME's 1/6000 s included) change how the mixer program runs.
    const double quantum = m_lineLen / 40.0;
    while (m_mainTime < t || (!m_soundResetLine && m_soundTime < t)) {
        double sliceEnd = std::min(t, std::min(m_mainTime, m_soundResetLine ? m_mainTime : m_soundTime) + quantum);

        while (m_mainTime < sliceEnd) {
            m_now = m_mainTime;
            checkTimer(2, m_now);
            m_mainTime += m_maincpu.step() * (double)m_mainDiv;
        }

        if (m_soundResetLine) {
            m_now = sliceEnd;
            checkTimer(0, m_now); checkTimer(1, m_now);
            produceAudio(m_now);
            m_soundTime = sliceEnd;
            continue;
        }
        while (m_soundTime < sliceEnd) {
            m_now = m_soundTime;
            checkTimer(0, m_now); checkTimer(1, m_now);
            produceAudio(m_now);
            if (!soundIdleSkip(sliceEnd)) m_soundTime += m_soundcpu.step();
            if (m_soundResetLine) { m_soundTime = sliceEnd; break; }
        }
    }
    m_now = t;
    checkTimer(0, t); checkTimer(1, t); checkTimer(2, t);
    produceAudio(t);
}

// The sound CPU usually parks in "LDA abs / BEQ *-3" (or BNE) polling a byte in
// shared RAM. Only the main CPU or an interrupt can change that byte, so while the
// loop condition holds we can advance the sound CPU's clock in whole loop
// iterations up to the next point where anything else could happen.
bool Vt1682Core::soundIdleSkip(double t)
{
    uint16_t pc = m_soundcpu.pc;
    if (pc >= 0x1ffb) return false;
    const uint8_t* sh = m_soundShare;
    uint16_t o = pc & 0xfff;
    if (o > 0xffa) return false;
    if (sh[o] != 0xad || (sh[o + 3] != 0xf0 && sh[o + 3] != 0xd0) || sh[o + 4] != 0xfb) return false;
    uint16_t addr = sh[o + 1] | (sh[o + 2] << 8);
    if (addr >= 0x2000) return false;          // only plain shared RAM is safe to skip on
    uint8_t v = sh[addr & 0xfff];
    bool loops = (sh[o + 3] == 0xf0) ? (v == 0) : (v != 0);
    if (!loops) return false;
    if (m_soundcpu.m_nmiPending || (m_soundcpu.m_irqLine && !(m_soundcpu.p & 0x04))) return false;

    double target = t;
    for (int i = 0; i < 2; i++)
        if (m_timer[i].period > 0 && m_timer[i].next < target) target = m_timer[i].next;
    const double loopCycles = 7.0;               // LDA abs (4) + taken BEQ, same page (3)
    double span = target - m_soundTime;
    if (span < loopCycles) return false;
    double k = std::floor(span / loopCycles);
    m_soundTime += k * loopCycles;
    m_soundcpu.a = v;
    m_soundcpu.p = (m_soundcpu.p & ~0x82) | (v & 0x80) | (v ? 0 : 0x02);
    return true;
}

void Vt1682Core::runFrame()
{
    double frameStart = m_lineStart;
    for (int line = 0; line < m_lines; line++) {
        double ls = frameStart + line * m_lineLen;
        m_vpos = line;
        runUntil(ls);
        m_now = ls;
        if (line == 240 && (m_2000 & 0x01)) {
            m_maincpu.pulseNmi();
            if (!m_scpu_is_in_reset && !m_soundResetLine) m_soundcpu.pulseNmi();
        }
        // registers are latched / the line is drawn at hpos 156 of 300
        runUntil(ls + m_lineLen * 156.0 / 300.0);
        if (line < 240) renderLine(line);
    }
    m_lineStart = frameStart + m_lines * m_lineLen;
    m_vpos = m_lines - 1;
    runUntil(m_lineStart);

    // keep times small to preserve precision
    double base = m_lineStart;
    m_mainTime -= base; m_soundTime -= base; m_nextSample -= base; m_now -= base;
    for (int i = 0; i < 3; i++) if (m_timer[i].period > 0) m_timer[i].next -= base;
    m_lineStart = 0;
}

size_t Vt1682Core::readAudio(int16_t* out, size_t maxSamples)
{
    size_t n = std::min(maxSamples, m_audio.size());
    memcpy(out, m_audio.data(), n * sizeof(int16_t));
    m_audio.erase(m_audio.begin(), m_audio.begin() + n);
    return n;
}

/************************************************************************
 Video
************************************************************************/

void Vt1682Core::draw_tile_pixline(int segment, int tile, int tileline, int x, int y, int palselect, int pal, int is16pix_high, int is16pix_wide, int bpp, int depth, int opaque, int flipx, int flipy)
{
    int tilesize_high = is16pix_high ? 16 : 8;
    if (bpp == 3) pal = 0x0;
    if (bpp == 2) pal &= 0xc;

    int startaddress = segment;
    int linebytes;
    if (bpp == 3) linebytes = is16pix_wide ? 16 : 8;
    else if (bpp == 2) linebytes = is16pix_wide ? 12 : 6;
    else linebytes = is16pix_wide ? 8 : 4;

    int tilesize_wide = is16pix_wide ? 16 : 8;
    int tilebytes = linebytes * tilesize_high;
    startaddress += tilebytes * tile;

    int currentaddress;
    if (!flipy) currentaddress = startaddress + tileline * linebytes;
    else currentaddress = startaddress + ((tilesize_high - 1) - tileline) * linebytes;

    int shift_amount, mask, bytes_in;
    if (bpp == 3) { shift_amount = 8; mask = 0xff; bytes_in = 4; }
    else if (bpp == 2) { shift_amount = 6; mask = 0x3f; bytes_in = 3; }
    else { shift_amount = 4; mask = 0x0f; bytes_in = 2; }

    int xbase = x;
    for (int xx = 0; xx < tilesize_wide; xx += 4) {
        uint32_t pixdata = 0;
        int shift = 0;
        for (int i = 0; i < bytes_in; i++) {
            pixdata |= romRead(currentaddress) << shift; currentaddress++;
            shift += 8;
        }
        shift = 0;
        for (int ii = 0; ii < 4; ii++) {
            uint8_t pen = (pixdata >> shift) & mask;
            if (opaque || pen) {
                int xdraw_real;
                if (!flipx) xdraw_real = xbase + xx + ii;
                else xdraw_real = xbase + ((tilesize_wide - 1) - xx - ii);
                if (xdraw_real >= 0 && xdraw_real <= 255) {
                    if (palselect & 1) {
                        if (depth < m_pal1_pri[xdraw_real]) {
                            m_pal1_pix[xdraw_real] = pen | (pal << 4);
                            m_pal1_pri[xdraw_real] = depth;
                        }
                    }
                    if (palselect & 2) {
                        if (depth < m_pal2_pri[xdraw_real]) {
                            m_pal2_pix[xdraw_real] = pen | (pal << 4);
                            m_pal2_pri[xdraw_real] = depth;
                        }
                    }
                }
            }
            shift += shift_amount;
        }
    }
}

void Vt1682Core::setup_video_pages(int which, int tilesize, int vs, int hs, int y8, int x8, uint16_t* pagebases)
{
    int vs_hs = (vs << 1) | hs;
    int y8_x8 = (y8 << 1) | x8;
    pagebases[0] = pagebases[1] = pagebases[2] = pagebases[3] = 0xffff;

    if (!tilesize) { // 8x8
        if (vs_hs == 0) {
            pagebases[0] = (y8_x8 == 0) ? 0x000 : 0x800;
            pagebases[1] = pagebases[2] = pagebases[3] = pagebases[0];
        } else if (vs_hs == 1) {
            if (y8_x8 & 1) { pagebases[0] = 0x800; pagebases[1] = 0x000; }
            else { pagebases[0] = 0x000; pagebases[1] = 0x800; }
            pagebases[2] = pagebases[0];
            pagebases[3] = pagebases[1];
        } else if (vs_hs == 2) {
            if (y8_x8 & 2) { pagebases[0] = 0x800; pagebases[2] = 0x000; }
            else { pagebases[0] = 0x000; pagebases[2] = 0x800; }
            pagebases[1] = pagebases[0];
            pagebases[3] = pagebases[2];
        } else {
            pagebases[0] = pagebases[1] = pagebases[2] = pagebases[3] = 0x000;
        }
    } else { // 16x16
        if (vs_hs == 0) {
            static const uint16_t b[4] = { 0x000, 0x200, 0x400, 0x600 };
            pagebases[0] = b[y8_x8];
            pagebases[1] = pagebases[2] = pagebases[3] = pagebases[0];
        } else if (vs_hs == 1) {
            if (y8_x8 & 1) { pagebases[0] = 0x200; pagebases[1] = 0x000; }
            else { pagebases[0] = 0x000; pagebases[1] = 0x200; }
            pagebases[2] = pagebases[0];
            pagebases[3] = pagebases[1];
        } else if (vs_hs == 2) {
            if (y8_x8 & 2) { pagebases[0] = 0x200; pagebases[2] = 0x000; }
            else { pagebases[0] = 0x000; pagebases[2] = 0x200; }
            pagebases[1] = pagebases[0];
            pagebases[3] = pagebases[2];
        } else {
            switch (y8_x8) {
            case 0x0: pagebases[0] = 0x000; pagebases[1] = 0x200; pagebases[2] = 0x400; pagebases[3] = 0x600; break;
            case 0x1: pagebases[0] = 0x200; pagebases[1] = 0x000; pagebases[2] = 0x600; pagebases[3] = 0x400; break;
            case 0x2: pagebases[0] = 0x400; pagebases[1] = 0x600; pagebases[2] = 0x000; pagebases[3] = 0x200; break;
            case 0x3: pagebases[0] = 0x600; pagebases[1] = 0x400; pagebases[2] = 0x200; pagebases[3] = 0x000; break;
            }
        }
    }

    if (tilesize && (which == 1)) {
        for (int i = 0; i < 4; i++) pagebases[i] += 0x800;
    }
}

int Vt1682Core::get_address_for_tilepos(int x, int y, int tilesize, uint16_t* pagebases)
{
    if (!tilesize) {
        x &= 0x3f; y &= 0x3f;
        int page = ((x & 0x20) ? 1 : 0) | ((y & 0x20) ? 2 : 0);
        x &= 0x1f; y &= 0x1f;
        return pagebases[page] + (y * 0x20 * 0x02) + (x * 0x2);
    } else {
        x &= 0x1f; y &= 0x1f;
        int page = ((x & 0x10) ? 1 : 0) | ((y & 0x10) ? 2 : 0);
        x &= 0x0f; y &= 0x0f;
        return pagebases[page] + (y * 0x10 * 0x02) + (x * 0x2);
    }
}

void Vt1682Core::draw_layer(int which, int opaque, int y)
{
    int bk_tilesize = (m_main_control_bk[which] & 0x01);
    int bk_line = (m_main_control_bk[which] & 0x02) >> 1;
    int bk_tilebpp = (m_main_control_bk[which] & 0x0c) >> 2;
    int bk_depth = (m_main_control_bk[which] & 0x30) >> 4;
    int bk_paldepth_mode = (m_main_control_bk[which] & 0x40) >> 6;
    int bk_enable = (m_main_control_bk[which] & 0x80) >> 7;

    if (!bk_enable || bk_line) return; // line modes are unimplemented (as in MAME)

    int yscroll = m_yscroll_7_0_bk[which];
    int yscrollmsb = (m_scroll_control_bk[which] & 0x02) >> 1;
    int page_layout_h = (m_scroll_control_bk[which] & 0x04) >> 2;
    int page_layout_v = (m_scroll_control_bk[which] & 0x08) >> 3;
    int segment = m_segment_7_0_bk[which] | (m_segment_11_8_bk[which] << 8);
    segment = segment * 0x2000;

    uint16_t bases[4];
    int palselect = (which == 0) ? (m_200f_bk_pal_sel & 0x03) : ((m_200f_bk_pal_sel & 0x0c) >> 2);

    int xscroll = m_xscroll_7_0_bk[which];
    int xscrollmsb = (m_scroll_control_bk[which] & 0x01);
    int rowscroll_pram_address = (m_2020_bk_linescroll & 0xf) * 0x100;
    bool rowscroll_enabled = ((which == 0) && (m_2020_bk_linescroll & 0x10)) || ((which == 1) && (m_2020_bk_linescroll & 0x20));
    if (rowscroll_enabled)
        xscroll += (int8_t)m_mainram[(rowscroll_pram_address + y) & 0xfff];

    setup_video_pages(which, bk_tilesize, page_layout_v, page_layout_h, yscrollmsb, xscrollmsb, bases);

    int ytile, ytileline;
    int ywithscroll = y - yscroll;
    if (bk_tilesize) { ytileline = ywithscroll & 0xf; ytile = ywithscroll >> 4; }
    else { ytileline = ywithscroll & 0x07; ytile = ywithscroll >> 3; }

    int xscrolltile, xscrolltile_part;
    if (bk_tilesize) { xscrolltile = xscroll >> 4; xscrolltile_part = xscroll & 0x0f; }
    else { xscrolltile = xscroll >> 3; xscrolltile_part = xscroll & 0x07; }

    for (int xtile = -1; xtile < (bk_tilesize ? 16 : 32); xtile++) {
        int count = get_address_for_tilepos(xtile - xscrolltile, ytile, bk_tilesize, bases);
        uint16_t word = vramRead(count & 0xffff);
        word |= vramRead((count + 1) & 0xffff) << 8;

        int tile = word & 0x0fff;
        if (!tile) continue;
        uint8_t pal = (word & 0xf000) >> 12;
        int xpos = xtile * (bk_tilesize ? 16 : 8);

        uint8_t realpal, realdepth;
        if (bk_paldepth_mode) { realdepth = pal & 0x03; realpal = (pal & 0x0c) | bk_depth; }
        else { realpal = pal; realdepth = bk_depth; }

        draw_tile_pixline(segment, tile, ytileline, xpos + xscrolltile_part, y, palselect, realpal, bk_tilesize, bk_tilesize, bk_tilebpp, (realdepth * 2) + 1, opaque, 0, 0);
    }
}

void Vt1682Core::draw_sprites(int line)
{
    int sp_en = (m_2018_spregs & 0x04) >> 2;
    int sp_pal_sel = (m_2018_spregs & 0x08) >> 3;
    int sp_size = (m_2018_spregs & 0x03);
    int segment = (m_201a_sp_segment_7_0 | (m_201b_sp_segment_11_8 << 8)) * 0x2000;
    const int SPRITE_STEP = 8;
    if (!sp_en) return;

    for (int i = 0; i < 240; i++) {
        const uint8_t* s = &m_spriteram[i * SPRITE_STEP];
        int attr2 = s[5];
        int ystart = s[4];
        if (attr2 & 0x01) ystart -= 256;
        int yend = ystart + ((sp_size & 0x2) ? 16 : 8);
        if (line < ystart || line >= yend) continue;

        int ytileline = line - ystart;
        int tilenum = s[0];
        int attr0 = s[1];
        int x = s[2];
        int attr1 = s[3];
        tilenum |= (attr0 & 0x0f) << 8;
        if (!tilenum) continue;

        int pal = (attr0 & 0xf0) >> 4;
        int flipx = (attr1 & 0x02) >> 1;
        int flipy = (attr1 & 0x04) >> 2;
        int depth = (attr1 & 0x18) >> 3;
        if (attr1 & 0x01) x -= 256;

        int palselect;
        if (sp_pal_sel) palselect = 3;
        else palselect = (attr2 & 0x02) ? 2 : 1;

        draw_tile_pixline(segment, tilenum, ytileline, x, line, palselect, pal, sp_size & 0x2, sp_size & 0x1, 0, depth * 2, 0, flipx, flipy);
    }
}

static inline uint32_t pal555(const uint8_t* vram, int idx)
{
    uint16_t w = vram[0x1c00 + idx * 2] | (vram[0x1c00 + idx * 2 + 1] << 8);
    uint32_t r = (w >> 10) & 0x1f, g = (w >> 5) & 0x1f, b = w & 0x1f;
    r = (r << 3) | (r >> 2); g = (g << 3) | (g >> 2); b = (b << 3) | (b >> 2);
    return 0xff000000u | (r << 16) | (g << 8) | b;
}

void Vt1682Core::renderLine(int y)
{
    memset(m_pal2_pri, 0xff, 256);
    memset(m_pal1_pri, 0xff, 256);
    memset(m_pal2_pix, 0x00, 256);
    memset(m_pal1_pix, 0x00, 256);

    draw_layer(0, 0, y);
    draw_layer(1, 0, y);
    draw_sprites(y);

    uint32_t* dst = &m_frame[y * 256];
    for (int x = 0; x < 256; x++) {
        uint8_t pix1 = m_pal1_pix[x], pix2 = m_pal2_pix[x];
        uint8_t pri1 = m_pal1_pri[x], pri2 = m_pal2_pri[x];
        int idx;
        if (pri1 <= pri2) {
            if (pix1) idx = pix1 | 0x100;
            else idx = pix2 ? pix2 : 0x100;
        } else {
            if (pix2) idx = pix2;
            else idx = pix1 ? (pix1 | 0x100) : 0x000;
        }
        dst[x] = pal555(m_vram, idx);
    }
}

/************************************************************************
 Save states
************************************************************************/

namespace {
struct Writer {
    std::vector<uint8_t>& v;
    template <typename T> void put(const T& x) { const uint8_t* p = (const uint8_t*)&x; v.insert(v.end(), p, p + sizeof(T)); }
    void raw(const void* p, size_t n) { v.insert(v.end(), (const uint8_t*)p, (const uint8_t*)p + n); }
};
struct Reader {
    const std::vector<uint8_t>& v; size_t pos = 0; bool ok = true;
    template <typename T> void get(T& x) { raw(&x, sizeof(T)); }
    void raw(void* p, size_t n) { if (pos + n > v.size()) { ok = false; return; } memcpy(p, &v[pos], n); pos += n; }
};
}

// All plain state lives in a contiguous run of members; we serialise them field by field.
#define VT_STATE_FIELDS(X) \
    X(m_mainram) X(m_soundShare) X(m_spriteram) X(m_vram) \
    X(m_2000) X(m_2001) X(m_2002_sprramaddr_2_0) X(m_2003_sprramaddr_10_3) X(m_2005_vramaddr_7_0) X(m_2006_vramaddr_15_8) \
    X(m_201a_sp_segment_7_0) X(m_201b_sp_segment_11_8) X(m_segment_7_0_bk) X(m_segment_11_8_bk) X(m_main_control_bk) \
    X(m_scroll_control_bk) X(m_xscroll_7_0_bk) X(m_yscroll_7_0_bk) X(m_200e_blend_pal_sel) X(m_200f_bk_pal_sel) \
    X(m_2008_lcd_vs_delay) X(m_2009_lcd_hs_delay_7_0) X(m_200a_lcd_fr_delay_7_0) X(m_200d_misc_vregs2) X(m_200c_misc_vregs1) \
    X(m_200b_misc_vregs0) X(m_2018_spregs) X(m_2019_bkgain) X(m_2020_bk_linescroll) X(m_2021_lum_offset) X(m_2022_saturation_misc) \
    X(m_2023_lightgun_reset) X(m_2024_lightgun1_y) X(m_2025_lightgun1_x) X(m_2026_lightgun2_y) X(m_2027_lightgun2_x) \
    X(m_2031_red_dac) X(m_2032_green_dac) X(m_2033_blue_dac) X(m_2028) X(m_2029) X(m_202a) X(m_202b) X(m_202e) X(m_2030) \
    X(m_scu_to_main_irq_active) X(m_timera_to_main_irq_active) X(m_current_main_vector) X(m_new_main_vector) \
    X(m_timera_to_sound_irq_active) X(m_timerb_to_sound_irq_active) X(m_current_sound_vector) X(m_new_sound_vector) \
    X(m_prgbank1_r0) X(m_prgbank1_r1) X(m_210c_prgbank1_r2) X(m_2100_prgbank1_r3) X(m_2118_prgbank1_r4_r5) \
    X(m_2107_prgbank0_r0) X(m_2108_prgbank0_r1) X(m_2109_prgbank0_r2) X(m_210a_prgbank0_r3) X(m_prgbank0_r4) X(m_prgbank0_r5) \
    X(m_210b_misc_cs_prg0_bank_sel) X(m_2105_vt1682_2105_comr6_tvmodes) X(m_211c_regs_ext2421) \
    X(m_2122_dma_dt_addr_7_0) X(m_2123_dma_dt_addr_15_8) X(m_2124_dma_sr_addr_7_0) X(m_2125_dma_sr_addr_15_8) \
    X(m_2126_dma_sr_bank_addr_22_15) X(m_2128_dma_sr_bank_addr_24_23) X(m_2106_enable_reg) X(m_scpu_is_in_reset) X(m_soundResetLine) \
    X(m_soundcpu_2118_dacleft_7_0) X(m_soundcpu_2119_dacleft_15_8) X(m_soundcpu_211a_dacright_7_0) X(m_soundcpu_211b_dacright_15_8) \
    X(m_dacLeft) X(m_dacRight) X(m_alu) X(m_timer) \
    X(m_210d_ioconfig) X(m_211e_adconfig) X(m_214c_ioefconfig) X(m_io_ab_o) X(m_io_cd_o) X(m_io_ef_o) \
    X(m_2129_uio_a_data) X(m_212a_uio_a_direction) X(m_212b_uio_a_attribute) X(m_2149_uio_b_data) X(m_214a_uio_b_direction) X(m_214b_uio_b_attribute) \
    X(m_mainTime) X(m_soundTime) X(m_now) X(m_nextSample) X(m_lineStart) X(m_rng)

static void putCpu(Writer& w, const M6502& c) { w.put(c.pc); w.put(c.a); w.put(c.x); w.put(c.y); w.put(c.s); w.put(c.p); w.put(c.m_irqLine); w.put(c.m_nmiPending); }
static void getCpu(Reader& r, M6502& c) { r.get(c.pc); r.get(c.a); r.get(c.x); r.get(c.y); r.get(c.s); r.get(c.p); r.get(c.m_irqLine); r.get(c.m_nmiPending); }

bool Vt1682Core::saveState(std::vector<uint8_t>& out)
{
    out.clear();
    Writer w{ out };
    w.put((uint32_t)0x31545650); // 'PVT1'
#define X(f) w.raw(&f, sizeof(f));
    VT_STATE_FIELDS(X)
#undef X
    putCpu(w, m_maincpu);
    putCpu(w, m_soundcpu);
    return true;
}

bool Vt1682Core::loadState(const std::vector<uint8_t>& in)
{
    Reader r{ in };
    uint32_t magic = 0; r.get(magic);
    if (magic != 0x31545650) return false;
#define X(f) r.raw(&f, sizeof(f));
    VT_STATE_FIELDS(X)
#undef X
    getCpu(r, m_maincpu);
    getCpu(r, m_soundcpu);
    m_audio.clear();
    return r.ok;
}

std::string Vt1682Core::debugInfo() const
{
    char b[512];
    snprintf(b, sizeof b, "main pc=%04x p=%02x irq=%d | snd pc=%04x p=%02x irq=%d rst=%d inrst=%d | 2000=%02x 2106=%02x scuirq=%d tmirq=%d tA=%02x tB=%02x tS=%02x/%04x 210b=%02x",
        m_maincpu.pc, m_maincpu.p, m_maincpu.m_irqLine, m_soundcpu.pc, m_soundcpu.p, m_soundcpu.m_irqLine, m_soundResetLine, m_scpu_is_in_reset,
        m_2000, m_2106_enable_reg, m_scu_to_main_irq_active, m_timera_to_main_irq_active, m_timer[0].enable, m_timer[1].enable, m_timer[2].enable,
        (m_timer[2].preload_15_8 << 8) | m_timer[2].preload_7_0, m_210b_misc_cs_prg0_bank_sel);
    return b;
}
