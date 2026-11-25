// PocketPlug - GeneralPlus GPL162xx LCD handhelds (port of MAME's generalplus_gpl162xx_lcdtype.cpp)
// SPDX-License-Identifier: BSD-3-Clause

#include "gpllcd.h"
#include <cstring>
#include <algorithm>

GplLcdCore::GplLcdCore(const char* flashName, uint32_t flashSize) : m_flashName(flashName), m_flashSize(flashSize)
{
    m_cpu.set_bus(this);
    m_cpu.set_iso(20);
    m_display.assign(0x40000, 0);
    m_out.assign(320 * 240, 0xff000000u);
}

bool GplLcdCore::load(const std::vector<RomFile>& roms, std::string& error)
{
    const RomFile *rom = nullptr, *flash = nullptr;
    for (auto& r : roms) { if (r.name == "internal.rom") rom = &r; else if (r.name == m_flashName) flash = &r; }
    if (!rom) { error = "Missing internal.rom"; return false; }
    if (!flash) { error = "Missing " + m_flashName; return false; }
    m_rom.assign(0x1000, 0xffff);
    for (size_t i = 0; i < 0x1000 && i * 2 + 1 < rom->data.size(); i++) m_rom[i] = uint16_t(rom->data[i * 2] << 8 | rom->data[i * 2 + 1]);   // big-endian words (vectors at FFF5h-FFFFh point into F000h)
    m_spi.assign(0x800000, 0xff);
    std::memcpy(m_spi.data(), flash->data.data(), std::min<size_t>(flash->data.size(), 0x800000));
    reset();
    return true;
}

void GplLcdCore::reset()
{
    std::memset(m_ram, 0, sizeof(m_ram));
    std::memset(m_bank7300, 0, sizeof(m_bank7300));
    std::memset(m_bank7400, 0, sizeof(m_bank7400));
    std::memset(m_7100, 0, sizeof(m_7100));
    std::fill(m_display.begin(), m_display.end(), 0);
    m_703a = m_707e = m_7860 = m_7868 = m_78a1 = 0;
    std::memset(m_dma, 0, sizeof(m_dma));
    m_spiState = 0; m_spiAddr = 0; std::memset(m_rx, 0, sizeof(m_rx));
    m_lcdState = 0; m_lcdCmd = 0; m_lcdAddr = 0;
    mCommand = mCommandBit = mResponse = mResponseBit = mShift = mClockState = 0; mState = 0; mPos = 0;
    m_cpu.reset();
}

// ---- the menu controller (bl_handhelds_menucontrol, unSP variant) ----

void GplLcdCore::menuClock(int state)
{
    if (state) { mClockState = 1; return; }
    mClockState = 0;
    mCommand = uint8_t((mCommand << 1) | mCommandBit);
    mResponseBit = uint8_t((mResponse >> (7 - mShift)) & 1);
    if (++mShift == 8) { mShift = 0; menuCommand(); }
}

void GplLcdCore::menuCommand()
{
    uint8_t c = mCommand;
    enum { READY = 0, C00, C01, C02, C03, C04, C05 };
    switch (mState) {
    case READY:
        if (c == 0x00) { mState = C00; mResponse = uint8_t(mPos); }
        else if (c == 0x01) { mState = C01; mResponse = uint8_t(mPos >> 8); }
        else if (c == 0x02) mState = C02;
        else if (c == 0x03) mState = C03;
        else if (c == 0x04) mState = C04;
        else if (c == 0x05) mState = C05;
        else if (c == 0x10) mPos = 0;
        else if (c == 0x30) mPos++;
        else if (c == 0x37) mPos--;
        else if (c == 0x39) mPos = uint16_t(mPos - 4);
        else if (c == 0x2c) mPos = 1;
        break;
    case C00: case C01: mState = READY; break;
    case C02: mPos = uint16_t((mPos & 0xff00) | ((c - 0x8) & 0xff)); mState = READY; break;
    case C03: mPos = uint16_t((mPos & 0x00ff) | (((c - 0x9) & 0xff) << 8)); mState = READY; break;
    case C04:
        if (c == 0x0d) mPos = uint16_t(mPos + 4);
        else if (c == 0x1e) mPos = uint16_t(mPos + 0x310);
        mState = READY;
        break;
    case C05:
        if (c == 0x0b) mPos = uint16_t(mPos - 0xdc);
        else if (c == 0x0e) mPos = uint16_t(mPos - 0x314);
        mState = READY;
        break;
    }
}

