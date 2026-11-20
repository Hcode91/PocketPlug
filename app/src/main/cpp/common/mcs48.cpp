// PocketPlug - Intel MCS-48 microcontroller
// SPDX-License-Identifier: BSD-3-Clause
//
// Port of MAME's mcs48.cpp. See mcs48.h.

#include "mcs48.h"

void Mcs48::reset()
{
    s.pc = 0;
    s.psw &= (C_FLAG | A_FLAG);
    s.f1 = 0;
    s.a11 = 0;
    s.tirqEnabled = s.xirqEnabled = 0;
    s.timecountEnabled = 0;
    s.timerFlag = 0;
    s.irqInProgress = 0;
    s.timerOverflow = 0;
    s.irqPolled = 0;
    s.dbbo = 0xff;
    m_bus->mcsBusWrite(s.ea ? 0xff : 0);
    s.p1 = 0xff;
    s.p2 = 0xff;
    m_bus->mcsPortWrite(1, s.p1);
    m_bus->mcsPortWrite(2, s.p2);
}

void Mcs48::burn(int count)
{
    if (s.timecountEnabled) {
        bool over = false;
        if (s.timecountEnabled & TIMER_ENABLED) {
            uint8_t old = s.timer;
            int p = s.prescaler + count;
            s.timer = uint8_t(s.timer + (p >> 5));
            s.prescaler = uint8_t(p & 0x1f);
            over = s.timer < old;
        } else if (s.timecountEnabled & COUNTER_ENABLED) {
            for (int i = 0; i < count; i++) {
                s.t1History = uint8_t((s.t1History << 1) | (m_bus->mcsTest(1) & 1));
                if ((s.t1History & 3) == 2 && ++s.timer == 0) over = true;
            }
        }
        if (over) {
            s.timerFlag = 1;
            if (s.tirqEnabled) s.timerOverflow = 1;
        }
    }
    m_cycles += count;
}

void Mcs48::pushPcPsw()
{
    uint8_t sp = s.psw & 7;
    ramW(uint8_t(8 + 2 * sp), uint8_t(s.pc));
    ramW(uint8_t(9 + 2 * sp), uint8_t(((s.pc >> 8) & 0x0f) | (s.psw & 0xf0)));
    s.psw = uint8_t((s.psw & 0xf0) | ((sp + 1) & 7));
}

void Mcs48::pullPcPsw()
{
    uint8_t sp = (s.psw - 1) & 7;
    s.pc = ramR(uint8_t(8 + 2 * sp));
    s.pc |= ramR(uint8_t(9 + 2 * sp)) << 8;
    s.psw = uint8_t(((s.pc >> 8) & 0xf0) | sp);
    s.pc &= s.irqInProgress ? 0x7ff : 0xfff;
}

void Mcs48::pullPc()
{
    uint8_t sp = (s.psw - 1) & 7;
    s.pc = ramR(uint8_t(8 + 2 * sp));
    s.pc |= ramR(uint8_t(9 + 2 * sp)) << 8;
    s.pc &= s.irqInProgress ? 0x7ff : 0xfff;
    s.psw = uint8_t((s.psw & 0xf0) | sp);
}

void Mcs48::add(uint8_t d)
{
    unsigned t = s.a + d, t4 = (s.a & 0x0f) + (d & 0x0f);
    s.psw &= ~(C_FLAG | A_FLAG);
    s.psw |= (t4 << 2) & A_FLAG;
    s.psw |= (t >> 1) & C_FLAG;
    s.a = uint8_t(t);
}

void Mcs48::addc(uint8_t d)
{
    unsigned c = (s.psw & C_FLAG) >> 7;
    unsigned t = s.a + d + c, t4 = (s.a & 0x0f) + (d & 0x0f) + c;
    s.psw &= ~(C_FLAG | A_FLAG);
    s.psw |= (t4 << 2) & A_FLAG;
    s.psw |= (t >> 1) & C_FLAG;
    s.a = uint8_t(t);
}

