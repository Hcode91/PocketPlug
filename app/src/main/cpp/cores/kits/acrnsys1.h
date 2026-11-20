// PocketPlug - Acorn System 1 (1979 microcomputer kit)
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Port of MAME's acrnsys1.cpp (Dirk Best, Robbbert; BSD-3-Clause):
// 6502 at 1.008 MHz, 1 KB RAM, a 512-byte monitor PROM, an INS8154 RAM-I/O
// chip driving eight 7-segment LEDs through a 7445 decoder, and the 25-key
// KB1 keypad. The cassette interface isn't emulated.
//
// The frame buffer is a status block for the skin, not a picture:
//   [0..63] segment brightness 0-255, digit d (0-7, MAME's numbering) * 8 + segment (a-g, dp).

#pragma once
#include "../../core_api.h"
#include "../../common/m6502.h"
#include <vector>

class AcornSystem1Core : public Core {
public:
    AcornSystem1Core();
    bool load(const std::vector<RomFile>& roms, std::string& error) override;
    void reset() override;
    void runFrame() override;
    const uint32_t* frameBuffer() const override { return m_out; }
    int frameWidth() const override { return 32; }
    int frameHeight() const override { return 16; }
    double refreshRate() const override { return 60.0; }
    int sampleRate() const override { return 44100; }
    size_t readAudio(int16_t* out, size_t maxSamples) override;
    // bits 0-15: hex keys 0-F; 16-23: M (memory), G (go), P, S, L, R, up, down; 24: RST
    void setInputs(uint32_t pressed) override;
    bool saveState(std::vector<uint8_t>& out) override;
    bool loadState(const std::vector<uint8_t>& in) override;

private:
    static uint8_t rdThunk(void* c, uint16_t a) { return static_cast<AcornSystem1Core*>(c)->read(a); }
    static void wrThunk(void* c, uint16_t a, uint8_t d) { static_cast<AcornSystem1Core*>(c)->write(a, d); }
    uint8_t read(uint16_t a);
    void write(uint16_t a, uint8_t d);
    uint8_t portAIn();
    uint8_t portBOut() const { return uint8_t((m_s.outB & m_s.odrb) | (m_s.odrb ^ 0xff)); }

    uint8_t m_rom[0x200];
    M6502 m_cpu;
    uint32_t m_keys = 0;
    uint32_t m_out[32 * 16];
    uint32_t m_acc[64];
    uint32_t m_slices = 0;
    uint32_t m_hist[3][64] = {};
    uint32_t m_histCycles[3] = {};
    double m_carry = 0;
    uint32_t m_audioDue = 0;

    struct State {
        uint8_t ram[0x400];
        uint8_t ioRam[0x80];
        uint8_t outA, outB, odra, odrb, mdr;
        uint8_t digit;          // port A bits 0-2 (7445 input) and 6
    } m_s;
};
