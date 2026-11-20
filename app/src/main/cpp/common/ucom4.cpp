// PocketPlug - NEC uCOM-4 family (port of MAME's ucom4.cpp / ucom4op.cpp, hap)
// SPDX-License-Identifier: BSD-3-Clause

#include "ucom4.h"

void UCOM4::configure(Family f, Io* io, bool d557l)
{
    m_family = f;
    m_557 = d557l;
    m_io = io ? io : &m_null;
    if (f == UCOM44) { m_stackLevels = 1; m_prgmask = 0x3ff; m_datamask = 0x3f; }
    else { m_stackLevels = 3; m_prgmask = 0x7ff; m_datamask = 0x7f; }
    std::memset(&s, 0, sizeof(s));
    s.timerLeft = -1;
}

void UCOM4::setRom(const std::vector<uint8_t>& rom)
{
    m_rom.assign(size_t(m_prgmask) + 1, 0);
    for (size_t i = 0; i < rom.size() && i < m_rom.size(); i++) m_rom[i] = rom[i];
}

void UCOM4::reset()
{
    s.pc = 0; s.op = 0; s.skip = 0;
    s.timerLeft = -1;
    s.intLine = 0; s.intF = 0;
    s.inteF = m_family == UCOM43 ? 0 : 1;
    for (int i = PC; i <= PI; i++) output(i, 0);
}

uint8_t UCOM4::ramAtC(uint8_t a) const
{
    a &= m_datamask;
    if (m_datamask == 0x7f && ((a >= 0x50 && a < 0x70))) return 0;
    return s.ram[a] & 0xf;
}
void UCOM4::ramAtW(uint8_t a, uint8_t d)
{
    a &= m_datamask;
    if (m_datamask == 0x7f && ((a >= 0x50 && a < 0x70))) return;
    s.ram[a] = d & 0xf;
}
uint8_t UCOM4::ramR() const { return ramAtC(uint8_t(s.dph << 4 | s.dpl)); }
void UCOM4::ramW(uint8_t d) { ramAtW(uint8_t(s.dph << 4 | s.dpl), d); }

uint8_t UCOM4::input(int index)
{
    index &= 0xf;
    if (m_557 && index == PB) return 0;
    uint8_t inp = 0;
    switch (index) {
    case PA: case PB: inp = m_io->input(index); break;
    case PC: case PD: inp = uint8_t(m_io->input(index) | s.portOut[index]); break;
    default: break;
    }
    return inp & 0xf;
}

void UCOM4::output(int index, uint8_t data)
{
    index &= 0xf;
    data &= 0xf;
    if (m_557) {
        if (index == PH || index == PI) return;
        if (index == PG) data &= 1;
    }
    if (index >= PC && index <= PI) m_io->output(index, index == PI ? (data & 7) : data);
    s.portOut[index] = data;
}

void UCOM4::push()
{
    for (int i = m_stackLevels - 1; i >= 1; i--) s.stack[i] = s.stack[i - 1];
    s.stack[0] = s.pc;
}
void UCOM4::pop()
{
    s.pc = s.stack[0] & m_prgmask;
    for (int i = 0; i < m_stackLevels - 1; i++) s.stack[i] = s.stack[i + 1];
}

void UCOM4::setInt(int state)
{
    if (!s.intLine && state) s.intF = 1;
    s.intLine = state ? 1 : 0;
}

void UCOM4::tick(int n)
{
    m_icount -= n;
    s.total += uint64_t(n);
    if (s.timerLeft >= 0) {
        s.timerLeft -= n;
        if (s.timerLeft <= 0) { s.timerLeft = -1; s.timerF = 1; }
    }
}

int UCOM4::run(int cycles)
{
    m_icount = cycles;
    while (m_icount > 0) execOne();
    return cycles - m_icount;
}

