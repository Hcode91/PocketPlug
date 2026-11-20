// PocketPlug - COP410/COP411 microcontroller
// SPDX-License-Identifier: BSD-3-Clause
//
// Port of the COP410 part of MAME's cop400.cpp (Curt Coder). See cop410.h.

#include "cop410.h"

void Cop410::reset()
{
    s.pc = 0;
    s.a = 0; s.b = 0; s.c = 0;
    m_bus->copWriteD(0);
    s.en = 0;
    writeG(0);
    s.skl = 1;
    s.halt = 0;
    s.secondByte = 0;
    s.skip = 0;
    s.skipLbi = 0;
}

void Cop410::step()
{
    if (s.halt) return;
    uint8_t operand = m_rom[s.pc & 0x1ff];
    s.pc = (s.pc + 1) & 0x1ff;
    if (!s.secondByte) s.opcode = operand;
    const uint8_t op = s.opcode;

    if (!s.secondByte && isTwoByte(op)) { s.secondByte = 1; return; }
    if (s.skip) { s.skip = 0; s.secondByte = 0; return; }

    if (!s.secondByte && (op == 0xff || op == 0xbf)) {
        // JID / LQID: transfer control first, then fetch the operand
        if (op == 0xbf) push(s.pc);
        s.pc = (s.pc & 0x700) | (s.a << 4) | ramR(s.b);
        s.secondByte = 1;
        return;
    }
    execute(op, operand);
    if (s.skipLbi > 0) s.skipLbi--;
    s.secondByte = 0;
}

