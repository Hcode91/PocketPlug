// PocketPlug - Hitachi HMCS40 family 4-bit MCUs (BSD-3-Clause)
// SPDX-License-Identifier: BSD-3-Clause
//
// Port of MAME's cpu/hmcs40 (hap). One machine cycle = 4 oscillator clocks.
// Program words are 10 bits, stored as little-endian 16-bit words.

#pragma once
#include <cstdint>
#include <cstring>
#include <vector>

class HMCS40 {
public:
    enum Family { HMCS42 = 0, HMCS43, HMCS44, HMCS45, HMCS46, HMCS47 };

    struct Io {
        virtual ~Io() {}
        virtual uint8_t readR(int index) { (void)index; return 0; }      // returns the pin levels (4 bits)
        virtual void writeR(int index, uint8_t data) { (void)index; (void)data; }
        virtual uint16_t readD() { return 0; }                          // all 16 D pins
        virtual void writeD(uint16_t data, uint16_t mask) { (void)data; (void)mask; }
    };

    // family, PMOS (false) or CMOS (true)
    void configure(Family f, bool cmos, Io* io);
    // rom: the MAME region (0x2000 bytes for 2K parts, 0x1000 for HMCS43)
    void setRom(const std::vector<uint8_t>& region);
    void reset();
    // runs at least `cycles` machine cycles; returns the cycles run
    int run(int cycles);
    void setInput(int line, int state);    // 0 = INT0, 1 = INT1, 2 = HLT
    uint64_t cycles() const { return s.total; }

    struct State {
        uint16_t stack[4];
        uint16_t op, prevOp;
        uint8_t i, eintLine, halt, prescaler, blockInt;
        uint16_t pc, prevPc;
        uint8_t pcUpper, a, b, x, spx, y, spy, s, c, tc, cf, ie, iri, irt, ifl[2], tf, intl[2];
        uint8_t r[8];
        uint16_t d;
        uint8_t ram[256];
        uint64_t total;
    } s;

private:
    uint16_t word(uint16_t a) const { a &= m_prgmask; return uint16_t(m_rom[a * 2] | m_rom[a * 2 + 1] << 8); }
    uint8_t ramR() const;
    void ramW(uint8_t d);
    uint8_t& ramAt(uint8_t a);
    void pushStack();
    void popStack();
    void incPc();
    void cycle() { s.total++; m_icount--; clockPrescaler(); }
    void clockPrescaler();
    void clockTimer();
    void takeInterrupt();
    uint8_t readR(int idx);
    void writeR(int idx, uint8_t d);
    int readD(int idx);
    void writeD(int idx, int state);
    void resetIo();
    void execOne();

    Io* m_io = nullptr;
    Io m_nullIo;
    Family m_family = HMCS44;
    uint16_t m_polarity = 0;
    int m_stackLevels = 4;
    uint16_t m_pcmask = 0x7ff, m_prgmask = 0xfff;
    uint8_t m_datamask = 0xff;
    std::vector<uint8_t> m_rom;
    int m_icount = 0;
};