// ---- SPI flash ----

void GplLcdCore::spiTx(uint8_t d)
{
    enum { READY, HI, MID, LO, D1, D2, READ, FHI = 8, FMID, FLO, FDUMMY, FREAD };
    switch (m_spiState) {
    case READY: if (d == 0x03) m_spiState = HI; else if (d == 0x0b) m_spiState = FHI; break;
    case HI: m_spiAddr = (m_spiAddr & 0xff00ffff) | uint32_t(d) << 16; m_spiState = MID; break;
    case MID: m_spiAddr = (m_spiAddr & 0xffff00ff) | uint32_t(d) << 8; m_spiState = LO; break;
    case LO: m_spiAddr = (m_spiAddr & 0xffffff00) | d; m_spiState = READ; break;
    case D1: m_spiState = D2; break;
    case FHI: m_spiAddr = (m_spiAddr & 0xff00ffff) | uint32_t(d) << 16; m_spiState = FMID; break;
    case FMID: m_spiAddr = (m_spiAddr & 0xffff00ff) | uint32_t(d) << 8; m_spiState = FLO; break;
    case FLO: m_spiAddr = (m_spiAddr & 0xffffff00) | d; m_spiState = FDUMMY; break;
    case FDUMMY: m_spiState = FREAD; break;
    default: break;
    }
}

uint8_t GplLcdCore::spiNext()
{
    if (m_spiState == 6 || m_spiState == 12) return m_spi[m_spiAddr++ & 0x7fffff];
    return 0;
}

// ---- the bus ----

uint16_t GplLcdCore::read_word(uint32_t a)
{
    a &= 0x3fffff;
    if (a < 0x7000) return m_ram[a];
    if (a >= 0xf000 && a < 0x10000) return m_rom[a - 0xf000];
    switch (a) {
    case 0x703a: return m_703a;
    case 0x707e: return m_707e;
    case 0x780f: return 0x0002;
    case 0x7860: {
        // IN0: menu controller status 08 / data 10 (active high), Start 4000, Up 8000 (active low); the rest high
        uint16_t v = 0xffff & ~0x0001 & ~0x0004 & ~0x0200 & ~0x0018;
        if (mClockState) v |= 0x08;
        if (mResponseBit) v |= 0x10;
        if (m_inputs & 0x01) v &= ~0x4000;
        if (m_inputs & 0x02) v &= ~0x8000;
        return v;
    }
    case 0x7868: return m_7868;
    case 0x7870: {
        uint16_t v = 0xffff;
        if (m_inputs & 0x80) v &= ~0x0020;
        if (m_inputs & 0x04) v &= ~0x0040;
        if (m_inputs & 0x08) v &= ~0x0080;
        if (m_inputs & 0x10) v &= ~0x0100;
        if (m_inputs & 0x20) v &= ~0x0200;
        if (m_inputs & 0x40) v &= ~0x0400;
        if (m_inputs & 0x100) v &= ~0x0800;
        return v;
    }
    case 0x78a1: return m_78a1;
    case 0x78d8: return 0xffff;
    case 0x7944: {
        uint8_t r = m_rx[0];
        if (m_spiState == 12) { m_rx[0] = m_rx[1]; m_rx[1] = m_rx[2]; m_rx[2] = m_rx[3]; m_rx[3] = m_rx[4]; m_rx[4] = spiNext(); }
        else { m_rx[0] = m_rx[1]; m_rx[1] = m_rx[2]; m_rx[2] = m_rx[3]; m_rx[3] = spiNext(); }
        return r;
    }
    case 0x7945: return 0;
    case 0x7abf: return 1;
    }
    if (a >= 0x7100 && a < 0x7200) return m_7100[a - 0x7100];
    if (a >= 0x7300 && a < 0x7400) return m_bank7300[((a - 0x7300) | ((m_703a & 0x0c) << 6)) & 0x3ff];
    if (a >= 0x7400 && a < 0x7800) return m_bank7400[(a - 0x7400) + ((m_707e & 1) ? 0x400 : 0)];
    if (a >= 0x7a80 && a < 0x7a88) return m_dma[a - 0x7a80];
    return 0;
}

