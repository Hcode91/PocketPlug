// PocketPlug - Texas Instruments TMS9980A CPU
// SPDX-License-Identifier: BSD-3-Clause
//
// The TMS9900 instruction set (workspace registers in memory, 16-bit words,
// CRU bit I/O) on an 8-bit data bus with a 14-bit address space. Interrupt
// handling as in MAME's tms9980a.cpp (Michael Zapf, BSD-3): the board drives
// a 3-bit level code; LOAD is taken at every instruction boundary while it is
// held; levels 1-4 are taken when not above the status mask, never right
// after BLWP or XOP. Cycle counts are those of MAME's microprograms for the
// 9980A (4 clocks per word access, 2 per ALU step), in 2 MHz machine clocks.

#pragma once
#include <cstdint>
#ifdef TMS9980_TRACE
#include <cstdio>
#endif

class Tms9980Bus {
public:
    virtual ~Tms9980Bus() {}
    virtual uint8_t read8(uint16_t addr) = 0;
    virtual void write8(uint16_t addr, uint8_t v) = 0;
    virtual int cruRead(uint16_t bit) = 0;                 // bit number 0-0x7ff
    virtual void cruWrite(uint16_t bit, int v) = 0;
    virtual void extOp(int code) { (void)code; }           // IDLE=2 RSET=3 CKON=5 CKOF=6 LREX=7
};

class Tms9980 {
public:
    explicit Tms9980(Tms9980Bus* b) : bus_(b) { reset(); }

    enum { ST_LH = 0x8000, ST_AGT = 0x4000, ST_EQ = 0x2000, ST_C = 0x1000, ST_OV = 0x0800, ST_OP = 0x0400, ST_X = 0x0200 };
    static const int LOAD = -1;

    uint16_t pc = 0, wp = 0, st = 0;
    bool loadLine = false;      // LOAD held
    int irqLevel = 0;           // 1-4 pending (0 = none)
    bool idle = false;
    uint16_t lastOp = 0;

    void reset()
    {
        st = 0;
        idle = false;
        lastOp = 0;
        contextSwitch(0x0000, -2);
    }

    // one instruction (or an interrupt); returns machine cycles
    int step()
    {
        if (loadLine) { contextSwitch(0x3ffc, LOAD); idle = false; return 34; }
        if (irqLevel > 0 && irqLevel <= (st & 0x0f) && !isXopOrBlwp(lastOp)) {
            int l = irqLevel;
            contextSwitch(uint16_t(l << 2), l);
            idle = false;
            return 34;
        }
        if (idle) { bus_->extOp(2); return 1; }
        cycles_ = 0;
#ifdef TMS9980_TRACE
        { extern int g_trace; if (g_trace > 0) { g_trace--; uint16_t a = pc & 0x3ffe; fprintf(stderr, "%04x: %02x%02x wp=%04x st=%04x\n", pc, bus_->read8(a), bus_->read8(a | 1), wp, st); } }
#endif
        uint16_t op = fetch();
        lastOp = op;
        exec(op);
        return cycles_;
    }

private:
    Tms9980Bus* bus_;
    int cycles_ = 0;

    static bool isXopOrBlwp(uint16_t op) { return (op & 0xfc00) == 0x2c00 || (op & 0xffc0) == 0x0400; }

    uint16_t rw(uint16_t a) { a &= 0x3ffe; return uint16_t(bus_->read8(a) << 8 | bus_->read8(a | 1)); }
    void ww(uint16_t a, uint16_t v) { a &= 0x3ffe; bus_->write8(a, uint8_t(v >> 8)); bus_->write8(a | 1, uint8_t(v)); }
    uint16_t fetch() { uint16_t v = rw(pc); pc = (pc + 2) & 0x3fff; return v; }
    uint16_t reg(int n) { return rw(uint16_t(wp + 2 * n)); }
    void setReg(int n, uint16_t v) { ww(uint16_t(wp + 2 * n), v); }

