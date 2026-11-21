// PocketPlug - Rockwell B6000 4-bit MCU
// SPDX-License-Identifier: BSD-3-Clause
//
// Port of MAME's rw5000base / b5000 / b5000op / b6000 (hap). See b6000.h.

#include "b6000.h"

void B6000::reset()
{
    // rw5000_base_device::device_reset (reset_pc: page 7)
    setPc(7, 0);
    s.prevPc = s.pc;
    s.sreg = s.pc;
    s.op = s.prevOp = 0;
    s.blDelay = s.buDelay = 0;
    s.sr = 0; s.skip = 0;
    s.atbStep = s.traStep = s.retStep = 0;
    // b6000_cpu_device::device_reset
    s.bl = 0; s.bu = 0; s.ramAddr = 0; s.a = 0; s.c = 0;
    segW(0);
    m_io->writeSTR(0);
    m_io->writeSPK(0);
}

void B6000::incPc()
{
    int feed = ((s.pc & 0x3e) == 0) ? 1 : 0;
    feed ^= (s.pc >> 1 ^ s.pc) & 1;
    s.pc = uint16_t((s.pc & ~0x3f) | (s.pc >> 1 & 0x1f) | (feed << 5));
}

int B6000::run(int n)
{
    for (int i = 0; i < n; i++) {
        s.prevOp = s.op;
        s.prevPc = s.pc;
        s.prevBl = s.bl;
        s.prevBu = s.bu;
        s.prev3C = s.prev2C;
        s.prev2C = s.prevC;
        s.prevC = s.c;

        s.op = m_rom[s.pc & 0x1ff];
        bool skip = s.skip && (s.op & 0xf8) != 0x30;     // TL can't be skipped
        s.skip = 0;
        incPc();
        s.cycles++;
        if (skip) s.op = 0;
        else execute();

        if (s.atbStep) {
            // the B6000 has no ATB sequence (0x77 is a plain ATB); kept for completeness
            s.atbStep = 0;
        }
        if (s.traStep) {
            if (s.traStep == 1) { s.skip = 1; s.traStep++; }
            else {
                if (!s.sr && (~s.prevOp & 0x40)) {
                    s.sr = 1;
                    s.sreg = uint16_t((s.sreg & ~0x3f) | (s.prevPc & 0x3f));
                }
                if (s.sr) setPc(uint8_t(0 ^ ((s.prevOp >> 6) & 1)), uint8_t(s.pc));
                setPc(uint8_t(s.pc >> 6), s.prevOp);
                s.traStep = 0;
            }
        }
        if (s.retStep) {
            if (s.retStep == 1) { s.skip = 1; s.retStep++; }
            else { s.pc = s.sreg; s.sr = 0; s.retStep = 0; }
        }

        s.ramAddr = uint8_t((s.bu << 4 & 0x30) | (s.bl & 0xf));
        if (s.blDelay) { s.ramAddr = uint8_t((s.ramAddr & ~0xf) | (s.prevBl & 0xf)); s.blDelay = 0; }
        if (s.buDelay) { s.ramAddr = uint8_t((s.ramAddr & 0xf) | (s.prevBu << 4 & 0x30)); s.buDelay = 0; }
    }
    return n;
}

void B6000::exc0()
{
    uint8_t a = s.a;
    s.a = ramR();
    ramW(a);
    setBu(uint8_t(s.op ^ s.bu));
}

void B6000::lb(uint8_t bl)
{
    if (!isLb(s.prevOp) && !isAtb(s.prevOp)) { s.bl = bl; setBu(s.op & 3); }
}

void B6000::atb()
{
    if (!isLb(s.prevOp) && !isAtb(s.prevOp)) { s.bl = s.a; s.blDelay = 1; }
}

void B6000::execute()
{
    const uint8_t op = s.op;
    // b6000 specials
    if (op == 0x76) { segW(0); m_io->writeSTR(uint16_t(1 << s.a)); return; }   // ATBZ
    if (op == 0x77) { atb(); return; }                                          // ATB
    switch (op & 0xf0) {
    case 0x40: s.a = ~op & 0xf; return;                                         // LAX
    case 0x60:
        if (op != 0x6f) { s.a += ~op & 0xf; s.skip = !(s.a >> 4 & 1); s.a &= 0xf; }   // ADX
        else { s.a += (~m_io->readKB() & 0xf); s.skip = !(s.a >> 4 & 1); s.a &= 0xf; } // READ
        return;
    case 0x80: case 0x90: case 0xa0: case 0xb0:
    case 0xc0: case 0xd0: case 0xe0: case 0xf0:
        s.traStep = 1; return;                                                  // TRA / TRS
    default: break;
    }
    switch (op & 0xfc) {
    case 0x04: s.skip = (m_io->readDIN() >> ((op - 1) & 3)) & 1; return;       // TDIN
    case 0x08: s.skip = !((ramR() >> (op & 3)) & 1); return;                    // TM
    case 0x10: ramW(ramR() | (1 << (op & 3))); return;                          // SM
    case 0x14: ramW(ramR() & ~(1 << (op & 3))); return;                         // RSM
    case 0x18: s.retStep = 1; return;                                           // RET
    case 0x20: lb(7); return;
    case 0x24: lb(10); return;
    case 0x28: lb(9); return;
    case 0x2c: lb(8); return;
    case 0x3c: lb(0); return;
    case 0x30: case 0x34:                                                       // TL
        setPc(op & 0xf, uint8_t(s.pc));
        s.sreg = uint16_t((s.pc & ~0x3f) | (s.sreg & 0x3f));
        return;
    case 0x50: s.a = ramR(); setBu(uint8_t(op ^ s.bu)); return;                 // LDA
    case 0x54: exc0(); s.bl = (s.bl + 1) & 0xf; s.skip = (s.bl & 7) == 0; s.blDelay = 1; return;   // EXCP
    case 0x58: exc0(); return;                                                  // EXC0
    case 0x5c: exc0(); s.bl = (s.bl - 1) & 0xf; s.skip = s.bl == 0xf; s.blDelay = 1; return;       // EXCM
    case 0x70:                                                                  // ADD
        s.a += ramR();
        if (~op & 2) { s.a += s.c; s.c = s.a >> 4 & 1; }
        if (op & 1) s.skip = !(s.a >> 4 & 1);
        s.a &= 0xf;
        return;
    case 0x78: s.a ^= 0xf; return;                                              // COMP
    case 0x7c: s.skip = s.a == ramR(); return;                                  // TAM
    default: break;
    }
    switch (op) {
    case 0x00: return;                                                          // NOP
    case 0x01: s.skip = s.c != 0; return;                                       // TC
    case 0x02: s.skip = (m_io->readKB() & 0xf) != 0; return;                    // TKB
    case 0x03: {                                                                // TKBS (B6000)
        static const uint8_t lut[0x10] = { 0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f, 0, 0, 0x00, 0x20, 0x40, 0 };
        segW(uint16_t(s.seg | lut[ramR() & 0xf]));
        m_io->writeSPK(s.c);
        return;
    }
    case 0x39: s.c = 0; return;                                                 // RSC
    case 0x3b: s.c = 1; return;                                                 // SC
    case 0x74: segW(0); return;                                                 // KSEG
    default: return;
    }
}
