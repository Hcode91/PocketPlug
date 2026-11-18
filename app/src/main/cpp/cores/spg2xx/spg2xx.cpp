// PocketPlug - SunPlus SPG2xx SoC glue and the 265-in-1 handheld board
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Replaces the MAME framework parts of spg2xx.cpp (address map, IRQ routing),
// screen_device (VBLANK/partial updates), the sound stream and the driver code
// of spg2xx_wiwi.cpp (spg2xx_game_marc250_state: ROM banking through port B,
// the port A "security" pulses, the IRQ patches and the doubled CPU clock).

#include "spg2xx.h"
#include <cstdio>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static const uint32_t SPG_XTAL = 27000000;

// ---------------------------------------------------------------------------
// framework stand-ins used by the generated device code
// ---------------------------------------------------------------------------
SpgMachine& spg2xx_io_device::machine() { return m_soc->m_machine; }
SpgMachine& spg2xx_audio_device::machine() { return m_soc->m_machine; }
uint32_t spg2xx_io_device::clock() { return SPG_XTAL * 2; }   // derived from the scaled CPU clock
uint16_t spg_renderer_device::read_word(uint32_t a) { return m_soc->read_word(a); }

void spg2xx_io_device::m_porta_out(int, uint16_t data, uint16_t) { m_soc->porta_w(data); }
void spg2xx_io_device::m_portb_out(int, uint16_t data, uint16_t) { m_soc->portb_w(data); }
void spg2xx_io_device::m_portc_out(int, uint16_t, uint16_t) { }
uint16_t spg2xx_io_device::m_porta_in(int, uint16_t) { return m_soc->porta_r(); }
uint16_t spg2xx_io_device::m_portb_in(int, uint16_t)
{
    // m489 "P2", active low: up 0x04, down 0x08, left 0x10, right 0x20, b1 0x40, b2 0x80
    uint32_t in = m_soc->m_inputs;
    uint16_t v = 0xffff;
    if (in & 0x01) v &= ~0x0004;
    if (in & 0x02) v &= ~0x0008;
    if (in & 0x04) v &= ~0x0010;
    if (in & 0x08) v &= ~0x0020;
    if (in & 0x10) v &= ~0x0040;
    if (in & 0x20) v &= ~0x0080;
    return v;
}
uint16_t spg2xx_io_device::m_portc_in(int, uint16_t)
{
    // "P3": start 0x01, active low
    return (m_soc->m_inputs & 0x40) ? 0xfffe : 0xffff;
}
uint16_t spg2xx_io_device::m_pal_read_cb() { return 1; }     // set_pal(true)
void spg2xx_io_device::m_timer_irq_cb(int s) { m_soc->timerirq_w(s); }
void spg2xx_io_device::m_uart_adc_irq_cb(int s) { m_soc->uartirq_w(s); }
void spg2xx_io_device::m_external_irq_cb(int s) { m_soc->extirq_w(s); }
void spg2xx_io_device::m_ffreq_tmr1_irq_cb(int s) { m_soc->ffreq1_w(s); }
void spg2xx_io_device::m_ffreq_tmr2_irq_cb(int s) { m_soc->ffreq2_w(s); }
void spg2xx_io_device::m_fiq_vector_w(uint8_t data) { m_soc->m_fiq_vector = data; }
void spg2xx_audio_device::m_irq_cb(int s) { m_soc->audioirq_w(s); }
void spg2xx_audio_device::m_ch_irq_cb(int s) { m_soc->audiochirq_w(s); }

#define TIMER(cls, fn) [](void* c, int p) { ((cls*)c)->fn(p); }

