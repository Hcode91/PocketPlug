// PocketPlug - Hitachi HD44780 character LCD controller
// SPDX-License-Identifier: BSD-3-Clause
//
// Port of MAME's hd44780.cpp (Sandro Ronco).

#include "hd44780.h"

void HD44780::reset()
{
    std::memset(&s, 0, sizeof(s));
    std::memset(s.ddram, 0x20, sizeof(s.ddram));
    s.activeRam = DDRAM;
    s.direction = 1;
    s.dataLen = 8;
    s.numLine = 1;
    s.charSize = 8;
    s.firstCmd = 1;
    setBusy(410);
}

void HD44780::blinkTick(int hostCycles)
{
    s.blinkCount -= hostCycles;
    while (s.blinkCount <= 0) {
        s.blinkCount += int32_t(102400 * m_ratio);
        s.blink = !s.blink;
    }
}

void HD44780::correctAc()
{
    if (s.activeRam == DDRAM) {
        int maxAc = (s.numLine == 1) ? 0x4f : 0x67;
        if (s.ac > maxAc) s.ac = int8_t(s.ac - (maxAc + 1));
        else if (s.ac < 0) s.ac = int8_t(maxAc);
        else if (s.numLine == 2 && s.ac > 0x27 && s.ac < 0x40) s.ac = int8_t(0x40 + (s.ac - 0x28));
    } else
        s.ac &= 0x3f;
}

void HD44780::updateAc(int direction)
{
    if (s.activeRam == DDRAM && s.numLine == 2 && direction == -1 && s.ac == 0x40) s.ac = 0x27;
    else s.ac = int8_t(s.ac + direction);
    correctAc();
}

void HD44780::shiftDisplay(int direction)
{
    s.dispShift = int8_t(s.dispShift + direction);
    if (s.dispShift == 0x50) s.dispShift = 0;
    else if (s.dispShift == -1) s.dispShift = 0x4f;
}

void HD44780::updateNibble(int rs, int rw)
{
    if (s.rsState != rs || s.rwState != rw) {
        s.rsState = uint8_t(rs);
        s.rwState = uint8_t(rw);
        s.nibble = 0;
    }
    s.nibble = !s.nibble;
}

uint8_t HD44780::read(int rs)
{
    if (s.dataLen == 4) updateNibble(rs & 1, 1);
    return (rs & 1) ? dataRead() : controlRead();
}

void HD44780::write(int rs, uint8_t data)
{
    if (s.dataLen == 4) updateNibble(rs & 1, 0);
    if (rs & 1) dataWrite(data);
    else controlWrite(data);
}

void HD44780::controlWrite(uint8_t data)
{
    if (s.dataLen == 4) {
        if (s.nibble) { s.ir = data & 0xf0; return; }
        s.ir |= (data >> 4) & 0x0f;
    } else
        s.ir = data;

    uint8_t ir = s.ir;
    if (ir & 0x80) {
        s.activeRam = DDRAM;
        s.ac = int8_t(ir & 0x7f);
        correctAc();
        setBusy(10);
        return;
    } else if (ir & 0x40) {
        s.activeRam = CGRAM;
        s.ac = int8_t(ir & 0x3f);
        setBusy(10);
        return;
    } else if (ir & 0x20) {
        // function set (only allowed first, or when the data length changes)
        if (!s.firstCmd && s.dataLen == ((ir & 0x10) ? 8 : 4) &&
            (s.charSize != ((ir & 0x04) ? 10 : 8) || s.numLine != ((ir >> 3 & 1) + 1)))
            return;
        s.firstCmd = 1;
        if (ir & 0x08) s.charSize = 8;
        else s.charSize = (ir & 0x04) ? 10 : 8;
        s.dataLen = (ir & 0x10) ? 8 : 4;
        s.numLine = uint8_t((ir >> 3 & 1) + 1);
        correctAc();
        setBusy(10);
        return;
    } else if (ir & 0x10) {
        int direction = (ir & 0x04) ? +1 : -1;
        if (ir & 0x08) shiftDisplay(direction);
        else updateAc(direction);
        setBusy(10);
    } else if (ir & 0x08) {
        s.displayOn = (ir >> 2) & 1;
        s.cursorOn = (ir >> 1) & 1;
        s.blinkOn = ir & 1;
        setBusy(10);
    } else if (ir & 0x04) {
        s.direction = (ir & 0x02) ? +1 : -1;
        s.shiftOn = ir & 1;
        setBusy(10);
    } else if (ir & 0x02) {
        s.ac = 0;
        s.activeRam = DDRAM;
        s.direction = 1;
        s.dispShift = 0;
        setBusy(410);
    } else if (ir & 0x01) {
        s.ac = 0;
        s.activeRam = DDRAM;
        s.direction = 1;
        s.dispShift = 0;
        std::memset(s.ddram, 0x20, sizeof(s.ddram));
        setBusy(410);
        return;
    }
    s.firstCmd = 0;
}

uint8_t HD44780::controlRead()
{
    bool busy = s.busy > 0;
    if (s.dataLen == 4) {
        if (s.nibble) return uint8_t((busy ? 0x80 : 0) | (s.ac & 0x70));
        return uint8_t((s.ac << 4) & 0xf0);
    }
    return uint8_t((busy ? 0x80 : 0) | (s.ac & 0x7f));
}

void HD44780::dataWrite(uint8_t data)
{
    if (s.dataLen == 4) {
        if (s.nibble) { s.dr = data & 0xf0; return; }
        s.dr |= (data >> 4) & 0x0f;
    } else
        s.dr = data;
    if (s.activeRam == DDRAM) s.ddram[s.ac & 0x7f] = s.dr;
    else s.cgram[s.ac & 0x3f] = s.dr;
    setBusy(10);
    updateAc(s.direction);
    if (s.shiftOn) shiftDisplay(s.direction);
}

uint8_t HD44780::dataRead()
{
    uint8_t data = (s.activeRam == DDRAM) ? s.ddram[s.ac & 0x7f] : s.cgram[s.ac & 0x3f];
    if (s.dataLen == 4) {
        if (s.nibble) return data & 0xf0;
        data = (data << 4) & 0xf0;
    }
    setBusy(10);
    updateAc(s.direction);
    return data;
}

void HD44780::render(uint8_t* out, int lines, int chars)
{
    std::memset(out, 0, size_t(lines) * chars * 8);
    if (!s.displayOn) return;
    int lineSize = 80 / s.numLine;
    for (int line = 0; line < s.numLine && line < lines; line++)
        for (int pos = 0; pos < chars && pos < lineSize; pos++) {
            int charPos = line * 0x40 + ((pos + s.dispShift) % lineSize);
            uint8_t code = s.ddram[charPos];
            const uint8_t* src;
            uint8_t tmp[16] = {};
            if (code < 0x10) src = (s.charSize == 8) ? s.cgram + (code & 7) * 8 : s.cgram + ((code >> 1) & 3) * 16;
            else src = m_cgrom ? m_cgrom + code * 16 : tmp;
            uint8_t* dst = out + (line * chars + pos) * 8;
            for (int r = 0; r < 8; r++) dst[r] = src[r] & 0x1f;
            if (charPos == s.ac) {
                if (s.cursorOn) dst[7] = 0x1f;
                if (!s.blink && s.blinkOn) for (int r = 0; r < 8; r++) dst[r] = 0x1f;
            }
        }
}
