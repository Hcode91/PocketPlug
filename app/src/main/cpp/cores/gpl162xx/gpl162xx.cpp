// PocketPlug - GeneralPlus GPL162xx B-series (see gpl162xx.h)
// SPDX-License-Identifier: BSD-3-Clause

#include "gpl162xx.h"
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "gpl_soc.inc"
#include "gpl_iomap.inc"

#define TIMER(cls, fn) [](void* c, int p) { ((cls*)c)->fn(p); }

static bool dbg() { static int d = getenv("GPLDBG") ? 1 : 0; return d; }

Gpl162xxCore::Gpl162xxCore(Kind k) : m_kind(k)
{
    m_cpuClock = (k == BEAMBOX) ? 48000000 : 96000000;
    if (k == SPISPI) m_ramTop = 0x7000;
    m_mainspace.c = this;
    m_csspace.c = this;
    m_machine.m_icountptr = &m_cpu.m_core->m_icount;
    m_machine.m_cpu_run = [this]() { m_cpu.execute_run(); };
    m_machine.set_cpu_clock(m_cpuClock);
    m_cpu.set_bus(this);
    m_cpu.set_iso(20);
    m_cpu.m_irq_ack = [](void* c, int line) { ((Gpl162xxCore*)c)->irq_vector_cb(line); };
    m_cpu.m_irq_ack_ctx = this;
    m_screen.m_machine = &m_machine;
    // MAME: 640 x 524 raster at 60 Hz; the video picks a 320 x 240 (or 640 x 480) visible area
    m_screen.configure(640, 524, 239, HZ_TO_ATTOSECONDS(60));
    m_audio.m_soc = this;
    m_video.m_screen = &m_screen;
    m_video.m_video_irq_cb = [this](int s) { videoirq_w(s); };
    m_video.m_set_visible = [this](int w, int h) {
        if (w == m_visW && h == m_visH) return;
        m_visW = w; m_visH = h;
        m_screen.m_vis_bottom = h - 1;
        m_screen.configure(640, 524, h - 1, HZ_TO_ATTOSECONDS(60));
    };
    m_video.m_update_partial = [this](int line) { update_partial(line); };
    m_video.set_video_space(&m_mainspace);
    m_video.set_cs_video_space(&m_csspace, m_csbase);
    if (k == BEIJUEHH) {
        m_disable_timebase_interrupts = true;
        m_video.set_has_gpl162xx_b_features();
        m_video.set_legacy_video_mode();
    } else {
        m_video.set_has_gpl162xx_b_features();
        m_video.set_has_vga_modes();
        m_video.set_has_gpl162xx_b_extended_sprites();
        m_video.set_has_3d_sprite_modes();
    }
    m_bitmap.assign(640 * 480, 0);
    m_out.assign(320 * 240, 0xff000000u);
}

Gpl162xxCore::~Gpl162xxCore()
{
    if (m_map) munmap(m_map, m_mapLen);
}

