// PocketPlug - Sharp SM511 4-bit LCD game MCU
// SPDX-License-Identifier: BSD-3-Clause
//
// Port of MAME's sm510base / sm510op / sm511 (hap). See sm511.h.

#include "sm511.h"
#include <cstring>

void SM511::setRom(const uint8_t* rom, int size)
{
    std::memset(m_rom, 0, sizeof(m_rom));
    std::memcpy(m_rom, rom, size > 0x1000 ? 0x1000 : size);
}

void SM511::setMelody(const uint8_t* rom, int size)
{
    std::memset(m_melody, 0, sizeof(m_melody));
    std::memcpy(m_melody, rom, size > 0x100 ? 0x100 : size);
    m_hasMelody = size > 0;
}

void SM511::reset()
{
    // RAM, the divider and the melody counters keep their contents through ACL
    State keep = s;
    std::memset(&s, 0, sizeof(s));
    std::memcpy(s.ram, keep.ram, sizeof(s.ram));
    s.div = keep.div;
    s.melodyAddr = keep.melodyAddr; s.melodyStep = keep.melodyStep;
    s.melodyDuty = keep.melodyDuty; s.melodyDutyIdx = keep.melodyDutyIdx;
    s.melodyRd = uint8_t(keep.melodyRd & ~1);
    s.stack[0] = keep.stack[0]; s.stack[1] = keep.stack[1];
    // sm510_base_device::device_reset
    branch(3, 7, 0);
    s.prevPc = s.pc;
    s.bp = 1;
    s.r = s.rOut = 0;
    m_io->writeR(0);
    // sm511_device::device_reset: 8 kHz instruction clock
    s.clkDiv = 4;
}

void SM511::clockMelody()
{
    if (!m_hasMelody) return;
    static const uint8_t lut[4 * 16] = {
        0, 0, 7, 8, 8, 9, 9, 10, 11, 11, 12, 13, 14, 14, 0, 0,
        0, 0, 8, 8, 9, 9, 10, 11, 11, 12, 13, 13, 14, 15, 0, 0,
        0, 0, 8, 8, 9, 9, 10, 10, 11, 12, 12, 13, 14, 15, 0, 0,
        0, 0, 8, 9, 9, 10, 10, 11, 11, 12, 13, 14, 14, 15, 0, 0,
    };
    uint8_t cmd = m_melody[s.melodyAddr] & 0x3f;
    uint8_t out = 0;
    if ((cmd & 0xf) >= 2 && (cmd & 0xf) <= 13) {
        out = s.melodyDutyIdx & s.melodyRd & 1;
        s.melodyDuty++;
        int index = s.melodyDutyIdx << 4 | (cmd & 0xf);
        int shift = ~cmd >> 4 & 1;   // OCT
        if (s.melodyDuty >= (lut[index] << shift)) { s.melodyDuty = 0; s.melodyDutyIdx = (s.melodyDutyIdx + 1) & 3; }
    } else if ((cmd & 0xf) == 1) s.melodyRd |= 2;
    if ((s.div & 0x7f) == 0) {
        uint8_t mask = (cmd & 0x20) ? 0x1f : 0x0f;
        s.melodyStep = (s.melodyStep + 1) & mask;
        if (s.melodyStep == 0) s.melodyAddr++;
    }
    if (out != s.rOut) { s.rOut = out; m_io->writeR(out); }
}

void SM511::tick(int clocks)
{
    while (clocks-- > 0) {
        s.div = uint16_t((s.div + 1) & 0x7fff);
        if (s.div == 0) s.gamma |= 1;
        clockMelody();
        if (++s.lcdPhase == 32) {
            s.lcdPhase = 0;
            uint16_t rows[3][4];
            const bool off = s.bc || !(s.bp & 1);
            for (int h = 0; h < 4; h++) {
                uint16_t a = 0, b = 0;
                if (!off)
                    for (int i = 0; i < 16; i++) { a |= uint16_t((s.ram[0x60 + i] >> h & 1) << i); b |= uint16_t((s.ram[0x70 + i] >> h & 1) << i); }
                uint8_t blink = (s.div & 0x4000) ? s.y : 0;
                uint8_t bs = uint8_t(((s.l & ~blink) >> h & 1) | ((s.x * 2) >> h & 2));
                rows[0][h] = a; rows[1][h] = b; rows[2][h] = off ? 0 : bs;
            }
            m_io->lcd(rows);
            m_io->tick1k();
        }
    }
}

int SM511::run(int n)
{
    int done = 0;
    while (done < n) {
        int cyc = 0;
        if (s.halt) {
            if (s.extWakeup || s.gamma) {
                s.halt = 0;
                branch(1, 0, 0);          // wakeup vector
                cyc = 1;
            } else {
                // idle until the divider or a key wakes it
                int c = s.clkDiv;
                done += c; tick(c);
                continue;
            }
        } else {
            cyc = 1;
            s.prevOp = s.op;
            s.prevPc = s.pc;
            s.op = m_rom[s.pc & 0xfff];
            incPc();
            const uint16_t op = s.op;
            if ((op >= 0x5f && op <= 0x61) || (op & 0xf0) == 0x70 || (op & 0xfc) == 0x68) {
                cyc++;
                s.param = m_rom[s.pc & 0xfff];
                incPc();
            }
            if (s.skip) { s.skip = 0; s.op = 0; }
            else {
                if ((s.op & 0xf0) >= 0xc0) cyc++;   // TM reads its vector table
                execute();
            }
        }
        int c = cyc * s.clkDiv;
        done += c;
        tick(c);
    }
    return done;
}