void UCOM4::execOne()
{
    if (s.intF && s.inteF && (s.op & 0xf0) != 0x90 && s.op != 0x31 && !s.skip) {
        tick(1);
        push();
        s.pc = 0xf << 2;
        s.intF = 0;
        s.inteF = m_family == UCOM43 ? 0 : 1;
    }
    s.prevOp = s.op;
    s.prevPc = s.pc;
    tick(1);
    s.op = m_rom[s.pc & m_prgmask];
    s.bitmask = uint8_t(1 << (s.op & 3));
    incPc();
    if ((s.op & 0xfc) == 0x14 || (s.op & 0xf0) == 0xa0 || s.op == 0x1e) {
        tick(1);
        s.arg = m_rom[s.pc & m_prgmask];
        incPc();
    }
    if (s.skip) { s.skip = 0; s.op = 0; }

    const uint8_t op = s.op;
    const bool u43 = m_family == UCOM43;
    auto xm = [&] { uint8_t o = s.acc; s.acc = ramR(); ramW(o); s.dph ^= (op & 3); };
    switch (op & 0xf0) {
    case 0x80: s.dph = 0; s.dpl = op & 0xf; return;                              // LDZ
    case 0x90: if ((s.prevOp & 0xf0) != (op & 0xf0)) s.acc = op & 0xf; return;  // LI
    case 0xa0: if (op & 8) push(); s.pc = uint16_t(((op & 7) << 8 | s.arg) & m_prgmask); return;   // JMP/CAL
    case 0xb0: push(); s.pc = uint16_t((op & 0xf) << 2); return;                  // CZP
    case 0xc0: case 0xd0: case 0xe0: case 0xf0: s.pc = uint16_t((s.pc & ~0x3f) | (op & 0x3f)); return;   // JCP
    default: break;
    }
    switch (op) {
    case 0x00: return;
    case 0x01: if (u43) s.inteF = 0; return;                                         // DI
    case 0x02: ramW(s.acc); return;                                                  // S
    case 0x03: s.skip = s.intF != 0; s.intF = 0; return;                             // TIT
    case 0x04: s.skip = s.carry != 0; return;                                        // TC
    case 0x05: if (u43) s.skip = s.timerF != 0; return;                              // TTM
    case 0x06: s.acc = (s.acc + 6) & 0xf; return;                                    // DAA
    case 0x07: s.dpl = s.acc; return;                                                // TAL
    case 0x08: s.acc += ramR(); s.skip = (s.acc & 0x10) != 0; s.acc &= 0xf; return;  // AD
    case 0x09: s.acc += ramR() + s.carry; s.carry = s.acc >> 4 & 1; s.acc &= 0xf; s.skip = s.carry != 0; return;   // ADS
    case 0x0a: s.acc = (s.acc + 10) & 0xf; return;                                   // DAS
    case 0x0b: s.carry = 0; return;                                                  // CLC
    case 0x0c: s.skip = s.acc == ramR(); return;                                     // CM
    case 0x0d: s.acc = (s.acc + 1) & 0xf; s.skip = s.acc == 0; return;               // INC
    case 0x0e: output(s.dpl, s.acc); return;                                         // OP
    case 0x0f: s.acc = (s.acc - 1) & 0xf; s.skip = s.acc == 0xf; return;             // DEC
    case 0x10: s.acc ^= 0xf; return;                                                 // CMA
    case 0x11: s.acc = ((s.acc ^ 0xf) + 1) & 0xf; return;                            // CIA
    case 0x12: s.acc = s.dpl; return;                                                // TLA
    case 0x13: s.dpl = (s.dpl - 1) & 0xf; s.skip = s.dpl == 0xf; return;             // DED
    case 0x14:                                                                       // STM
        if (u43) { s.timerF = 0; s.timerLeft = 63 * ((s.arg & 0x3f) + 1); }
        return;
    case 0x15: s.dph = s.arg >> 4 & 0xf; s.dpl = s.arg & 0xf; return;                // LDI
    case 0x16: s.skip = s.dpl == (s.arg & 0xf); return;                              // CLI
    case 0x17: s.skip = s.acc == (s.arg & 0xf); return;                              // CI
    case 0x18: s.acc ^= ramR(); return;                                              // EXL
    case 0x19: s.acc += ramR() + s.carry; s.carry = s.acc >> 4 & 1; s.acc &= 0xf; return;   // ADC
    case 0x1a: if (u43) { uint8_t c = s.carry; s.carry = s.carryS; s.carryS = c; } return;  // XC
    case 0x1b: s.carry = 1; return;                                                  // STC
    case 0x1d: if (u43) { uint8_t v = (ramR() + 1) & 0xf; ramW(v); s.skip = v == 0; } return;     // INM
    case 0x1e: output(PD, s.arg >> 4); output(PC, s.arg & 0xf); return;              // OCD
    case 0x1f: if (u43) { uint8_t v = (ramR() - 1) & 0xf; ramW(v); s.skip = v == 0xf; } return;   // DEM
    case 0x30: if (u43) { uint8_t c = s.acc & 1; s.acc = uint8_t(s.acc >> 1 | s.carry << 3); s.carry = c; } return;   // RAR
    case 0x31: if (u43) s.inteF = 1; return;                                         // EI
    case 0x32: s.acc = input(s.dpl); return;                                         // IP
    case 0x33: s.dpl = (s.dpl + 1) & 0xf; s.skip = s.dpl == 0; return;               // IND
    case 0x40: tick(1); s.acc = input(PA); return;                                   // IA
    case 0x41: tick(1); s.pc = uint16_t((s.pc & ~0x3f) | (s.acc << 2)); return;       // JPA
    case 0x42: if (u43) { tick(1); regW(5, s.acc); } return;                         // TAZ
    case 0x43: if (u43) { tick(1); regW(4, s.acc); } return;                         // TAW
    case 0x44: tick(1); output(PE, s.acc); return;                                   // OE
    case 0x46: if (u43) { tick(1); regW(1, s.dpl); } return;                         // TLY
    case 0x47: if (u43) { tick(1); regW(0, s.dph); } return;                         // THX
    case 0x48: tick(1); pop(); return;                                               // RT
    case 0x49: tick(1); pop(); s.skip = 1; return;                                   // RTS
    case 0x4a: if (u43) { tick(1); uint8_t o = s.acc; s.acc = regR(5); regW(5, o); } return;   // XAZ
    case 0x4b: if (u43) { tick(1); uint8_t o = s.acc; s.acc = regR(4); regW(4, o); } return;   // XAW
    case 0x4c: if (u43) { tick(1); uint8_t o = s.dpl; s.dpl = regR(3); regW(3, o); } return;   // XLS
    case 0x4d: if (u43) { tick(1); uint8_t o = s.dph; s.dph = regR(2); regW(2, o); } return;   // XHR
    case 0x4e: if (u43) { tick(1); uint8_t o = s.dpl; s.dpl = regR(1); regW(1, o); } return;   // XLY
    case 0x4f: if (u43) { tick(1); uint8_t o = s.dph; s.dph = regR(0); regW(0, o); } return;   // XHX
    default: break;
    }
    switch (op & 0xfc) {
    case 0x20: if (u43) { tick(1); s.skip = (regR(6) & s.bitmask) == 0; } return;          // FBF
    case 0x24: s.skip = (s.acc & s.bitmask) != 0; return;                                  // TAB
    case 0x28: xm(); return;                                                               // XM
    case 0x2c: xm(); s.dpl = (s.dpl - 1) & 0xf; s.skip = s.dpl == 0xf; return;             // XMD
    case 0x34: s.skip = (s.acc & s.bitmask) == (ramR() & s.bitmask); return;               // CMB
    case 0x38: s.acc = ramR(); s.dph ^= (op & 3); return;                                  // LM
    case 0x3c: xm(); s.dpl = (s.dpl + 1) & 0xf; s.skip = s.dpl == 0; return;               // XMI
    case 0x50: s.skip = (input(s.dpl) & s.bitmask) != 0; return;                           // TPB
    case 0x54: s.skip = (input(PA) & s.bitmask) != 0; return;                              // TPA
    case 0x58: s.skip = (ramR() & s.bitmask) != 0; return;                                 // TMB
    case 0x5c: if (u43) { tick(1); s.skip = (regR(6) & s.bitmask) != 0; } return;          // FBT
    case 0x60: output(s.dpl, s.portOut[s.dpl] & ~s.bitmask); return;                       // RPB
    case 0x64: tick(1); output(PE, s.portOut[PE] & ~s.bitmask); return;                    // REB
    case 0x68: ramW(ramR() & ~s.bitmask); return;                                          // RMB
    case 0x6c: if (u43) { tick(1); regW(6, regR(6) & ~s.bitmask); } return;                // RFB
    case 0x70: output(s.dpl, s.portOut[s.dpl] | s.bitmask); return;                        // SPB
    case 0x74: tick(1); output(PE, s.portOut[PE] | s.bitmask); return;                     // SEB
    case 0x78: ramW(ramR() | s.bitmask); return;                                           // SMB
    case 0x7c: if (u43) { tick(1); regW(6, regR(6) | s.bitmask); } return;                 // SFB
    default: return;
    }
}