void spg2xx_io_device::device_start()
{
    SpgMachine& m = machine();
    m_tmb1 = m.timer_alloc(TIMER(spg2xx_io_device, tmb_timer_tick<0>), this);
    m_tmb2 = m.timer_alloc(TIMER(spg2xx_io_device, tmb_timer_tick<1>), this);
    m_uart_tx_timer = m.timer_alloc(TIMER(spg2xx_io_device, uart_transmit_tick), this);
    m_uart_rx_timer = m.timer_alloc(TIMER(spg2xx_io_device, uart_receive_tick), this);
    m_4khz_timer = m.timer_alloc(TIMER(spg2xx_io_device, system_timer_tick), this);
    m_timer_src_ab = m.timer_alloc(TIMER(spg2xx_io_device, timer_ab_tick), this);
    m_timer_src_c = m.timer_alloc(TIMER(spg2xx_io_device, timer_c_tick), this);
    m_rng_timer = m.timer_alloc(TIMER(spg2xx_io_device, rng_clock_tick), this);
    m_watchdog_timer = m.timer_alloc(TIMER(spg2xx_io_device, watchdog_tick), this);
    m_spi_tx_timer = m.timer_alloc(TIMER(spg2xx_io_device, spi_tx_tick), this);
    m_adc_timer[0] = m.timer_alloc(TIMER(spg2xx_io_device, adc_convert_tick<0>), this);
    m_adc_timer[1] = m.timer_alloc(TIMER(spg2xx_io_device, adc_convert_tick<1>), this);
    m_adc_timer[2] = m.timer_alloc(TIMER(spg2xx_io_device, adc_convert_tick<2>), this);
    m_adc_timer[3] = m.timer_alloc(TIMER(spg2xx_io_device, adc_convert_tick<3>), this);
}

void spg2xx_video_device::device_start()
{
    m_screenpos_timer = m_soc->m_machine.timer_alloc(TIMER(spg2xx_video_device, screenpos_hit), this);
    m_screenpos_timer->adjust(attotime::never());
}

void spg2xx_video_device::screen_update(int min_y, int max_y, uint32_t* bitmap)
{
    const uint32_t page1_addr = m_video_regs[0x20];
    const uint32_t page2_addr = m_video_regs[0x21];
    const uint32_t sprite_addr = 0x40 * m_video_regs[0x22];
    uint16_t *page1_scroll = m_video_regs + 0x10;
    uint16_t *page2_scroll = m_video_regs + 0x16;
    uint16_t *page1_regs = m_video_regs + 0x12;
    uint16_t *page2_regs = m_video_regs + 0x18;

    for (uint32_t scanline = (uint32_t)min_y; scanline <= (uint32_t)max_y; scanline++)
    {
        m_renderer->new_line();
        for (int i = 0; i < 4; i++)
        {
            m_renderer->draw_page(scanline, i, page1_addr, page1_scroll, page1_regs, m_paletteram, m_scrollram, 0);
            m_renderer->draw_page(scanline, i, page2_addr, page2_scroll, page2_regs, m_paletteram, m_scrollram, 1);
            m_renderer->draw_sprites(scanline, i, sprite_addr, m_paletteram, m_spriteram, 256);
        }
        m_renderer->apply_saturation_and_fade(bitmap + scanline * 320);
    }
}

void spg_renderer_device::device_start()
{
    m_rgb555_to_rgb888 = std::make_unique<uint32_t[]>(0x8000);
    m_rgb555_to_rgb888_current = std::make_unique<uint32_t[]>(0x8000);
    for (uint8_t i = 0; i < 32; i++)
        m_rgb5_to_rgb8[i] = (i << 3) | (i >> 2);
    for (uint16_t i = 0; i < 0x8000; i++)
    {
        m_rgb555_to_rgb888[i] = (m_rgb5_to_rgb8[(i >> 10) & 0x1f] << 16) |
                                (m_rgb5_to_rgb8[(i >>  5) & 0x1f] <<  8) |
                                (m_rgb5_to_rgb8[(i >>  0) & 0x1f] <<  0);
        m_rgb555_to_rgb888_current[i] = 0x0000;
    }
}

void spg_renderer_device::new_line()
{
    update_palette_lookup();
    for (int x = 0; x < 320; x++)
        m_linebuf[x] = 0x8000; // transparent
}

void spg_renderer_device::apply_saturation_and_fade(uint32_t* dst)
{
    for (int x = 0; x < 320; x++)
    {
        uint16_t px = (m_linebuf[x] & 0x8000) ? 0x0 : m_linebuf[x];
        dst[x] = 0xff000000u | m_rgb555_to_rgb888_current[px];
    }
}

