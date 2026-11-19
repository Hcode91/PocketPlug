// PocketPlug - Novag Accord chess computer
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Port of MAME's novag/accord.cpp (hap). See accord.h.

#include "accord.h"
#include <algorithm>
#include <cstring>

namespace {
constexpr int E_HZ = 2000000;      // 8 MHz LC oscillator / 4
constexpr int AUDIO_HZ = 44100;
}

bool AccordCore::load(const std::vector<RomFile>& roms, std::string& error)
{
    for (auto& r : roms) {
        if (r.name != m_romName) continue;
        if (r.data.size() != 0x1000) { error = m_romName + " has the wrong size"; return false; }
        m_rom = r.data;
        m_cpu.setRom(m_rom.data());
        std::memset(m_cpu.state()->ram, 0, sizeof(m_cpu.state()->ram));
        m_s.board.init(false, E_HZ * 15 / 100);   // buttons, 150 ms
        reset();
        return true;
    }
    error = "Missing " + m_romName;
    return false;
}

void AccordCore::reset()
{
    m_s.inpMux = 0;
    m_s.mx = m_s.my = 0;
    m_s.speaker = 0;
    m_cpu.reset();
}

uint8_t AccordCore::portIn(int port)
{
    if (port != 3) return 0xff;
    // P30-P37: buttons (P27 low) or the selected board ranks
    uint8_t data = 0;
    if (m_s.inpMux & 0x100) data |= m_keys;
    for (int i = 0; i < 8; i++)
        if (m_s.inpMux >> i & 1) data |= m_s.board.readRank(i, true);
    return uint8_t(~data);
}

void AccordCore::portOut(int port, uint8_t data, uint8_t ddr)
{
    (void)ddr;
    switch (port) {
    case 2:   // P26 speaker, P27 button mux
        m_s.speaker = (~data >> 6) & 1;
        m_s.inpMux = uint16_t((m_s.inpMux & 0xff) | ((~data << 1) & 0x100));
        break;
    case 6:   // P60-P67: board mux, LED data
        m_s.inpMux = uint16_t((m_s.inpMux & 0x100) | (data ^ 0xff));
        m_s.mx = uint8_t(~data);
        break;
    case 7:   // P70-P72: LED select
        m_s.my = uint8_t(~data & 7);
        break;
    }
}

void AccordCore::sampleOutputs()
{
    for (int y = 0; y < 3; y++) {
        if (!(m_s.my >> y & 1)) continue;
        for (int x = 0; x < 8; x++) if (m_s.mx >> x & 1) m_ledAcc[y * 8 + x]++;
    }
    m_slices++;
    m_audio.push_back(m_s.speaker ? 7000 : 0);
}

void AccordCore::runFrame()
{
    const int perFrame = E_HZ / 60;
    const double perSample = double(E_HZ) / AUDIO_HZ;
    int done = 0;
    while (done < perFrame) {
        m_carry += perSample;
        int slice = int(m_carry);
        m_carry -= slice;
        int n = m_cpu.run(slice);
        m_s.board.tick(n);
        done += n;
        m_carry -= (n - slice);
        sampleOutputs();
    }
    std::memset(m_out, 0, sizeof(m_out));
    for (int i = 0; i < 64; i++) m_out[i] = m_s.board.piece[i];
    m_out[64] = m_s.board.hand;
    m_out[65] = uint32_t(m_s.board.handPos + 1);
    for (int i = 0; i < 24; i++) m_out[130 + i] = std::min<uint32_t>(255, m_slices ? m_ledAcc[i] * 255 * 3 / m_slices : 0);
    std::memset(m_ledAcc, 0, sizeof(m_ledAcc));
    m_slices = 0;
}

size_t AccordCore::readAudio(int16_t* out, size_t maxSamples)
{
    size_t n = std::min(maxSamples, m_audio.size());
    std::memcpy(out, m_audio.data(), n * sizeof(int16_t));
    m_audio.erase(m_audio.begin(), m_audio.begin() + long(n));
    return n;
}

bool AccordCore::saveState(std::vector<uint8_t>& out)
{
    out.clear();
    uint32_t magic = 0x44524341;   // 'ACRD'
    auto put = [&](const void* p, size_t n) { const uint8_t* b = (const uint8_t*)p; out.insert(out.end(), b, b + n); };
    put(&magic, 4);
    put(&m_s, sizeof(m_s));
    put(m_cpu.state(), sizeof(Hd6301::State));
    return true;
}

bool AccordCore::loadState(const std::vector<uint8_t>& in)
{
    if (in.size() != 4 + sizeof(m_s) + sizeof(Hd6301::State)) return false;
    uint32_t magic; std::memcpy(&magic, in.data(), 4);
    if (magic != 0x44524341) return false;
    std::memcpy(&m_s, in.data() + 4, sizeof(m_s));
    std::memcpy(m_cpu.state(), in.data() + 4 + sizeof(m_s), sizeof(Hd6301::State));
    return true;
}
