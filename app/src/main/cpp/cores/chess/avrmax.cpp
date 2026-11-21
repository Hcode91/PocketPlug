// PocketPlug - Elektor AVR-Max chess computers (port of MAME's elektor/avrmax.cpp). See avrmax.h.
// SPDX-License-Identifier: GPL-2.0-or-later

#include "avrmax.h"
#include <algorithm>
#include <cstring>

bool AvrMaxCore::load(const std::vector<RomFile>& roms, std::string& error)
{
    m_rom.clear(); m_cgrom.clear();
    for (auto& r : roms) {
        if (r.name == m_romName && r.data.size() == 0x2000) m_rom = r.data;
        if (r.name == "hd44780u_a02.bin" && r.data.size() == 0x1000) m_cgrom = r.data;
    }
    if (m_rom.empty()) { error = std::string("Missing ") + m_romName; return false; }
    if (m_lcd && m_cgrom.empty()) { error = "Missing hd44780u_a02.bin (MAME's hd44780u zip)"; return false; }
    m_hz = m_lcd ? 16000000 : 8000000;
    std::memset(&m_s, 0, sizeof(m_s));
    std::memset(&m_cpu.s, 0, sizeof(m_cpu.s));
    m_s.board.init(false, 0);
    m_cpu.setRom(m_rom.data(), int(m_rom.size()));
    m_hd.setCgrom(m_cgrom.empty() ? nullptr : m_cgrom.data());
    m_hd.setClockRatio(double(m_hz) / 270000.0);
    m_cpu.portIn = [this](int port) -> uint8_t {
        if (port != AVR8::PB) return 0;
        // PB0-PB2: multiplexed inputs (active low)
        uint8_t data = 0;
        for (int i = 0; i < 4; i++) if (m_s.inpMux >> i & 1) data |= uint8_t(m_keys >> i & 1 | (m_keys >> (i + 4) & 1) << 1 | (m_keys >> (i + 8) & 1) << 2);
        return uint8_t(~data & 7);
    };
    m_cpu.portOut = [this](int port, uint8_t d) {
        if (port == AVR8::PC) {
            // PC0-PC3: input mux, also the digit select
            flushDigits();
            m_s.inpMux = uint8_t(~d & 0xf);
        } else if (port == AVR8::PD) {
            if (!m_lcd) { flushDigits(); m_s.segs = uint8_t(~d); return; }
            // PD2: CD4094 data, PD3: clock; Q2-Q5 LCD data, Q6 RS, Q7 E (ANDed with the data line)
            int data = d >> 2 & 1, clk = d >> 3 & 1;
            if (clk && !m_s.shiftClk) m_s.shiftReg = uint8_t(m_s.shiftReg << 1 | data);
            m_s.shiftClk = uint8_t(clk);
            int e = (m_s.shiftReg >> 6 & 1) & data;
            if (!e && m_s.lcdE) m_hd.write(m_s.shiftReg >> 5 & 1, uint8_t(m_s.shiftReg << 3 & 0xf0));
            m_s.lcdE = uint8_t(e);
        }
    };
    std::memset(m_cpu.s.eeprom, 0, sizeof(m_cpu.s.eeprom));
    m_hd.reset();
    m_cpu.reset();
    std::memset(m_acc, 0, sizeof(m_acc));
    return true;
}

void AvrMaxCore::reset()
{
    m_hd.reset();
    m_cpu.reset();
}

void AvrMaxCore::flushDigits()
{
    uint64_t now = m_cpu.cycles();
    uint64_t dt = now - m_lastFlush;
    m_lastFlush = now;
    if (m_lcd || !dt) return;
    for (int d = 0; d < 4; d++) {
        if (!(m_s.inpMux >> d & 1)) continue;
        for (int sg = 0; sg < 8; sg++) if (m_s.segs >> sg & 1) m_acc[d * 8 + sg] += dt;
    }
}

