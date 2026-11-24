// PocketPlug - GeneralPlus GPL951xx mini arcades (see gpl951.h)
// SPDX-License-Identifier: BSD-3-Clause

#include "gpl951.h"
#include <cstdio>
#include <cstdlib>

#include "gpl951_soc.inc"
#include "gpl951_iomap.inc"

static bool dbg951() { static int d = getenv("GPLDBG") ? 1 : 0; return d; }

Gpl951Core::Gpl951Core()
{
    m_mainspace.c = this;
    m_chx.c = this;
    m_machine.m_icountptr = &m_cpu.m_core->m_icount;
    m_machine.m_cpu_run = [this]() { m_cpu.execute_run(); };
    m_machine.set_cpu_clock(96000000 / 2);
    m_cpu.set_bus(this);
    m_cpu.set_iso(20);
    m_cpu.set_bootvectorbase(0x8ff8);     // boots from the vector in the internal ROM
    m_cpu.m_irq_ack = [](void* c, int line) { ((Gpl951Core*)c)->irq_vector_cb(line); };
    m_cpu.m_irq_ack_ctx = this;
    m_screen.m_machine = &m_machine;
    // MAME: 640 x 524 at 30 Hz ("60 is definitely too fast for the IRQ rate"), 320 x 240 visible
    m_screen.configure(640, 524, 239, HZ_TO_ATTOSECONDS(30));
    m_audio.m_soc = this;
    m_video.m_screen = &m_screen;
    m_video.m_video_irq_cb = [this](int s) { videoirq_w(s); };
    m_video.m_set_visible = [](int, int) {};
    m_video.m_update_partial = [this](int line) { update_partial(line); };
    m_video.set_video_space(&m_mainspace);
    m_video.set_disallow_resolution_control();
    m_bitmap.assign(640 * 480, 0);
    m_out.assign(320 * 240, 0xff000000u);
}

bool Gpl951Core::load(const std::vector<RomFile>& roms, std::string& error)
{
    const RomFile* boot = nullptr; const RomFile* flash = nullptr;
    for (auto& r : roms) {
        if (r.name == "gpl951.bin") boot = &r;
        else if (!flash || r.data.size() > flash->data.size()) flash = &r;
    }
    if (!boot || boot->data.size() < 0x2000) { error = "Missing gpl951.bin (the chip's boot ROM)"; return false; }
    if (!flash || flash->data.size() < 0x1000) { error = "Missing the SPI flash dump"; return false; }
    memcpy(m_internal, boot->data.data(), 0x2000);       // little-endian words
    // init_fif: every 16-bit word is XORed and bit-swapped
    size_t n = flash->data.size() & ~size_t(1);
    u32 size = 1; while (size < n) size <<= 1;
    m_spi.assign(size, 0);
    static const int order[16] = { 3, 1, 11, 9, 6, 14, 0, 2, 8, 7, 13, 15, 4, 5, 12, 10 };
    for (size_t i = 0; i < n; i += 2) {
        u16 w = u16(flash->data[i] | (flash->data[i + 1] << 8)) ^ 0xdd0d, o = 0;
        for (int b = 0; b < 16; b++) if (w & (1 << order[b])) o |= u16(1 << (15 - b));
        m_spi[i] = u8(o); m_spi[i + 1] = u8(o >> 8);
    }
    m_spiregion = m_spi.data(); m_spisize = size;
    m_flash.ptr = m_spiregion; m_flash.len = size;
    startDevices();
    reset();
    return true;
}

