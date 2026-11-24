// PocketPlug - Unicard Bridge Companion (1985): port of MAME's unicard/bbcbc.cpp
// (Incog, Robbbert; BSD-3-Clause). Z80 at 3.5625 MHz, 16 KB of ROM, 2 KB of RAM,
// a Z80 PIO that reads the buttons four at a time, and a TMS9129 VDP with 16 KB
// of VRAM (50 Hz). Cartridges map at 4000-BFFF. No sound hardware.
//
// Frame: 272 x 208 (the 256 x 192 picture in a border of the backdrop colour).
// Input bits (1 = pressed):
//   0 Pass  1 Spades  2 Clubs  3 Rdbl  4 NT  5 Hearts / Up  6 Play, Yes  7 Back
//   8 Dbl  9 Diamonds / Down  10 Start  11 Play, No
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once
#include "../../core_api.h"
#include "../../common/z80/z80.h"
#include "../../common/tms9928a.h"
#include <vector>

class BbcbcCore : public Core {
public:
    bool load(const std::vector<RomFile>& roms, std::string& error) override;
    void reset() override;
    void runFrame() override;
    const uint32_t* frameBuffer() const override { return m_frame.data(); }
    int frameWidth() const override { return W; }
    int frameHeight() const override { return H; }
    double refreshRate() const override { return 50.0; }
    int sampleRate() const override { return 44100; }
    size_t readAudio(int16_t* out, size_t maxSamples) override;
    void setInputs(uint32_t pressed) override { m_in = pressed; }
    bool mediaInsert(int drive, const std::string& path, std::string& error) override;
    void mediaEject(int drive) override;
    bool saveState(std::vector<uint8_t>& out) override;
    bool loadState(const std::vector<uint8_t>& in) override;

    static constexpr int W = 272, H = 208;

    uint8_t memRead(uint16_t a);
    void memWrite(uint16_t a, uint8_t d);
    uint8_t ioRead(uint8_t p);
    void ioWrite(uint8_t p, uint8_t d);

private:
    uint8_t buttons() const;
    uint8_t m_rom[0x4000];
    std::vector<uint8_t> m_cart;
    z80 m_z80;
    Tms9928a m_vdp;
    uint32_t m_in = 0;
    double m_carry = 0;
    struct Port { uint8_t mode, ior, output, input, next; };   // next: 1 = IOR byte, 2 = mask byte
    struct State {
        uint8_t ram[0x800];
        Port port[2];
        uint8_t select;
    } m_s;
    std::vector<uint32_t> m_frame = std::vector<uint32_t>(W * H, 0xff000000u);
    std::vector<int16_t> m_audio;
    uint8_t pioRead(int reg);
    void pioWrite(int reg, uint8_t d);
};