    void flag(uint16_t f, bool on) { if (on) st |= f; else st &= ~f; }
    void lae(uint16_t a, uint16_t b)
    {
        flag(ST_EQ, a == b);
        flag(ST_LH, a > b);
        flag(ST_AGT, int16_t(a) > int16_t(b));
    }
    void parity(uint8_t v) { int n = 0; for (int i = 0; i < 8; i++) n += (v >> i) & 1; flag(ST_OP, n & 1); }

    void contextSwitch(uint16_t vec, int level)
    {
        uint16_t nwp = rw(vec) & 0x3fff;
        uint16_t npc = rw(uint16_t(vec + 2)) & 0x3fff;
        uint16_t owp = wp;
        wp = nwp;
        if (level != -2) {
            ww(uint16_t(wp + 30), st);
            ww(uint16_t(wp + 28), pc);
            ww(uint16_t(wp + 26), owp);
        }
        pc = npc;
        if (level > 0) st = uint16_t((st & 0xfff0) | (level - 1));
        if (level == -2) st = 0;
    }

    // general addressing: returns the operand address (Ts / Td)
    uint16_t addr(int t, int r, bool byte)
    {
        switch (t) {
        case 0: cycles_ += 4; return uint16_t(wp + 2 * r);
        case 1: cycles_ += 10; return reg(r);
        case 3: {                          // *Rn+
            cycles_ += byte ? 14 : 16;
            uint16_t a = reg(r);
            setReg(r, uint16_t(a + (byte ? 1 : 2)));
            return a;
        }
        default: {                         // @addr, @addr(Rn)
            cycles_ += r ? 16 : 14;
            uint16_t base = fetch();
            if (r) base = uint16_t(base + reg(r));
            return base;
        }
        }
    }
    // byte operands live in the high byte of a word (even address) or the low (odd)
    uint8_t rb(uint16_t a) { uint16_t w = rw(a); return (a & 1) ? uint8_t(w) : uint8_t(w >> 8); }
    void wb(uint16_t a, uint8_t v)
    {
        uint16_t w = rw(a);   // the 9980 reads the word before writing a byte into it
        w = (a & 1) ? uint16_t((w & 0xff00) | v) : uint16_t((w & 0x00ff) | (v << 8));
        ww(a, w);
    }

    void cruOut(uint16_t base, int count, uint16_t v)
    {
        for (int i = 0; i < count; i++) {
            bus_->cruWrite(uint16_t(((base >> 1) + i) & 0x7ff), (v >> i) & 1);
        }
    }
    uint16_t cruIn(uint16_t base, int count)
    {
        uint16_t v = 0;
        for (int i = 0; i < count; i++) {
            if (bus_->cruRead(uint16_t(((base >> 1) + i) & 0x7ff))) v |= uint16_t(1 << i);
        }
        return v;
    }

    void exec(uint16_t op);
};

