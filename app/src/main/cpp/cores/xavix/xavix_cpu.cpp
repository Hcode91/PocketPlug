// PocketPlug - XaviX CPU: a 6502 with code / data banks and far calls
// SPDX-License-Identifier: BSD-3-Clause
//
// After MAME's cpu/m6502/xavix.cpp and oxavix.lst / dxavix.lst (David Haywood),
// with the base 6502 behaviour of MAME's m6502 core (Olivier Galibert), BSD-3-Clause.
// Opcode and operand fetches use the code bank; data reads of $8000-$FFFF (and
// all of the bus when the data bank is $80 or above) use the data bank; zero
// page and the stack are always the low bus, with $FE / $FF reading and
// writing the code and data bank registers.

#include "xavix.h"

enum { FC = 0x01, FZ = 0x02, FI = 0x04, FD = 0x08, FB = 0x10, FU = 0x20, FV = 0x40, FN = 0x80 };

uint8_t XavixCore::fetch(uint16_t pc)
{
    uint32_t a = (uint32_t(m_cpu.codebank) << 16) | pc;
    if ((a & 0x8000) || (a & 0x800000)) return rom(a);
    return lowRead(a & 0x7fff);
}

uint8_t XavixCore::rdZ(uint8_t a)
{
    if (a == 0xfe) return m_cpu.codebank;
    if (a == 0xff) return m_cpu.databank;
    return lowRead(a);
}

void XavixCore::wrZ(uint8_t a, uint8_t d)
{
    if (a == 0xfe) { m_cpu.codebank = d; return; }
    if (a == 0xff) { m_cpu.databank = d; return; }
    lowWrite(a, d);
}

uint8_t XavixCore::readFull(uint8_t bank, uint16_t a)
{
    if (bank < 0x80 && a < 0x8000) {
        if (a == 0xfe) return m_cpu.codebank;
        if (a == 0xff) return m_cpu.databank;
        return lowRead(a);
    }
    return rom((uint32_t(bank) << 16) | a);
}

void XavixCore::writeFull(uint8_t bank, uint16_t a, uint8_t d)
{
    if (bank < 0x80 && a < 0x8000) {
        if (a == 0xfe) { m_cpu.codebank = d; return; }
        if (a == 0xff) { m_cpu.databank = d; return; }
        lowWrite(a, d);
    }
    // writes to the external bus (ROM) go nowhere
}

uint8_t XavixCore::rdD(uint16_t a) { return readFull(m_cpu.databank, a); }
void XavixCore::wrD(uint16_t a, uint8_t d) { writeFull(m_cpu.databank, a, d); }

void XavixCore::adc(uint8_t v)
{
    uint8_t& a = m_cpu.a; uint8_t& p = m_cpu.p;
    int c = p & FC;
    if (p & FD) {
        int lo = (a & 0x0f) + (v & 0x0f) + c;
        int hi = (a & 0xf0) + (v & 0xf0);
        p &= ~(FN | FV | FZ | FC);
        if (!((a + v + c) & 0xff)) p |= FZ;
        if (lo > 9) { hi += 0x10; lo += 6; }
        if (hi & 0x80) p |= FN;
        if (~(a ^ v) & (a ^ hi) & 0x80) p |= FV;
        if (hi > 0x90) hi += 0x60;
        if (hi & 0xff00) p |= FC;
        a = (uint8_t)((lo & 0x0f) | (hi & 0xf0));
    } else {
        int sum = a + v + c;
        p &= ~(FV | FC);
        if (~(a ^ v) & (a ^ sum) & 0x80) p |= FV;
        if (sum & 0x100) p |= FC;
        a = (uint8_t)sum;
        setNZ(a);
    }
}