void Gpl951Core::startDevices()
{
    m_sound_update_timer = m_machine.timer_alloc([](void* c, int) { ((Gpl951Core*)c)->m_audio.stream_update(); }, this);
    m_sound_update_timer->adjust(attotime::from_hz(50), 0, attotime::from_hz(50));
    m_vblank_begin_timer = m_machine.timer_alloc([](void* c, int) { ((Gpl951Core*)c)->vblank_begin(); }, this);
    m_vblank_end_timer = m_machine.timer_alloc([](void* c, int) { ((Gpl951Core*)c)->vblank_end(); }, this);
    m_scanline0_timer = m_machine.timer_alloc([](void* c, int) { ((Gpl951Core*)c)->reset_partial_updates(); }, this);
    m_vblank_begin_timer->adjust(m_screen.time_until_vblank_start());
    m_scanline0_timer->adjust(m_screen.time_until_pos(0));
    m_screen.m_vblank_start_time = attotime::zero();
    m_screen.m_vblank_end_time = attotime(0, m_screen.m_vblank_period);
    m_audio.device_start();
    m_video.device_start(m_machine);
    static void (*const cbs[8])(void*, int) = {
        [](void* c, int p) { ((Gpl951Core*)c)->timer_cb<0>(p); }, [](void* c, int p) { ((Gpl951Core*)c)->timer_cb<1>(p); },
        [](void* c, int p) { ((Gpl951Core*)c)->timer_cb<2>(p); }, [](void* c, int p) { ((Gpl951Core*)c)->timer_cb<3>(p); },
        [](void* c, int p) { ((Gpl951Core*)c)->timer_cb<4>(p); }, [](void* c, int p) { ((Gpl951Core*)c)->timer_cb<5>(p); },
        [](void* c, int p) { ((Gpl951Core*)c)->timer_cb<6>(p); }, [](void* c, int p) { ((Gpl951Core*)c)->timer_cb<7>(p); },
    };
    for (int i = 0; i < 8; i++) { m_timer[i] = m_machine.timer_alloc(cbs[i], this); m_timer[i]->adjust(attotime::never()); }
    m_adc_timer = m_machine.timer_alloc([](void* c, int p) { ((Gpl951Core*)c)->adc_timer_cb(p); }, this);
    m_adc_timer->adjust(attotime::never());
    for (int i = 0; i < 3; i++) {
        m_timebasetimer[i] = m_machine.timer_alloc([](void* c, int p) {
            auto* s = (Gpl951Core*)c; s->m_timebase_ctrl[p] |= 0x8000; s->update_interrupts(1); }, this);
        m_timebasetimer[i]->adjust(attotime::never(), i);
    }
    // the RTC counts at 32768 Hz: advanced per frame in runFrame instead of a timer
}

void Gpl951Core::reset()
{
    memset(m_mainram, 0, sizeof(m_mainram));
    m_audio.device_reset();
    m_video.device_reset();
    m_byteswap = 0;
    for (int i = 0; i < 8; i++) { m_timer_ctrl[i] = 0; m_timer_preload[i] = 0; }
    m_spifc_ctrl = m_spifc_ctrl2 = 0; m_spifc_addr = 0; m_spifc_cmd = m_spifc_para = m_spifc_rx_bc = m_spifc_tx_bc = m_spifc_timing = 0;
    m_bytes_in_spifc_rx_fifo = 0; m_spifc_rx_read_latch = 0; memset(m_spifc_rx_fifo, 0, sizeof(m_spifc_rx_fifo));
    m_memmode_wcmd = 0; m_spi_bank = 0; m_sys_ctrl = 0; m_clock_ctrl = 0; m_cache_ctrl = 0;
    m_int_priority_1 = m_int_priority_2 = m_int_priority_3 = 0; m_misc_int_ctrl = 0; m_pllchange = 0; m_tft_rgb_ctrl = 0;
    for (int i = 0; i < 6; i++) { m_io_dir[i] = 0; m_io_attrib[i] = 0; }
    m_chx.reset();
    m_rtc = Rtc();
    m_flash.reset();
    for (int j = 0; j < 4; j++) { for (int i = 0; i < 8; i++) m_dma_params[i][j] = 0; m_dma_latched[j] = false; }
    m_system_dma_memtype = 0; m_dma_status = 0;
    for (int i = 0; i < 3; i++) m_timebase_ctrl[i] = 0;
    m_timebase_reset = 0;
    m_dac[0] = m_dac[1] = 0;
    m_cpu.reset();
    m_samples.clear();
}

// gpspi_direct_internal_map
uint16_t Gpl951Core::read_word(uint32_t a)
{
    a &= 0x3fffff;
    if (a < 0x2800) return m_mainram[a];
    if (a < 0x7000) return 0;
    if (a < 0x8000) return io_read(a);
    if (a < 0x9000) return m_internal[a - 0x8000];
    if (a < 0x200000) return spi_direct_r(a - 0x9000);
    return spi_direct_bank_r(a - 0x200000);
}

void Gpl951Core::write_word(uint32_t a, uint16_t d)
{
    a &= 0x3fffff;
    if (a < 0x2800) { m_mainram[a] = d; return; }
    if (a >= 0x7000 && a < 0x8000) io_write(a, d);
}