bool Gpl162xxCore::load(const std::vector<RomFile>& roms, std::string& error)
{
    const RomFile* r = nullptr;
    for (auto& f : roms) if (!r || f.data.size() > r->data.size() || (!f.path.empty() && r->path.empty())) r = &f;
    if (!r) { error = "Missing ROM"; return false; }
    if (!r->path.empty()) {
        int fd = open(r->path.c_str(), O_RDONLY);
        if (fd < 0) { error = "Could not open " + r->name; return false; }
        struct stat st;
        fstat(fd, &st);
        m_mapLen = size_t(st.st_size);
        m_map = mmap(nullptr, m_mapLen, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
        close(fd);
        if (m_map == MAP_FAILED) { m_map = nullptr; error = "Could not map " + r->name; return false; }
        m_rom = (uint16_t*)m_map;
        m_romWords = m_mapLen / 2;
    } else {
        m_romCopy.assign((r->data.size() + 1) / 2, 0);
        memcpy(m_romCopy.data(), r->data.data(), r->data.size());
        m_rom = m_romCopy.data();
        m_romWords = m_romCopy.size();
    }
    if (m_kind == SPISPI) {
        // the boot ROM's vectors go through the jump table the program puts at 6fea (the RAM
        // ends at 6fff on these); MAME's init_spi uses 2fe0, which the first stage never fills
        m_vectorbase = 0x6fe0;
        // external RAM on the chip selects (MAME: "CS0 should be RAM on these?")
        m_sdram.assign(0x400000, 0);
        m_sdram2.assign(0x10000, 0);
    } else if (m_kind == BEIJUEHH) {
        // the dump's words are little-endian (ROM_LOAD16_WORD_SWAP of the big-endian region): used as they are
    } else {
        nandInit();
    }
    startDevices();
    reset();
    return true;
}

void Gpl162xxCore::startDevices()
{
    m_sound_update_timer = m_machine.timer_alloc([](void* c, int) { ((Gpl162xxCore*)c)->m_audio.stream_update(); }, this);
    m_sound_update_timer->adjust(attotime::from_hz(50), 0, attotime::from_hz(50));
    m_vblank_begin_timer = m_machine.timer_alloc([](void* c, int) { ((Gpl162xxCore*)c)->vblank_begin(); }, this);
    m_vblank_end_timer = m_machine.timer_alloc([](void* c, int) { ((Gpl162xxCore*)c)->vblank_end(); }, this);
    m_scanline0_timer = m_machine.timer_alloc([](void* c, int) { ((Gpl162xxCore*)c)->reset_partial_updates(); }, this);
    m_vblank_begin_timer->adjust(m_screen.time_until_vblank_start());
    m_scanline0_timer->adjust(m_screen.time_until_pos(0));
    m_screen.m_vblank_start_time = attotime::zero();
    m_screen.m_vblank_end_time = attotime(0, m_screen.m_vblank_period);

    m_audio.device_start();
    m_video.device_start(m_machine);
    m_scheduler = m_machine.timer_alloc(TIMER(Gpl162xxCore, scheduler_cb), this);
    m_scheduler->adjust(attotime::never());
    for (int i = 0; i < 3; i++) {
        m_timebasetimer[i] = m_machine.timer_alloc([](void* c, int p) { ((Gpl162xxCore*)c)->timebase_cb(p); }, this);
        m_timebasetimer[i]->adjust(attotime::never(), i);
    }
}

void Gpl162xxCore::reset()
{
    memset(m_mainram, 0, sizeof(m_mainram));
    // unsp_20_device::device_reset + generalplus_gpl162xx_base_device::device_reset
    m_dac_pga = 0; m_782d = 0; m_clock_ctrl = 0; m_membankswitch_7810 = 0x0001; m_7816 = 0; m_pllchange = 0; m_cache_ctrl = 0;
    for (auto& v : m_782x) v = 0;
    m_7835 = 0; m_7862_porta_direction = 0; m_7863_porta_attribute = 0; m_786a_portb_direction = 0; m_786b_portb_attribute = 0;
    m_ioc_data = 0; m_7872_portc_direction = 0; m_7873_portc_attribute = 0; m_ioe_dir = 0; m_ioe_attrib = 0;
    m_int_status1 = 0; m_int_priority_1 = m_int_priority_2 = m_int_priority_3 = 0; m_misc_int_ctrl = 0; m_cha_ctrl = 0;
    m_rtc_ctrl = 0; m_rtc_int_status = 0; m_rtc_int_ctrl = 0; m_adc_setup = 0; m_madc_ctrl = 0;
    for (int i = 0; i < 6; i++) { m_timer_ctrl[i] = 0; m_timer_preload[i] = 0; }
    for (int i = 0; i < 3; i++) { m_timer_ccp_ctrl[i] = 0; m_timer_cc_reg[i] = 0; }
    m_nand_addr_low = m_nand_addr_high = m_nand_dma_ctrl = m_nand_ctrl = 0;
    m_nand_ecc_cpckr_lb = m_nand_ecc_lpr_ckh_lb = m_nand_ecc_lpr_ckl_lb = m_nand_bch_ctrl = m_nand_ecc_ctrl = 0;
    m_esp_ctrl = 0; m_byteswap = 0;
    // sound, video, dma, timebase
    m_audio.device_reset();
    m_video.device_reset();
    for (int j = 0; j < 4; j++) { for (int i = 0; i < 8; i++) m_dma_params[i][j] = 0; m_dma_latched[j] = false; }
    m_system_dma_memtype = 0; m_dma_status = 0;
    for (int i = 0; i < 3; i++) m_timebase_ctrl[i] = 0;
    m_timebase_reset = 0;
    m_cpu.reset();

    // driver machine_reset
    if (m_kind == BEIJUEHH) {
        m_portb_data = 0; m_portd_data = 0; m_bank = 0; m_upperbase = 0;
        cs_callback(0, 0, 0, 0, 0);
    } else if (m_kind == SPISPI) {
        cs_callback(0, 0, 0, 0, 0);
        m_spi = SpiFlash();
        // init_spi: copy the first 0x2000 words to the address in header byte 0x15, point the
        // boot vector at dest + 0x20 and the other vectors at the RAM table at 2fe0
        const uint8_t* b = (const uint8_t*)m_rom;
        int dest = b[0x15] << 8;
        for (int i = 0; i < 0x2000 && uint64_t(i) < m_romWords; i++) write_word(uint32_t(dest + i), m_rom[i]);
        memset(m_internal, 0, sizeof(m_internal));
        m_internal[0x7ff5] = u16(m_vectorbase + 0x0a);
        m_internal[0x7ff6] = u16(m_vectorbase + 0x0c);
        m_internal[0x7ff7] = u16(dest + 0x20);
        for (int i = 0; i < 8; i++) m_internal[0x7ff8 + i] = u16(m_vectorbase + 0x10 + i * 2);
        m_boot_mode = 0;
    } else {
        write_word(0x007820, 0x0047);
        write_word(0x007821, 0xff47);
        write_word(0x007822, 0x00c7);
        write_word(0x007823, 0x0047);
        write_word(0x007824, 0x0047);
        m_nand = Nand();
        // simulate the bootstrap / internal ROM (generalplus_gpac800_game_state::machine_reset)
        int dest = m_stripped[0x15] << 8 | (m_stripped[0x16] << 16);
        for (int i = 0; i < m_initialCopyWords; i++) {
            u16 w = u16(m_stripped[size_t(i) * 2] | (m_stripped[size_t(i) * 2 + 1] << 8));
            write_word(uint32_t(dest + i), w);
        }
        memset(m_internal, 0, sizeof(m_internal));
        auto vec = [&](int idx, uint32_t addr) { m_internal[idx] = u16(0xfe80 | (addr >> 16)); m_internal[idx + 1] = u16(addr & 0xffff); };
        vec(0x7f00, (m_vectorbase + 0x0a) & 0xfffff);
        vec(0x7f02, (m_vectorbase + 0x0c) & 0xfffff);
        vec(0x7f04, uint32_t(dest + 0x20) & 0x1fffff);
        for (int i = 0; i < 8; i++) vec(0x7f06 + i * 2, (m_vectorbase + 0x10 + i * 2) & 0xfffff);
        m_internal[0x7ff5] = 0xff00;
        for (int i = 0; i < 10; i++) m_internal[0x7ff6 + i] = u16(0xff02 + i * 2);
        m_boot_mode = 0;
    }
    m_cpu.reset();
    m_samples.clear();
}

// ---------------------------------------------------------------------------------------------
// program space (gpl16218b_map / gpl16258vb_map)

uint16_t Gpl162xxCore::read_word(uint32_t a)
{
    a &= 0x3fffff;
    if (a < m_ramTop) return m_mainram[a];
    if (a < 0x7000) return 0;
    if (a < 0x8000) return m_kind == BEIJUEHH ? io_read_16218b(a) : io_read_16258vb(a);
    if (a < 0x10000) return m_kind == BEIJUEHH ? cs_space_boot_mirror_r(a - 0x8000) : internalrom_lower32_r(a - 0x8000);
    if (a < 0x20000) return m_kind == BEIJUEHH ? 0 : 0;
    if (a < 0x200000) return cs_space_r(a - 0x20000);
    return cs_bank_space_r(a - 0x200000);
}

void Gpl162xxCore::write_word(uint32_t a, uint16_t d)
{
    a &= 0x3fffff;
    if (a < m_ramTop) { m_mainram[a] = d; return; }
    if (a < 0x7000) return;
    if (a < 0x8000) { if (m_kind == BEIJUEHH) io_write_16218b(a, d); else io_write_16258vb(a, d); return; }
    if (a < 0x20000) return;
    if (a < 0x200000) { cs_space_w(a - 0x20000, d); return; }
    cs_bank_space_w(a - 0x200000, d);
}

// external chip-select space (gcm394_game_state::cs_callback)
void Gpl162xxCore::cs_callback(u16 cs0, u16 cs1, u16 cs2, u16 cs3, u16 cs4)
{
    u16 cs[5] = { cs0, cs1, cs2, cs3, cs4 };
    u32 start = 0;
    for (int i = 0; i < 5; i++) {
        u32 size = ((u32(cs[i] & 0xff00) >> 8) + 1) * 0x10000;
        m_csStart[i] = start;
        m_csEnd[i] = start + size - 1;
        start += size;
    }
}

u16 Gpl162xxCore::cs_read(u32 a)
{
    if (a <= m_csEnd[0]) return cs0_r(a - m_csStart[0]);
    if (a >= m_csStart[1] && (a <= m_csEnd[1] || m_kind == SPISPI)) return cs1_r(a - m_csStart[1]);
    return 0;
}

void Gpl162xxCore::cs_write(u32 a, u16 d)
{
    if (a <= m_csEnd[0]) { cs0_w(a - m_csStart[0], d); return; }
    if (a >= m_csStart[1] && (a <= m_csEnd[1] || m_kind == SPISPI)) { cs1_w(a - m_csStart[1], d); return; }
}

u16 Gpl162xxCore::cs0_r(offs_t offset)
{
    if (m_kind == BEIJUEHH) {
        uint64_t i = (offset & 0x3fffff) + uint64_t(m_upperbase);
        return i < m_romWords ? m_rom[i] : 0;
    }
    return m_sdram2[offset & 0xffff];
}
void Gpl162xxCore::cs0_w(offs_t offset, u16 data)
{
    if (m_kind != BEIJUEHH) m_sdram2[offset & 0xffff] = data;
}
u16 Gpl162xxCore::cs1_r(offs_t offset)
{
    if (m_kind == BEIJUEHH) return 0;
    return m_sdram[offset & (m_sdram.size() - 1)];
}
void Gpl162xxCore::cs1_w(offs_t offset, u16 data)
{
    if (m_kind != BEIJUEHH) m_sdram[offset & (m_sdram.size() - 1)] = data;
}

// ---------------------------------------------------------------------------------------------
// ports

// SPI type (bkrankp): buttons on port A bits 10-15 and port B bits 10-11, active high
u16 Gpl162xxCore::porta_in() { return m_kind == SPISPI ? u16((m_inputs & 0x3f) << 10) : 0; }
u16 Gpl162xxCore::portb_in() { return m_kind == SPISPI ? u16(((m_inputs >> 6) & 3) << 10) : 0; }
u16 Gpl162xxCore::portc_in()
{
    if (m_kind != BEIJUEHH) return 0xffff;
    // IN2: up / down / left / right on bits 5-8 (active low); bit 12 must read low
    u16 v = 0xefff;
    for (int i = 0; i < 4; i++) if (m_inputs & (1u << i)) v &= u16(~(0x20 << i));
    return v;
}
u16 Gpl162xxCore::portd_in()
{
    if (m_kind != BEIJUEHH) return 0xffff;
    // IN3: buttons 1, 2 and Menu on bits 1-3 (active low)
    u16 v = 0xffff;
    for (int i = 0; i < 3; i++) if (m_inputs & (1u << (4 + i))) v &= u16(~(2 << i));
    return v;
}
void Gpl162xxCore::porta_out(u16) {}
void Gpl162xxCore::portc_out(u16) {}
void Gpl162xxCore::portb_out(u16 data)
{
    if (m_kind == BEIJUEHH) {
        if (m_cpu.pc() < 0xf000) {
            if (data & 0x0020) m_bank |= 0x04; else m_bank &= ~0x04;
            if (data & 0x0080) m_bank |= 0x08; else m_bank &= ~0x08;
            m_upperbase = u32(m_bank) * 0x400000;
        }
        m_portb_data = data;
    }
}
void Gpl162xxCore::portd_out(u16 data)
{
    if (m_kind == BEIJUEHH) {
        if (m_cpu.pc() < 0xf000) {
            if (data & 0x4000) m_bank |= 0x02; else m_bank &= ~0x02;
            if (data & 0x8000) m_bank |= 0x01; else m_bank &= ~0x01;
            m_upperbase = u32(m_bank) * 0x400000;
        }
        m_portd_data = data;
    }
}

// ---------------------------------------------------------------------------------------------
// timebase / DMA (gpl_timebase_device, gpl_dma_device)

attotime Gpl162xxCore::get_timer_period(int timer, int ctrlval)
{
    int which = (timer << 2) | ctrlval;
    if (which != 0) return attotime::from_hz(1 << (which - 1));
    return attotime::never();
}

void Gpl162xxCore::timebase_cb(int t)
{
    m_timebase_ctrl[t] |= 0x8000;
    update_interrupts(1);
}

void Gpl162xxCore::timebase_ctrl_w(int t, u16 data)
{
    if (data & 0x8000) { m_timebase_ctrl[t] = data & 0x7fff; update_interrupts(1); }
    else m_timebase_ctrl[t] = data;
    if (m_timebase_ctrl[t] & 0x2000) {
        attotime period = get_timer_period(t, m_timebase_ctrl[t] & 0x0003);
        m_timebasetimer[t]->adjust(period, t, period);
    } else {
        m_timebasetimer[t]->adjust(attotime::never(), t);
    }
}

void Gpl162xxCore::write_dma_params(int channel, int offset, u16 data)
{
    m_dma_params[offset][channel] = data;
    if (offset == 3) {
        m_dma_latched[channel] = true;
        if (m_dma_params[0][channel] & 1) trigger_systemm_dma(channel);
    }
    if (offset == 0 && (data & 1)) {
        if (m_dma_latched[channel]) trigger_systemm_dma(channel);
    }
}

void Gpl162xxCore::trigger_systemm_dma(int channel)
{
    u16 mode = m_dma_params[0][channel];
    u32 source = m_dma_params[1][channel] | (u32(m_dma_params[4][channel]) << 16);
    u32 dest = m_dma_params[2][channel] | (u32(m_dma_params[5][channel]) << 16);
    u32 length = m_dma_params[3][channel] | (u32(m_dma_params[6][channel]) << 16);
    int sourcedelta = 0, destdelta = 0;
    if ((mode & 0xa0) == 0x00) sourcedelta = 1;
    else if ((mode & 0xa0) == 0x20) sourcedelta = -1;
    if ((mode & 0x50) == 0x00) destdelta = 1;
    else if ((mode & 0x50) == 0x10) destdelta = -1;
    source &= 0x0fffffff;
    length &= 0x0fffffff;
    for (u32 i = 0; i < length; i++) {
        u16 val;
        if (mode & 0x1000) { u16 lo = read_space(source) & 0xff; u16 hi = read_space(source); val = u16(lo | (hi << 8)); i++; }
        else val = read_space(source);
        source += sourcedelta;
        if (mode & 0x2000) {
            if (m_kind == SPISPI && !(mode & 0x1000)) i++;   // byte lengths (the SPI DMA pairs)
            write_space(dest, val & 0xff);
            dest += destdelta;
            write_space(dest, val >> 8);
        } else {
            write_space(dest, val);
        }
        dest += destdelta;
    }
    m_dma_params[0][channel] = m_dma_params[0][channel] & 0x00f7;
    m_dma_params[1][channel] = m_dma_params[2][channel] = m_dma_params[3][channel] = m_dma_params[4][channel] = m_dma_params[5][channel] = m_dma_params[6][channel] = 0;
    m_dma_latched[channel] = false;
    m_dma_status |= 1 << channel;
}

// ---------------------------------------------------------------------------------------------
// NAND (beambox): HY27US08121A-style 512 + 16 byte pages

void Gpl162xxCore::nandInit()
{
    // generalplus_gpac800_game_state::nand_create_stripped_region: drop the spare bytes
    const uint8_t* raw = (const uint8_t*)m_rom;
    size_t rawLen = size_t(m_romWords) * 2;
    size_t pages = rawLen / 528;
    m_stripped.assign(pages * 512, 0);
    for (size_t p = 0; p < pages; p++) memcpy(&m_stripped[p * 512], raw + p * 528, 512);
    m_sdram.assign(0x400000, 0);       // m_sdram_kwords default (4 Mwords)
    m_sdram2.assign(0x10000, 0);
    m_vectorbase = 0x2fe0;             // nand_beambox
}

void Gpl162xxCore::nand_command_out(u8 d)
{
    m_nand.cmd = d;
    switch (d) {
    case 0x00: case 0x01: case 0x50: m_nand.mode = 1; m_nand.addrCount = 0; m_nand.col = d == 0x01 ? 256 : d == 0x50 ? 512 : 0; break;
    case 0x70: m_nand.mode = 2; break;
    case 0x90: m_nand.mode = 3; m_nand.idPos = 0; m_nand.addrCount = 0; break;
    case 0xff: m_nand.mode = 0; break;
    default: break;
    }
}

void Gpl162xxCore::nand_address_out(u8 d)
{
    if (m_nand.mode == 1) {
        if (m_nand.addrCount == 0) m_nand.col = (m_nand.col & ~0xffu) + d;
        else if (m_nand.addrCount == 1) m_nand.row = d;
        else if (m_nand.addrCount == 2) m_nand.row |= u32(d) << 8;
        else if (m_nand.addrCount == 3) m_nand.row |= u32(d) << 16;
        m_nand.addrCount++;
        m_nand.pos = m_nand.row * 528 + m_nand.col;
    }
}

u16 Gpl162xxCore::nand_data_in()
{
    if (m_nand.mode == 2) return 0x40 | 0x80;           // ready, not write protected
    if (m_nand.mode == 3) {
        static const u8 id[4] = { 0xad, 0x76, 0, 0 };     // HY27US08121A
        return id[(m_nand.idPos++) & 3];
    }
    if (m_nand.mode == 1) {
        size_t len = size_t(m_romWords) * 2;
        u8 v = m_nand.pos < len ? ((const uint8_t*)m_rom)[m_nand.pos] : 0xff;
        m_nand.pos++;
        return v;
    }
    return 0xff;
}

void Gpl162xxCore::nand_data_out(u8) {}

// ---------------------------------------------------------------------------------------------
// screen

bool Gpl162xxCore::update_partial(int scanline)
{
    if (scanline < m_screen.m_last_partial_scan) return false;
    if (m_screen.m_last_partial_scan == 0 && m_screen.m_last_partial_reset > m_machine.time()) return false;
    int top = std::max(m_screen.m_vis_top, m_screen.m_last_partial_scan);
    int bottom = std::min(m_screen.m_vis_bottom, scanline);
    if (top > bottom) return false;
    gpl::bitmap_rgb32 bm; bm.base = m_bitmap.data(); bm.rowpixels = 640;
    gpl::rectangle clip(0, m_visW - 1, top, bottom);
    m_video.screen_update(bm, clip);
    m_screen.m_last_partial_scan = scanline + 1;
    return true;
}

void Gpl162xxCore::reset_partial_updates()
{
    m_screen.m_last_partial_reset = m_machine.time();
    m_screen.m_last_partial_scan = 0;
    m_scanline0_timer->adjust(m_screen.time_until_pos(0));
}

void Gpl162xxCore::vblank_begin()
{
    m_screen.m_vblank_start_time = m_machine.time();
    m_screen.m_vblank_end_time = m_screen.m_vblank_start_time + attotime(0, m_screen.m_vblank_period);
    update_partial(m_screen.m_vis_bottom);
    m_outW = m_visW; m_outH = m_visH;
    m_out.resize(size_t(m_outW) * m_outH);
    for (int y = 0; y < m_outH; y++)
        for (int x = 0; x < m_outW; x++) m_out[size_t(y) * m_outW + x] = m_bitmap[size_t(y) * 640 + x] | 0xff000000u;
    m_frameDone = true;
    m_video.vblank(1);
    m_vblank_begin_timer->adjust(m_screen.time_until_vblank_start());
    m_vblank_end_timer->adjust(m_screen.time_until_vblank_end());
}

void Gpl162xxCore::vblank_end()
{
    m_video.vblank(0);
    m_screen.m_frame_number++;
}

void Gpl162xxCore::runFrame()
{
    m_frameDone = false;
    int guard = 0;
    while (!m_frameDone && guard++ < 100000) {
        m_machine.timeslice(attotime::never());
        m_machine.execute_timers();
    }
    auto& o = m_audio.m_out;
    for (size_t i = 0; i + 1 < o.size(); i += 2) {
        m_resampleSum += (o[i] + o[i + 1]) / 2;
        m_resampleN++;
        m_resampleAcc += 44100.0;
        if (m_resampleAcc >= spg2xx_audio_device::STREAM_RATE) {
            m_resampleAcc -= spg2xx_audio_device::STREAM_RATE;
            double v = double(m_resampleSum) / m_resampleN;
            m_resampleSum = 0; m_resampleN = 0;
            double y = v - m_dcPrevIn + 0.995 * m_dcPrevOut;
            m_dcPrevIn = v; m_dcPrevOut = y;
            int s = int(y * 2.0);
            m_samples.push_back(int16_t(std::max(-32768, std::min(32767, s))));
        }
    }
    o.clear();
    if (m_samples.size() > 44100) m_samples.erase(m_samples.begin(), m_samples.end() - 4096);
    if (dbg()) fprintf(stderr, "pc %06x\n", m_cpu.pc());
}

size_t Gpl162xxCore::readAudio(int16_t* out, size_t maxSamples)
{
    size_t n = std::min(maxSamples, m_samples.size());
    memcpy(out, m_samples.data(), n * sizeof(int16_t));
    m_samples.erase(m_samples.begin(), m_samples.begin() + long(n));
    return n;
}

// ---------------------------------------------------------------------------------------------
// SPI flash (SPISPI kind)

void Gpl162xxCore::spiTx(uint8_t d)
{
    // full duplex: the byte clocked in while this one goes out
    m_spi.rx.push_back(spiRead());
    if (m_spi.rx.size() > 0x100000) m_spi.rx.pop_front();
    switch (m_spi.state) {
    case 0: if (d == 0x03) m_spi.state = 1; else if (d == 0x0b) m_spi.state = 11; else if (d == 0x9f) m_spi.state = 20; else if (d == 0x05) m_spi.state = 30; break;
    case 1: case 11: m_spi.addr = uint32_t(d) << 16; m_spi.state++; break;
    case 2: case 12: m_spi.addr |= uint32_t(d) << 8; m_spi.state++; break;
    case 3: m_spi.addr |= d; m_spi.state = 5; break;
    case 13: m_spi.addr |= d; m_spi.state = 14; break;
    case 14: m_spi.state = 5; break;
    default: break;
    }
}

uint8_t Gpl162xxCore::spiRead()
{
    if (m_spi.state == 5) {
        uint64_t i = m_spi.addr++ & 0xffffff;
        const uint8_t* b = (const uint8_t*)m_rom;
        return i < m_romWords * 2 ? b[i] : 0xff;
    }
    if (m_spi.state == 30) return 0x00;
    if (m_spi.state >= 20 && m_spi.state < 23) { static const uint8_t id[3] = { 0xc2, 0x20, 0x16 }; return id[m_spi.state++ - 20]; }
    return 0xff;
}
