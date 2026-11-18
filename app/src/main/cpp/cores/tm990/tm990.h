// PocketPlug - Texas Instruments TM 990/189 University Board (+ Video Board)
// SPDX-License-Identifier: BSD-3-Clause
//
// TMS9980A at 8 MHz (2 MHz machine clock), 2 KB RAM, two TMS9901s (user port
// + LEDs + interrupts; display digits, segments, keyboard rows, buzzer), a
// 10-digit LED display and a 45-key calculator keyboard running University
// BASIC. The "v" model adds TI's colour video board: a TMS9918 with 16 KB of
// VRAM, two demo ROMs and joystick buttons. Wiring after MAME's
// ti/tm990189.cpp (BSD-3, Raphael Nabet, Robbbert); the CPU, 9901 and 9918
// are our own.
//
// Frame: 256 x 193. Rows 0-191 are the 9918 picture (black without the video
// board); row 192 carries the board's lights for the skin: pixels 0-9 the
// segment patterns of the ten digits (bit 0 = a ... bit 6 = g, bit 7 = point)
// and pixel 10 the seven LEDs (bit n = LED n lit).

#pragma once
#include "../../core_api.h"
#include "tms9980.h"
#include "tms9918.h"

class Tm990Core : public Core, public Tms9980Bus {
public:
    explicit Tm990Core(bool video);
    bool load(const std::vector<RomFile>& roms, std::string& error) override;
    void reset() override;
    void runFrame() override;
    const uint32_t* frameBuffer() const override { return fb_.data(); }
    int frameWidth() const override { return 256; }
    int frameHeight() const override { return 193; }
    double refreshRate() const override { return 60.0; }
    int sampleRate() const override { return kRate; }
    size_t readAudio(int16_t* out, size_t maxSamples) override;
    // video board joystick buttons: bit 0 = joystick 1, bit 1 = joystick 2
    void setInputs(uint32_t pressed) override { joyButtons_ = pressed; }
    // key code = row * 8 + bit (rows 0-8, bits 0-4, MAME's LINE0-8); 0x50 = LOAD switch
    void keyEvent(int code, bool down) override;
    bool saveState(std::vector<uint8_t>& out) override;
    bool loadState(const std::vector<uint8_t>& in) override;

    // Tms9980Bus
    uint8_t read8(uint16_t addr) override;
    void write8(uint16_t addr, uint8_t v) override;
    int cruRead(uint16_t bit) override;
    void cruWrite(uint16_t bit, int v) override;
    void extOp(int code) override;

    static const int kRate = 44100;
    static const int kClock = 2000000;     // 8 MHz / 4

    // debugging
    Tms9980& cpu() { return cpu_; }
    const uint8_t* digits() const { return digitOut_; }
    uint8_t leds() const { return ledOut_; }

    struct Tms9901 {
        bool clockMode = false;
        uint16_t clockReg = 0, decrementer = 0, clockRead = 0;
        uint16_t intMask = 0, intLines = 0;
        bool timerPending = false;
        uint16_t pioDir = 0, pioOut = 0;
        int intLevel = 15, lastLevel = -1;
        bool intPending = false;
        int div = 0;
    };

private:
    void cpuIntFromUsr(int level);
    void prioritize(Tms9901& c, bool usr);
    void tick9901(Tms9901& c, bool usr);
    void pioWrite(Tms9901& c, bool usr, int p, bool v);
    void sysOut(int p, bool v);
    int sysRead(int crubit);
    void drawDigit();
    void displayCallback();
    void holdLoad();
    void renderAudio(int cycles);

    bool video_;
    Tms9980 cpu_;
    Tms9918 vdp_;
    uint8_t mem_[0x4000];
    bool romMask_[0x4000 / 0x800];
    std::vector<uint32_t> fb_;
    std::vector<int16_t> audio_;

    // board state (saved)
    struct State {
        Tms9901 usr, sys;
        uint64_t cycles;
        uint64_t lineNo;           // scanlines run since reset
        uint64_t loadUntil;        // LOAD held until this cycle
        bool loadState;
        int digitsel;
        int segment;
        uint64_t displayEnaUntil;
        uint8_t segState[10], oldSegState[10];
        uint8_t ledState;
        uint64_t nextDisplay;
        uint64_t displayCount;
        uint8_t speaker;
        uint8_t bogusRead;
        uint32_t sampleAcc;
    } s_;
    uint16_t keys_[9];             // active-high key bits per row
    uint32_t joyButtons_ = 0;
    uint8_t digitOut_[10];
    uint8_t ledOut_ = 0;
    float hp_ = 0, lastX_ = 0;
};
