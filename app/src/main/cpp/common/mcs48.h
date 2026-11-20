// PocketPlug - Intel MCS-48 (8048/8049) microcontroller
// SPDX-License-Identifier: BSD-3-Clause
//
// Port of MAME's mcs48.cpp (Dan Boris, Mirko Buffoni, Aaron Giles, Chris Kirmse;
// BSD-3-Clause), plain 8048/8049 opcode set (no UPI-41, no 8243 expander).
// Cycles are machine cycles (clock / 15).

#pragma once
#include <cstdint>

class Mcs48 {
public:
    struct Bus {
        virtual uint8_t mcsProgram(uint16_t a) = 0;     // external program memory
        virtual uint8_t mcsExtRead(uint8_t a) = 0;      // MOVX
        virtual void mcsExtWrite(uint8_t a, uint8_t d) = 0;
        virtual uint8_t mcsPortRead(int port) = 0;      // 1, 2
        virtual void mcsPortWrite(int port, uint8_t d) = 0;
        virtual int mcsTest(int t) = 0;                 // T0, T1
        virtual uint8_t mcsBusRead() { return 0xff; }
        virtual void mcsBusWrite(uint8_t) {}
    };

    Mcs48(Bus* bus, const uint8_t* internalRom, int romSize, int ramSize)
        : m_bus(bus), m_irom(internalRom), m_romSize(romSize), m_ramMask(ramSize - 1) {}
    void reset();
    int step();           // one instruction, returns machine cycles
    void setIrq(bool on) { s.irqState = on; }
    void setEa(bool on) { s.ea = on; }

    struct State {
        uint16_t pc, prevpc, a11;
        uint8_t a, psw, p1, p2, timer, prescaler, t1History, dbbo;
        uint8_t f1, tirqEnabled, xirqEnabled, timecountEnabled, timerFlag, timerOverflow;
        uint8_t irqInProgress, irqPolled, irqState, ea;
        uint8_t ram[256];
    } s{};

private:
    enum { C_FLAG = 0x80, A_FLAG = 0x40, F_FLAG = 0x20, B_FLAG = 0x10 };
    enum { TIMER_ENABLED = 1, COUNTER_ENABLED = 2 };

    uint8_t prog(uint16_t a) { return (!s.ea && a < m_romSize) ? m_irom[a] : m_bus->mcsProgram(a); }
    uint8_t fetch() { uint16_t a = s.pc; s.pc = ((s.pc + 1) & 0x7ff) | (s.pc & 0x800); return prog(a); }
    uint8_t& reg(int n) { return s.ram[((s.psw & B_FLAG) ? 24 : 0) + n]; }
    uint8_t ramR(uint8_t a) const { return s.ram[a & m_ramMask]; }
    void ramW(uint8_t a, uint8_t v) { s.ram[a & m_ramMask] = v; }
    void burn(int n);
    void pushPcPsw();
    void pullPcPsw();
    void pullPc();
    void add(uint8_t d);
    void addc(uint8_t d);
    void jmp(uint16_t a) { s.pc = a | (s.irqInProgress ? 0 : s.a11); }
    void call(uint16_t a) { pushPcPsw(); jmp(a); }
    void jcc(bool r) { uint16_t pch = s.pc & 0xf00; uint8_t off = fetch(); if (r) s.pc = pch | off; }
    void checkIrqs();

    Bus* m_bus;
    const uint8_t* m_irom;
    uint16_t m_romSize;
    uint8_t m_ramMask;
    int m_cycles = 0;
};