void SM511::execute()
{
    const uint8_t op = uint8_t(s.op);
    auto bit = [&](uint8_t o) { return uint8_t(1 << (o & 3)); };
    switch (op & 0xf0) {
    case 0x20: if ((s.op & ~0xf) != (s.prevOp & ~0xf)) s.acc = op & 0xf; goto done;   // LAX
    case 0x30: s.acc += (op & 0xf); s.skip = ((op & 0xf) != 10 && (s.acc & 0x10) != 0); s.acc &= 0xf; goto done;   // ADX
    case 0x40: s.bm = uint8_t((s.bm & 4) | (op & 3)); s.bl = uint8_t((op >> 2 & 3) | ((op & 0xc) ? 0xc : 0)); goto done;   // LB
    case 0x70: branch(s.param >> 6 & 3, op & 0xf, s.param & 0x3f); goto done;   // TL
    case 0x80: case 0x90: case 0xa0: case 0xb0: s.pc = uint16_t((s.pc & ~0x3f) | (op & 0x3f)); goto done;   // T
    case 0xc0: case 0xd0: case 0xe0: case 0xf0: {   // TM
        push();
        uint8_t idx = m_rom[op & 0x3f];
        branch(idx >> 6 & 3, 4, idx & 0x3f);
        goto done;
    }
    default: break;
    }
    switch (op & 0xfc) {
    case 0x04: ramW(ramR() & ~bit(op)); goto done;            // RM
    case 0x0c: ramW(ramR() | bit(op)); goto done;             // SM
    case 0x10: exc(); goto done;                              // EXC
    case 0x14: exc(); incb(); goto done;                      // EXCI
    case 0x18: s.acc = ramR(); s.bm ^= (op & 3); goto done;   // LDA
    case 0x1c: exc(); decb(); goto done;                      // EXCD
    case 0x54: s.skip = (ramR() & bit(op)) != 0; goto done;   // TMI
    case 0x68: push(); branch(s.param >> 6 & 3, op & 3, s.param & 0x3f); goto done;   // TML
    default: break;
    }
    switch (op) {
    case 0x00: { uint8_t c = s.acc & 1; s.acc = uint8_t(s.acc >> 1 | s.c << 3); s.c = c; break; }   // ROT
    case 0x01: s.acc = s.div >> 11 & 0xf; break;               // DTA
    case 0x02: break;                                          // SBM (bmask below)
    case 0x03: s.pc = uint16_t((s.prevPc & ~0xf) | s.acc); break;   // ATPL
    case 0x08: s.acc = (s.acc + ramR()) & 0xf; break;          // ADD
    case 0x09: s.acc = uint8_t(s.acc + ramR() + s.c); s.c = s.acc >> 4 & 1; s.skip = s.c; s.acc &= 0xf; break;   // ADD11
    case 0x0a: s.acc ^= 0xf; break;                            // COMA
    case 0x0b: { uint8_t a = s.acc; s.acc = s.bl; s.bl = a; break; }   // EXBLA
    case 0x50: s.acc = m_io->readK() & 0xf; break;             // KTA
    case 0x51: s.skip = m_io->readB() != 0; break;             // TB
    case 0x52: s.skip = !s.c; break;                           // TC
    case 0x53: s.skip = s.acc == ramR(); break;                // TAM
    case 0x58: s.skip = !s.gamma; s.gamma = 0; break;          // TIS
    case 0x59: s.l = s.acc; break;                             // ATL
    case 0x5a: s.skip = !s.acc; break;                         // TA0
    case 0x5b: s.skip = s.acc == s.bl; break;                  // TABL
    case 0x5c: s.x = s.acc; break;                             // ATX
    case 0x5d: s.halt = 1; break;                              // CEND
    case 0x5e: s.skip = m_io->readBA() != 0; break;            // TAL
    case 0x5f: s.bl = s.param & 0xf; s.bm = uint8_t((s.param & 0x7f) >> 4); break;   // LBL
    case 0x61: s.melodyAddr = s.param; s.melodyStep = 0; break;   // PRE
    case 0x62: s.w = uint8_t(s.w << 1); break;                 // WR
    case 0x63: s.w = uint8_t(s.w << 1 | 1); break;             // WS
    case 0x64: incb(); break;                                  // INCB
    case 0x65: s.div = 0; break;                               // IDIV
    case 0x66: s.c = 0; break;                                 // RC
    case 0x67: s.c = 1; break;                                 // SC
    case 0x6c: decb(); break;                                  // DECB
    case 0x6d: m_io->writeS(s.w); break;                       // PTW
    case 0x6e: pop(); break;                                   // RTN0
    case 0x6f: pop(); s.skip = 1; break;                       // RTN1
    case 0x60:
        s.op = uint16_t(op << 8 | s.param);
        switch (s.param) {
        case 0x30: s.melodyRd &= ~1; break;                    // RME
        case 0x31: s.melodyRd |= 1; break;                     // SME
        case 0x32: s.skip = (s.melodyRd & 2) != 0; s.melodyRd &= ~2; break;   // TMEL
        case 0x33: s.y = s.acc; break;                         // ATFC
        case 0x34: s.bc = s.c != 0; break;                     // BDC
        case 0x35: s.bp = s.acc; break;                        // ATBP
        case 0x36: s.clkDiv = 2; break;                        // CLKHI
        case 0x37: s.clkDiv = 4; break;                        // CLKLO
        default: break;
        }
        break;
    default: break;
    }
done:
    s.bmask = (s.op == 0x02) ? 0x40 : 0;
}