void GplLcdCore::write_word(uint32_t a, uint16_t d)
{
    a &= 0x3fffff;
    if (a < 0x7000) { m_ram[a] = d; return; }
    if (a >= 0x7100 && a < 0x7200) { m_7100[a - 0x7100] = d; return; }
    if (a >= 0x7300 && a < 0x7400) { m_bank7300[((a - 0x7300) | ((m_703a & 0x0c) << 6)) & 0x3ff] = d; return; }
    if (a >= 0x7400 && a < 0x7800) { m_bank7400[(a - 0x7400) + ((m_707e & 1) ? 0x400 : 0)] = d; return; }
    if (a >= 0x7a80 && a < 0x7a88) {
        m_dma[a - 0x7a80] = d;
        if (a == 0x7a80 && (d == 0x4009 || d == 0x6009)) {
            uint32_t src = m_dma[1] | uint32_t(m_dma[4]) << 16, dst = m_dma[2] | uint32_t(m_dma[5]) << 16;
            uint32_t len = m_dma[3] | uint32_t(m_dma[6]) << 16;
            for (uint32_t i = 0; i < len; i++) {
                uint16_t v = read_word(src++);
                if (d & 0x2000) { write_word(dst++, v & 0xff); write_word(dst++, v >> 8); }
                else write_word(dst++, v);
            }
        }
        return;
    }
    switch (a) {
    case 0x703a: m_703a = d; break;
    case 0x707e: m_707e = d; break;
    case 0x7860: mCommandBit = (d >> 4) & 1; menuClock((d >> 5) & 1); m_7860 = d; break;
    case 0x7863: if (d == 0x3cf7) { mState = 0; mShift = 0; mCommand = 0; } break;
    case 0x7868:
        if ((m_7868 & 0x100) != (d & 0x100) && !(d & 0x100)) { std::memset(m_rx, 0xff, 4); m_spiState = 0; }
        m_7868 = d;
        break;
    case 0x78d8: if (d & 0x8000) m_78a1 &= ~0x8000; break;
    case 0x7942: spiTx(uint8_t(d)); break;
    case 0x200000:
        d &= 0xff;
        if (m_lcdState == 1) { m_lcdCmd = d; m_lcdState = 2; }
        else if (d == 0) { m_lcdState = 1; m_lcdCmd = 0; }
        break;
    case 0x20fc00:
        if (m_lcdState == 2 && m_lcdCmd == 0x22) {
            m_display[m_lcdAddr++] = uint8_t(d);
            if (m_lcdAddr >= 320 * 240 * 2) m_lcdAddr = 0;
        }
        break;
    }
}

void GplLcdCore::runFrame()
{
    const int total = 96000000 / 60;
    // vblank: IRQ 4 (MAME holds it for 10 us)
    m_cpu.set_state_unsynced(UNSP_IRQ4_LINE, 1);
    m_78a1 |= 0x8000;
    m_cpu.m_core->m_icount = 960;
    m_cpu.execute_run();
    m_cpu.set_state_unsynced(UNSP_IRQ4_LINE, 0);
    m_cpu.m_core->m_icount = total - 960;
    m_cpu.execute_run();
    m_silence = std::min(m_silence + 735, 735 * 4);
    for (int i = 0; i < 320 * 240; i++) {
        uint16_t px = uint16_t(m_display[i * 2] << 8 | m_display[i * 2 + 1]);
        m_out[i] = 0xff000000u | uint32_t(((px >> 11) & 0x1f) << 3) << 16 | uint32_t(((px >> 5) & 0x3f) << 2) << 8 | uint32_t((px & 0x1f) << 3);
    }
}

size_t GplLcdCore::readAudio(int16_t* out, size_t maxSamples)
{
    size_t n = std::min<size_t>(maxSamples, size_t(m_silence));
    std::memset(out, 0, n * sizeof(int16_t));
    m_silence -= int(n);
    return n;
}
