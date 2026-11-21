// PocketPlug - Fidelity Backgammon Challenger
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Port of MAME's fidelity/cc7.cpp (hap, BSD-3-Clause), Backgammon Challenger set:
// Z80 at 3.58 MHz, 4 KB ROM, 256 bytes RAM, a 4-digit LED display (FROM / DBL /
// TO), "I WIN" and "I LOSE" lamps and 16 keys. The board itself has no sensors:
// the checkers are moved by hand, so the skin keeps them.
//
// The frame is a status block for the skin:
//   [130 + d * 8 + s] digit d (0 = left) segment s (a..g, 7 = the dot) brightness,
//   [170] I WIN lamp, [171] I LOSE lamp.
// Input bits: row * 4 + col of MAME's IN.0-IN.3:
//   IN.0: EN, 9, 6, 3   IN.1: CL, 8, 5, 2   IN.2: GM, 7, 4, 1   IN.3: RE, PB, PV, 0
#pragma once
#include "../../core_api.h"
#include "../../common/z80/z80.h"
#include <vector>

class BkcCore : public Core {
public:
    bool load(const std::vector<RomFile>& roms, std::string& error) override;
    void reset() override;
    void runFrame() override;
    const uint32_t* frameBuffer() const override { return m_out; }
    int frameWidth() const override { return 32; }
    int frameHeight() const override { return 16; }
    double refreshRate() const override { return 60.0; }
    int sampleRate() const override { return 44100; }
    size_t readAudio(int16_t* out, size_t maxSamples) override;
    void setInputs(uint32_t pressed) override { m_keys = pressed & 0xffff; }
    bool saveState(std::vector<uint8_t>& out) override;
    bool loadState(const std::vector<uint8_t>& in) override;

    uint8_t memRead(uint16_t a);
    void memWrite(uint16_t a, uint8_t d);
    uint8_t ioRead(uint8_t p);
    void ioWrite(uint8_t p, uint8_t d);

private:
    void flushDisplay();

    uint8_t m_rom[0x1000];
    z80 m_z80;
    uint32_t m_keys = 0;
    size_t m_audioPending = 0;
    uint32_t m_out[32 * 16];
    uint64_t m_time = 0, m_lastFlush = 0;
    uint64_t m_acc[6 * 8];
    uint8_t m_held[6] = {}, m_age[6] = { 255, 255, 255, 255, 255, 255 };
    struct State {
        uint8_t ram[0x100];
        uint8_t select, segData;
    } m_s;
};
