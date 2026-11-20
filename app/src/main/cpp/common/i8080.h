// PocketPlug - Intel 8080 CPU
// SPDX-License-Identifier: BSD-3-Clause
//
// The documented instruction set plus the undocumented aliases (08/10/...
// = NOP, CB = JMP, D9 = RET, DD/ED/FD = CALL), 8080 flag rules (AC on
// ANA is the OR of bit 3 of the operands, SUB sets AC like an add of the
// complement, bit 1 of F always set) and the 8080 cycle counts. Memory and
// I/O go through callbacks; each memory access is reported with its kind so
// a front panel can show the bus.

#pragma once
#include <cstdint>

class I8080 {
public:
    enum Access : uint8_t { FETCH, READ, WRITE, STACK_READ, STACK_WRITE };
    using Read = uint8_t (*)(void* ctx, uint16_t a, Access k);
    using Write = void (*)(void* ctx, uint16_t a, uint8_t d, Access k);
    using In = uint8_t (*)(void* ctx, uint8_t port);
    using Out = void (*)(void* ctx, uint8_t port, uint8_t d);

    struct Regs {
        uint8_t a, f, b, c, d, e, h, l;
        uint16_t sp, pc;
        uint8_t inte, halted, eiPending, irq, irqVector;
    } r{};

    void* ctx = nullptr;
    Read rd = nullptr;
    Write wr = nullptr;
    In in = nullptr;
    Out out = nullptr;

    void reset() { r.pc = 0; r.inte = 0; r.halted = 0; r.eiPending = 0; r.f = 0x02; }
    // raise an interrupt; the CPU executes the given opcode (normally RST n) when INTE is set
    void interrupt(uint8_t opcode) { r.irq = 1; r.irqVector = opcode; }
    int step();      // one instruction, returns cycles

    uint16_t bc() const { return uint16_t(r.b << 8 | r.c); }
    uint16_t de() const { return uint16_t(r.d << 8 | r.e); }
    uint16_t hl() const { return uint16_t(r.h << 8 | r.l); }

private:
    uint8_t m(uint16_t a) { return rd(ctx, a, READ); }
    void wm(uint16_t a, uint8_t d) { wr(ctx, a, d, WRITE); }
    uint8_t fetch() { return rd(ctx, r.pc++, FETCH); }
    uint16_t fetch16() { uint8_t lo = fetch(); return uint16_t(fetch() << 8 | lo); }
    void push(uint16_t v) { wr(ctx, --r.sp, uint8_t(v >> 8), STACK_WRITE); wr(ctx, --r.sp, uint8_t(v), STACK_WRITE); }
    uint16_t pop() { uint8_t lo = rd(ctx, r.sp++, STACK_READ); return uint16_t(rd(ctx, r.sp++, STACK_READ) << 8 | lo); }
    uint8_t reg(int i);
    void setReg(int i, uint8_t v);
    void szp(uint8_t v);
    void add(uint8_t v, int c);
    void sub(uint8_t v, int c, bool store);
    void ana(uint8_t v);
    void xra(uint8_t v);
    void ora(uint8_t v);
    bool cond(int c) const;
    int exec(uint8_t op);
};
