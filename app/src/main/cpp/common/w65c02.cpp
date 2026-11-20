// PocketPlug - WDC W65C02S CPU
// SPDX-License-Identifier: GPL-2.0-or-later

#include "w65c02.h"

namespace {
// base cycles (W65C02S datasheet); page crossing and decimal mode add one
const uint8_t kCycles[256] = {
//  0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
    7, 6, 2, 1, 5, 3, 5, 5, 3, 2, 2, 1, 6, 4, 6, 5,  // 0
    2, 5, 5, 1, 5, 4, 6, 5, 2, 4, 2, 1, 6, 4, 6, 5,  // 1
    6, 6, 2, 1, 3, 3, 5, 5, 4, 2, 2, 1, 4, 4, 6, 5,  // 2
    2, 5, 5, 1, 4, 4, 6, 5, 2, 4, 2, 1, 4, 4, 6, 5,  // 3
    6, 6, 2, 1, 3, 3, 5, 5, 3, 2, 2, 1, 3, 4, 6, 5,  // 4
    2, 5, 5, 1, 4, 4, 6, 5, 2, 4, 3, 1, 8, 4, 6, 5,  // 5
    6, 6, 2, 1, 3, 3, 5, 5, 4, 2, 2, 1, 6, 4, 6, 5,  // 6
    2, 5, 5, 1, 4, 4, 6, 5, 2, 4, 4, 1, 6, 4, 6, 5,  // 7
    3, 6, 2, 1, 3, 3, 3, 5, 2, 2, 2, 1, 4, 4, 4, 5,  // 8
    2, 6, 5, 1, 4, 4, 4, 5, 2, 5, 2, 1, 4, 5, 5, 5,  // 9
    2, 6, 2, 1, 3, 3, 3, 5, 2, 2, 2, 1, 4, 4, 4, 5,  // A
    2, 5, 5, 1, 4, 4, 4, 5, 2, 4, 2, 1, 4, 4, 4, 5,  // B
    2, 6, 2, 1, 3, 3, 5, 5, 2, 2, 2, 3, 4, 4, 6, 5,  // C
    2, 5, 5, 1, 4, 4, 6, 5, 2, 4, 3, 3, 4, 4, 7, 5,  // D
    2, 6, 2, 1, 3, 3, 5, 5, 2, 2, 2, 1, 4, 4, 6, 5,  // E
    2, 5, 5, 1, 4, 4, 6, 5, 2, 4, 4, 1, 4, 4, 7, 5,  // F
};
}

void W65C02::reset()
{
    s = 0xfd;
    p = uint8_t((p | I | U | B) & ~D);
    waiting = stopped = false;
    m_nmiPending = false;
    pc = uint16_t(m_bus->readVector(0xfffc, false) | (m_bus->readVector(0xfffd, false) << 8));
}

void W65C02::interrupt(uint16_t vec, bool brk, bool irq)
{
    push(uint8_t(pc >> 8));
    push(uint8_t(pc));
    push(uint8_t((p | U) & ~B) | (brk ? B : 0));
    p = uint8_t((p | I) & ~D);
    uint8_t lo = m_bus->readVector(vec, irq);
    pc = uint16_t(lo | (m_bus->readVector(uint16_t(vec + 1), irq) << 8));
    m_cyc += 7;
}

int W65C02::run(int cycles)
{
    m_cyc = 0;
    while (m_cyc < cycles) {
        if (stopped) { m_cyc = cycles; break; }
        if (m_nmiPending) { m_nmiPending = false; waiting = false; interrupt(0xfffa, false); continue; }
        if (m_irqLine) {
            waiting = false;
            if (!(p & I)) { interrupt(0xfffe, false, true); continue; }
        }
        if (waiting) { m_cyc = cycles; break; }
        step();
        if (m_yield) { m_yield = false; break; }
    }
    return m_cyc;
}

void W65C02::branch(bool cond)
{
    int8_t off = int8_t(fetch());
    if (cond) {
        uint16_t t = uint16_t(pc + off);
        m_cyc += ((t ^ pc) & 0xff00) ? 2 : 1;
        pc = t;
    }
}