void XavixCore::sbc(uint8_t v)
{
    uint8_t& a = m_cpu.a; uint8_t& p = m_cpu.p;
    int c = (p & FC) ? 0 : 1;
    int diff = a - v - c;
    if (p & FD) {
        int lo = (a & 0x0f) - (v & 0x0f) - c;
        int hi = (a & 0xf0) - (v & 0xf0);
        p &= ~(FN | FV | FZ | FC);
        if (lo & 0x10) { lo -= 6; hi--; }
        if ((a ^ v) & (a ^ diff) & 0x80) p |= FV;
        if (!(diff & 0xff00)) p |= FC;
        if (!(diff & 0xff)) p |= FZ;
        if (diff & 0x80) p |= FN;
        if (hi & 0x0100) hi -= 0x60;
        a = (uint8_t)((lo & 0x0f) | (hi & 0xf0));
    } else {
        p &= ~(FV | FC);
        if ((a ^ v) & (a ^ diff) & 0x80) p |= FV;
        if (!(diff & 0xff00)) p |= FC;
        a = (uint8_t)diff;
        setNZ(a);
    }
}

void XavixCore::cmp(uint8_t r, uint8_t v)
{
    int d = r - v;
    m_cpu.p = (m_cpu.p & ~FC) | (d >= 0 ? FC : 0);
    setNZ((uint8_t)d);
}

// brk_xav_imp: the code bank goes on the private stack and is set to 0; the
// vectors come from the vector registers when they are enabled
void XavixCore::cpuInterrupt(bool nmi, bool brk)
{
    Cpu& c = m_cpu;
    pushBank(c.codebank);
    c.codebank = 0;
    push(c.pc >> 8);
    push(c.pc & 0xff);
    push(brk ? (c.p | FB | FU) : ((c.p | FU) & ~FB));
    uint8_t lo, hi;
    if (m_s.vectorEnable) {
        lo = nmi ? m_s.nmiLo : m_s.irqLo;
        hi = nmi ? m_s.nmiHi : m_s.irqHi;
    } else {
        uint16_t v = nmi ? 0xfffa : 0xfffe;
        lo = fetch(v); hi = fetch(v + 1);
    }
    c.pc = lo | (hi << 8);
    c.p |= FI;
}

