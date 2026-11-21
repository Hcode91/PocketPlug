// PocketPlug - Bally Astrocade (Professional Arcade / Home Library Computer)
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Port of MAME's bally/astrohome.cpp, astrocde_v.cpp (Nicola Salmoria, Mike Coates,
// Frank Palazzolo, Aaron Giles) and sound/astrocde.cpp (Aaron Giles, Frank Palazzolo),
// BSD-3-Clause: a Z80 at 1.79 MHz, the "data chip" (4-colour bitmap from 4 KB of screen
// RAM, colour split, function generator for shifted / ORed / XORed writes, scanline
// interrupts in IM 2) and the custom I/O chip (three tones, noise, vibrato, the keypad
// and the hand controllers). Cartridges up to 8 KB sit at 0x2000.
//
// Input bits: 0 up, 1 down, 2 left, 3 right, 4 trigger (hand controller 1);
// 8 + column * 6 + row: the 24-key keypad (MAME's KEYPAD0-3 = columns, bit = row).
// Analog stick 0 x (-1..1) turns the controller's knob.

#pragma once
#include "../../core_api.h"
#include "../../common/z80/z80.h"
#include <vector>

class AstrocadeCore : public Core {
public:
    explicit AstrocadeCore(const char* bios) : m_biosName(bios) {}
    bool load(const std::vector<RomFile>& roms, std::string& error) override;
    void reset() override;
    void runFrame() override;
    const uint32_t* frameBuffer() const override { return m_fb; }
    int frameWidth() const override { return W; }
    int frameHeight() const override { return H; }
    double refreshRate() const override { return 7159090.5 / (455.0 * 262.0); }
    int sampleRate() const override { return 44100; }
    size_t readAudio(int16_t* out, size_t maxSamples) override;
    void setInputs(uint32_t pressed) override { m_keys = pressed; }
    void setAnalog(int stick, float x, float y) override;
    bool saveState(std::vector<uint8_t>& out) override;
    bool loadState(const std::vector<uint8_t>& in) override;

    uint8_t memRead(uint16_t a);
    void memWrite(uint16_t a, uint8_t d);
    uint8_t ioRead(uint16_t port);
    void ioWrite(uint16_t port, uint8_t d);

    static constexpr int W = 352, H = 240;

private:
    void funcgenWrite(uint16_t offset, uint8_t data);
    void renderLine(int y);
    void sound(int cycles);
    void cpuRun(int64_t untilPx);

    const char* m_biosName;
    uint8_t m_bios[0x2000];
    std::vector<uint8_t> m_cart;
    uint32_t m_palette[512];
    uint32_t m_fb[W * H];
    z80 m_z80;
    uint32_t m_keys = 0;
    std::vector<int16_t> m_audio;

    struct State {
        uint8_t vram[0x1000];
        // data chip
        uint8_t colors[8], colorsplit, bgdata, vblank, videoMode;
        uint8_t intEnable, intFeedback, intScanline, intVector, intLine, intClearOnAck, intPulse;
        uint8_t vertFeedback, horizFeedback;
        uint8_t fgExpandColor[2], fgControl, fgExpandCount, fgRotateCount, fgRotateData[4], fgShiftPrev, fgIntercept;
        // I/O chip
        uint8_t reg[8];
        uint8_t masterCount, noiseClock, aCount, aState, bCount, bState, cCount, cState;
        uint16_t noiseState, vibratoClock;
        uint8_t knob;
        // timing
        int64_t px;             // pixel clocks into the frame (4 per CPU cycle)
        int64_t intOffAt;       // pixel clock the interrupt line drops (-1 none)
        double audioAcc, audioCycles;
    } m_s;
};
