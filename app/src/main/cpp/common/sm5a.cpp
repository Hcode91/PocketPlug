// PocketPlug - Sharp SM5A 4-bit LCD game MCU
// SPDX-License-Identifier: BSD-3-Clause
//
// Port of MAME's sm510base / sm510op / sm500 / sm500op / sm5a (hap). See sm5a.h.

#include "sm5a.h"
#include <cstring>

void SM5A::setRom(const uint8_t* rom, int size)
{
    std::memset(m_rom, 0, sizeof(m_rom));
    std::memcpy(m_rom, rom, size > 0x800 ? 0x800 : size);
}

void SM5A::reset()
{
    uint8_t ram[0x80];
    std::memcpy(ram, s.ram, sizeof(ram));
    uint16_t div = s.div;
    std::memset(&s, 0, sizeof(s));
    std::memcpy(s.ram, ram, sizeof(ram));     // RAM and the divider keep running through a reset
    s.div = div;
    // sm510_base_device::device_reset
    branch(0, 0xf, 0);
    s.prevPc = s.pc;
    s.bp = 1;
    s.r = s.rOut = 0;
    m_io->writeR(0);
    // sm500_device::device_reset
    push();
    s.div &= 0x3f;
    s.gamma = 1;
    s.r = 0xf;
}

void SM5A::incPc()
{
    const int msb = 0x20;
    int feed = ((s.pc >> 1 ^ s.pc) & 1) ? 0 : msb;
    s.pc = uint16_t(feed | (s.pc >> 1 & 0x1f) | (s.pc & ~0x3f));
}

uint8_t SM5A::digit() const
{
    static const uint8_t lut[0x20] = {
        0xe, 0x0, 0xc, 0x8, 0x2, 0xa, 0xe, 0x2, 0xe, 0xa, 0x0, 0x0, 0x2, 0xa, 0x2, 0x2,
        0xb, 0x9, 0x7, 0xf, 0xd, 0xe, 0xe, 0xb, 0xf, 0xf, 0x4, 0x0, 0xd, 0xe, 0x4, 0x0
    };
    uint8_t sel = s.bp >> 3 & 1;
    return uint8_t(lut[sel << 4 | s.acc] | (sel ? 0 : s.mx));
}

void SM5A::clockMelody()
{
    uint8_t out = uint8_t(~s.r & 0xf);    // R mask option "direct"
    if (out != s.rOut) { s.rOut = out; m_io->writeR(out); }
}

void SM5A::tick(int n)
{
    while (n-- > 0) {
        s.div = uint16_t((s.div + 1) & 0x7fff);
        if (s.div == 0) s.gamma |= 1;
        clockMelody();
        if (++s.lcdPhase == 32) {
            s.lcdPhase = 0;
            uint8_t segs[9][2];
            for (int o = 0; o < 9; o++) {
                segs[o][0] = (s.bp & 1) ? s.o[o] : 0;
                segs[o][1] = (s.bp & 1) ? s.ox[o] : 0;
            }
            m_io->lcd(segs);
            m_io->tick1k();
        }
    }
}

int SM5A::run(int n)
{
    int done = 0;
    while (done < n) {
        int cyc;
        if (s.halt) {
            if (s.extWakeup || s.gamma) {
                // do_interrupt: wakeup vector
                s.halt = 0;
                s.cb = 0;
                branch(0, 0, 0);
                cyc = 1;
            } else {
                cyc = 1;     // idle; the divider keeps counting
                done += cyc; tick(cyc * 2);
                continue;
            }
            done += cyc; tick(cyc * 2);
            continue;
        }
        cyc = 1;
        s.prevOp = s.op;
        s.prevPc = s.pc;
        s.op = rom(s.pc);
        incPc();
        if (s.op == 0x5e || s.op == 0x5f) {
            cyc++;
            s.param = rom(s.pc);
            incPc();
        }
        if (s.skip) { s.skip = 0; s.op = 0; }
        else execute();
        done += cyc;
        tick(cyc * 2);
    }
    return done;
}

void SM5A::exc()
{
    uint8_t a = s.acc;
    s.acc = ramR();
    ramW(a);
    s.bm ^= (s.op & 3);
}

void SM5A::incb()
{
    s.bl = (s.bl + 1) & 0xf;
    s.skip = s.bl == 8;
}

void SM5A::decb()
{
    s.bl = (s.bl - 1) & 0xf;
    s.skip = s.bl == 0xf;
}

