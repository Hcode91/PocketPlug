// PocketPlug - 4004 Nixie Clock (John L. Weinrich, 2008)
// SPDX-License-Identifier: BSD-3-Clause
//
// A hobbyist clock built round the Intel 4004: sixteen 256-byte 4001-style
// ROMs, 4002 RAM, six nixie tubes and four neon dots, timed by the mains
// frequency on the CPU's TEST pin. Wiring after MAME's homebrew/4004clk.cpp
// (BSD-3, Miodrag Milanovic); the CPU is our own (i4004.h).
//
// The "frame" is not a picture: pixels 0-5 hold the six tubes' digits
// (0-9, 10 = dark) and pixel 6 the neon dots (bits 0-3); the skin draws the
// tubes from that.

#pragma once
#include "../../core_api.h"
#include "i4004.h"

class NixieClockCore : public Core, public I4004Bus {
public:
    NixieClockCore();
    bool load(const std::vector<RomFile>& roms, std::string& error) override;
    void reset() override;
    void runFrame() override;
    const uint32_t* frameBuffer() const override { return fb_; }
    int frameWidth() const override { return 8; }
    int frameHeight() const override { return 1; }
    double refreshRate() const override { return 60.0; }
    int sampleRate() const override { return kRate; }
    size_t readAudio(int16_t* out, size_t maxSamples) override;
    void setInputs(uint32_t pressed) override { buttons_ = pressed; }
    // id 1: tick-tock sound (1 = on), id 2: mains frequency (50 / 60)
    void setOption(int id, int value) override;
    bool advanceTime(double seconds) override;
    void setWallClock(int64_t localSeconds) override;
    bool saveState(std::vector<uint8_t>& out) override;
    bool loadState(const std::vector<uint8_t>& in) override;

    enum { BTN_SELECT = 1, BTN_BROWSE = 2 };
    static const int kRate = 44100;
    // 5 MHz crystal / 8 = 625 kHz CPU clock; 8 clocks per instruction cycle
    static const int kCyclesPerSec = 78125;

    // I4004Bus
    uint8_t romRead(uint16_t addr) override { return rom_[addr & 0xfff]; }
    uint8_t romPortRead(uint16_t addr) override;
    void romPortWrite(uint16_t addr, uint8_t v) override;
    void ramPortWrite(uint8_t addr, uint8_t v) override;
    bool testPin() override;

    // the program's time-of-day and date counters in 4002 RAM (bank 0): each
    // digit is held as an offset that overflows to 16 when the digit rolls over
    static const int kRamTime = 0;   // s, 10s, m, 10m, h, 10h
    static const int kRamDate = 6;   // d, 10d, mo, 10mo, y, 10y

    // debugging
    I4004& cpu() { return cpu_; }
    int digit(int i) const { return fb_[i]; }
    int neons() const { return int(fb_[6]); }

private:
    void runCycles(uint32_t n, bool sound);
    void updateDisplay();
    void applyWallClock();

    I4004 cpu_;
    uint8_t rom_[0x1000];
    uint16_t nixie_[16];
    uint32_t fb_[8];
    uint32_t buttons_ = 0;
    bool tickTock_ = true;
    int hz_ = 50;
    uint64_t cycles_ = 0;     // since reset
    uint32_t frames_ = 0;
    uint8_t dac_ = 0;
    std::vector<int16_t> audio_;
    // PocketPlug starts the clock (its Select press after power-on) and sets
    // it from the phone, as if someone had set it by hand.
    int32_t bootFrames_ = 0;     // frames since power-on, until the clock is running
    bool running_ = false;
    bool haveWall_ = false;
    int64_t wall_ = 0;           // phone's local time, seconds since 1970
    uint64_t wallAtCycle_ = 0;   // cycles_ when wall_ was given
    uint32_t sampleAcc_ = 0;
    float hp_ = 0, lastX_ = 0;
};
