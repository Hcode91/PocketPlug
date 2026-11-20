// PocketPlug - Intel 8080 CPU
// SPDX-License-Identifier: BSD-3-Clause
#include "i8080.h"

namespace {
enum : uint8_t { FS = 0x80, FZ = 0x40, FA = 0x10, FP = 0x04, FC = 0x01 };

const uint8_t kCycles[256] = {
    4, 10, 7, 5, 5, 5, 7, 4, 4, 10, 7, 5, 5, 5, 7, 4,
    4, 10, 7, 5, 5, 5, 7, 4, 4, 10, 7, 5, 5, 5, 7, 4,
    4, 10, 16, 5, 5, 5, 7, 4, 4, 10, 16, 5, 5, 5, 7, 4,
    4, 10, 13, 5, 10, 10, 10, 4, 4, 10, 13, 5, 5, 5, 7, 4,
    5, 5, 5, 5, 5, 5, 7, 5, 5, 5, 5, 5, 5, 5, 7, 5,
    5, 5, 5, 5, 5, 5, 7, 5, 5, 5, 5, 5, 5, 5, 7, 5,
    5, 5, 5, 5, 5, 5, 7, 5, 5, 5, 5, 5, 5, 5, 7, 5,
    7, 7, 7, 7, 7, 7, 7, 7, 5, 5, 5, 5, 5, 5, 7, 5,
    4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
    4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
    4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
    4, 4, 4, 4, 4, 4, 7, 4, 4, 4, 4, 4, 4, 4, 7, 4,
    5, 10, 10, 10, 11, 11, 7, 11, 5, 10, 10, 10, 11, 17, 7, 11,
    5, 10, 10, 10, 11, 11, 7, 11, 5, 10, 10, 10, 11, 17, 7, 11,
    5, 10, 10, 18, 11, 11, 7, 11, 5, 5, 10, 4, 11, 17, 7, 11,
    5, 10, 10, 4, 11, 11, 7, 11, 5, 5, 10, 4, 11, 17, 7, 11,
};

bool parity(uint8_t v)
{
    v ^= v >> 4; v ^= v >> 2; v ^= v >> 1;
    return !(v & 1);
}
}

uint8_t I8080::reg(int i)
{
    switch (i) {
    case 0: return r.b; case 1: return r.c; case 2: return r.d; case 3: return r.e;
    case 4: return r.h; case 5: return r.l; case 6: return m(hl()); default: return r.a;
    }
}

void I8080::setReg(int i, uint8_t v)
{
    switch (i) {
    case 0: r.b = v; break; case 1: r.c = v; break; case 2: r.d = v; break; case 3: r.e = v; break;
    case 4: r.h = v; break; case 5: r.l = v; break; case 6: wm(hl(), v); break; default: r.a = v; break;
    }
}

void I8080::szp(uint8_t v)
{
    r.f = uint8_t((r.f & (FA | FC)) | 0x02 | (v & FS) | (v ? 0 : FZ) | (parity(v) ? FP : 0));
}

void I8080::add(uint8_t v, int c)
{
    unsigned res = unsigned(r.a) + v + c;
    uint8_t ac = ((r.a & 0x0f) + (v & 0x0f) + c) > 0x0f ? FA : 0;
    r.a = uint8_t(res);
    r.f = uint8_t((res > 0xff ? FC : 0) | ac);
    szp(r.a);
}

void I8080::sub(uint8_t v, int borrow, bool store)
{
    // add of the complement: AC and CY as the 8080 sets them
    uint8_t nv = uint8_t(~v);
    int cin = borrow ? 0 : 1;
    unsigned res = unsigned(r.a) + nv + cin;
    uint8_t ac = ((r.a & 0x0f) + (nv & 0x0f) + cin) > 0x0f ? FA : 0;
    r.f = uint8_t((res > 0xff ? 0 : FC) | ac);
    uint8_t out = uint8_t(res);
    szp(out);
    if (store) r.a = out;
}

void I8080::ana(uint8_t v)
{
    uint8_t ac = ((r.a | v) & 0x08) ? FA : 0;
    r.a &= v;
    r.f = ac;
    szp(r.a);
}

void I8080::xra(uint8_t v) { r.a ^= v; r.f = 0; szp(r.a); }
void I8080::ora(uint8_t v) { r.a |= v; r.f = 0; szp(r.a); }

bool I8080::cond(int c) const
{
    switch (c) {
    case 0: return !(r.f & FZ); case 1: return r.f & FZ;
    case 2: return !(r.f & FC); case 3: return r.f & FC;
    case 4: return !(r.f & FP); case 5: return r.f & FP;
    case 6: return !(r.f & FS); default: return r.f & FS;
    }
}

int I8080::step()
{
    if (r.irq && r.inte && !r.eiPending) {
        r.irq = 0;
        r.inte = 0;
        r.halted = 0;
        return exec(r.irqVector) + 1;
    }
    r.eiPending = 0;
    if (r.halted) return 4;
    return exec(fetch());
}

