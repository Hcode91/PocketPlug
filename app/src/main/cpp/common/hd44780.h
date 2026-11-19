// PocketPlug - Hitachi HD44780 character LCD controller
// SPDX-License-Identifier: BSD-3-Clause
//
// Port of MAME's hd44780.cpp (Sandro Ronco). The busy flag runs on the host
// CPU's clock: tick() advances it.

#pragma once
#include <cstdint>
#include <cstring>

class HD44780 {
public:
    // cgrom: the 4 KB character generator (16 bytes a character), not owned
    void setCgrom(const uint8_t* cgrom) { m_cgrom = cgrom; }
    // ratio of host CPU cycles to LCD oscillator cycles (for the busy flag)
    void setClockRatio(double hostPerLcd) { m_ratio = hostPerLcd; }
    void reset();
    uint8_t read(int rs);
    void write(int rs, uint8_t data);
    void tick(int hostCycles) { if (s.busy > 0) s.busy -= hostCycles; }
    // 5-bit rows of the characters on screen: lines x chars x 8 rows
    void render(uint8_t* out, int lines, int chars);
    // blink runs every 102400 LCD cycles
    void blinkTick(int hostCycles);

    struct State {
        uint8_t ddram[0x80], cgram[0x40];
        int8_t ac;
        uint8_t dr, ir, activeRam, displayOn, cursorOn, shiftOn, blinkOn;
        int8_t direction;
        uint8_t dataLen, numLine, charSize;
        int8_t dispShift;
        uint8_t blink, nibble, firstCmd, rsState, rwState;
        int32_t busy;          // host cycles left
        int32_t blinkCount;
    } s;

private:
    enum { DDRAM = 0, CGRAM = 1 };
    const uint8_t* m_cgrom = nullptr;
    double m_ratio = 1.0;
    void setBusy(int lcdCycles) { s.busy = int32_t(lcdCycles * m_ratio); }
    void correctAc();
    void updateAc(int direction);
    void shiftDisplay(int direction);
    void updateNibble(int rs, int rw);
    void controlWrite(uint8_t data);
    uint8_t controlRead();
    void dataWrite(uint8_t data);
    uint8_t dataRead();
};