int XavixCore::cpuStep()
{
    Cpu& c = m_cpu;
    if (c.nmiPending) {
        c.nmiPending = 0;
        cpuInterrupt(true, false);
        return 7;
    }
    if (c.irqLine && !(c.p & FI)) {
        cpuInterrupt(false, false);
        return 7;
    }

    uint8_t op = fetch(c.pc++);
    int cyc = 2;
    if (m_cfg.cpu && step2000(op, cyc)) return cyc;
    uint16_t ad = 0;
    bool zp = false;          // effective address is on zero page (bank registers visible)
    bool cross = false;
    uint8_t v, t;
    uint8_t& a = c.a; uint8_t& x = c.x; uint8_t& y = c.y; uint8_t& p = c.p;

#define RD() (zp ? rdZ((uint8_t)ad) : rdD(ad))
#define WR(d) (zp ? wrZ((uint8_t)ad, (d)) : wrD(ad, (d)))
#define F16() (fetch(c.pc) | (fetch((uint16_t)(c.pc + 1)) << 8))
#define IMMV() fetch(c.pc++)
#define ZP()   (zp = true, ad = fetch(c.pc++))
#define ZPX()  (zp = true, ad = (uint8_t)(fetch(c.pc++) + x))
#define ZPY()  (zp = true, ad = (uint8_t)(fetch(c.pc++) + y))
#define ABS()  (ad = F16(), c.pc += 2)
#define ABX()  { uint16_t b_ = F16(); c.pc += 2; ad = b_ + x; cross = (b_ ^ ad) & 0xff00; }
#define ABY()  { uint16_t b_ = F16(); c.pc += 2; ad = b_ + y; cross = (b_ ^ ad) & 0xff00; }
#define IZX()  { uint8_t z_ = fetch(c.pc++) + x; ad = rdZ(z_) | (rdZ((uint8_t)(z_ + 1)) << 8); }
#define IZY()  { uint8_t z_ = fetch(c.pc++); uint16_t b_ = rdZ(z_) | (rdZ((uint8_t)(z_ + 1)) << 8); ad = b_ + y; cross = (b_ ^ ad) & 0xff00; }
#define BR(cond) { int8_t o_ = (int8_t)fetch(c.pc++); cyc = 2; if (cond) { uint16_t n_ = c.pc + o_; cyc += ((n_ ^ c.pc) & 0xff00) ? 2 : 1; c.pc = n_; } }
#define RMW(addrmode, cy, body) { addrmode; v = RD(); WR(v); body; WR(v); cyc = cy; }

    switch (op) {
    case 0xa9: a = IMMV(); setNZ(a); break;
    case 0xa5: ZP();  a = RD(); setNZ(a); cyc = 3; break;
    case 0xb5: ZPX(); a = RD(); setNZ(a); cyc = 4; break;
    case 0xad: ABS(); a = RD(); setNZ(a); cyc = 4; break;
    case 0xbd: ABX(); a = RD(); setNZ(a); cyc = 4 + cross; break;
    case 0xb9: ABY(); a = RD(); setNZ(a); cyc = 4 + cross; break;
    case 0xa1: IZX(); a = RD(); setNZ(a); cyc = 6; break;
    case 0xb1: IZY(); a = RD(); setNZ(a); cyc = 5 + cross; break;

    case 0xa2: x = IMMV(); setNZ(x); break;
    case 0xa6: ZP();  x = RD(); setNZ(x); cyc = 3; break;
    case 0xb6: ZPY(); x = RD(); setNZ(x); cyc = 4; break;
    case 0xae: ABS(); x = RD(); setNZ(x); cyc = 4; break;
    case 0xbe: ABY(); x = RD(); setNZ(x); cyc = 4 + cross; break;

    case 0xa0: y = IMMV(); setNZ(y); break;
    case 0xa4: ZP();  y = RD(); setNZ(y); cyc = 3; break;
    case 0xb4: ZPX(); y = RD(); setNZ(y); cyc = 4; break;
    case 0xac: ABS(); y = RD(); setNZ(y); cyc = 4; break;
    case 0xbc: ABX(); y = RD(); setNZ(y); cyc = 4 + cross; break;

    case 0x85: ZP();  WR(a); cyc = 3; break;
    case 0x95: ZPX(); WR(a); cyc = 4; break;
    case 0x8d: ABS(); WR(a); cyc = 4; break;
    case 0x9d: ABX(); WR(a); cyc = 5; break;
    case 0x99: ABY(); WR(a); cyc = 5; break;
    case 0x81: IZX(); WR(a); cyc = 6; break;
    case 0x91: IZY(); WR(a); cyc = 6; break;
    case 0x86: ZP();  WR(x); cyc = 3; break;
    case 0x96: ZPY(); WR(x); cyc = 4; break;
    case 0x8e: ABS(); WR(x); cyc = 4; break;
    case 0x84: ZP();  WR(y); cyc = 3; break;
    case 0x94: ZPX(); WR(y); cyc = 4; break;
    case 0x8c: ABS(); WR(y); cyc = 4; break;

    case 0xaa: x = a; setNZ(x); break;
    case 0xa8: y = a; setNZ(y); break;
    case 0x8a: a = x; setNZ(a); break;
    case 0x98: a = y; setNZ(a); break;
    case 0xba: x = c.s; setNZ(x); break;
    case 0x9a: c.s = x; break;

    case 0x48: push(a); cyc = 3; break;
    case 0x08: push(p | FB | FU); cyc = 3; break;
    case 0x68: a = pop(); setNZ(a); cyc = 4; break;
    case 0x28: p = pop() | FB | FU; cyc = 4; break;

#define ALU(base, OPER) \
    case base + 0x09: v = IMMV(); OPER; cyc = 2; break; \
    case base + 0x05: ZP();  v = RD(); OPER; cyc = 3; break; \
    case base + 0x15: ZPX(); v = RD(); OPER; cyc = 4; break; \
    case base + 0x0d: ABS(); v = RD(); OPER; cyc = 4; break; \
    case base + 0x1d: ABX(); v = RD(); OPER; cyc = 4 + cross; break; \
    case base + 0x19: ABY(); v = RD(); OPER; cyc = 4 + cross; break; \
    case base + 0x01: IZX(); v = RD(); OPER; cyc = 6; break; \
    case base + 0x11: IZY(); v = RD(); OPER; cyc = 5 + cross; break;
    ALU(0x00, (a |= v, setNZ(a)))
    ALU(0x20, (a &= v, setNZ(a)))
    ALU(0x40, (a ^= v, setNZ(a)))
    ALU(0x60, adc(v))
    ALU(0xc0, cmp(a, v))
    ALU(0xe0, sbc(v))
#undef ALU
    case 0xeb: sbc(IMMV()); break;

    case 0xe0: cmp(x, IMMV()); break;
    case 0xe4: ZP();  cmp(x, RD()); cyc = 3; break;
    case 0xec: ABS(); cmp(x, RD()); cyc = 4; break;
    case 0xc0: cmp(y, IMMV()); break;
    case 0xc4: ZP();  cmp(y, RD()); cyc = 3; break;
    case 0xcc: ABS(); cmp(y, RD()); cyc = 4; break;

    case 0x24: ZP();  v = RD(); p = (p & ~(FN | FV | FZ)) | (v & 0xc0) | ((a & v) ? 0 : FZ); cyc = 3; break;
    case 0x2c: ABS(); v = RD(); p = (p & ~(FN | FV | FZ)) | (v & 0xc0) | ((a & v) ? 0 : FZ); cyc = 4; break;

    case 0xe6: RMW(ZP(),  5, (v++, setNZ(v))) break;
    case 0xf6: RMW(ZPX(), 6, (v++, setNZ(v))) break;
    case 0xee: RMW(ABS(), 6, (v++, setNZ(v))) break;
    case 0xfe: RMW(ABX(), 7, (v++, setNZ(v))) break;
    case 0xc6: RMW(ZP(),  5, (v--, setNZ(v))) break;
    case 0xd6: RMW(ZPX(), 6, (v--, setNZ(v))) break;
    case 0xce: RMW(ABS(), 6, (v--, setNZ(v))) break;
    case 0xde: RMW(ABX(), 7, (v--, setNZ(v))) break;
    case 0xe8: x++; setNZ(x); break;
    case 0xca: x--; setNZ(x); break;
    case 0xc8: y++; setNZ(y); break;
    case 0x88: y--; setNZ(y); break;

#define ASL_(r) (p = (p & ~FC) | ((r) >> 7), r <<= 1, setNZ(r))
#define LSR_(r) (p = (p & ~FC) | ((r) & 1), r >>= 1, setNZ(r))
#define ROL_(r) (t = (r) >> 7, r = (uint8_t)((r) << 1 | (p & FC)), p = (p & ~FC) | t, setNZ(r))
#define ROR_(r) (t = (r) & 1, r = (uint8_t)((r) >> 1 | ((p & FC) << 7)), p = (p & ~FC) | t, setNZ(r))
#define SHIFT(base, OPR) \
    case base + 0x0a: OPR(a); cyc = 2; break; \
    case base + 0x06: RMW(ZP(),  5, OPR(v)) break; \
    case base + 0x16: RMW(ZPX(), 6, OPR(v)) break; \
    case base + 0x0e: RMW(ABS(), 6, OPR(v)) break; \
    case base + 0x1e: RMW(ABX(), 7, OPR(v)) break;
    SHIFT(0x00, ASL_)
    SHIFT(0x20, ROL_)
    SHIFT(0x40, LSR_)
    SHIFT(0x60, ROR_)
#undef SHIFT

    case 0x4c: c.pc = F16(); cyc = 3; break;
    case 0x6c: { uint16_t i_ = F16(); c.pc = rdD(i_) | (rdD((i_ & 0xff00) | ((i_ + 1) & 0xff)) << 8); cyc = 5; } break;
    case 0x20: { uint8_t lo = fetch(c.pc++); push(c.pc >> 8); push(c.pc & 0xff); c.pc = lo | (fetch(c.pc) << 8); cyc = 6; } break;
    case 0x60: { uint16_t l_ = pop(); c.pc = (l_ | (pop() << 8)) + 1; cyc = 6; } break;
    case 0x40: {
        p = pop() | FB | FU; uint16_t l_ = pop(); c.pc = l_ | (pop() << 8);
        c.codebank = popBank();
        cyc = 6; } break;
    case 0x00: c.pc++; cpuInterrupt(c.nmiPending, true); if (c.nmiPending) c.nmiPending = 0; cyc = 7; break;

    // CALLF bank:addr - the code bank goes on the private stack, the return address on the stack
    case 0x22: {
        pushBank(c.codebank);
        uint8_t bank = fetch(c.pc++);
        uint8_t lo = fetch(c.pc++);
        push(c.pc >> 8); push(c.pc & 0xff);
        c.pc = lo | (fetch(c.pc) << 8);
        c.codebank = bank;
        cyc = 8; } break;
    // RETF
    case 0x80: {
        uint16_t l_ = pop(); c.pc = l_ | (pop() << 8);
        c.codebank = popBank();
        c.pc++;
        cyc = 7; } break;

    case 0x10: BR(!(p & FN)) break;
    case 0x30: BR(p & FN) break;
    case 0x50: BR(!(p & FV)) break;
    case 0x70: BR(p & FV) break;
    case 0x90: BR(!(p & FC)) break;
    case 0xb0: BR(p & FC) break;
    case 0xd0: BR(!(p & FZ)) break;
    case 0xf0: BR(p & FZ) break;

    case 0x18: p &= ~FC; break;
    case 0x38: p |= FC; break;
    case 0x58: p &= ~FI; break;
    case 0x78: p |= FI; break;
    case 0xb8: p &= ~FV; break;
    case 0xd8: p &= ~FD; break;
    case 0xf8: p |= FD; break;
    case 0xea: break;

    // undocumented opcodes, as on NMOS parts
    case 0xa7: ZP();  a = x = RD(); setNZ(a); cyc = 3; break;
    case 0xb7: ZPY(); a = x = RD(); setNZ(a); cyc = 4; break;
    case 0xaf: ABS(); a = x = RD(); setNZ(a); cyc = 4; break;
    case 0xbf: ABY(); a = x = RD(); setNZ(a); cyc = 4 + cross; break;
    case 0xa3: IZX(); a = x = RD(); setNZ(a); cyc = 6; break;
    case 0xb3: IZY(); a = x = RD(); setNZ(a); cyc = 5 + cross; break;
    case 0x87: ZP();  WR(a & x); cyc = 3; break;
    case 0x97: ZPY(); WR(a & x); cyc = 4; break;
    case 0x8f: ABS(); WR(a & x); cyc = 4; break;
    case 0x83: IZX(); WR(a & x); cyc = 6; break;
#define UNDOC_RMW(base, BODY) \
    case base + 0x07: RMW(ZP(),  5, BODY) break; \
    case base + 0x17: RMW(ZPX(), 6, BODY) break; \
    case base + 0x0f: RMW(ABS(), 6, BODY) break; \
    case base + 0x1f: RMW(ABX(), 7, BODY) break; \
    case base + 0x1b: RMW(ABY(), 7, BODY) break; \
    case base + 0x03: RMW(IZX(), 8, BODY) break; \
    case base + 0x13: RMW(IZY(), 8, BODY) break;
    UNDOC_RMW(0x00, (ASL_(v), a |= v, setNZ(a)))
    UNDOC_RMW(0x20, (ROL_(v), a &= v, setNZ(a)))
    UNDOC_RMW(0x40, (LSR_(v), a ^= v, setNZ(a)))
    UNDOC_RMW(0x60, (ROR_(v), adc(v)))
    UNDOC_RMW(0xc0, (v--, cmp(a, v)))
    UNDOC_RMW(0xe0, (v++, sbc(v)))
#undef UNDOC_RMW
    case 0x0b: case 0x2b: a &= IMMV(); setNZ(a); p = (p & ~FC) | (a >> 7); break;
    case 0x4b: a &= IMMV(); LSR_(a); break;
    case 0x6b: a &= IMMV(); ROR_(a); break;
    case 0xcb: { int r_ = (a & x) - IMMV(); p = (p & ~FC) | (r_ >= 0 ? FC : 0); x = (uint8_t)r_; setNZ(x); } break;
    case 0x1a: case 0x3a: case 0x5a: case 0x7a: case 0xda: case 0xfa: break;
    case 0x82: case 0x89: case 0xc2: case 0xe2: c.pc++; break;
    case 0x04: case 0x44: case 0x64: ZP(); RD(); cyc = 3; break;
    case 0x14: case 0x34: case 0x54: case 0x74: case 0xd4: case 0xf4: ZPX(); RD(); cyc = 4; break;
    case 0x0c: ABS(); RD(); cyc = 4; break;
    case 0x1c: case 0x3c: case 0x5c: case 0x7c: case 0xdc: case 0xfc: ABX(); RD(); cyc = 4 + cross; break;
    default: break;   // KIL / unstable: treated as a 2-cycle NOP
    }
#undef RD
#undef WR
#undef F16
#undef IMMV
#undef ZP
#undef ZPX
#undef ZPY
#undef ABS
#undef ABX
#undef ABY
#undef IZX
#undef IZY
#undef BR
#undef RMW
    return cyc;
}