void spg2xx_audio_device::device_start()
{
    SpgMachine& m = machine();
    m_audio_beat = m.timer_alloc(TIMER(spg2xx_audio_device, audio_beat_tick), this);
    m_audio_beat->adjust(attotime::never());
    for (int i = 0; i < 16; i++)
    {
        memset(m_adpcm36_state + i, 0, sizeof(adpcm36_state));
        m_channel_irq[i] = m.timer_alloc(TIMER(spg2xx_audio_device, irq_tick), this);
        m_channel_irq[i]->adjust(attotime::never());
    }
}

void spg2xx_audio_device::stream_update()
{
    // sound_stream::update(): produce the samples due up to "now"
    const attotime now = machine().time();
    // floor(attoseconds * rate / 1e18) without 128-bit math
    const uint64_t a_hi = (uint64_t)now.attoseconds() / 1000000000ULL, a_lo = (uint64_t)now.attoseconds() % 1000000000ULL;
    const uint64_t target = (uint64_t)now.seconds() * STREAM_RATE
        + (a_hi * STREAM_RATE + (a_lo * STREAM_RATE) / 1000000000ULL) / 1000000000ULL;
    if (target <= m_stream_samples) return;
    int n = (int)std::min<uint64_t>(target - m_stream_samples, 1 << 16);
    m_stream_samples = target;
    int16_t l[512], r[512];
    while (n > 0) {
        int c = std::min(n, 512);
        sound_stream_update(l, r, c);
        for (int i = 0; i < c; i++) { m_out.push_back(l[i]); m_out.push_back(r[i]); }
        n -= c;
    }
}


// ---------------------------------------------------------------------------
// SoC
// ---------------------------------------------------------------------------
Spg2xxCore::Spg2xxCore(const Spg2xxConfig& c) : m_cfg(c)
{
    memset(m_bitmap, 0, sizeof(m_bitmap));
    memset(m_out, 0, sizeof(m_out));
    m_machine.m_icountptr = &m_cpu.m_core->m_icount;
    m_machine.m_cpu_run = [this]() { m_cpu.execute_run(); };
    m_machine.set_cpu_clock(SPG_XTAL * 2);   // set_clock_scale(2.0) in the marc250 driver
    m_cpu.set_bus(this);
    m_screen.m_machine = &m_machine;
    m_screen.configure(320, 262, 239, HZ_TO_ATTOSECONDS(50));

    m_spg_io.m_soc = this; m_spg_io.m_screen = &m_screen; m_spg_io.m_cpu = &m_cpu;
    m_spg_video.m_soc = this; m_spg_video.m_screen = &m_screen; m_spg_video.m_renderer = &m_renderer;
    m_spg_video.m_scrollram = m_scrollram; m_spg_video.m_hcompram = m_hcompram;
    m_spg_video.m_paletteram = m_paletteram; m_spg_video.m_spriteram = m_spriteram;
    m_renderer.m_soc = this;
    m_spg_sysdma.m_soc = this;
    m_spg_audio.m_soc = this;
}

Spg2xxCore::~Spg2xxCore()
{
    if (m_map) munmap(m_map, m_mapLen);
}