// IN2 (port C): start 0x02, button 0x08, right 0x10, left 0x20, down 0x40, up 0x80 (active high),
// "Demo Play" 0x1000 high = off; the other ports read as all ones (MAME's debug ports)
u16 Gpl951Core::port_in(int port)
{
    if (port != 2) return 0xffff;
    u16 v = 0x1000;
    // app bits as the mini arcade cabinet skin: A 0x01, Start 0x08, Up 0x10, Down 0x20, Left 0x40, Right 0x80
    if (m_inputs & 0x10) v |= 0x80;
    if (m_inputs & 0x20) v |= 0x40;
    if (m_inputs & 0x40) v |= 0x20;
    if (m_inputs & 0x80) v |= 0x10;
    if (m_inputs & 0x03) v |= 0x08;   // A (or B from a gamepad)
    if (m_inputs & 0x08) v |= 0x02;
    return v;
}

// generic_spi_flash_device::write (the commands these games use)
void Gpl951Core::SpiFlash::write(u8 d)
{
    auto addrStep = [&]() {
        if (step == 0) addr = (addr & 0x00ffff) | (u32(d) << 16);
        else if (step == 1) addr = (addr & 0xff00ff) | (u32(d) << 8);
        else addr = (addr & 0xffff00) | d;
        step++;
    };
    switch (state) {
    case 0:
        state = d;
        step = 0;
        if (d == 0x06) { state = 0; status |= 2; }
        else if (d == 0x04) { state = 0; status &= ~2; }
        else if (d == 0x50 || d == 0x66 || d == 0x99 || d == 0xab || d == 0xb9 || d == 0xff) state = 0;
        break;
    case 0x03:
        if (step < 3) addrStep(); else latch = ptr[(addr++) & (len - 1)];
        break;
    case 0x0b:
        if (step < 3) addrStep(); else if (step == 3) step++; else latch = ptr[(addr++) & (len - 1)];
        break;
    case 0xeb:
        if (step < 3) addrStep(); else if (step < 6) step++; else latch = ptr[(addr++) & (len - 1)];
        break;
    case 0x05:
        if (step == 0) { latch = status; step++; } else { latch = 0; state = 0; }
        break;
    case 0x35:
        latch = status; state = 0;
        break;
    case 0x15:
        latch = 0; state = 0;
        break;
    case 0x9f:
        latch = id[step < 3 ? step : 2];
        if (step < 2) step++;
        break;
    case 0x90:
        if (step < 4) step++; else state = 0;
        break;
    case 0x01:
        if (step == 0) step++; else state = 0;
        break;
    default:   // page program, sector erase, ...: the flash stays read-only
        break;
    }
}

// timebase / DMA (as gpl162xx.cpp)
void Gpl951Core::timebase_ctrl_w(int t, u16 data)
{
    if (data & 0x8000) { m_timebase_ctrl[t] = data & 0x7fff; update_interrupts(1); }
    else m_timebase_ctrl[t] = data;
    if (m_timebase_ctrl[t] & 0x2000) {
        int which = (t << 2) | (m_timebase_ctrl[t] & 3);
        attotime period = which ? attotime::from_hz(1 << (which - 1)) : attotime::never();
        m_timebasetimer[t]->adjust(period, t, period);
    } else {
        m_timebasetimer[t]->adjust(attotime::never(), t);
    }
}

void Gpl951Core::write_dma_params(int channel, int offset, u16 data)
{
    m_dma_params[offset][channel] = data;
    if (offset == 3) { m_dma_latched[channel] = true; if (m_dma_params[0][channel] & 1) trigger_systemm_dma(channel); }
    if (offset == 0 && (data & 1) && m_dma_latched[channel]) trigger_systemm_dma(channel);
}