// SSD 2000 / 2002 (MAME oxavix2000.lst): J K L M scratch registers, PA / PB 24-bit
// pointers, PHX / PLX / PHY / PLY, accumulator ops, STZ, ASR, far jumps
bool XavixCore::step2000(uint8_t op, int& cyc)
{
    Cpu& c = m_cpu;
    uint8_t& a = c.a; uint8_t& p = c.p;
    auto asr = [&](uint8_t v) -> uint8_t {
        p &= ~(FN | FZ | FC);
        if (v & 1) p |= FC;
        v >>= 1;
        if (!v) p |= FZ;
        else if (v & 0x40) { p |= FN; v |= 0x80; }
        return v;
    };
    auto rdP = [&](uint32_t ptr) { return readFull(uint8_t(ptr >> 16), uint16_t(ptr)); };
    auto f16 = [&]() { uint16_t v = fetch(c.pc) | (fetch(uint16_t(c.pc + 1)) << 8); c.pc += 2; return v; };
    uint8_t* reg = nullptr;
    switch (op & 0x0f) {
    case 0x3: if (!(op & 0x10)) reg = &c.j; break;
    case 0x7: if (!(op & 0x10)) reg = &c.k; break;
    case 0xb: if (!(op & 0x10)) reg = &c.l; break;
    case 0xf: if (!(op & 0x10)) reg = &c.m; break;
    }
    if (reg) {
        // column 3 / 7 / B / F, even rows: ORA AND EOR ADC STA LDA CMP SBC with J / K / L / M
        uint8_t v = *reg;
        switch (op >> 5) {
        case 0: a |= v; setNZ(a); break;
        case 1: a &= v; setNZ(a); break;
        case 2: a ^= v; setNZ(a); break;
        case 3: adc(v); break;
        case 4: *reg = a; break;
        case 5: a = v; setNZ(a); break;
        case 6: cmp(a, v); break;
        case 7: sbc(v); break;
        }
        cyc = 2;
        return true;
    }
    if ((op & 0x1f) == 0x13 || (op & 0x1f) == 0x17) {
        // odd rows, columns 3 / 7: the same ops through PA / PB
        uint32_t ptr = (op & 4) ? c.pb : c.pa;
        switch (op >> 5) {
        case 0: a |= rdP(ptr); setNZ(a); break;
        case 1: a &= rdP(ptr); setNZ(a); break;
        case 2: a ^= rdP(ptr); setNZ(a); break;
        case 3: adc(rdP(ptr)); break;
        case 4: writeFull(uint8_t(ptr >> 16), uint16_t(ptr), a); break;
        case 5: a = rdP(ptr); setNZ(a); break;
        case 6: cmp(a, rdP(ptr)); break;
        case 7: sbc(rdP(ptr)); break;
        }
        cyc = 3;
        return true;
    }
    if ((op & 0x1f) == 0x1b || (op & 0x1f) == 0x1f) {
        uint32_t& ptr = (op & 4) ? c.pb : c.pa;
        int sh = 0;
        switch (op >> 5) {
        case 0: case 2: case 4: sh = (op >> 6) * 8; ptr = (ptr & ~(0xffu << sh)) | (uint32_t(a) << sh); break;      // SPA0-2 / SPB0-2
        case 1: case 3: case 5: sh = (op >> 6) * 8; a = uint8_t(ptr >> sh); setNZ(a); break;                         // LPA0-2 / LPB0-2
        case 6: ptr = (ptr & ~0xffu) | ((ptr - 1) & 0xff); setNZ(uint8_t(ptr)); break;                                // DECPA / DECPB
        case 7: ptr = (ptr & ~0xffu) | ((ptr + 1) & 0xff); setNZ(uint8_t(ptr)); break;                                // INCPA / INCPB
        }
        cyc = 2;
        return true;
    }
    uint16_t ad;
    uint8_t v;
    switch (op) {
    case 0x02: p ^= FC; break;                                       // CMC
    case 0x12: push(c.x); cyc = 3; break;                            // PHX
    case 0x32: c.x = pop(); setNZ(c.x); cyc = 4; break;              // PLX
    case 0x52: push(c.y); cyc = 3; break;                            // PHY
    case 0x72: c.y = pop(); setNZ(c.y); cyc = 4; break;              // PLY
    case 0xb2: a = 0; setNZ(a); break;                               // CLR A
    case 0xc2: a--; setNZ(a); break;                                 // DEC A
    case 0xd2: a ^= 0xff; setNZ(a); break;                           // NOT A
    case 0xe2: a++; setNZ(a); break;                                 // INC A
    case 0xf2: a = uint8_t(0 - a); setNZ(a); break;                  // NEG A
    case 0x89: p |= FV; break;                                       // SEV
    case 0x82: wrZ(fetch(c.pc++), 0); cyc = 3; break;                // STZ zp
    case 0x92: wrD(f16(), 0); cyc = 4; break;                        // STZ abs
    case 0x9c: ad = f16(); wrD(uint16_t(ad + c.x), c.y); cyc = 5; break;   // STY abs,X
    case 0x9e: ad = f16(); wrD(uint16_t(ad + c.y), c.x); cyc = 5; break;   // STX abs,Y
    case 0x3a: v = fetch(c.pc++);                                    // BIT #imm
        p = (p & ~(FN | FV | FZ)) | (v & 0xc0) | ((a & v) ? 0 : FZ); break;
    case 0x34: { uint8_t z = uint8_t(fetch(c.pc++) + c.x); v = rdZ(z);   // BIT zp,X
        p = (p & ~(FN | FV | FZ)) | (v & 0xc0) | ((a & v) ? 0 : FZ); cyc = 4; } break;
    case 0x3c: ad = uint16_t(f16() + c.x); v = rdD(ad);              // BIT abs,X
        p = (p & ~(FN | FV | FZ)) | (v & 0xc0) | ((a & v) ? 0 : FZ); cyc = 4; break;
    case 0x04: { uint8_t z = fetch(c.pc++); v = rdZ(z); asr(v); wrZ(z, v); cyc = 5; } break;   // ASR zp (MAME keeps the value)
    case 0x14: { uint8_t z = uint8_t(fetch(c.pc++) + c.x); wrZ(z, asr(rdZ(z))); cyc = 6; } break;
    case 0x0c: ad = f16(); wrD(ad, asr(rdD(ad))); cyc = 6; break;
    case 0x1c: ad = uint16_t(f16() + c.x); wrD(ad, asr(rdD(ad))); cyc = 7; break;
    case 0x1a: a = asr(a); break;
    case 0x42: case 0x44: case 0x54: case 0x5a: case 0x62: case 0x64: case 0x74: case 0x7a:
    case 0xd4: case 0xda: case 0xdc: case 0xea: case 0xf4: case 0xfa: case 0xfc:
        break;                                                       // NOP (implied)
    case 0x5c: {                                                     // JMP bank:addr
        uint8_t bank = fetch(c.pc++);
        c.pc = f16();
        c.codebank = bank;
        cyc = 4; } break;
    case 0x7c: {                                                     // JMPF (ind): addr, bank
        ad = f16();
        uint8_t lo = rdD(ad), hi = rdD(uint16_t((ad & 0xff00) | ((ad + 1) & 0xff)));
        uint8_t bank = rdD(uint16_t(ad + 2));
        c.pc = lo | (hi << 8);
        c.codebank = bank;
        cyc = 6; } break;
    default:
        return false;
    }
    return true;
}
