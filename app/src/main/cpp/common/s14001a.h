// PocketPlug - SSi TSI S14001A speech chip
// SPDX-License-Identifier: BSD-3-Clause
//
// Port of MAME's s14001a.cpp (Jonathan Gevaryahu, Ed Bernard): the two-phase
// state machine, one clock() per chip clock, 4-bit output 0..15 (7 = silence).

#pragma once
#include <cstdint>
#include <cstring>

class S14001A {
public:
    struct State {
        uint8_t phase1, stateP1, stateP2;
        uint16_t dar13To05P1, dar13To05P2;
        uint8_t dar04To00P1, dar04To00P2;
        uint16_t cwarP1, cwarP2;
        uint8_t stopP1, stopP2, voicedP1, voicedP2, silenceP1, silenceP2;
        uint8_t lengthP1, lengthP2, xRepeatP1, xRepeatP2, deltaOldP1, deltaOldP2;
        uint8_t dar04To00CarryP2, ppqCarryP2, repeatCarryP2, lengthCarryP2;
        uint16_t romAddrP1, romAddrP2;
        uint8_t busyP1, start, word, outputP1, outputP2, bank;
    } s;

    S14001A() { std::memset(&s, 0, sizeof(s)); s.outputP1 = s.outputP2 = 7; }
    void setRom(const uint8_t* rom, size_t size) { m_rom = rom; m_size = size; }
    void setBank(int b) { s.bank = uint8_t(b); }
    int busy() const { return s.busyP1; }
    void dataW(uint8_t d) { s.word = d & 0x3f; }
    void startW(int state) { if (state && !s.start) s.stateP1 = WORDWAIT; s.start = state != 0; }
    int output() const { return s.outputP2; }