void SM5A::execute()
{
    const uint8_t op = uint8_t(s.op);
    switch (op & 0xf0) {
    case 0x20:   // LAX
        if ((s.op & ~0xf) != (s.prevOp & ~0xf)) s.acc = op & 0xf;
        return;
    case 0x30: { // ADX
        s.acc += (op & 0xf);
        s.skip = ((op & 0xf) != 10 && (s.acc & 0x10) != 0);
        s.acc &= 0xf;
        return;
    }
    case 0x40:   // LB
        s.bm = op & 3;
        s.bl = uint8_t((op >> 2 & 3) | ((op & 0xc) ? 8 : 0));
        return;
    case 0x70:   // SSR
        setSu(op & 0xf);
        return;
    case 0x80: case 0x90: case 0xa0: case 0xb0:   // TR
        s.pc = uint16_t((s.pc & ~0x3f) | (op & 0x3f));
        if (!s.rsub) branch(s.cb, su(), s.pc & 0x3f);
        return;
    case 0xc0: case 0xd0: case 0xe0: case 0xf0:   // TRS
        if (!s.rsub) {
            s.rsub = 1;
            uint8_t u = su();
            push();
            branch(1, 0, op & 0x3f);
            if ((s.prevOp & 0xf0) == 0x70) branch(s.cb, u, s.pc & 0x3f);
        } else s.pc = uint16_t((s.pc & ~0xff) | (op << 2 & 0xc0) | (op & 0xf));
        return;
    default: break;
    }
    switch (op & 0xfc) {
    case 0x04: ramW(ramR() & ~(1 << (op & 3))); return;          // RM
    case 0x0c: ramW(ramR() | (1 << (op & 3))); return;           // SM
    case 0x10: exc(); return;                                    // EXC
    case 0x14: exc(); incb(); return;                            // EXCI
    case 0x18: s.acc = ramR(); s.bm ^= (op & 3); return;         // LDA
    case 0x1c: exc(); decb(); return;                            // EXCD
    case 0x54: s.skip = (ramR() & (1 << (op & 3))) != 0; return; // TM
    default: break;
    }
    switch (op) {
    case 0x00: return;                                           // SKIP
    case 0x01: s.r = s.acc; clockMelody(); return;               // ATR
    case 0x02: s.bm |= 4; return;                                // SBM
    case 0x03: s.bp = s.acc; return;                             // ATBP
    case 0x08: s.acc = (s.acc + ramR()) & 0xf; return;           // ADD
    case 0x09:                                                   // ADDC
        s.acc = uint8_t(s.acc + ramR() + s.c);
        s.c = s.acc >> 4 & 1;
        s.skip = s.c == 1;
        s.acc &= 0xf;
        return;
    case 0x0a: s.acc ^= 0xf; return;                             // COMA
    case 0x0b: { uint8_t a = s.acc; s.acc = s.bl; s.bl = a; return; }   // EXBLA
    case 0x50: s.skip = m_io->readBA() != 0; return;             // TA
    case 0x51: s.skip = m_io->readB() != 0; return;              // TB
    case 0x52: s.skip = !s.c; return;                            // TC
    case 0x53: s.skip = s.acc == ramR(); return;                 // TAM
    case 0x58: s.skip = !s.gamma; s.gamma = 0; return;           // TG
    case 0x59: s.o[8] = s.ox[8]; s.o[7] = s.ox[7]; return;       // PTW
    case 0x5a: s.skip = !s.acc; return;                          // TA0
    case 0x5b: s.skip = s.acc == s.bl; return;                   // TABL
    case 0x5c: for (int i = 0; i < 9; i++) s.o[i] = s.ox[i]; return;   // TW
    case 0x5d: shiftW(); s.ox[8] = digit(); return;              // DTW
    case 0x5f: s.bl = s.param & 0xf; s.bm = uint8_t((s.param & 0x7f) >> 4); return;   // LBL
    case 0x60: s.bp ^= 8; return;                                // COMCN
    case 0x61: s.ox[7] = s.ox[8]; s.ox[8] = digit(); return;     // PDTW
    case 0x62: shiftW(); s.ox[8] = s.acc & 7; return;            // WR
    case 0x63: shiftW(); s.ox[8] = s.acc | 8; return;            // WS
    case 0x64: incb(); return;                                   // INCB
    case 0x65: s.div &= 0x3f; return;                            // IDIV
    case 0x66: s.c = 0; return;                                  // RC
    case 0x67: s.c = 1; return;                                  // SC
    case 0x68: s.mx = 0; s.acc = 0; return;                      // RMF
    case 0x69: s.mx = 1; return;                                 // SMF
    case 0x6a: s.acc = m_io->readK() & 0xf; return;              // KTA
    case 0x6b: s.bm &= ~4; return;                               // RBM
    case 0x6c: decb(); return;                                   // DECB
    case 0x6d: s.cb ^= 1; return;                                // COMCB
    case 0x6e: pop(); s.rsub = 0; return;                        // RTN
    case 0x6f: pop(); s.rsub = 0; s.skip = 1; return;            // RTNS
    case 0x5e:
        s.op = uint16_t(op << 8 | s.param);
        if (s.param == 0x00) s.halt = 1;                         // CEND
        else if (s.param == 0x04) s.acc = s.div >> 11 & 0xf;     // DTA
        return;
    default: return;
    }
}
