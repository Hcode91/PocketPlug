// PocketPlug - NEC uCOM-4 family 4-bit MCUs (BSD-3-Clause)
// SPDX-License-Identifier: BSD-3-Clause
//
// Port of MAME's cpu/ucom4 (hap). One machine cycle = 4 oscillator clocks.
// Ports: A,B inputs; C,D in/out; E..I outputs (index 0 = A ... 8 = I).

#pragma once
#include <cstdint>
#include <cstring>
#include <vector>

class UCOM4 {
public:
    enum Family { UCOM43, UCOM44 };
    enum Port { PA = 0, PB, PC, PD, PE, PF, PG, PH, PI };
    struct Io {
        virtual ~Io() {}
        virtual uint8_t input(int port) { (void)port; return 0; }
        virtual void output(int port, uint8_t data) { (void)port; (void)data; }
    };

    // d553 / d546 / d650: UCOM43, 2K ROM, 96x4 RAM, 3-level stack; d557l: same but ports B,H,I gone (stripped=true); d552: UCOM44
    void configure(Family f, Io* io, bool d557l = false);
    void setRom(const std::vector<uint8_t>& rom);
    void reset();
    int run(int cycles);
    void setInt(int state);
    uint64_t cycles() const { return s.total; }

    struct State {
        uint16_t stack[3];
        uint8_t portOut[16];
        uint8_t op, prevOp, arg, bitmask, skip;
        uint16_t pc, prevPc;
        uint8_t acc, dpl, dph, carry, carryS, timerF, intF, inteF, intLine;
        int32_t timerLeft;     // machine cycles until the timer flag sets (-1 = off)
        uint8_t ram[128];
        uint64_t total;
    } s;

private:
    uint8_t ramR() const;
    void ramW(uint8_t d);
    uint8_t regR(int i) const { return ramAtC(uint8_t(m_datamask - i)); }
    void regW(int i, uint8_t d) { ramAtW(uint8_t(m_datamask - i), d); }
    uint8_t ramAtC(uint8_t a) const;
    void ramAtW(uint8_t a, uint8_t d);
    uint8_t input(int i);
    void output(int i, uint8_t d);
    void push();
    void pop();
    void incPc() { s.pc = uint16_t((s.pc & ~0xff) | ((s.pc + 1) & 0xff)); }
    void tick(int n);
    void execOne();

    Io* m_io = nullptr;
    Io m_null;
    Family m_family = UCOM43;
    bool m_557 = false;
    int m_stackLevels = 3;
    uint16_t m_prgmask = 0x7ff;
    uint8_t m_datamask = 0x7f;
    std::vector<uint8_t> m_rom;
    int m_icount = 0;
};