bool Spg2xxCore::load(const std::vector<RomFile>& roms, std::string& error)
{
    const RomFile* r = nullptr;
    for (auto& f : roms) if (f.name == m_cfg.romName) r = &f;
    if (!r && roms.size() == 1) r = &roms[0];
    if (!r) { error = "Missing " + m_cfg.romName; return false; }
    if (!r->path.empty()) {
        // map the (huge) ROM privately: the IRQ patches below are copy-on-write
        int fd = open(r->path.c_str(), O_RDONLY);
        if (fd < 0) { error = "Could not open " + m_cfg.romName; return false; }
        struct stat st;
        fstat(fd, &st);
        m_mapLen = (size_t)st.st_size;
        m_map = mmap(nullptr, m_mapLen, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
        close(fd);
        if (m_map == MAP_FAILED) { m_map = nullptr; error = "Could not map " + m_cfg.romName; return false; }
        m_rom = (uint16_t*)m_map;
        m_romWords = m_mapLen / 2;
    } else {
        m_romCopy.assign((r->data.size() + 1) / 2, 0);
        memcpy(m_romCopy.data(), r->data.data(), r->data.size());
        m_rom = m_romCopy.data();
        m_romWords = m_romCopy.size();
    }
    // ROM_LOAD16_WORD_SWAP on a little-endian host: words are stored low byte first
    for (size_t i = 0; i < m_cfg.romPatches.size(); i++)
        if (m_cfg.romPatches[i] < m_romWords) m_rom[m_cfg.romPatches[i]] = m_cfg.romPatchValues[i];
    startDevices();
    reset();
    return true;
}

void Spg2xxCore::startDevices()
{
    // sound_manager's periodic stream update is the first timer MAME creates
    m_sound_update_timer = m_machine.timer_alloc([](void* c, int) {
        ((Spg2xxCore*)c)->m_spg_audio.stream_update(); }, this);
    m_sound_update_timer->adjust(attotime::from_hz(50), 0, attotime::from_hz(50));

    // screen_device::device_start
    m_vblank_begin_timer = m_machine.timer_alloc([](void* c, int) { ((Spg2xxCore*)c)->vblank_begin(); }, this);
    m_vblank_end_timer = m_machine.timer_alloc([](void* c, int) { ((Spg2xxCore*)c)->vblank_end(); }, this);
    m_scanline0_timer = m_machine.timer_alloc([](void* c, int) { ((Spg2xxCore*)c)->reset_partial_updates(); }, this);
    m_vblank_begin_timer->adjust(m_screen.time_until_vblank_start());
    m_scanline0_timer->adjust(m_screen.time_until_pos(0));
    m_screen.m_vblank_start_time = attotime::zero();
    m_screen.m_vblank_end_time = attotime(0, m_screen.m_vblank_period);

    m_spg_audio.device_start();
    m_spg_io.device_start();
    m_spg_video.device_start();
    m_renderer.device_start();

    // spg2xx_game_marc101_state / marc250_state::machine_start
    m_pulse_timer = m_machine.timer_alloc([](void* c, int) { auto* s = (Spg2xxCore*)c; s->m_toggle = !s->m_toggle; }, this);
    m_pulse_timer->adjust(attotime::never());
    m_pulse_timer2 = m_machine.timer_alloc([](void* c, int) { auto* s = (Spg2xxCore*)c; s->m_toggle2 = !s->m_toggle2; }, this);
    m_pulse_timer2->adjust(attotime::never());
}

void Spg2xxCore::reset()
{
    // running_machine::soft_reset: device_reset of every device in tree order,
    // then the driver's machine_reset
    memset(m_ram, 0, sizeof(m_ram));
    memset(m_scrollram, 0, sizeof(m_scrollram));
    memset(m_hcompram, 0, sizeof(m_hcompram));
    memset(m_paletteram, 0, sizeof(m_paletteram));
    memset(m_spriteram, 0, sizeof(m_spriteram));
    m_cpu.reset();
    m_fiq_vector = 0xff;
    m_spg_audio.device_reset();
    m_spg_io.device_reset();
    m_spg_sysdma.device_reset();
    m_spg_video.device_reset();
    m_renderer.device_reset();

    m_current_bank = -1;
    switch_bank(0);
    m_cpu.reset();
    m_pulse_timer->adjust(attotime::never());
    m_prev_porta = 0;
    m_prev_portb = 0;
    m_toggle = false;
    switch_bank(31);
    m_cpu.reset();
    m_pulse_timer2->adjust(attotime::never());
    m_toggle2 = false;
    m_audio.clear();
}

// ---------------------------------------------------------------------------
// address map (spg2xx_device::internal_map + mem_map_4m)
// ---------------------------------------------------------------------------
uint16_t Spg2xxCore::read_word(uint32_t a)
{
    a &= 0x7fffff;
    if (a < 0x4000) {
        if (a < 0x2800) return m_ram[a];
        if (a < 0x2900) return m_spg_video.video_r(a - 0x2800);
        if (a < 0x2a00) return m_scrollram[a - 0x2900];
        if (a < 0x2b00) return m_hcompram[a - 0x2a00];
        if (a < 0x2c00) return m_paletteram[a - 0x2b00];
        if (a < 0x3000) return m_spriteram[a - 0x2c00];
        if (a < 0x3200) return m_spg_audio.audio_r(a - 0x3000);
        if (a < 0x3400) return m_spg_audio.audio_phase_r(a - 0x3200);
        if (a < 0x3800) return m_spg_audio.audio_ctrl_r(a - 0x3400);
        if (a >= 0x3d00 && a < 0x3d30) return m_spg_io.io_r(a - 0x3d00);
        if (a >= 0x3d30 && a < 0x3e00) return m_spg_io.io_extended_r(a - 0x3d30);
        if (a >= 0x3e00 && a < 0x3e04) return m_spg_sysdma.dma_r(a - 0x3e00);
    }
    return rom_word(a);
}

void Spg2xxCore::write_word(uint32_t a, uint16_t d)
{
    a &= 0x7fffff;
    if (a >= 0x4000) return;
    if (a < 0x2800) { m_ram[a] = d; return; }
    if (a < 0x2900) { m_spg_video.video_w(a - 0x2800, d); return; }
    if (a < 0x2a00) { m_scrollram[a - 0x2900] = d; return; }
    if (a < 0x2b00) { m_hcompram[a - 0x2a00] = d; return; }
    if (a < 0x2c00) { m_paletteram[a - 0x2b00] = d; return; }
    if (a < 0x3000) { m_spriteram[a - 0x2c00] = d; return; }
    if (a < 0x3200) { m_spg_audio.audio_w(a - 0x3000, d); return; }
    if (a < 0x3400) { m_spg_audio.audio_phase_w(a - 0x3200, d); return; }
    if (a < 0x3800) { m_spg_audio.audio_ctrl_w(a - 0x3400, d); return; }
    if (a >= 0x3d00 && a < 0x3d30) { m_spg_io.io_w(a - 0x3d00, d); return; }
    if (a >= 0x3d30 && a < 0x3e00) { m_spg_io.io_extended_w(a - 0x3d30, d); return; }
    if (a >= 0x3e00 && a < 0x3e04) { m_spg_sysdma.dma_w(a - 0x3e00, d); return; }
}

// ---------------------------------------------------------------------------
// spg2xx_game_marc250_state
// ---------------------------------------------------------------------------
uint16_t Spg2xxCore::porta_r()
{
    uint16_t ret = 0xffff & ~0x6000;     // "P1" is all unused (active low)
    ret |= m_toggle ? 0x2000 : 0x0000;
    ret |= m_toggle2 ? 0x4000 : 0x0000;
    return ret;
}

void Spg2xxCore::porta_w(uint16_t data)
{
    if ((data & 0x1000) != (m_prev_porta & 0x1000))
    {
        if (!(data & 0x1000))
        {
            m_pulse_timer->adjust(attotime::from_hz(6), 0, attotime::from_hz(6));
            m_pulse_timer2->adjust(attotime::from_hz(12), 0, attotime::from_hz(12));
        }
    }
    m_prev_porta = data;
}

void Spg2xxCore::portb_w(uint16_t data)
{
    if ((m_cpu.pc() < 0x2000) && (data != 0x00ff) && (data != 0x0000))
    {
        static const uint8_t banks[32] = {
            // index = ((data >> 11) & 3) << 3 | (data & 7): 0x1000 = bit 12, 0x0800 = bit 11
            0, 1, 2, 3, 16, 17, 18, 19,        // 0x0000..0x0007
            8, 9, 10, 11, 24, 25, 26, 27,      // 0x0800..0x0807
            4, 5, 6, 7, 20, 21, 22, 23,        // 0x1000..0x1007
            12, 13, 14, 15, 28, 29, 30, 31 };  // 0x1800..0x1807
        int idx = (((data >> 11) & 3) << 3) | (data & 7);
        switch_bank(banks[idx]);
    }
}

// ---------------------------------------------------------------------------
// screen
// ---------------------------------------------------------------------------
bool Spg2xxCore::update_partial(int scanline)
{
    if (scanline < m_screen.m_last_partial_scan) return false;
    if (m_screen.m_last_partial_scan == 0 && m_screen.m_last_partial_reset > m_machine.time()) return false;
    int top = std::max(m_screen.m_vis_top, m_screen.m_last_partial_scan);
    int bottom = std::min(m_screen.m_vis_bottom, scanline);
    if (top > bottom) return false;
    m_spg_video.screen_update(top, bottom, m_bitmap);
    m_screen.m_last_partial_scan = scanline + 1;
    return true;
}

void Spg2xxCore::reset_partial_updates()
{
    m_screen.m_last_partial_reset = m_machine.time();
    m_screen.m_last_partial_scan = 0;
    m_scanline0_timer->adjust(m_screen.time_until_pos(0));
}

void Spg2xxCore::vblank_begin()
{
    m_screen.m_vblank_start_time = m_machine.time();
    m_screen.m_vblank_end_time = m_screen.m_vblank_start_time + attotime(0, m_screen.m_vblank_period);
    // video_manager::frame_update -> finish_screen_updates
    update_partial(m_screen.m_vis_bottom);
    memcpy(m_out, m_bitmap, sizeof(m_out));
    m_frameDone = true;
    m_spg_video.vblank(1);
    m_vblank_begin_timer->adjust(m_screen.time_until_vblank_start());
    m_vblank_end_timer->adjust(m_screen.time_until_vblank_end());
}

void Spg2xxCore::vblank_end()
{
    m_spg_video.vblank(0);
    m_screen.m_frame_number++;
}

// ---------------------------------------------------------------------------
// frame loop and audio
// ---------------------------------------------------------------------------
void Spg2xxCore::runFrame()
{
    m_frameDone = false;
    while (!m_frameDone) {
        m_machine.timeslice(attotime::never());
        m_machine.execute_timers();
    }
    // stereo 70312 Hz -> mono 44.1 kHz (box filter) with a DC blocker
    auto& o = m_spg_audio.m_out;
    for (size_t i = 0; i + 1 < o.size(); i += 2) {
        m_resampleSumL += (o[i] + o[i + 1]) / 2;
        m_resampleN++;
        m_resampleAcc += 44100.0;
        if (m_resampleAcc >= spg2xx_audio_device::STREAM_RATE) {
            m_resampleAcc -= spg2xx_audio_device::STREAM_RATE;
            double v = (double)m_resampleSumL / m_resampleN;
            m_resampleSumL = 0; m_resampleN = 0;
            double y = v - m_dcPrevIn + 0.995 * m_dcPrevOut;
            m_dcPrevIn = v; m_dcPrevOut = y;
            // MAME routes the SoC at 0.5 to each speaker side
            int s = (int)(y * 0.5 * 2.0);
            m_audio.push_back((int16_t)std::max(-32768, std::min(32767, s)));
        }
    }
    o.clear();
    if (m_audio.size() > 44100) m_audio.erase(m_audio.begin(), m_audio.end() - 4096);
}

size_t Spg2xxCore::readAudio(int16_t* out, size_t maxSamples)
{
    size_t n = std::min(maxSamples, m_audio.size());
    memcpy(out, m_audio.data(), n * sizeof(int16_t));
    m_audio.erase(m_audio.begin(), m_audio.begin() + n);
    return n;
}

// ---------------------------------------------------------------------------
// save states
// ---------------------------------------------------------------------------
namespace {
struct Writer {
    std::vector<uint8_t>& o;
    template <typename T> void put(const T& v) { const uint8_t* p = (const uint8_t*)&v; o.insert(o.end(), p, p + sizeof(T)); }
    void raw(const void* p, size_t n) { const uint8_t* b = (const uint8_t*)p; o.insert(o.end(), b, b + n); }
};
struct Reader {
    const uint8_t* p; const uint8_t* end; bool ok = true;
    template <typename T> void get(T& v) { if (p + sizeof(T) > end) { ok = false; return; } memcpy(&v, p, sizeof(T)); p += sizeof(T); }
    void raw(void* d, size_t n) { if (p + n > end) { ok = false; return; } memcpy(d, p, n); p += n; }
};
}

#define SPG_STATE(X) \
    X(m_ram) X(m_scrollram) X(m_hcompram) X(m_paletteram) X(m_spriteram) X(m_fiq_vector) \
    X(m_current_bank) X(m_prev_porta) X(m_prev_portb) X(m_toggle) X(m_toggle2) \
    X(m_cpu.m_local_core) \
    X(m_machine.m_basetime) X(m_machine.m_cpu_localtime) X(m_machine.m_seq_counter) \
    X(m_screen.m_vblank_start_time) X(m_screen.m_vblank_end_time) X(m_screen.m_last_partial_scan) X(m_screen.m_last_partial_reset) X(m_screen.m_frame_number) \
    X(m_spg_io.m_io_regs) X(m_spg_io.m_uart_rx_fifo) X(m_spg_io.m_uart_rx_fifo_start) X(m_spg_io.m_uart_rx_fifo_end) X(m_spg_io.m_uart_rx_fifo_count) \
    X(m_spg_io.m_uart_rx_available) X(m_spg_io.m_uart_rx_irq) X(m_spg_io.m_uart_tx_irq) X(m_spg_io.m_spi_tx_fifo) X(m_spg_io.m_spi_tx_fifo_start) \
    X(m_spg_io.m_spi_tx_fifo_end) X(m_spg_io.m_spi_tx_fifo_count) X(m_spg_io.m_spi_tx_buf) X(m_spg_io.m_spi_tx_bit) X(m_spg_io.m_spi_rx_fifo) \
    X(m_spg_io.m_spi_rx_fifo_start) X(m_spg_io.m_spi_rx_fifo_end) X(m_spg_io.m_spi_rx_fifo_count) X(m_spg_io.m_spi_rx_buf) X(m_spg_io.m_spi_rx_bit) \
    X(m_spg_io.m_extint) X(m_spg_io.m_timer_a_preload) X(m_spg_io.m_timer_b_preload) X(m_spg_io.m_timer_b_divisor) X(m_spg_io.m_timer_b_tick_rate) \
    X(m_spg_io.m_2khz_divider) X(m_spg_io.m_1khz_divider) X(m_spg_io.m_4hz_divider) X(m_spg_io.m_uart_baud_rate) X(m_spg_io.m_spi_rate) \
    X(m_spg_io.m_sio_bits_remaining) X(m_spg_io.m_sio_writing) \
    X(m_spg_video.m_video_regs) \
    X(m_renderer.m_video_regs_1c) X(m_renderer.m_video_regs_1d) X(m_renderer.m_video_regs_1e) X(m_renderer.m_video_regs_2a) \
    X(m_renderer.m_video_regs_30) X(m_renderer.m_video_regs_3c) X(m_renderer.m_video_regs_42) X(m_renderer.m_ycmp_table) X(m_renderer.m_linebuf) \
    X(m_spg_sysdma.m_dma_regs) \
    X(m_spg_audio.m_audio_regs) X(m_spg_audio.m_audio_phase_regs) X(m_spg_audio.m_audio_ctrl_regs) X(m_spg_audio.m_sample_shift) \
    X(m_spg_audio.m_sample_count) X(m_spg_audio.m_channel_rate) X(m_spg_audio.m_channel_rate_accum) X(m_spg_audio.m_rampdown_frame) \
    X(m_spg_audio.m_envclk_frame) X(m_spg_audio.m_envelope_addr) X(m_spg_audio.m_audio_curr_beat_base_count) X(m_spg_audio.m_adpcm) \
    X(m_spg_audio.m_adpcm36_state) X(m_spg_audio.m_stream_samples) \
    X(m_bitmap)

bool Spg2xxCore::saveState(std::vector<uint8_t>& out)
{
    out.clear();
    Writer w{out};
    w.put((uint32_t)0x32475053);   // 'SPG2'
#define PUTX(v) w.raw(&v, sizeof(v));
    SPG_STATE(PUTX)
#undef PUTX
    for (auto* t : m_machine.m_timers) {
        w.put(t->m_start); w.put(t->m_expire); w.put(t->m_period); w.put(t->m_param); w.put(t->m_enabled); w.put(t->m_seq);
    }
    return true;
}

bool Spg2xxCore::loadState(const std::vector<uint8_t>& in)
{
    Reader r{in.data(), in.data() + in.size()};
    uint32_t magic = 0; r.get(magic);
    if (magic != 0x32475053) return false;
#define GETX(v) r.raw(&v, sizeof(v));
    SPG_STATE(GETX)
#undef GETX
    for (auto* t : m_machine.m_timers) {
        r.get(t->m_start); r.get(t->m_expire); r.get(t->m_period); r.get(t->m_param); r.get(t->m_enabled); r.get(t->m_seq);
    }
    m_machine.m_head_dirty = true;
    m_renderer.m_brightness_or_saturation_dirty = true;
    m_spg_audio.m_out.clear();
    m_audio.clear();
    return r.ok && r.p == r.end;
}