void W65C02::adc(uint8_t v)
{
    if (p & D) {
        m_cyc++;
        unsigned c = p & C;
        unsigned lo = (a & 0x0f) + (v & 0x0f) + c;
        unsigned hi = (a & 0xf0) + (v & 0xf0);
        if (lo > 0x09) { hi += 0x10; lo += 0x06; }
        // V from the binary sum of the high nibbles (as the 65C02 computes it)
        uint8_t bin = uint8_t(a + v + c);
        bool ov = (~(a ^ v) & (a ^ bin) & 0x80) != 0;
        if (hi > 0x90) hi += 0x60;
        uint8_t r = uint8_t((lo & 0x0f) | (hi & 0xf0));
        p = uint8_t((p & ~(C | V)) | (hi > 0xff ? C : 0) | (ov ? V : 0));
        a = r;
        nz(a);
    } else {
        unsigned t = a + v + (p & C);
        p = uint8_t((p & ~(C | V)) | (t > 0xff ? C : 0) | ((~(a ^ v) & (a ^ t) & 0x80) ? V : 0));
        a = uint8_t(t);
        nz(a);
    }
}

void W65C02::sbc(uint8_t v)
{
    if (p & D) {
        m_cyc++;
        unsigned c = (p & C) ? 0 : 1;
        int t = a - v - int(c);
        int lo = (a & 0x0f) - (v & 0x0f) - int(c);
        uint8_t bin = uint8_t(t);
        bool ov = ((a ^ v) & (a ^ bin) & 0x80) != 0;
        if (t < 0) t -= 0x60;
        if (lo < 0) t -= 0x06;
        p = uint8_t((p & ~(C | V)) | ((a - v - int(c)) >= 0 ? C : 0) | (ov ? V : 0));
        a = uint8_t(t);
        nz(a);
    } else {
        unsigned t = a - v - ((p & C) ? 0 : 1);
        p = uint8_t((p & ~(C | V)) | (t < 0x100 ? C : 0) | (((a ^ v) & (a ^ t) & 0x80) ? V : 0));
        a = uint8_t(t);
        nz(a);
    }
}

