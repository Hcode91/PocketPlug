// PocketPlug - shared NMOS 6502 CPU core
// SPDX-License-Identifier: BSD-3-Clause

#include "m6502.h"

enum { FC = 0x01, FZ = 0x02, FI = 0x04, FD = 0x08, FB = 0x10, FU = 0x20, FV = 0x40, FN = 0x80 };

void M6502::reset()
{
    s = (uint8_t)(s - 3);
    p |= FI | FU;
    pc = rd16(0xfffc);
    m_nmiPending = false;
}

void M6502::interrupt(uint16_t vector, bool brk)
{
    push(pc >> 8);
    push(pc & 0xff);
    push((p | FU | (brk ? FB : 0)) & (brk ? 0xff : ~FB));
    p |= FI;
    pc = rd16(vector);
}

void M6502::adc(uint8_t v)
{
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

void M6502::sbc(uint8_t v)
{
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

void M6502::cmp(uint8_t r, uint8_t v)
{
    int d = r - v;
    p = (p & ~FC) | (d >= 0 ? FC : 0);
    setNZ((uint8_t)d);
}

int M6502::step()
{
    if (m_nmiPending) {
        m_nmiPending = false;
        interrupt(0xfffa, false);
        return 7;
    }
    if (m_irqLine && !(p & FI)) {
        interrupt(0xfffe, false);
        return 7;
    }

    uint8_t op = rd(pc++);
    if (m_optable) op = m_optable[op];

    int cyc = 2;
    uint16_t ad = 0;
    bool cross = false;
    uint8_t v, t;

    // ---- addressing helpers ----
#define IMM()  (ad = pc++)
#define ZP()   (ad = rd(pc++))
#define ZPX()  (ad = (uint8_t)(rd(pc++) + x))
#define ZPY()  (ad = (uint8_t)(rd(pc++) + y))
#define ABS()  (ad = rd16(pc), pc += 2)
#define ABX()  { uint16_t b_ = rd16(pc); pc += 2; ad = b_ + x; cross = (b_ ^ ad) & 0xff00; }
#define ABY()  { uint16_t b_ = rd16(pc); pc += 2; ad = b_ + y; cross = (b_ ^ ad) & 0xff00; }
#define IZX()  { uint8_t z_ = rd(pc++) + x; ad = rd(z_) | (rd((uint8_t)(z_ + 1)) << 8); }
#define IZY()  { uint8_t z_ = rd(pc++); uint16_t b_ = rd(z_) | (rd((uint8_t)(z_ + 1)) << 8); ad = b_ + y; cross = (b_ ^ ad) & 0xff00; }
#define BR(cond) { int8_t o_ = (int8_t)rd(pc++); cyc = 2; if (cond) { uint16_t n_ = pc + o_; cyc += ((n_ ^ pc) & 0xff00) ? 2 : 1; pc = n_; } }

#define RMW(addrmode, c, body) { addrmode; v = rd(ad); wr(ad, v); body; wr(ad, v); cyc = c; }

    switch (op) {
    // ---- loads ----
    case 0xa9: IMM(); a = rd(ad); setNZ(a); cyc = 2; break;
    case 0xa5: ZP();  a = rd(ad); setNZ(a); cyc = 3; break;
    case 0xb5: ZPX(); a = rd(ad); setNZ(a); cyc = 4; break;
    case 0xad: ABS(); a = rd(ad); setNZ(a); cyc = 4; break;
    case 0xbd: ABX(); a = rd(ad); setNZ(a); cyc = 4 + cross; break;
    case 0xb9: ABY(); a = rd(ad); setNZ(a); cyc = 4 + cross; break;
    case 0xa1: IZX(); a = rd(ad); setNZ(a); cyc = 6; break;
    case 0xb1: IZY(); a = rd(ad); setNZ(a); cyc = 5 + cross; break;

    case 0xa2: IMM(); x = rd(ad); setNZ(x); cyc = 2; break;
    case 0xa6: ZP();  x = rd(ad); setNZ(x); cyc = 3; break;
    case 0xb6: ZPY(); x = rd(ad); setNZ(x); cyc = 4; break;
    case 0xae: ABS(); x = rd(ad); setNZ(x); cyc = 4; break;
    case 0xbe: ABY(); x = rd(ad); setNZ(x); cyc = 4 + cross; break;

    case 0xa0: IMM(); y = rd(ad); setNZ(y); cyc = 2; break;
    case 0xa4: ZP();  y = rd(ad); setNZ(y); cyc = 3; break;
    case 0xb4: ZPX(); y = rd(ad); setNZ(y); cyc = 4; break;
    case 0xac: ABS(); y = rd(ad); setNZ(y); cyc = 4; break;
    case 0xbc: ABX(); y = rd(ad); setNZ(y); cyc = 4 + cross; break;

    // ---- stores ----
    case 0x85: ZP();  wr(ad, a); cyc = 3; break;
    case 0x95: ZPX(); wr(ad, a); cyc = 4; break;
    case 0x8d: ABS(); wr(ad, a); cyc = 4; break;
    case 0x9d: ABX(); wr(ad, a); cyc = 5; break;
    case 0x99: ABY(); wr(ad, a); cyc = 5; break;
    case 0x81: IZX(); wr(ad, a); cyc = 6; break;
    case 0x91: IZY(); wr(ad, a); cyc = 6; break;
    case 0x86: ZP();  wr(ad, x); cyc = 3; break;
    case 0x96: ZPY(); wr(ad, x); cyc = 4; break;
    case 0x8e: ABS(); wr(ad, x); cyc = 4; break;
    case 0x84: ZP();  wr(ad, y); cyc = 3; break;
    case 0x94: ZPX(); wr(ad, y); cyc = 4; break;
    case 0x8c: ABS(); wr(ad, y); cyc = 4; break;

    // ---- transfers ----
    case 0xaa: x = a; setNZ(x); break;
    case 0xa8: y = a; setNZ(y); break;
    case 0x8a: a = x; setNZ(a); break;
    case 0x98: a = y; setNZ(a); break;
    case 0xba: x = s; setNZ(x); break;
    case 0x9a: s = x; break;

    // ---- stack ----
    case 0x48: push(a); cyc = 3; break;
    case 0x08: push(p | FB | FU); cyc = 3; break;
    case 0x68: a = pop(); setNZ(a); cyc = 4; break;
    case 0x28: p = (pop() & ~FB) | FU; cyc = 4; break;

    // ---- logic / arithmetic ----
#define ALU(base, OPER) \
    case base + 0x09: IMM(); v = rd(ad); OPER; cyc = 2; break; \
    case base + 0x05: ZP();  v = rd(ad); OPER; cyc = 3; break; \
    case base + 0x15: ZPX(); v = rd(ad); OPER; cyc = 4; break; \
    case base + 0x0d: ABS(); v = rd(ad); OPER; cyc = 4; break; \
    case base + 0x1d: ABX(); v = rd(ad); OPER; cyc = 4 + cross; break; \
    case base + 0x19: ABY(); v = rd(ad); OPER; cyc = 4 + cross; break; \
    case base + 0x01: IZX(); v = rd(ad); OPER; cyc = 6; break; \
    case base + 0x11: IZY(); v = rd(ad); OPER; cyc = 5 + cross; break;

    ALU(0x00, (a |= v, setNZ(a)))
    ALU(0x20, (a &= v, setNZ(a)))
    ALU(0x40, (a ^= v, setNZ(a)))
    ALU(0x60, adc(v))
    ALU(0xc0, cmp(a, v))
    ALU(0xe0, sbc(v))
#undef ALU
    case 0xeb: IMM(); sbc(rd(ad)); cyc = 2; break; // undocumented SBC

    case 0xe0: IMM(); cmp(x, rd(ad)); cyc = 2; break;
    case 0xe4: ZP();  cmp(x, rd(ad)); cyc = 3; break;
    case 0xec: ABS(); cmp(x, rd(ad)); cyc = 4; break;
    case 0xc0: IMM(); cmp(y, rd(ad)); cyc = 2; break;
    case 0xc4: ZP();  cmp(y, rd(ad)); cyc = 3; break;
    case 0xcc: ABS(); cmp(y, rd(ad)); cyc = 4; break;

    case 0x24: ZP();  v = rd(ad); p = (p & ~(FN | FV | FZ)) | (v & 0xc0) | ((a & v) ? 0 : FZ); cyc = 3; break;
    case 0x2c: ABS(); v = rd(ad); p = (p & ~(FN | FV | FZ)) | (v & 0xc0) | ((a & v) ? 0 : FZ); cyc = 4; break;

    // ---- inc / dec ----
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

    // ---- shifts ----
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

    // ---- jumps / calls ----
    case 0x4c: pc = rd16(pc); cyc = 3; break;
    case 0x6c: { uint16_t i_ = rd16(pc); pc = rd(i_) | (rd((i_ & 0xff00) | ((i_ + 1) & 0xff)) << 8); cyc = 5; } break;
    case 0x20: { uint16_t d_ = rd16(pc); pc++; push(pc >> 8); push(pc & 0xff); pc = d_; cyc = 6; } break;
    case 0x60: { uint16_t l_ = pop(); pc = (l_ | (pop() << 8)) + 1; cyc = 6; } break;
    case 0x40: { p = (pop() & ~FB) | FU; uint16_t l_ = pop(); pc = l_ | (pop() << 8); cyc = 6; } break;
    case 0x00: pc++; interrupt(0xfffe, true); cyc = 7; break;

    // ---- branches ----
    case 0x10: BR(!(p & FN)) break;
    case 0x30: BR(p & FN) break;
    case 0x50: BR(!(p & FV)) break;
    case 0x70: BR(p & FV) break;
    case 0x90: BR(!(p & FC)) break;
    case 0xb0: BR(p & FC) break;
    case 0xd0: BR(!(p & FZ)) break;
    case 0xf0: BR(p & FZ) break;

    // ---- flags ----
    case 0x18: p &= ~FC; break;
    case 0x38: p |= FC; break;
    case 0x58: p &= ~FI; break;
    case 0x78: p |= FI; break;
    case 0xb8: p &= ~FV; break;
    case 0xd8: p &= ~FD; break;
    case 0xf8: p |= FD; break;
    case 0xea: break;

    // ---- undocumented opcodes (as on NMOS parts) ----
    case 0xa7: ZP();  a = x = rd(ad); setNZ(a); cyc = 3; break;          // LAX
    case 0xb7: ZPY(); a = x = rd(ad); setNZ(a); cyc = 4; break;
    case 0xaf: ABS(); a = x = rd(ad); setNZ(a); cyc = 4; break;
    case 0xbf: ABY(); a = x = rd(ad); setNZ(a); cyc = 4 + cross; break;
    case 0xa3: IZX(); a = x = rd(ad); setNZ(a); cyc = 6; break;
    case 0xb3: IZY(); a = x = rd(ad); setNZ(a); cyc = 5 + cross; break;
    case 0x87: ZP();  wr(ad, a & x); cyc = 3; break;                     // SAX
    case 0x97: ZPY(); wr(ad, a & x); cyc = 4; break;
    case 0x8f: ABS(); wr(ad, a & x); cyc = 4; break;
    case 0x83: IZX(); wr(ad, a & x); cyc = 6; break;
#define UNDOC_RMW(base, BODY) \
    case base + 0x07: RMW(ZP(),  5, BODY) break; \
    case base + 0x17: RMW(ZPX(), 6, BODY) break; \
    case base + 0x0f: RMW(ABS(), 6, BODY) break; \
    case base + 0x1f: RMW(ABX(), 7, BODY) break; \
    case base + 0x1b: RMW(ABY(), 7, BODY) break; \
    case base + 0x03: RMW(IZX(), 8, BODY) break; \
    case base + 0x13: RMW(IZY(), 8, BODY) break;
    UNDOC_RMW(0x00, (ASL_(v), a |= v, setNZ(a)))         // SLO
    UNDOC_RMW(0x20, (ROL_(v), a &= v, setNZ(a)))         // RLA
    UNDOC_RMW(0x40, (LSR_(v), a ^= v, setNZ(a)))         // SRE
    UNDOC_RMW(0x60, (ROR_(v), adc(v)))                   // RRA
    UNDOC_RMW(0xc0, (v--, cmp(a, v)))                    // DCP
    UNDOC_RMW(0xe0, (v++, sbc(v)))                       // ISC
#undef UNDOC_RMW
    case 0x0b: case 0x2b: IMM(); a &= rd(ad); setNZ(a); p = (p & ~FC) | (a >> 7); break; // ANC
    case 0x4b: IMM(); a &= rd(ad); LSR_(a); break;                        // ALR
    case 0x6b: IMM(); a &= rd(ad); ROR_(a); break;                        // ARR (approx.)
    case 0xcb: { IMM(); int r_ = (a & x) - rd(ad); p = (p & ~FC) | (r_ >= 0 ? FC : 0); x = (uint8_t)r_; setNZ(x); } break; // AXS
    // NOPs of various sizes
    case 0x1a: case 0x3a: case 0x5a: case 0x7a: case 0xda: case 0xfa: break;
    case 0x80: case 0x82: case 0x89: case 0xc2: case 0xe2: pc++; break;
    case 0x04: case 0x44: case 0x64: pc++; cyc = 3; break;
    case 0x14: case 0x34: case 0x54: case 0x74: case 0xd4: case 0xf4: pc++; cyc = 4; break;
    case 0x0c: pc += 2; cyc = 4; break;
    case 0x1c: case 0x3c: case 0x5c: case 0x7c: case 0xdc: case 0xfc: ABX(); rd(ad); cyc = 4 + cross; break;
    default: break; // KIL / unstable opcodes: treat as 2-cycle NOP
    }
#undef IMM
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