    void clock()
    {
        if (s.phase1) {
            s.phase1 = 0;
            s.stateP2 = s.stateP1; s.dar13To05P2 = s.dar13To05P1; s.dar04To00P2 = s.dar04To00P1;
            s.cwarP2 = s.cwarP1; s.stopP2 = s.stopP1; s.voicedP2 = s.voicedP1; s.silenceP2 = s.silenceP1;
            s.lengthP2 = s.lengthP1; s.xRepeatP2 = s.xRepeatP1; s.deltaOldP2 = s.deltaOldP1;
            s.outputP2 = s.outputP1; s.romAddrP2 = s.romAddrP1;
            s.dar04To00CarryP2 = s.dar04To00P2 == 0x1f;
            s.ppqCarryP2 = s.dar04To00CarryP2 && ((s.lengthP2 & 0x03) == 0x03);
            s.repeatCarryP2 = s.ppqCarryP2 && ((s.lengthP2 & 0x0c) == 0x0c);
            s.lengthCarryP2 = s.repeatCarryP2 && (s.lengthP2 == 0x7f);
            return;
        }
        s.phase1 = 1;
        switch (s.stateP1) {
        case IDLE:
            s.outputP1 = 7;
            if (s.start) s.stateP1 = WORDWAIT;
            s.busyP1 = 0;
            break;
        case WORDWAIT:
            s.dar13To05P1 = uint16_t((s.word & 0x3c) >> 2);
            s.dar04To00P1 = uint8_t((s.word & 0x03) << 3);
            s.romAddrP1 = uint16_t((s.dar13To05P1 << 3) | (s.dar04To00P1 >> 2));
            s.outputP1 = 7;
            s.stateP1 = s.start ? WORDWAIT : CWARMSB;
            s.busyP1 = 1;
            break;
        case CWARMSB:
            s.cwarP1 = uint16_t(mem(s.romAddrP2) << 4);
            s.dar04To00P1 += 4;
            if (s.dar04To00P1 >= 32) s.dar04To00P1 = 0;
            s.romAddrP1 = uint16_t((s.dar13To05P1 << 3) | (s.dar04To00P1 >> 2));
            s.outputP1 = 7;
            s.stateP1 = s.start ? WORDWAIT : CWARLSB;
            break;
        case CWARLSB:
            s.cwarP1 = uint16_t(s.cwarP2 | (mem(s.romAddrP2) >> 4));
            s.romAddrP1 = s.cwarP1;
            s.outputP1 = 7;
            s.stateP1 = s.start ? WORDWAIT : DARMSB;
            break;
        case DARMSB:
            s.dar13To05P1 = uint16_t(mem(s.romAddrP2) << 1);
            s.dar04To00P1 = 0;
            s.cwarP1++;
            s.romAddrP1 = s.cwarP1;
            s.outputP1 = 7;
            s.stateP1 = s.start ? WORDWAIT : CTRLBITS;
            break;
        case CTRLBITS: {
            uint8_t data = mem(s.romAddrP2);
            s.stopP1 = (data & 0x80) != 0;
            s.voicedP1 = (data & 0x40) != 0;
            s.silenceP1 = (data & 0x20) != 0;
            s.xRepeatP1 = data & 0x03;
            s.lengthP1 = uint8_t((data & 0x1f) << 2);
            s.dar04To00P1 = 0;
            s.cwarP1++;
            s.romAddrP1 = uint16_t((s.dar13To05P1 << 3) | (s.dar04To00P1 >> 2));
            s.outputP1 = 7;
            s.stateP1 = s.start ? WORDWAIT : PLAY;
            break;
        }
        case PLAY: {
            uint8_t ppq = s.lengthP2 & 0x03;
            bool ppqStart = s.dar04To00P2 == 0;
            // Mux8To2
            uint8_t deltaAdr = s.dar04To00P2 & 0x03;
            if (s.voicedP2 && (ppq & 1)) deltaAdr ^= 0x03;
            uint8_t delta = uint8_t(mem(s.romAddrP2) >> (~deltaAdr << 1 & 0x06) & 0x03);
            // CalculateIncrement
            static const uint8_t inc[4][4] = { { 3, 3, 1, 1 }, { 1, 1, 0, 0 }, { 0, 0, 1, 1 }, { 1, 1, 3, 3 } };
            uint8_t deltaOld = s.deltaOldP2;
            if (ppq == 0 && ppqStart) deltaOld = 0x02;
            bool mirror = ppq & 1;
            uint8_t incr; bool add;
            if (!s.voicedP2 || !mirror) { incr = inc[delta][deltaOld]; add = delta >= 0x02; }
            else { incr = inc[deltaOld][delta]; add = deltaOld < 0x02; }
            s.deltaOldP1 = delta;
            if (s.voicedP2 && ppqStart && mirror) incr = 0;
            // CalculateOutput
            bool silence = (ppq >> 1) & 1;
            uint8_t out;
            if (s.silenceP2 || (s.voicedP2 && silence)) out = 7;
            else {
                uint8_t l = s.outputP2;
                if (ppq == 0 && ppqStart) l = 7;
                uint8_t t = l;
                if (!add) t ^= 0x0f;
                t = uint8_t(t + incr);
                if (t > 15) t = 15;
                if (!add) t ^= 0x0f;
                out = t;
            }
            s.outputP1 = out;

            s.dar04To00P1++;
            if (s.dar04To00CarryP2) {
                s.dar04To00P1 = 0;
                s.lengthP1++;
                if (s.lengthP1 >= 0x80) s.lengthP1 = 0;
            }
            if (s.voicedP2 && s.repeatCarryP2) {
                s.lengthP1 &= 0x70;
                s.lengthP1 |= uint8_t(s.xRepeatP1 << 2);
                s.dar13To05P1++;
                if (s.dar13To05P1 >= 0x200) s.dar13To05P1 = 0;
            }
            if (!s.voicedP2 && s.dar04To00CarryP2) {
                s.dar13To05P1++;
                if (s.dar13To05P1 >= 0x200) s.dar13To05P1 = 0;
            }
            uint16_t ra = s.dar04To00P1;
            if (s.voicedP2 && (s.lengthP1 & 0x1)) ra ^= 0x1f;
            s.romAddrP1 = uint16_t((s.dar13To05P1 << 3) | ra >> 2);
            if (s.start) s.stateP1 = WORDWAIT;
            else if (s.stopP2 && s.lengthCarryP2) s.stateP1 = DELAY;
            else if (s.lengthCarryP2) { s.stateP1 = DARMSB; s.romAddrP1 = s.cwarP1; }
            else s.stateP1 = PLAY;
            break;
        }
        case DELAY:
            s.outputP1 = 7;
            s.stateP1 = s.start ? WORDWAIT : IDLE;
            break;
        }
    }

private:
    enum : uint8_t { IDLE = 0, WORDWAIT, CWARMSB, CWARLSB, DARMSB, CTRLBITS, PLAY, DELAY };
    const uint8_t* m_rom = nullptr;
    size_t m_size = 0;
    uint8_t mem(uint16_t a) const
    {
        if (!m_rom || !m_size) return 0;
        size_t off = (size_t(s.bank) * 0x1000 + (a & 0xfff)) % m_size;
        return m_rom[off];
    }
};