void W65C02::step()
{
    uint8_t op = fetch();
    m_cyc += kCycles[op];
    uint16_t ea;
    uint8_t v;

    // the regular "cc = 01" group: ORA AND EOR ADC STA LDA CMP SBC
    if (op != 0x89 && ((op & 3) == 1 || ((op & 0x0f) == 0x02 && (op & 0x10)))) {
        bool store = (op & 0xe0) == 0x80;
        if ((op & 0x1f) == 0x12) ea = izp();
        else switch (op & 0x1c) {
            case 0x00: ea = izx(); break;
            case 0x04: ea = zp(); break;
            case 0x08: ea = pc++; break;          // immediate
            case 0x0c: ea = abs(); break;
            case 0x10: ea = izy(!store); break;
            case 0x14: ea = zpx(); break;
            case 0x18: ea = absy(!store); break;
            default:   ea = absx(!store); break;
        }
        if (store) { wr(ea, a); return; }
        v = rd(ea);
        switch (op >> 5) {
            case 0: a |= v; nz(a); break;
            case 1: a &= v; nz(a); break;
            case 2: a ^= v; nz(a); break;
            case 3: adc(v); break;
            case 5: a = v; nz(a); break;
            case 6: cmp(a, v); break;
            case 7: sbc(v); break;
        }
        return;
    }

    // RMB / SMB / BBR / BBS
    if ((op & 0x0f) == 0x07) {
        ea = zp(); v = rd(ea);
        uint8_t bit = uint8_t(1 << ((op >> 4) & 7));
        wr(ea, (op & 0x80) ? uint8_t(v | bit) : uint8_t(v & ~bit));
        return;
    }
    if ((op & 0x0f) == 0x0f) {
        ea = zp(); v = rd(ea);
        uint8_t bit = uint8_t(1 << ((op >> 4) & 7));
        bool set = (v & bit) != 0;
        branch((op & 0x80) ? set : !set);
        return;
    }

    switch (op) {
    // ---- read-modify-write ----
    case 0x06: ea = zp();       wr(ea, asl(rd(ea))); break;
    case 0x16: ea = zpx();      wr(ea, asl(rd(ea))); break;
    case 0x0e: ea = abs();      wr(ea, asl(rd(ea))); break;
    case 0x1e: ea = absx(true); wr(ea, asl(rd(ea))); break;
    case 0x0a: a = asl(a); break;
    case 0x26: ea = zp();       wr(ea, rol(rd(ea))); break;
    case 0x36: ea = zpx();      wr(ea, rol(rd(ea))); break;
    case 0x2e: ea = abs();      wr(ea, rol(rd(ea))); break;
    case 0x3e: ea = absx(true); wr(ea, rol(rd(ea))); break;
    case 0x2a: a = rol(a); break;
    case 0x46: ea = zp();       wr(ea, lsr(rd(ea))); break;
    case 0x56: ea = zpx();      wr(ea, lsr(rd(ea))); break;
    case 0x4e: ea = abs();      wr(ea, lsr(rd(ea))); break;
    case 0x5e: ea = absx(true); wr(ea, lsr(rd(ea))); break;
    case 0x4a: a = lsr(a); break;
    case 0x66: ea = zp();       wr(ea, ror(rd(ea))); break;
    case 0x76: ea = zpx();      wr(ea, ror(rd(ea))); break;
    case 0x6e: ea = abs();      wr(ea, ror(rd(ea))); break;
    case 0x7e: ea = absx(true); wr(ea, ror(rd(ea))); break;
    case 0x6a: a = ror(a); break;
    case 0xc6: ea = zp();        v = uint8_t(rd(ea) - 1); wr(ea, v); nz(v); break;
    case 0xd6: ea = zpx();       v = uint8_t(rd(ea) - 1); wr(ea, v); nz(v); break;
    case 0xce: ea = abs();       v = uint8_t(rd(ea) - 1); wr(ea, v); nz(v); break;
    case 0xde: ea = absx(false); v = uint8_t(rd(ea) - 1); wr(ea, v); nz(v); break;
    case 0x3a: a--; nz(a); break;
    case 0xe6: ea = zp();        v = uint8_t(rd(ea) + 1); wr(ea, v); nz(v); break;
    case 0xf6: ea = zpx();       v = uint8_t(rd(ea) + 1); wr(ea, v); nz(v); break;
    case 0xee: ea = abs();       v = uint8_t(rd(ea) + 1); wr(ea, v); nz(v); break;
    case 0xfe: ea = absx(false); v = uint8_t(rd(ea) + 1); wr(ea, v); nz(v); break;
    case 0x1a: a++; nz(a); break;
    // TSB / TRB
    case 0x04: case 0x0c: case 0x14: case 0x1c:
        ea = (op & 0x08) ? abs() : zp();
        v = rd(ea);
        p = uint8_t((p & ~Z) | ((v & a) ? 0 : Z));
        wr(ea, (op & 0x10) ? uint8_t(v & ~a) : uint8_t(v | a));
        break;
    // BIT
    case 0x24: v = rd(zp());        p = uint8_t((p & ~(N | V | Z)) | (v & (N | V)) | ((v & a) ? 0 : Z)); break;
    case 0x2c: v = rd(abs());       p = uint8_t((p & ~(N | V | Z)) | (v & (N | V)) | ((v & a) ? 0 : Z)); break;
    case 0x34: v = rd(zpx());       p = uint8_t((p & ~(N | V | Z)) | (v & (N | V)) | ((v & a) ? 0 : Z)); break;
    case 0x3c: v = rd(absx(true));  p = uint8_t((p & ~(N | V | Z)) | (v & (N | V)) | ((v & a) ? 0 : Z)); break;
    case 0x89: v = fetch();         p = uint8_t((p & ~Z) | ((v & a) ? 0 : Z)); break;
    // loads / stores of X, Y, zero
    case 0xa2: x = fetch(); nz(x); break;
    case 0xa6: x = rd(zp()); nz(x); break;
    case 0xb6: x = rd(zpy()); nz(x); break;
    case 0xae: x = rd(abs()); nz(x); break;
    case 0xbe: x = rd(absy(true)); nz(x); break;
    case 0xa0: y = fetch(); nz(y); break;
    case 0xa4: y = rd(zp()); nz(y); break;
    case 0xb4: y = rd(zpx()); nz(y); break;
    case 0xac: y = rd(abs()); nz(y); break;
    case 0xbc: y = rd(absx(true)); nz(y); break;
    case 0x86: wr(zp(), x); break;
    case 0x96: wr(zpy(), x); break;
    case 0x8e: wr(abs(), x); break;
    case 0x84: wr(zp(), y); break;
    case 0x94: wr(zpx(), y); break;
    case 0x8c: wr(abs(), y); break;
    case 0x64: wr(zp(), 0); break;
    case 0x74: wr(zpx(), 0); break;
    case 0x9c: wr(abs(), 0); break;
    case 0x9e: wr(absx(false), 0); break;
    // compares
    case 0xe0: cmp(x, fetch()); break;
    case 0xe4: cmp(x, rd(zp())); break;
    case 0xec: cmp(x, rd(abs())); break;
    case 0xc0: cmp(y, fetch()); break;
    case 0xc4: cmp(y, rd(zp())); break;
    case 0xcc: cmp(y, rd(abs())); break;
    // transfers, flags, stack
    case 0xaa: x = a; nz(x); break;
    case 0xa8: y = a; nz(y); break;
    case 0x8a: a = x; nz(a); break;
    case 0x98: a = y; nz(a); break;
    case 0xba: x = s; nz(x); break;
    case 0x9a: s = x; break;
    case 0xca: x--; nz(x); break;
    case 0x88: y--; nz(y); break;
    case 0xe8: x++; nz(x); break;
    case 0xc8: y++; nz(y); break;
    case 0x18: p &= ~C; break;
    case 0x38: p |= C; break;
    case 0x58: p &= ~I; break;
    case 0x78: p |= I; break;
    case 0xb8: p &= ~V; break;
    case 0xd8: p &= ~D; break;
    case 0xf8: p |= D; break;
    case 0x48: push(a); break;
    case 0x68: a = pull(); nz(a); break;
    case 0xda: push(x); break;
    case 0xfa: x = pull(); nz(x); break;
    case 0x5a: push(y); break;
    case 0x7a: y = pull(); nz(y); break;
    case 0x08: push(p | B | U); break;
    case 0x28: p = uint8_t(pull() | U | B); break;
    // flow
    case 0x10: branch(!(p & N)); break;
    case 0x30: branch(p & N); break;
    case 0x50: branch(!(p & V)); break;
    case 0x70: branch(p & V); break;
    case 0x90: branch(!(p & C)); break;
    case 0xb0: branch(p & C); break;
    case 0xd0: branch(!(p & Z)); break;
    case 0xf0: branch(p & Z); break;
    case 0x80: branch(true); break;
    case 0x4c: pc = abs(); break;
    case 0x6c: { uint16_t t = abs(); pc = uint16_t(rd(t) | (rd(uint16_t(t + 1)) << 8)); break; }
    case 0x7c: { uint16_t t = uint16_t(abs() + x); pc = uint16_t(rd(t) | (rd(uint16_t(t + 1)) << 8)); break; }
    case 0x20: { uint16_t t = abs(); pc--; push(uint8_t(pc >> 8)); push(uint8_t(pc)); pc = t; break; }
    case 0x60: { uint16_t l = pull(); pc = uint16_t((l | (pull() << 8)) + 1); break; }
    case 0x40: { p = uint8_t(pull() | U | B); uint16_t l = pull(); pc = uint16_t(l | (pull() << 8)); m_bus->rti(); break; }
    case 0x00: pc++; m_cyc -= 7; interrupt(0xfffe, true); break;
    case 0xcb: waiting = true; break;
    case 0xdb: stopped = true; break;
    // NOPs with operands
    case 0x02: case 0x22: case 0x42: case 0x62: case 0x82: case 0xc2: case 0xe2:
    case 0x44: case 0x54: case 0xd4: case 0xf4:
        pc++; break;
    case 0x5c: case 0xdc: case 0xfc:
        pc += 2; break;
    default: break;   // EA and the one-byte, one-cycle NOPs
    }
}
