// PocketPlug - WDC W65C02S CPU (CMOS 6502 with the Rockwell bit instructions)
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Own interpreter. Covers the full W65C02S set: the NMOS opcodes, the CMOS
// additions (BRA, STZ, TRB/TSB, PHX/PHY/PLX/PLY, (zp), JMP (abs,X), INC/DEC A,
// BIT imm/zp,X/abs,X), RMB/SMB/BBR/BBS, WAI and STP; decimal mode sets N/Z
// from the BCD result as the CMOS parts do. Undefined opcodes are NOPs of the
// datasheet's length.

#pragma once
#include <cstdint>

class W65C02 {
public:
    struct Bus {
        virtual ~Bus() = default;
        virtual uint8_t read(uint16_t a) = 0;
        virtual void write(uint16_t a, uint8_t d) = 0;
        // interrupt and reset vectors (irq: an IRQ is being taken, not BRK / NMI / reset)
        virtual uint8_t readVector(uint16_t a, bool irq) { (void)irq; return read(a); }
        virtual void rti() {}
    };

    explicit W65C02(Bus* bus) : m_bus(bus) {}
    void reset();
    // runs at least `cycles` cycles; returns how many it actually ran
    int run(int cycles);
    void nmi() { m_nmiPending = true; }           // edge
    void setIrq(bool on) { m_irqLine = on; }
    // cycles run so far by the run() in progress
    int executed() const { return m_cyc; }
    // ends the run() in progress after the current instruction (a new event is due sooner)
    void yield() { m_yield = true; }

    uint16_t pc = 0;
    uint8_t a = 0, x = 0, y = 0, s = 0xfd, p = 0x34;
    bool waiting = false, stopped = false;
    bool m_irqLine = false, m_nmiPending = false;
    bool m_yield = false;

private:
    enum { C = 0x01, Z = 0x02, I = 0x04, D = 0x08, B = 0x10, U = 0x20, V = 0x40, N = 0x80 };
    Bus* m_bus;
    int m_cyc = 0;

    uint8_t rd(uint16_t a) { return m_bus->read(a); }
    void wr(uint16_t a, uint8_t d) { m_bus->write(a, d); }
    uint8_t fetch() { return rd(pc++); }
    uint16_t fetch16() { uint16_t l = fetch(); return uint16_t(l | (fetch() << 8)); }
    void push(uint8_t v) { wr(uint16_t(0x100 | s), v); s--; }
    uint8_t pull() { s++; return rd(uint16_t(0x100 | s)); }
    void nz(uint8_t v) { p = uint8_t((p & ~(N | Z)) | (v & N) | (v ? 0 : Z)); }
    void interrupt(uint16_t vec, bool brk, bool irq = false);
    void step();

    void adc(uint8_t v);
    void sbc(uint8_t v);
    void cmp(uint8_t r, uint8_t v) { uint16_t t = uint16_t(r - v); p = uint8_t((p & ~C) | (r >= v ? C : 0)); nz(uint8_t(t)); }
    uint8_t asl(uint8_t v) { p = uint8_t((p & ~C) | (v >> 7)); v <<= 1; nz(v); return v; }
    uint8_t lsr(uint8_t v) { p = uint8_t((p & ~C) | (v & 1)); v >>= 1; nz(v); return v; }
    uint8_t rol(uint8_t v) { uint8_t c = p & C; p = uint8_t((p & ~C) | (v >> 7)); v = uint8_t((v << 1) | c); nz(v); return v; }
    uint8_t ror(uint8_t v) { uint8_t c = p & C; p = uint8_t((p & ~C) | (v & 1)); v = uint8_t((v >> 1) | (c << 7)); nz(v); return v; }

    // addressing modes (return the effective address, count page-cross cycles)
    uint16_t zp() { return fetch(); }
    uint16_t zpx() { return uint8_t(fetch() + x); }
    uint16_t zpy() { return uint8_t(fetch() + y); }
    uint16_t abs() { return fetch16(); }
    uint16_t absx(bool pen) { uint16_t b = fetch16(), e = uint16_t(b + x); if (pen && (b ^ e) & 0xff00) m_cyc++; return e; }
    uint16_t absy(bool pen) { uint16_t b = fetch16(), e = uint16_t(b + y); if (pen && (b ^ e) & 0xff00) m_cyc++; return e; }
    uint16_t zp16(uint8_t z) { return uint16_t(rd(z) | (rd(uint8_t(z + 1)) << 8)); }
    uint16_t izx() { return zp16(uint8_t(fetch() + x)); }
    uint16_t izy(bool pen) { uint16_t b = zp16(fetch()), e = uint16_t(b + y); if (pen && (b ^ e) & 0xff00) m_cyc++; return e; }
    uint16_t izp() { return zp16(fetch()); }
    void branch(bool cond);
};