void Cop410::execute(uint8_t op, uint8_t operand)
{
    auto skip = [&]() { s.skip = 1; };
    auto lbi = [&](uint8_t v) {
        s.skipLbi++;
        if (s.skipLbi > 1) return;
        s.skipLbi++;
        if (v & 0x80) s.b = v & 0x7f;
        else s.b = (v & 0x30) | (((v & 0x0f) + 1) & 0x0f);
    };

    // 0x80-0xff: JP / JSRP (0xbf LQID, 0xff JID handled as second byte)
    if (op & 0x80) {
        if (op == 0xff) { s.pc = (s.pc & 0x700) | operand; return; }
        if (op == 0xbf) { s.q = operand; pop(); return; }
        uint8_t page = uint8_t(s.pc >> 6);
        if (page == 2 || page == 3) s.pc = (s.pc & 0x780) | (op & 0x7f);
        else if ((op & 0xc0) == 0xc0) s.pc = (s.pc & 0x7c0) | (op & 0x3f);
        else { push(s.pc); s.pc = 0x80 | (op & 0x3f); }
        return;
    }
    if (op < 0x40) {
        switch (op & 0x0f) {
        case 0x04: case 0x05: case 0x06: case 0x07: {
            uint8_t r = op & 0x30;
            uint8_t t = ramR(s.b);
            switch (op & 0x0f) {
            case 0x05: s.a = t; s.b ^= r; return;                              // LD
            case 0x06: ramW(s.b, s.a); s.a = t; s.b ^= r; return;             // X
            case 0x04: {                                                       // XIS
                ramW(s.b, s.a); s.a = t;
                uint8_t bd = ((s.b & 0x0f) + 1) & 0x0f;
                s.b = uint8_t(((s.b & 0x70) | bd) ^ r);
                if (bd == 0) skip();
                return;
            }
            default: {                                                         // XDS
                ramW(s.b, s.a); s.a = t;
                uint8_t bd = ((s.b & 0x0f) - 1) & 0x0f;
                s.b = uint8_t(((s.b & 0x70) | bd) ^ r);
                if (bd == 0x0f) skip();
                return;
            }
            }
        }
        default: break;
        }
        if (op & 0x08) { lbi(op); return; }                                    // LBI (short)
        switch (op) {
        case 0x00: s.a = 0; return;                                            // CLRA
        case 0x01: if (!(ramR(s.b) & 1)) skip(); return;                       // SKMBZ 0
        case 0x02: s.a ^= ramR(s.b); return;                                   // XOR
        case 0x03: if (!(ramR(s.b) & 4)) skip(); return;                       // SKMBZ 2
        case 0x11: if (!(ramR(s.b) & 2)) skip(); return;                       // SKMBZ 1
        case 0x13: if (!(ramR(s.b) & 8)) skip(); return;                       // SKMBZ 3
        case 0x20: if (s.c) skip(); return;                                    // SKC
        case 0x21: if (s.a == ramR(s.b)) skip(); return;                       // SKE
        case 0x22: s.c = 1; return;                                            // SC
        case 0x23:                                                             // XAD
            if (operand == 0xbf) { uint8_t rd = operand & 0x7f; uint8_t t = s.a; s.a = ramR(rd); ramW(rd, t); }
            return;
        case 0x30: {                                                           // ASC
            s.a = uint8_t(s.a + s.c + ramR(s.b));
            if (s.a > 0xf) { s.c = 1; skip(); s.a &= 0xf; } else s.c = 0;
            return;
        }
        case 0x31: s.a = (s.a + ramR(s.b)) & 0x0f; return;                     // ADD
        case 0x32: s.c = 0; return;                                            // RC
        case 0x33: {
            uint8_t g = uint8_t(m_bus->copReadG() & m_gMask);
            switch (operand) {
            case 0x01: if (!(g & 1)) skip(); return;                           // SKGBZ 0
            case 0x03: if (!(g & 4)) skip(); return;                           // SKGBZ 2
            case 0x11: if (!(g & 2)) skip(); return;                           // SKGBZ 1
            case 0x13: if (!(g & 8)) skip(); return;                           // SKGBZ 3
            case 0x21: if (g == 0) skip(); return;                             // SKGZ
            case 0x2a: s.a = g; return;                                        // ING
            case 0x2e: { uint8_t l = m_bus->copReadL(); ramW(s.b, l >> 4); s.a = l & 0x0f; return; }   // INL
            case 0x38: s.halt = 1; return;                                     // HALT
            case 0x3a: writeG(ramR(s.b)); return;                              // OMG
            case 0x3c: s.q = uint8_t((s.a << 4) | ramR(s.b)); return;          // CAMQ
            case 0x3e: m_bus->copWriteD(uint8_t(s.b & 0x0f & m_dMask)); return; // OBD
            default:
                if ((operand & 0xf0) == 0x60) s.en = operand & 0x0f;           // LEI
                return;
            }
        }
        default: return;
        }
    }
    // 0x40-0x7f
    if (op >= 0x70) { uint8_t y = op & 0x0f; ramW(s.b, y); s.b = uint8_t((s.b & 0x70) + (((s.b & 0x0f) + 1) & 0x0f)); return; }   // STII
    if (op >= 0x51 && op <= 0x5f) { s.a = uint8_t(s.a + (op & 0x0f)); if (s.a > 0x0f) { skip(); s.a &= 0x0f; } return; }       // AISC
    switch (op) {
    case 0x40: s.a ^= 0x0f; return;                                            // COMP
    case 0x42: ramW(s.b, ramR(s.b) & 0xb); return;                             // RMB 2
    case 0x43: ramW(s.b, ramR(s.b) & 0x7); return;                             // RMB 3
    case 0x44: return;                                                         // NOP
    case 0x45: ramW(s.b, ramR(s.b) & 0xd); return;                             // RMB 1
    case 0x46: ramW(s.b, ramR(s.b) | 0x4); return;                             // SMB 2
    case 0x47: ramW(s.b, ramR(s.b) | 0x2); return;                             // SMB 1
    case 0x48: pop(); return;                                                  // RET
    case 0x49: pop(); skip(); return;                                          // RETSK
    case 0x4b: ramW(s.b, ramR(s.b) | 0x8); return;                             // SMB 3
    case 0x4c: ramW(s.b, ramR(s.b) & 0xe); return;                             // RMB 0
    case 0x4d: ramW(s.b, ramR(s.b) | 0x1); return;                             // SMB 0
    case 0x4e: s.a = s.b & 0x0f; return;                                       // CBA
    case 0x4f: { uint8_t t = s.sio; s.sio = s.a; s.a = t; s.skl = s.c; return; }   // XAS
    case 0x50: s.b = uint8_t((s.b & 0x70) | s.a); return;                      // CAB
    case 0x60: case 0x61: s.pc = uint16_t(((op & 7) << 8) | operand); return;  // JMP
    case 0x68: case 0x69: push(s.pc); s.pc = uint16_t(((op & 7) << 8) | operand); return;   // JSR
    default: return;
    }
}