void AvrMaxCore::runFrame()
{
    const int perFrame = m_hz / 60;
    int done = 0;
    while (done < perFrame) {
        int n = m_cpu.run(std::min(256, perFrame - done));
        m_hd.tick(n);
        m_hd.blinkTick(n);
        done += n;
    }
    flushDigits();
    std::memset(m_out, 0, sizeof(m_out));
    for (int i = 0; i < 64; i++) m_out[i] = m_s.board.piece[i];
    m_out[64] = m_s.board.hand;
    m_out[65] = uint32_t(m_s.board.handPos + 1);
    if (!m_lcd) {
        // a digit keeps what it last showed for a few frames (it may not be strobed every frame)
        uint64_t mx = 0;
        for (int i = 0; i < 32; i++) mx = std::max(mx, m_acc[i]);
        for (int d = 0; d < 4; d++) {
            uint8_t m = 0; uint64_t dmx = 0;
            for (int sg = 0; sg < 8; sg++) dmx = std::max(dmx, m_acc[d * 8 + sg]);
            for (int sg = 0; sg < 8; sg++) if (dmx && m_acc[d * 8 + sg] * 3 >= dmx) m |= uint8_t(1 << sg);
            if (dmx) { m_held[d] = m; m_age[d] = 0; } else if (m_age[d] < 255) m_age[d]++;
            uint8_t show = m_age[d] < 12 ? m_held[d] : 0;
            for (int sg = 0; sg < 8; sg++) m_out[130 + d * 8 + sg] = (show >> sg & 1) ? 255 : 0;
        }
        std::memset(m_acc, 0, sizeof(m_acc));
    } else {
        // the 20 x 4 panel is a 2 x 40 controller: lines 2 and 3 continue lines 0 and 1
        uint8_t buf[2 * 40 * 8];
        m_hd.render(buf, 2, 40);
        for (int line = 0; line < 4; line++)
            for (int col = 0; col < 20; col++)
                for (int r = 0; r < 8; r++)
                    m_out[200 + (line * 20 + col) * 8 + r] = buf[((line & 1) * 40 + (line >> 1) * 20 + col) * 8 + r];
    }
    // silent machine: keep the audio clock going
    m_audio.insert(m_audio.end(), 735, 0);
    if (m_audio.size() > 44100) m_audio.erase(m_audio.begin(), m_audio.end() - 4410);
}

size_t AvrMaxCore::readAudio(int16_t* out, size_t maxSamples)
{
    size_t n = std::min(maxSamples, m_audio.size());
    std::memcpy(out, m_audio.data(), n * sizeof(int16_t));
    m_audio.erase(m_audio.begin(), m_audio.begin() + long(n));
    return n;
}

namespace { constexpr uint32_t MAGIC = 0x31525641; }  // 'AVR1'

bool AvrMaxCore::saveState(std::vector<uint8_t>& out)
{
    out.clear();
    auto put = [&](const void* p, size_t n) { const uint8_t* b = (const uint8_t*)p; out.insert(out.end(), b, b + n); };
    uint32_t magic = MAGIC;
    put(&magic, 4); put(&m_s, sizeof(m_s)); put(&m_cpu.s, sizeof(m_cpu.s)); put(&m_hd.s, sizeof(m_hd.s));
    return true;
}

bool AvrMaxCore::loadState(const std::vector<uint8_t>& in)
{
    if (in.size() != 4 + sizeof(m_s) + sizeof(m_cpu.s) + sizeof(m_hd.s)) return false;
    uint32_t magic; std::memcpy(&magic, in.data(), 4);
    if (magic != MAGIC) return false;
    const uint8_t* p = in.data() + 4;
    std::memcpy(&m_s, p, sizeof(m_s)); p += sizeof(m_s);
    std::memcpy(&m_cpu.s, p, sizeof(m_cpu.s)); p += sizeof(m_cpu.s);
    std::memcpy(&m_hd.s, p, sizeof(m_hd.s));
    m_lastFlush = m_cpu.cycles();
    return true;
}