int I8080::exec(uint8_t op)
{
    int cyc = kCycles[op];
    if (op >= 0x40 && op < 0x80) {
        if (op == 0x76) { r.halted = 1; return 7; }
        setReg((op >> 3) & 7, reg(op & 7));
        return cyc;
    }
    if (op >= 0x80 && op < 0xc0) {
        uint8_t v = reg(op & 7);
        switch ((op >> 3) & 7) {
        case 0: add(v, 0); break;
        case 1: add(v, r.f & FC); break;
        case 2: sub(v, 0, true); break;
        case 3: sub(v, r.f & FC, true); break;
        case 4: ana(v); break;
        case 5: xra(v); break;
        case 6: ora(v); break;
        case 7: sub(v, 0, false); break;
        }
        return cyc;
    }
    switch (op) {
    case 0x00: case 0x08: case 0x10: case 0x18: case 0x20: case 0x28: case 0x30: case 0x38: break;
    // LXI
    case 0x01: { uint16_t v = fetch16(); r.b = uint8_t(v >> 8); r.c = uint8_t(v); break; }
    case 0x11: { uint16_t v = fetch16(); r.d = uint8_t(v >> 8); r.e = uint8_t(v); break; }
    case 0x21: { uint16_t v = fetch16(); r.h = uint8_t(v >> 8); r.l = uint8_t(v); break; }
    case 0x31: r.sp = fetch16(); break;
    // STAX / LDAX
    case 0x02: wm(bc(), r.a); break;
    case 0x12: wm(de(), r.a); break;
    case 0x0a: r.a = m(bc()); break;
    case 0x1a: r.a = m(de()); break;
    // SHLD / LHLD / STA / LDA
    case 0x22: { uint16_t a = fetch16(); wm(a, r.l); wm(uint16_t(a + 1), r.h); break; }
    case 0x2a: { uint16_t a = fetch16(); r.l = m(a); r.h = m(uint16_t(a + 1)); break; }
    case 0x32: wm(fetch16(), r.a); break;
    case 0x3a: r.a = m(fetch16()); break;
    // INX / DCX
    case 0x03: { uint16_t v = uint16_t(bc() + 1); r.b = uint8_t(v >> 8); r.c = uint8_t(v); break; }
    case 0x13: { uint16_t v = uint16_t(de() + 1); r.d = uint8_t(v >> 8); r.e = uint8_t(v); break; }
    case 0x23: { uint16_t v = uint16_t(hl() + 1); r.h = uint8_t(v >> 8); r.l = uint8_t(v); break; }
    case 0x33: r.sp++; break;
    case 0x0b: { uint16_t v = uint16_t(bc() - 1); r.b = uint8_t(v >> 8); r.c = uint8_t(v); break; }
    case 0x1b: { uint16_t v = uint16_t(de() - 1); r.d = uint8_t(v >> 8); r.e = uint8_t(v); break; }
    case 0x2b: { uint16_t v = uint16_t(hl() - 1); r.h = uint8_t(v >> 8); r.l = uint8_t(v); break; }
    case 0x3b: r.sp--; break;
    // INR / DCR / MVI
    case 0x04: case 0x0c: case 0x14: case 0x1c: case 0x24: case 0x2c: case 0x34: case 0x3c: {
        int i = (op >> 3) & 7;
        uint8_t v = uint8_t(reg(i) + 1);
        setReg(i, v);
        r.f = uint8_t((r.f & FC) | ((v & 0x0f) == 0 ? FA : 0));
        szp(v);
        break;
    }
    case 0x05: case 0x0d: case 0x15: case 0x1d: case 0x25: case 0x2d: case 0x35: case 0x3d: {
        int i = (op >> 3) & 7;
        uint8_t v = uint8_t(reg(i) - 1);
        setReg(i, v);
        r.f = uint8_t((r.f & FC) | ((v & 0x0f) != 0x0f ? FA : 0));
        szp(v);
        break;
    }
    case 0x06: case 0x0e: case 0x16: case 0x1e: case 0x26: case 0x2e: case 0x36: case 0x3e:
        setReg((op >> 3) & 7, fetch());
        break;
    // DAD
    case 0x09: case 0x19: case 0x29: case 0x39: {
        uint16_t v = op == 0x09 ? bc() : op == 0x19 ? de() : op == 0x29 ? hl() : r.sp;
        uint32_t res = uint32_t(hl()) + v;
        r.h = uint8_t(res >> 8); r.l = uint8_t(res);
        r.f = uint8_t((r.f & ~FC) | (res > 0xffff ? FC : 0));
        break;
    }
    // rotates
    case 0x07: { int c = r.a >> 7; r.a = uint8_t(r.a << 1 | c); r.f = uint8_t((r.f & ~FC) | c); break; }
    case 0x0f: { int c = r.a & 1; r.a = uint8_t(r.a >> 1 | c << 7); r.f = uint8_t((r.f & ~FC) | c); break; }
    case 0x17: { int c = r.a >> 7; r.a = uint8_t(r.a << 1 | (r.f & FC)); r.f = uint8_t((r.f & ~FC) | c); break; }
    case 0x1f: { int c = r.a & 1; r.a = uint8_t(r.a >> 1 | (r.f & FC) << 7); r.f = uint8_t((r.f & ~FC) | c); break; }
    // DAA, CMA, STC, CMC
    case 0x27: {
        int c = r.f & FC;
        uint8_t add = 0;
        if ((r.f & FA) || (r.a & 0x0f) > 9) add = 0x06;
        if (c || (r.a >> 4) > 9 || ((r.a >> 4) >= 9 && (r.a & 0x0f) > 9)) { add |= 0x60; c = 1; }
        uint8_t ac = ((r.a & 0x0f) + (add & 0x0f)) > 0x0f ? FA : 0;
        r.a = uint8_t(r.a + add);
        r.f = uint8_t(ac | c);
        szp(r.a);
        break;
    }
    case 0x2f: r.a = uint8_t(~r.a); break;
    case 0x37: r.f |= FC; break;
    case 0x3f: r.f ^= FC; break;
    // immediates
    case 0xc6: add(fetch(), 0); break;
    case 0xce: add(fetch(), r.f & FC); break;
    case 0xd6: sub(fetch(), 0, true); break;
    case 0xde: sub(fetch(), r.f & FC, true); break;
    case 0xe6: ana(fetch()); break;
    case 0xee: xra(fetch()); break;
    case 0xf6: ora(fetch()); break;
    case 0xfe: sub(fetch(), 0, false); break;
    // jumps, calls, returns
    case 0xc3: case 0xcb: r.pc = fetch16(); break;
    case 0xc2: case 0xca: case 0xd2: case 0xda: case 0xe2: case 0xea: case 0xf2: case 0xfa: {
        uint16_t a = fetch16();
        if (cond((op >> 3) & 7)) r.pc = a;
        break;
    }
    case 0xcd: case 0xdd: case 0xed: case 0xfd: { uint16_t a = fetch16(); push(r.pc); r.pc = a; break; }
    case 0xc4: case 0xcc: case 0xd4: case 0xdc: case 0xe4: case 0xec: case 0xf4: case 0xfc: {
        uint16_t a = fetch16();
        if (cond((op >> 3) & 7)) { push(r.pc); r.pc = a; cyc = 17; }
        break;
    }
    case 0xc9: case 0xd9: r.pc = pop(); break;
    case 0xc0: case 0xc8: case 0xd0: case 0xd8: case 0xe0: case 0xe8: case 0xf0: case 0xf8:
        if (cond((op >> 3) & 7)) { r.pc = pop(); cyc = 11; }
        break;
    case 0xc7: case 0xcf: case 0xd7: case 0xdf: case 0xe7: case 0xef: case 0xf7: case 0xff:
        push(r.pc); r.pc = uint16_t(op & 0x38);
        break;
    case 0xe9: r.pc = hl(); break;
    // stack
    case 0xc5: push(bc()); break;
    case 0xd5: push(de()); break;
    case 0xe5: push(hl()); break;
    case 0xf5: push(uint16_t(r.a << 8 | ((r.f & 0xd5) | 0x02))); break;
    case 0xc1: { uint16_t v = pop(); r.b = uint8_t(v >> 8); r.c = uint8_t(v); break; }
    case 0xd1: { uint16_t v = pop(); r.d = uint8_t(v >> 8); r.e = uint8_t(v); break; }
    case 0xe1: { uint16_t v = pop(); r.h = uint8_t(v >> 8); r.l = uint8_t(v); break; }
    case 0xf1: { uint16_t v = pop(); r.a = uint8_t(v >> 8); r.f = uint8_t((v & 0xd5) | 0x02); break; }
    case 0xe3: {
        uint8_t lo = rd(ctx, r.sp, STACK_READ), hi = rd(ctx, uint16_t(r.sp + 1), STACK_READ);
        wr(ctx, uint16_t(r.sp + 1), r.h, STACK_WRITE); wr(ctx, r.sp, r.l, STACK_WRITE);
        r.h = hi; r.l = lo;
        break;
    }
    case 0xf9: r.sp = hl(); break;
    case 0xeb: { uint8_t t = r.d; r.d = r.h; r.h = t; t = r.e; r.e = r.l; r.l = t; break; }
    // I/O and interrupts
    case 0xd3: { uint8_t p = fetch(); out(ctx, p, r.a); break; }
    case 0xdb: { uint8_t p = fetch(); r.a = in(ctx, p); break; }
    case 0xf3: r.inte = 0; break;
    case 0xfb: r.inte = 1; r.eiPending = 1; break;
    default: break;
    }
    return cyc;
}
