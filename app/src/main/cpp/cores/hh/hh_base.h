// PocketPlug - dedicated handheld / tabletop games (MAME's handheld/hh_*.cpp)
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Shared parts: the SVG screen from the ROM zip, the PWM display matrix that
// lights it, a 1-bit speaker, and the frame/audio timing. Each CPU family has
// its own core (hh_hmcs40.cpp, ...) with a table of games.
//
// Common input bits (1 = pressed) used by the skins:
//   0 up, 1 down, 2 left, 3 right, 4..11 buttons 1-8 (per game, see the
//   machine list), 12..15 the second player's up/down/left/right.
// Switches (difficulty, skill, power) are options: setOption(id, value);
// option 7 is the power switch (1 = off) and option 5 the sound switch
// (1 = off) on games that have them.

#pragma once
#include "../../core_api.h"
#include "../../common/svgscreen.h"
#include "../../common/pwmdisplay.h"
#include <string>
#include <vector>

class HhBase : public Core {
public:
    const uint32_t* frameBuffer() const override { return m_fb.data(); }
    int frameWidth() const override { return m_fbW; }
    int frameHeight() const override { return m_fbH; }
    double refreshRate() const override { return 60.0; }
    int sampleRate() const override { return 44100; }
    size_t readAudio(int16_t* out, size_t maxSamples) override;
    void setInputs(uint32_t pressed) override { m_in = pressed; inputsChanged(); }
    void setOption(int id, int value) override { if (id >= 0 && id < 8) m_opt[id] = value; inputsChanged(); }

protected:
    // svgName: the SVG file in the ROM set (empty: a LED / lamp game drawn by
    // the skin, whose frame is then the matrix itself: cols x rows pixels,
    // white = lit); rows x cols: the PWM matrix
    bool loadScreen(const std::vector<RomFile>& roms, const std::string& svgName, int rows, int cols, std::string& error);
    // the game's machine cycles per second
    void setClock(double cyclesPerSecond) { m_hz = cyclesPerSecond; }
    // run the CPU for about n cycles, returning the cycles run
    virtual int runCpu(int n) = 0;
    virtual uint64_t now() const = 0;
    virtual void inputsChanged() {}
    // called once per frame before the CPU runs (e.g. for timers)
    void runFrameCommon();
    void speaker(int level) { speakerLevel(level ? 1.0 : 0.0); }
    void speakerLevel(double level);   // 0..1 (multi-level speakers)
    bool in(int bit) const { return m_in >> bit & 1; }

    SvgScreen m_svg;
    bool m_hasSvg = false;
    int m_fbW = 1, m_fbH = 1;
    PwmDisplay m_disp;
    std::vector<uint32_t> m_fb;
    std::vector<int16_t> m_keyOf;        // y * 64 + x -> svg key (-1 = none)
    std::vector<uint8_t> m_levels;
    std::vector<int16_t> m_audio;
    uint32_t m_in = 0;
    int m_opt[8] = {};
    double m_hz = 100000;
    double m_carry = 0;
    double m_spk = 0;
    bool m_wasOff = false;
    double m_spkPos = 0;                 // cycles already turned into audio
    double m_spkSum = 0;                 // on-cycles in the current sample
    double m_spkFill = 0;                // cycles in the current sample
    void audioTo(double t);
};