inline void Tms9980::exec(uint16_t op)
{
    if (op >= 0x4000) {
        // format I: two general operands
        int fn = op >> 12;
        bool byte = fn & 1;
        int ts = (op >> 4) & 3, s = op & 15, td = (op >> 10) & 3, d = (op >> 6) & 15;
        cycles_ += (fn == 8 || fn == 9) ? 12 : 14;
        uint16_t sa = addr(ts, s, byte);
        uint16_t sv = byte ? uint16_t(rb(sa) << 8) : rw(sa);
        uint16_t da = addr(td, d, byte);
        if (fn == 8 || fn == 9) {          // C, CB
            uint16_t dv = byte ? uint16_t(rb(da) << 8) : rw(da);
            if (byte) parity(uint8_t(sv >> 8));
            lae(sv, dv);
            return;
        }
        uint16_t dv = 0;
        if (fn != 12 && fn != 13) dv = byte ? uint16_t(rb(da) << 8) : rw(da);
        uint32_t r = 0;
        switch (fn) {
        case 4: case 5: r = dv & ~sv; break;                                    // SZC
        case 6: case 7: r = uint32_t(dv) + (~sv & 0xffff) + 1;                  // S
            flag(ST_C, r & 0x10000);
            flag(ST_OV, (dv ^ sv) & (dv ^ r) & 0x8000);
            break;
        case 10: case 11: r = uint32_t(dv) + sv;                               // A
            flag(ST_C, r & 0x10000);
            flag(ST_OV, (r ^ dv) & (r ^ sv) & 0x8000);
            break;
        case 12: case 13: r = sv; break;                                        // MOV
        case 14: case 15: r = dv | sv; break;                                   // SOC
        }
        if (byte) {
            r &= 0xff00;
            parity(uint8_t(r >> 8));
            lae(uint16_t(r & 0xff00), 0);
            wb(da, uint8_t(r >> 8));
        } else {
            lae(uint16_t(r), 0);
            ww(da, uint16_t(r));
        }
        return;
    }
    if (op >= 0x2000) {
        int fn = (op >> 10) & 7;
        int ts = (op >> 4) & 3, s = op & 15, d = (op >> 6) & 15;
        switch (fn) {
        case 0: case 1: case 2: {          // COC, CZC, XOR
            cycles_ += fn == 2 ? 18 : 16;
            uint16_t sv = rw(addr(ts, s, false));
            uint16_t dv = reg(d);
            if (fn == 0) flag(ST_EQ, (sv & dv) == sv);
            else if (fn == 1) flag(ST_EQ, (sv & ~dv) == sv);
            else { uint16_t r = sv ^ dv; lae(r, 0); setReg(d, r); }
            return;
        }
        case 3: {                          // XOP
            cycles_ += 48;
            uint16_t sa = addr(ts, s, false);
            uint16_t vec = uint16_t(0x40 + 4 * d);
            uint16_t nwp = rw(vec) & 0x3fff, npc = rw(uint16_t(vec + 2)) & 0x3fff;
            uint16_t owp = wp;
            wp = nwp;
            ww(uint16_t(wp + 22), sa);
            ww(uint16_t(wp + 30), st);
            ww(uint16_t(wp + 28), pc);
            ww(uint16_t(wp + 26), owp);
            pc = npc;
            flag(ST_X, true);
            return;
        }
        case 4: case 5: {                  // LDCR, STCR
            int count = d ? d : 16;
            bool byte = count <= 8;
            uint16_t sa = addr(ts, s, byte);
            uint16_t base = reg(12);
            if (fn == 4) {
                cycles_ += 24 + 2 * count;
                uint16_t v = byte ? rb(sa) : rw(sa);
                if (byte) { parity(uint8_t(v)); lae(uint16_t(v << 8), 0); } else lae(v, 0);
                cruOut(base, count, v);
            } else {
                cycles_ += byte ? 54 : 70;
                uint16_t v = cruIn(base, count);
                if (byte) { parity(uint8_t(v)); lae(uint16_t(v << 8), 0); wb(sa, uint8_t(v)); }
                else { lae(v, 0); ww(sa, v); }
            }
            return;
        }
        case 6: {                          // MPY
            cycles_ += 58;
            uint16_t sv = rw(addr(ts, s, false));
            uint32_t r = uint32_t(sv) * reg(d);
            setReg(d, uint16_t(r >> 16));
            setReg((d + 1) & 15, uint16_t(r));
            return;
        }
        case 7: {                          // DIV
            uint16_t sv = rw(addr(ts, s, false));
            uint16_t hi = reg(d);
            if (hi >= sv) { flag(ST_OV, true); cycles_ += 14; return; }
            cycles_ += 92;
            flag(ST_OV, false);
            uint32_t v = (uint32_t(hi) << 16) | reg((d + 1) & 15);
            setReg(d, uint16_t(v / sv));
            setReg((d + 1) & 15, uint16_t(v % sv));
            for (uint16_t rem = uint16_t(v % sv); rem; rem >>= 1) cycles_ += 2;
            return;
        }
        }
    }
    if (op >= 0x1000) {
        int sub = (op >> 8) & 15;
        int8_t disp = int8_t(op & 0xff);
        if (sub >= 0xd) {                  // SBO, SBZ, TB
            cycles_ += 16;
            uint16_t bitAddr = uint16_t(reg(12) + disp * 2);
            if (sub == 0xd) cruOut(bitAddr, 1, 1);
            else if (sub == 0xe) cruOut(bitAddr, 1, 0);
            else flag(ST_EQ, cruIn(bitAddr, 1) != 0);
            return;
        }
        bool c = false;
        switch (sub) {
        case 0x0: c = true; break;                                       // JMP
        case 0x1: c = !(st & (ST_AGT | ST_EQ)); break;                   // JLT
        case 0x2: c = !(st & ST_LH); break;                              // JLE
        case 0x3: c = st & ST_EQ; break;                                 // JEQ
        case 0x4: c = st & (ST_LH | ST_EQ); break;                       // JHE
        case 0x5: c = st & ST_AGT; break;                                // JGT
        case 0x6: c = !(st & ST_EQ); break;                              // JNE
        case 0x7: c = !(st & ST_C); break;                               // JNC
        case 0x8: c = st & ST_C; break;                                  // JOC
        case 0x9: c = !(st & ST_OV); break;                              // JNO
        case 0xa: c = !(st & (ST_LH | ST_EQ)); break;                    // JL
        case 0xb: c = st & ST_LH; break;                                 // JH
        case 0xc: c = st & ST_OP; break;                                 // JOP
        }
        cycles_ += c ? 12 : 10;
        if (c) pc = uint16_t((pc + disp * 2) & 0x3fff);
        return;
    }
    if (op >= 0x0800 && op < 0x0c00) {    // shifts
        int fn = (op >> 8) & 3, cnt = (op >> 4) & 15, w = op & 15;
        cycles_ += 18;
        if (cnt == 0) { cycles_ += 10; cnt = reg(0) & 15; if (cnt == 0) cnt = 16; }
        uint16_t v = reg(w);
        bool carry = false, ov = false;
        uint16_t sign = (fn == 0) ? (v & 0x8000) : 0;
        for (int i = 0; i < cnt; i++) {
            cycles_ += 2;
            switch (fn) {
            case 0: case 1: carry = v & 1; v = uint16_t((v >> 1) | sign); break;   // SRA, SRL
            case 2: carry = v & 0x8000; v = uint16_t(v << 1); if (carry != bool(v & 0x8000)) ov = true; break;  // SLA
            case 3: carry = v & 1; v = uint16_t((v >> 1) | (carry ? 0x8000 : 0)); break;  // SRC
            }
        }
        flag(ST_C, carry);
        if (fn == 2) flag(ST_OV, ov);
        lae(v, 0);
        setReg(w, v);
        return;
    }
    if (op >= 0x0400 && op < 0x0800) {   // single operand
        int fn = (op >> 6) & 15, ts = (op >> 4) & 3, s = op & 15;
        uint16_t a = addr(ts, s, false);
        switch (fn) {
        case 0x0: {                       // BLWP
            cycles_ += 34;
            uint16_t nwp = rw(a) & 0x3fff, npc = rw(uint16_t(a + 2)) & 0x3fff;
            uint16_t owp = wp;
            wp = nwp;
            ww(uint16_t(wp + 30), st);
            ww(uint16_t(wp + 28), pc);
            ww(uint16_t(wp + 26), owp);
            pc = npc;
            return;
        }
        case 0x1: cycles_ += 8; pc = a & 0x3fff; return;                         // B
        case 0x2: { cycles_ += 8; uint16_t x = rw(a); exec(x); cycles_ -= 4; return; }   // X (no fetch of its own)
        case 0x3: cycles_ += 12; ww(a, 0); return;                                 // CLR
        case 0x4: {                       // NEG
            cycles_ += 12;
            uint16_t v = rw(a);
            uint32_t r = uint32_t(~v & 0xffff) + 1;
            flag(ST_OV, v == 0x8000);
            flag(ST_C, r & 0x10000);
            lae(uint16_t(r), 0);
            ww(a, uint16_t(r));
            return;
        }
        case 0x5: { cycles_ += 12; uint16_t r = uint16_t(~rw(a)); lae(r, 0); ww(a, r); return; }   // INV
        case 0x6: case 0x7: case 0x8: case 0x9: {    // INC INCT DEC DECT
            cycles_ += 12;
            uint16_t v = rw(a);
            uint32_t add = fn == 6 ? 1 : fn == 7 ? 2 : fn == 8 ? 0xffff : 0xfffe;
            uint16_t sign = fn >= 8 ? 0x8000 : 0;
            uint32_t r = v + add;
            flag(ST_OV, (v & 0x8000) == sign && (r & 0x8000) != sign);
            flag(ST_C, r & 0x10000);
            lae(uint16_t(r), 0);
            ww(a, uint16_t(r));
            return;
        }
        case 0xa: cycles_ += 14; setReg(11, pc); pc = a & 0x3fff; return;        // BL
        case 0xb: { cycles_ += 12; uint16_t v = rw(a); ww(a, uint16_t((v << 8) | (v >> 8))); return; }  // SWPB
        case 0xc: cycles_ += 12; ww(a, 0xffff); return;                           // SETO
        case 0xd: {                       // ABS
            cycles_ += 10;
            uint16_t v = rw(a);
            flag(ST_OV, v == 0x8000);
            flag(ST_C, false);
            lae(v, 0);
            if (v & 0x8000) { cycles_ += 6; ww(a, uint16_t(~v + 1)); }
            return;
        }
        default: cycles_ += 6; return;
        }
    }
    if (op >= 0x0200 && op < 0x0400) {   // immediates and control
        int fn = (op >> 5) & 15, w = op & 15;
        switch (fn) {
        case 0x0: { cycles_ += 18; uint16_t v = fetch(); lae(v, 0); setReg(w, v); return; }       // LI
        case 0x1: {                       // AI
            cycles_ += 22;
            uint16_t im = fetch(), v = reg(w);
            uint32_t r = uint32_t(v) + im;
            flag(ST_C, r & 0x10000);
            flag(ST_OV, (r ^ v) & (r ^ im) & 0x8000);
            lae(uint16_t(r), 0);
            setReg(w, uint16_t(r));
            return;
        }
        case 0x2: { cycles_ += 22; uint16_t r = uint16_t(fetch() & reg(w)); lae(r, 0); setReg(w, r); return; }   // ANDI
        case 0x3: { cycles_ += 22; uint16_t r = uint16_t(fetch() | reg(w)); lae(r, 0); setReg(w, r); return; }   // ORI
        case 0x4: { cycles_ += 20; uint16_t im = fetch(); lae(reg(w), im); return; }                 // CI
        case 0x5: cycles_ += 12; setReg(w, wp); return;                                             // STWP
        case 0x6: cycles_ += 12; setReg(w, st); return;                                             // STST
        case 0x7: cycles_ += 14; wp = fetch() & 0x3fff; return;                                     // LWPI
        case 0x8: cycles_ += 18; st = uint16_t((st & 0xfff0) | (fetch() & 0x0f)); return;          // LIMI
        case 0xa: cycles_ += 14; idle = true; bus_->extOp(2); return;                              // IDLE
        case 0xb: cycles_ += 14; st &= 0xfff0; bus_->extOp(3); return;                             // RSET
        case 0xc: {                       // RTWP
            cycles_ += 22;
            uint16_t nst = reg(15), npc = reg(14), nwp = reg(13);
            st = nst; pc = npc & 0x3fff; wp = nwp & 0x3fff;
            return;
        }
        case 0xd: cycles_ += 14; bus_->extOp(5); return;                                           // CKON
        case 0xe: cycles_ += 14; bus_->extOp(6); return;                                           // CKOF
        case 0xf: cycles_ += 14; bus_->extOp(7); return;                                           // LREX
        default: cycles_ += 6; return;
        }
    }
    cycles_ += 6;   // illegal opcodes act as NOPs here
}
