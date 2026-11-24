// PocketPlug - Novag Beluga chess computer (port of MAME's novag/primo.cpp). See beluga.h.
// SPDX-License-Identifier: GPL-2.0-or-later

#include "beluga.h"
#include <algorithm>
#include <cstring>

namespace {
constexpr int E_HZ = 4000000;      // 16 MHz / 4
constexpr int AUDIO_HZ = 44100;
}

bool BelugaCore::load(const std::vector<RomFile>& roms, std::string& error)
{
    const RomFile* rom = nullptr; const RomFile* svg = nullptr;
    for (auto& r : roms) {
        if (r.name == "novag_903_31y0rm59p.u1") rom = &r;
        if (r.name == "nvip.svg") svg = &r;
    }
    if (!rom || rom->data.size() != 0x4000) { error = "Missing novag_903_31y0rm59p.u1"; return false; }
    if (!svg) { error = "Missing nvip.svg (the LCD artwork in the MAME zip)"; return false; }
    std::string text(svg->data.begin(), svg->data.end());
    if (!m_svg.load(text, W, error)) return false;
    m_keyOf.assign(40, -1);
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 10; x++) m_keyOf[y * 10 + x] = int16_t(m_svg.key("s" + std::to_string(y) + "." + std::to_string(x)));
    m_levels.assign(size_t(m_svg.keyCount()) + 1, 0);
    m_lcd.init(4, 10);
    m_led.init(2, 8);
    m_rom = rom->data;
    m_cpu.setY0(true, true);
    m_cpu.setRom(m_rom.data());
    m_s.board.init(false, E_HZ * 35 / 100);   // buttons, 350 ms
    m_s.board.presetChess();
    reset();
    return true;
}

void BelugaCore::reset()
{
    m_s.inpMux = 0; m_s.select = 0; m_s.ledData = 0; m_s.lcdStrobe = 0; m_s.speaker = 0;
    m_cpu.reset();
}

uint8_t BelugaCore::extRead(uint16_t a)
{
    if (a >= 0x4000 && a < 0x8000) return m_s.ram[a & 0x7ff];
    return 0xff;
}

void BelugaCore::extWrite(uint16_t a, uint8_t d)
{
    if (a >= 0x4000 && a < 0x8000) m_s.ram[a & 0x7ff] = d;
}

uint8_t BelugaCore::portIn(int port)
{
    if (port != 2) return 0xff;
    // P20: 4051 Z (the selected board file / keypad row bit); P23: power switch (on);
    // P24 is tied to P52
    uint8_t data = 0;
    for (int i = 0; i < 8; i++)
        if (m_s.ledData >> i & 1) data |= uint8_t(m_s.board.readRank(m_s.inpMux) >> i & 1);
    for (int i = 0; i < 2; i++)
        if (m_s.select >> (i + 6) & 1) data |= uint8_t((m_keys >> (i * 8)) >> m_s.inpMux & 1);
    data ^= 1;
    return uint8_t((data & 0xef) | ((m_s.select << 2) & 0x10));
}

void BelugaCore::portOut(int port, uint8_t data, uint8_t ddr)
{
    (void)ddr;
    switch (port) {
    case 2:
        // P21: 4066 strobe to the LCD (falling edge), P22: speaker, P25-P27: 4051 select
        if (m_s.lcdStrobe && !(data & 2)) {
            uint32_t lcdData = uint32_t((m_s.select << 2) & 0x300) | m_s.ledData;
            m_lcd.matrix(now(), m_s.select & 0xf, lcdData);
        }
        m_s.lcdStrobe = (data & 2) ? 1 : 0;
        m_s.speaker = (data >> 2) & 1;
        m_s.inpMux = (data >> 5) & 7;
        break;
    case 5:   // P50-P53 LCD control, P54-P55 LED select, P56-P57 keypad mux / LCD data
        m_s.select = uint8_t(data ^ 0xf0);
        m_led.matrix(now(), (m_s.select >> 4) & 3, m_s.ledData);
        break;
    case 6:   // P60-P67: LED data, LCD data, board mux
        m_s.ledData = uint8_t(~data);
        m_led.matrix(now(), (m_s.select >> 4) & 3, m_s.ledData);
        break;
    }
}

void BelugaCore::runFrame()
{
    const int perFrame = E_HZ / 60;
    const double perSample = double(E_HZ) / AUDIO_HZ;
    uint64_t start = now();
    int done = 0;
    while (done < perFrame) {
        m_carry += perSample;
        int slice = int(m_carry);
        m_carry -= slice;
        int n = m_cpu.run(slice);
        m_cycles += uint64_t(n);
        m_s.board.tick(n);
        done += n;
        m_carry -= (n - slice);
        m_audio.push_back(m_s.speaker ? 7000 : 0);
    }
    uint32_t span = uint32_t(now() - start);
    m_lcd.frame(now(), span);
    m_led.frame(now(), span);
    std::fill(m_out.begin(), m_out.begin() + W * LCD_TOP, 0u);
    for (int i = 0; i < 64; i++) m_out[i] = m_s.board.piece[i];
    m_out[64] = m_s.board.hand;
    m_out[65] = uint32_t(m_s.board.handPos + 1);
    for (int y = 0; y < 2; y++)
        for (int x = 0; x < 8; x++) m_out[130 + y * 8 + x] = m_led.on(y, x) ? 255u : 0u;
    std::fill(m_levels.begin(), m_levels.end(), 0);
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 10; x++) {
            int k = m_keyOf[y * 10 + x];
            if (k >= 0 && m_lcd.on(y, x)) m_levels[size_t(k)] = 255;
        }
    m_svg.render(m_levels.data(), m_out.data() + W * LCD_TOP);
    if (m_audio.size() > AUDIO_HZ / 4) m_audio.erase(m_audio.begin(), m_audio.end() - AUDIO_HZ / 10);
}

size_t BelugaCore::readAudio(int16_t* out, size_t maxSamples)
{
    size_t n = std::min(maxSamples, m_audio.size());
    std::memcpy(out, m_audio.data(), n * sizeof(int16_t));
    m_audio.erase(m_audio.begin(), m_audio.begin() + long(n));
    return n;
}

bool BelugaCore::saveState(std::vector<uint8_t>& out)
{
    out.clear();
    uint32_t magic = 0x41554c42;   // 'BLUA'
    auto put = [&](const void* p, size_t n) { const uint8_t* b = (const uint8_t*)p; out.insert(out.end(), b, b + n); };
    put(&magic, 4);
    put(&m_s, sizeof(m_s));
    put(m_cpu.state(), sizeof(Hd6301::State));
    return true;
}

bool BelugaCore::loadState(const std::vector<uint8_t>& in)
{
    if (in.size() != 4 + sizeof(m_s) + sizeof(Hd6301::State)) return false;
    uint32_t magic; std::memcpy(&magic, in.data(), 4);
    if (magic != 0x41554c42) return false;
    std::memcpy(&m_s, in.data() + 4, sizeof(m_s));
    std::memcpy(m_cpu.state(), in.data() + 4 + sizeof(m_s), sizeof(Hd6301::State));
    return true;
}
