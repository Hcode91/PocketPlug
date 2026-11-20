// PocketPlug - Rockwell AIM 65 (1977 single-board computer)
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Port of MAME's rockwell/aim65.cpp and aim65_m.cpp (Peter Trauner, Dan Boris,
// Dirk Best, Robbbert; GPL-2.0+): 6502 at 1 MHz, 4 KB of RAM, the monitor ROMs
// at E000-FFFF, a 6532 RIOT scanning the keyboard, the system 6522 VIA driving
// the 20-column thermal printer (MAME's approximation of its timing), a user
// 6522, and a 6821 PIA writing five DL1416T 4-character 16-segment displays.
// Optional ROMs (BASIC, assembler...) plug into the sockets at B000-DFFF.
//
// Frame: 160 x 201. Rows 0-199 are the printer paper (MAME's 160 x 200 view,
// newest line at the bottom; 1 = dot); row 200 holds the 20 display digits'
// segment masks (MAME's led16seg bit order), leftmost first.

#pragma once
#include "../../core_api.h"
#include "../../common/m6502.h"
#include "../../common/via6522.h"
#include <vector>

class Aim65Core : public Core {
public:
    Aim65Core();
    bool load(const std::vector<RomFile>& roms, std::string& error) override;
    void reset() override;
    void runFrame() override;
    const uint32_t* frameBuffer() const override { return m_out.data(); }
    int frameWidth() const override { return 160; }
    int frameHeight() const override { return 201; }
    double refreshRate() const override { return 60.0; }
    int sampleRate() const override { return 44100; }
    size_t readAudio(int16_t* out, size_t maxSamples) override;
    void setInputs(uint32_t pressed) override;       // bit 0: RESET button
    void keyEvent(int code, bool down) override;     // row * 8 + bit of MAME's KEY.0-7
    // drives 3/4/5: ROM sockets Z26 (B000), Z25 (C000), Z24 (D000), 4 KB each
    bool mediaInsert(int drive, const std::string& path, std::string& error) override;
    void mediaEject(int drive) override;
    bool saveState(std::vector<uint8_t>& out) override;
    bool loadState(const std::vector<uint8_t>& in) override;

private:
    static uint8_t rdThunk(void* c, uint16_t a) { return static_cast<Aim65Core*>(c)->read(a); }
    static void wrThunk(void* c, uint16_t a, uint8_t d) { static_cast<Aim65Core*>(c)->write(a, d); }
    uint8_t read(uint16_t a);
    void write(uint16_t a, uint8_t d);
    void riotWrite(int off, uint8_t d);
    uint8_t riotRead(int off);
    void riotTick(int n);
    void updateIrq();
    void piaWrite(int r, uint8_t d);
    uint8_t piaRead(int r);
    void displayPa(uint8_t d);
    void displayPb(uint8_t d);
    void printerPa(uint8_t d);
    void printerPb(uint8_t d);
    void printerCb2(int state);
    void renderPrinter();

    uint8_t m_rom[0x2000];            // E000-FFFF
    std::vector<uint8_t> m_sockets[3];  // B000, C000, D000
    M6502 m_cpu;
    Via6522 m_via0, m_via1;
    uint32_t m_resetBtn = 0;
    std::vector<uint32_t> m_out;
    uint32_t m_audioDue = 0;

    struct State {
        uint8_t ram[0x1000];
        uint8_t riotRam[0x80], piaRam[0x40];
        // RIOT
        uint8_t riotPa, riotPaDdr, riotPb, riotPbDdr, riotShift, riotIrqTimer, riotIeTimer, riotIrqEdge, riotIeEdge, riotSpinning;
        int32_t riotTimer;            // ticks left (in prescaled units * prescale)
        // PIA
        uint8_t piaOra, piaOrb, piaDdra, piaDdrb, piaCra, piaCrb;
        // displays
        uint8_t dsRam[5][4], dsCursor[5][4], dsWr[5], dsCeIn[5], dsCeLatch[5], dsAddrIn[5], dsAddrLatch[5], dsCu, dsData;
        // printer
        uint16_t printerRam[64 * 100];
        int32_t printerX, printerY, printerFlag, printerLevel, printerTimer;   // timer: cycles to the next toggle (-1 = off)
        uint8_t ca2, cb2, pbSave;
        uint8_t keys[8];
    } m_s;
};