void Mcs48::checkIrqs()
{
    if (s.irqInProgress) return;
    if (s.irqState && s.xirqEnabled) {
        burn(2);
        s.irqInProgress = 1;
        if (s.irqPolled) {
            s.pc = ((s.prevpc + 1) & 0x7ff) | (s.prevpc & 0x800);
            jcc(true);
        }
        call(0x03);
    } else if (s.timerOverflow && s.tirqEnabled) {
        burn(2);
        s.irqInProgress = 1;
        call(0x07);
        s.timerOverflow = 0;
    }
}

int Mcs48::step()
{
    m_cycles = 0;
    checkIrqs();
    s.irqPolled = 0;
    s.prevpc = s.pc;
    uint8_t op = fetch();
    uint8_t lo = op & 7;

    // register-operand groups (opcode & 0xf8)
    if (op & 0x08) {
        uint8_t& r = reg(lo);
        switch (op & 0xf8) {
        case 0x08:                     // 08-0F: INS A,BUS / IN A,P1/P2 / MOVD (expander, not wired)
            switch (op) {
            case 0x08: burn(2); s.a = m_bus->mcsBusRead(); return m_cycles;
            case 0x09: burn(2); s.a = m_bus->mcsPortRead(1) & s.p1; return m_cycles;
            case 0x0a: burn(2); s.a = m_bus->mcsPortRead(2) & s.p2; return m_cycles;
            default: burn(op == 0x0b ? 1 : 2); return m_cycles;
            }
        case 0x18: burn(1); r++; return m_cycles;                     // INC Rn
        case 0x28: { burn(1); uint8_t t = s.a; s.a = r; r = t; return m_cycles; }   // XCH A,Rn
        case 0x38:                                                    // OUTL P1/P2, MOVD
            switch (op) {
            case 0x39: burn(2); s.p1 = s.a; m_bus->mcsPortWrite(1, s.p1); return m_cycles;
            case 0x3a: burn(2); s.p2 = s.a; m_bus->mcsPortWrite(2, s.p2); return m_cycles;
            default: burn(op == 0x38 || op == 0x3b ? 1 : 2); return m_cycles;
            }
        case 0x48: burn(1); s.a |= r; return m_cycles;                // ORL A,Rn
        case 0x58: burn(1); s.a &= r; return m_cycles;                // ANL A,Rn
        case 0x68: burn(1); add(r); return m_cycles;                  // ADD A,Rn
        case 0x78: burn(1); addc(r); return m_cycles;                 // ADDC A,Rn
        case 0x88:                                                    // ORL BUS/P1/P2,#n
            switch (op) {
            case 0x88: burn(2); m_bus->mcsBusWrite(m_bus->mcsBusRead() | fetch()); return m_cycles;
            case 0x89: burn(2); s.p1 |= fetch(); m_bus->mcsPortWrite(1, s.p1); return m_cycles;
            case 0x8a: burn(2); s.p2 |= fetch(); m_bus->mcsPortWrite(2, s.p2); return m_cycles;
            default: burn(op == 0x8b ? 1 : 2); return m_cycles;
            }
        case 0x98:                                                    // ANL BUS/P1/P2,#n
            switch (op) {
            case 0x98: burn(2); m_bus->mcsBusWrite(m_bus->mcsBusRead() & fetch()); return m_cycles;
            case 0x99: burn(2); s.p1 &= fetch(); m_bus->mcsPortWrite(1, s.p1); return m_cycles;
            case 0x9a: burn(2); s.p2 &= fetch(); m_bus->mcsPortWrite(2, s.p2); return m_cycles;
            default: burn(op == 0x9b ? 1 : 2); return m_cycles;
            }
        case 0xa8: burn(1); r = s.a; return m_cycles;                 // MOV Rn,A
        case 0xb8: burn(2); r = fetch(); return m_cycles;             // MOV Rn,#n
        case 0xc8: burn(1); r--; return m_cycles;                     // DEC Rn
        case 0xd8: burn(1); s.a ^= r; return m_cycles;                // XRL A,Rn
        case 0xe8: burn(2); jcc(--r != 0); return m_cycles;           // DJNZ Rn
        case 0xf8: burn(1); s.a = r; return m_cycles;                 // MOV A,Rn
        default: break;
        }
    }

    // 8-way jump/call groups: xxx0 0100 = JMP, xxx1 0100 = CALL; xxx1 0010 = JBb
    if ((op & 0x1f) == 0x04) { burn(2); jmp(uint16_t(fetch() | ((op >> 5) << 8))); return m_cycles; }
    if ((op & 0x1f) == 0x14) { burn(2); call(uint16_t(fetch() | ((op >> 5) << 8))); return m_cycles; }
    if ((op & 0x1f) == 0x12) { burn(2); jcc((s.a >> (op >> 5)) & 1); return m_cycles; }

    uint8_t& r0 = reg(0); uint8_t& r1 = reg(1);
    uint8_t ri = (op & 1) ? r1 : r0;
    switch (op) {
    case 0x00: burn(1); break;                                                  // NOP
    case 0x02: burn(2); m_bus->mcsBusWrite(s.a); break;                         // OUTL BUS,A
    case 0x03: burn(2); add(fetch()); break;
    case 0x05: burn(1); s.xirqEnabled = 1; break;                               // EN I
    case 0x07: burn(1); s.a--; break;
    case 0x10: case 0x11: burn(1); ramW(ri, uint8_t(ramR(ri) + 1)); break;      // INC @Ri
    case 0x13: burn(2); addc(fetch()); break;
    case 0x15: burn(1); s.xirqEnabled = 0; break;                               // DIS I
    case 0x16: burn(2); jcc(s.timerFlag); s.timerFlag = 0; break;               // JTF
    case 0x17: burn(1); s.a++; break;
    case 0x20: case 0x21: { burn(1); uint8_t t = s.a; s.a = ramR(ri); ramW(ri, t); break; }
    case 0x23: burn(2); s.a = fetch(); break;
    case 0x25: burn(1); s.tirqEnabled = 1; break;                               // EN TCNTI
    case 0x26: burn(2); jcc(m_bus->mcsTest(0) == 0); break;                     // JNT0
    case 0x27: burn(1); s.a = 0; break;
    case 0x30: case 0x31: {                                                     // XCHD A,@Ri
        burn(1);
        uint8_t o = ramR(ri);
        ramW(ri, uint8_t((o & 0xf0) | (s.a & 0x0f)));
        s.a = uint8_t((s.a & 0xf0) | (o & 0x0f));
        break;
    }
    case 0x35: burn(1); s.tirqEnabled = 0; s.timerOverflow = 0; break;          // DIS TCNTI
    case 0x36: burn(2); jcc(m_bus->mcsTest(0) != 0); break;                     // JT0
    case 0x37: burn(1); s.a ^= 0xff; break;
    case 0x40: case 0x41: burn(1); s.a |= ramR(ri); break;
    case 0x42: burn(1); s.a = s.timer; break;                                   // MOV A,T
    case 0x43: burn(2); s.a |= fetch(); break;
    case 0x45:                                                                  // STRT CNT
        burn(1);
        if (!(s.timecountEnabled & COUNTER_ENABLED)) s.t1History = uint8_t(m_bus->mcsTest(1));
        s.timecountEnabled = COUNTER_ENABLED;
        break;
    case 0x46: burn(2); jcc(m_bus->mcsTest(1) == 0); break;                     // JNT1
    case 0x47: burn(1); s.a = uint8_t((s.a << 4) | (s.a >> 4)); break;          // SWAP
    case 0x50: case 0x51: burn(1); s.a &= ramR(ri); break;
    case 0x53: burn(2); s.a &= fetch(); break;
    case 0x55: burn(1); s.timecountEnabled = TIMER_ENABLED; s.prescaler = 0; break;   // STRT T
    case 0x56: burn(2); jcc(m_bus->mcsTest(1) != 0); break;                     // JT1
    case 0x57:                                                                  // DA A
        burn(1);
        if ((s.a & 0x0f) > 9 || (s.psw & A_FLAG)) {
            if (s.a > 0xf9) s.psw |= C_FLAG;
            s.a = uint8_t(s.a + 6);
        }
        if ((s.a & 0xf0) > 0x90 || (s.psw & C_FLAG)) { s.a = uint8_t(s.a + 0x60); s.psw |= C_FLAG; }
        break;
    case 0x60: case 0x61: burn(1); add(ramR(ri)); break;
    case 0x62: burn(1); s.timer = s.a; break;                                   // MOV T,A
    case 0x65: burn(1); s.timecountEnabled = 0; break;                          // STOP TCNT
    case 0x67: { burn(1); uint8_t nc = uint8_t((s.a << 7) & C_FLAG); s.a = uint8_t((s.a >> 1) | (s.psw & C_FLAG)); s.psw = uint8_t((s.psw & ~C_FLAG) | nc); break; }   // RRC
    case 0x70: case 0x71: burn(1); addc(ramR(ri)); break;
    case 0x75: burn(1); break;                                                  // ENT0 CLK
    case 0x76: burn(2); jcc(s.f1); break;                                       // JF1
    case 0x77: burn(1); s.a = uint8_t((s.a >> 1) | (s.a << 7)); break;          // RR
    case 0x80: case 0x81: burn(2); s.a = m_bus->mcsExtRead(ri); break;          // MOVX A,@Ri
    case 0x83: burn(2); pullPc(); break;                                        // RET
    case 0x85: burn(1); s.psw &= ~F_FLAG; break;                                // CLR F0
    case 0x86: burn(2); s.irqPolled = s.irqState == 0; jcc(s.irqState != 0); break;   // JNI
    case 0x90: case 0x91: burn(2); m_bus->mcsExtWrite(ri, s.a); break;          // MOVX @Ri,A
    case 0x93: burn(2); s.irqInProgress = 0; pullPcPsw(); break;                // RETR
    case 0x95: burn(1); s.psw ^= F_FLAG; break;                                 // CPL F0
    case 0x96: burn(2); jcc(s.a != 0); break;                                   // JNZ
    case 0x97: burn(1); s.psw &= ~C_FLAG; break;                                // CLR C
    case 0xa0: case 0xa1: burn(1); ramW(ri, s.a); break;                        // MOV @Ri,A
    case 0xa3: burn(2); s.a = prog(uint16_t((s.pc & 0xf00) | s.a)); break;      // MOVP A,@A
    case 0xa5: burn(1); s.f1 = 0; break;
    case 0xa7: burn(1); s.psw ^= C_FLAG; break;
    case 0xb0: case 0xb1: burn(2); ramW(ri, fetch()); break;                    // MOV @Ri,#n
    case 0xb3: burn(2); s.pc &= 0xf00; s.pc |= prog(uint16_t(s.pc | s.a)); break;   // JMPP @A
    case 0xb5: burn(1); s.f1 = !s.f1; break;
    case 0xb6: burn(2); jcc(s.psw & F_FLAG); break;                             // JF0
    case 0xc5: burn(1); s.psw &= ~B_FLAG; break;                                // SEL RB0
    case 0xc6: burn(2); jcc(s.a == 0); break;                                   // JZ
    case 0xc7: burn(1); s.a = s.psw | 0x08; break;                              // MOV A,PSW
    case 0xd0: case 0xd1: burn(1); s.a ^= ramR(ri); break;
    case 0xd3: burn(2); s.a ^= fetch(); break;
    case 0xd5: burn(1); s.psw |= B_FLAG; break;                                 // SEL RB1
    case 0xd7: burn(1); s.psw = s.a & ~0x08; break;                             // MOV PSW,A
    case 0xe3: burn(2); s.a = prog(uint16_t(0x300 | s.a)); break;               // MOVP3
    case 0xe5: burn(1); s.a11 = 0x000; break;                                   // SEL MB0
    case 0xe6: burn(2); jcc(!(s.psw & C_FLAG)); break;                          // JNC
    case 0xe7: burn(1); s.a = uint8_t((s.a << 1) | (s.a >> 7)); break;          // RL
    case 0xf0: case 0xf1: burn(1); s.a = ramR(ri); break;
    case 0xf5: burn(1); s.a11 = 0x800; break;                                   // SEL MB1
    case 0xf6: burn(2); jcc(s.psw & C_FLAG); break;                             // JC
    case 0xf7: { burn(1); uint8_t nc = s.a & C_FLAG; s.a = uint8_t((s.a << 1) | (s.psw >> 7)); s.psw = uint8_t((s.psw & ~C_FLAG) | nc); break; }   // RLC
    default: burn(1); break;                                                    // illegal
    }
    return m_cycles;
}