void Gpl951Core::trigger_systemm_dma(int channel)
{
    u16 mode = m_dma_params[0][channel];
    u32 source = m_dma_params[1][channel] | (u32(m_dma_params[4][channel]) << 16);
    u32 dest = m_dma_params[2][channel] | (u32(m_dma_params[5][channel]) << 16);
    u32 length = m_dma_params[3][channel] | (u32(m_dma_params[6][channel]) << 16);
    int sd = 0, dd = 0;
    if ((mode & 0xa0) == 0x00) sd = 1; else if ((mode & 0xa0) == 0x20) sd = -1;
    if ((mode & 0x50) == 0x00) dd = 1; else if ((mode & 0x50) == 0x10) dd = -1;
    source &= 0x0fffffff; length &= 0x0fffffff;
    for (u32 i = 0; i < length; i++) {
        u16 val;
        if (mode & 0x1000) { val = u16((read_word(source) & 0xff) | (read_word(source) << 8)); i++; }
        else val = read_word(source);
        source += sd;
        if (mode & 0x2000) { write_word(dest, val & 0xff); dest += dd; write_word(dest, val >> 8); }
        else write_word(dest, val);
        dest += dd;
    }
    m_dma_params[0][channel] &= 0x00f7;
    for (int i = 1; i <= 6; i++) m_dma_params[i][channel] = 0;
    m_dma_latched[channel] = false;
    m_dma_status |= 1 << channel;
}

// screen

bool Gpl951Core::update_partial(int scanline)
{
    if (scanline < m_screen.m_last_partial_scan) return false;
    if (m_screen.m_last_partial_scan == 0 && m_screen.m_last_partial_reset > m_machine.time()) return false;
    int top = std::max(m_screen.m_vis_top, m_screen.m_last_partial_scan);
    int bottom = std::min(m_screen.m_vis_bottom, scanline);
    if (top > bottom) return false;
    gpl::bitmap_rgb32 bm; bm.base = m_bitmap.data(); bm.rowpixels = 640;
    gpl::rectangle clip(0, 319, top, bottom);
    m_video.screen_update(bm, clip);
    m_screen.m_last_partial_scan = scanline + 1;
    return true;
}

void Gpl951Core::reset_partial_updates()
{
    m_screen.m_last_partial_reset = m_machine.time();
    m_screen.m_last_partial_scan = 0;
    m_scanline0_timer->adjust(m_screen.time_until_pos(0));
}

void Gpl951Core::vblank_begin()
{
    m_screen.m_vblank_start_time = m_machine.time();
    m_screen.m_vblank_end_time = m_screen.m_vblank_start_time + attotime(0, m_screen.m_vblank_period);
    update_partial(m_screen.m_vis_bottom);
    for (int y = 0; y < 240; y++)
        for (int x = 0; x < 320; x++) m_out[size_t(y) * 320 + x] = m_bitmap[size_t(y) * 640 + x] | 0xff000000u;
    m_frameDone = true;
    m_video.vblank(1);
    m_vblank_begin_timer->adjust(m_screen.time_until_vblank_start());
    m_vblank_end_timer->adjust(m_screen.time_until_vblank_end());
}

void Gpl951Core::vblank_end()
{
    m_video.vblank(0);
    m_screen.m_frame_number++;
}

void Gpl951Core::runFrame()
{
    m_frameDone = false;
    int guard = 0;
    while (!m_frameDone && guard++ < 100000) {
        m_machine.timeslice(attotime::never());
        m_machine.execute_timers();
    }
    m_rtc.m_timerval += 32768 / 30;
    auto& o = m_audio.m_out;
    size_t ns = o.size() / 2;
    for (size_t i = 0; i + 1 < o.size(); i += 2) {
        // the CHA / CHB DACs, stretched over the frame (held when a FIFO runs dry)
        size_t k = i / 2;
        if (!m_dacA.empty()) m_dac[0] = m_dacA[std::min(m_dacA.size() - 1, k * m_dacA.size() / ns)];
        if (!m_dacB.empty()) m_dac[1] = m_dacB[std::min(m_dacB.size() - 1, k * m_dacB.size() / ns)];
        m_resampleSum += (o[i] + o[i + 1]) / 2 + (m_dac[0] + m_dac[1]) / 4;
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
    m_dacA.clear(); m_dacB.clear();
    if (m_samples.size() > 44100) m_samples.erase(m_samples.begin(), m_samples.end() - 4096);
    if (dbg951()) fprintf(stderr, "pc %06x\n", m_cpu.pc());
}

size_t Gpl951Core::readAudio(int16_t* out, size_t maxSamples)
{
    size_t n = std::min(maxSamples, m_samples.size());
    memcpy(out, m_samples.data(), n * sizeof(int16_t));
    m_samples.erase(m_samples.begin(), m_samples.begin() + long(n));
    return n;
}
