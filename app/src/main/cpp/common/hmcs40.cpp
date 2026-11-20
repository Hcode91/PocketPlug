// PocketPlug - Hitachi HMCS40 family (port of MAME's hmcs40.cpp / hmcs40op.cpp, hap)
// SPDX-License-Identifier: BSD-3-Clause

#include "hmcs40.h"

void HMCS40::configure(Family f, bool cmos, Io* io)
{
    m_family = f;
    m_polarity = cmos ? 0xffff : 0;
    m_io = io ? io : &m_nullIo;
    switch (f) {
    case HMCS42: case HMCS43: m_stackLevels = 3; m_pcmask = 0x3ff; m_prgmask = 0x7ff; m_datamask = 0x7f; break;
    case HMCS44: case HMCS45: m_stackLevels = 4; m_pcmask = 0x7ff; m_prgmask = 0xfff; m_datamask = 0xff; break;
    default:                  m_stackLevels = 4; m_pcmask = 0xfff; m_prgmask = 0xfff; m_datamask = 0xff; break;
    }
    std::memset(&s, 0, sizeof(s));
    s.s = 1;
}

void HMCS40::setRom(const std::vector<uint8_t>& region)
{
    m_rom.assign(size_t(m_prgmask + 1) * 2, 0);
    for (size_t i = 0; i < region.size() && i < m_rom.size(); i++) m_rom[i] = region[i];
}

void HMCS40::reset()
{
    uint64_t total = s.total;
    uint8_t intl[2] = { s.intl[0], s.intl[1] };
    uint8_t halt = s.halt;
    uint8_t ram[256]; std::memcpy(ram, s.ram, sizeof(ram));
    std::memset(&s, 0, sizeof(s));
    std::memcpy(s.ram, ram, sizeof(ram));
    s.total = total; s.intl[0] = intl[0]; s.intl[1] = intl[1]; s.halt = halt;
    s.s = 1;
    s.pc = m_pcmask;
    s.ifl[0] = s.ifl[1] = s.tf = 1;
    resetIo();
    if (m_family == HMCS46 || m_family == HMCS47) s.r[7] |= 1;
}

// ---- memory ----

uint8_t& HMCS40::ramAt(uint8_t a)
{
    a &= m_datamask;
    if (m_datamask == 0x7f) {             // 80x4
        if (a >= 0x40) a = 0x40 | (a & 0x0f);
    } else if (m_family <= HMCS45) {      // 160x4
        if (a >= 0xc0) a = 0xc0 | (a & 0x0f);
        else if (a >= 0x80) a = 0x80 | (a & 0x0f);
    }
    return s.ram[a];
}
uint8_t HMCS40::ramR() const { return const_cast<HMCS40*>(this)->ramAt(uint8_t(s.x << 4 | s.y)) & 0xf; }
void HMCS40::ramW(uint8_t d) { ramAt(uint8_t(s.x << 4 | s.y)) = d & 0xf; }

void HMCS40::popStack()
{
    s.pc = s.stack[0] & m_pcmask;
    for (int i = 0; i < m_stackLevels - 1; i++) s.stack[i] = s.stack[i + 1];
}
void HMCS40::pushStack()
{
    for (int i = m_stackLevels - 1; i >= 1; i--) s.stack[i] = s.stack[i - 1];
    s.stack[0] = s.pc;
}

// ---- I/O ----

void HMCS40::resetIo()
{
    s.d = m_polarity;
    m_io->writeD(m_polarity, 0xffff);
    for (int i = 0; i < 8; i++) {
        s.r[i] = m_polarity & 0xf;
        m_io->writeR(i, s.r[i]);
    }
}

uint8_t HMCS40::readR(int index)
{
    index &= 7;
    uint8_t inp = m_io->readR(index);
    return m_polarity ? uint8_t((inp & s.r[index]) & 0xf) : uint8_t((inp | s.r[index]) & 0xf);
}

void HMCS40::writeR(int index, uint8_t data)
{
    index &= 7;
    switch (m_family) {
    case HMCS42: case HMCS43: if (index == 0 || index >= 4) return; break;
    case HMCS44: if (index >= 6) return; break;
    case HMCS45: if (index == 7) return; break;
    case HMCS46: if (index == 6) return; break;
    default: break;
    }
    data &= 0xf;
    s.r[index] = data;
    m_io->writeR(index, data);
}

int HMCS40::readD(int index)
{
    index &= 0xf;
    uint16_t inp = m_io->readD();
    return m_polarity ? (inp & s.d) >> index & 1 : (inp | s.d) >> index & 1;
}

void HMCS40::writeD(int index, int state)
{
    index &= 0xf;
    uint16_t mask = uint16_t(1 << index);
    s.d = uint16_t((s.d & ~mask) | (state ? mask : 0));
    m_io->writeD(s.d, mask);
}

// ---- interrupts / timer ----

void HMCS40::setInput(int line, int state)
{
    state = state ? 1 : 0;
    if (line == 2) { s.halt = uint8_t(state); return; }
    if (line != 0 && line != 1) return;
    if (state && !s.intl[line]) {
        if (!s.ifl[line]) { s.eintLine = uint8_t(line); s.iri = 1; s.ifl[line] = 1; }
        if (s.cf && line == 1) clockTimer();
    }
    s.intl[line] = uint8_t(state);
}

void HMCS40::clockTimer()
{
    s.tc = (s.tc + 1) & 0xf;
    if (s.tc == 0 && !s.tf) { s.irt = 1; s.tf = 1; }
}

void HMCS40::clockPrescaler()
{
    s.prescaler = (s.prescaler + 1) & 0x3f;
    if (s.prescaler == 0 && !s.cf) clockTimer();
}

void HMCS40::takeInterrupt()
{
    pushStack();
    s.ie = 0;
    s.pc = uint16_t(0x3f | (s.iri ? 0x40 : 0));
    if (s.iri) s.iri = 0; else s.irt = 0;
    s.prevPc = s.pc;
    cycle();
}

void HMCS40::incPc()
{
    // the low 6 bits are an LFSR, as on the TMS1000
    const uint8_t mask = 0x3f;
    uint8_t low = s.pc & mask;
    int fb = (low << 1 & 0x20) == (low & 0x20);
    if (low == (mask >> 1)) fb = 1;
    else if (low == mask) fb = 0;
    s.pc = uint16_t((s.pc & ~mask) | ((s.pc << 1 | fb) & mask));
}

// ---- execution ----

int HMCS40::run(int cycles)
{
    if (s.halt) { s.total += uint64_t(cycles); return cycles; }
    m_icount = cycles;
    while (m_icount > 0) execOne();
    return cycles - m_icount;
}

static inline uint8_t rev4(uint16_t v) { return uint8_t((v & 1) << 3 | (v & 2) << 1 | (v & 4) >> 1 | (v & 8) >> 3); }

void HMCS40::execOne()
{
    if ((s.prevOp & 0x7e0) == 0x340)     // LPU takes effect a cycle later
        s.pc = uint16_t(((s.pcUpper << 6) | (s.pc & 0x3f)) & m_pcmask);
    s.prevOp = s.op;
    s.prevPc = s.pc;
    if (s.ie && (s.iri || s.irt) && !s.blockInt) takeInterrupt();
    s.blockInt = 0;

    s.op = word(s.pc) & 0x3ff;
    s.i = rev4(s.op);
    incPc();
    cycle();

    const uint16_t op = s.op;
    auto xsp = [&] {
        if (op & 1) { uint8_t t = s.x; s.x = s.spx; s.spx = t; }
        if (op & 2) { uint8_t t = s.y; s.y = s.spy; s.spy = t; }
    };
    auto iy = [&] { s.y = (s.y + 1) & 0xf; s.s = s.y != 0; };
    auto dy = [&] { s.y = (s.y - 1) & 0xf; s.s = s.y != 0xf; };
    auto rtn = [&] { popStack(); };

    switch (op & 0x3f0) {
    case 0x1c0: case 0x1d0: case 0x1e0: case 0x1f0:   // BR
        if (s.s) s.pc = uint16_t((s.pc & ~0x3f) | (op & 0x3f)); else s.s = 1;
        return;
    case 0x3c0: case 0x3d0: case 0x3e0: case 0x3f0:   // CAL
        if (s.s) { s.blockInt = 1; pushStack(); s.pc = op & 0x3f; } else s.s = 1;
        return;
    case 0x340: case 0x350:                            // LPU
        if (s.s) {
            s.blockInt = 1;
            s.pcUpper = op & 0x1f;
            if (m_family == HMCS46 || m_family == HMCS47) s.pcUpper |= uint8_t(~s.r[7] << 5 & 0x20);
        } else s.op |= 0x400;
        return;
    case 0x010: ramW(s.i); iy(); return;              // LMIIY
    case 0x070: s.a = s.i; return;                    // LAI
    case 0x080: s.a += s.i; s.s = s.a >> 4 & 1; s.a &= 0xf; return;   // AI
    case 0x0f0: {                                     // XAMR
        uint8_t address = op & 0xf;
        if (m_family == HMCS42 || m_family == HMCS43) address |= (address < 4) ? uint8_t(address << 4) : 0x40;
        else address |= 0xf0;
        uint8_t& m = ramAt(address & m_datamask);
        uint8_t old = s.a; s.a = m & 0xf; m = old & 0xf;
        return;
    }
    case 0x140: s.x = s.i; return;                    // LXI
    case 0x150: s.y = s.i; return;                    // LYI
    case 0x160: s.b = s.i; return;                    // LBI
    case 0x170: s.tc = s.i; s.prescaler = 0; return;  // LTI
    case 0x210: s.s = s.i != ramR(); return;          // MNEI
    case 0x270: s.s = s.a <= s.i; return;             // ALEI
    case 0x280: s.s = s.y != s.i; return;             // YNEI
    default: break;
    }
    switch (op & 0x3fc) {
    case 0x0c0: case 0x0c4: s.a = readR(op & 7); return;          // LAR
    case 0x0e0: case 0x0e4: s.b = readR(op & 7); return;          // LBR
    case 0x2c0: case 0x2c4: writeR(op & 7, s.a); return;          // LRA
    case 0x2e0: case 0x2e4: writeR(op & 7, s.b); return;          // LRB
    case 0x360: case 0x364: {                                      // TBR
        uint16_t a = uint16_t(s.a | s.b << 4 | s.c << 8 | (op & 7) << 9 | (s.pc & ~0x3f));
        s.pc = a & m_pcmask;
        return;
    }
    case 0x368: case 0x36c: {                                      // P
        cycle();
        uint16_t a = uint16_t(s.a | s.b << 4 | s.c << 8 | (op & 7) << 9 | (s.pc & ~0x3f));
        uint16_t data = word(a & m_prgmask);
        if (data & 0x100) { s.a = rev4(data); s.b = data >> 4 & 0xf; }
        if (data & 0x200) {
            uint8_t d = 0;
            for (int k = 0; k < 8; k++) d |= uint8_t((data >> k & 1) << (7 - k));
            writeR(2, d & 0xf);
            writeR(3, d >> 4 & 0xf);
        }
        return;
    }
    case 0x000: xsp(); return;                                      // XSP
    case 0x004: ramW(uint8_t(ramR() | (1 << (op & 3)))); return;   // SEM
    case 0x008: s.a = ramR(); xsp(); return;                        // LAM
    case 0x020: s.b = ramR(); xsp(); return;                        // LBM
    case 0x0d0: writeD(op & 3, 1); return;                          // SEDD
    case 0x200: s.s = ramR() >> (op & 3) & 1; return;              // TM
    case 0x204: ramW(uint8_t(ramR() & ~(1 << (op & 3)))); return;  // REM
    case 0x208: { uint8_t o = s.a; s.a = ramR(); ramW(o); xsp(); return; }   // XMA
    case 0x220: { uint8_t o = s.b; s.b = ramR(); ramW(o); xsp(); return; }   // XMB
    case 0x2d0: writeD(op & 3, 0); return;                          // REDD
    default: break;
    }
    switch (op) {
    case 0x024: s.s = s.b <= ramR(); return;                                         // BLEM
    case 0x030: s.a += ramR() + s.c; s.c = s.a >> 4 & 1; s.s = s.c; s.a &= 0xf; return;   // AMC
    case 0x034: s.a += ramR(); s.s = s.a >> 4 & 1; s.a &= 0xf; return;               // AM
    case 0x03c: s.tc = s.a; s.prescaler = 0; return;                                 // LTA
    case 0x040: s.x = s.a; return;                                                   // LXA
    case 0x045: if (!s.c || s.a > 9) { s.a = (s.a + 10) & 0xf; s.c = 0; } return;     // DAS
    case 0x046: if (s.c || s.a > 9) { s.a = (s.a + 6) & 0xf; s.c = 1; } return;      // DAA
    case 0x04c: s.c = 0; return;                                                     // REC
    case 0x04f: s.c = 1; return;                                                     // SEC
    case 0x050: s.y = s.a; return;                                                   // LYA
    case 0x054: iy(); return;                                                        // IY
    case 0x058: s.y += s.a; s.s = s.y >> 4 & 1; s.y &= 0xf; return;                  // AYY
    case 0x060: s.b = s.a; return;                                                   // LBA
    case 0x064: s.b = (s.b + 1) & 0xf; s.s = s.b != 0; return;                       // IB
    case 0x090: writeD(s.y, 1); return;                                              // SED
    case 0x094: s.s = uint8_t(readD(s.y)); return;                                   // TD
    case 0x0a0: s.ifl[1] = 1; return;                                                // SEIF1
    case 0x0a1: s.cf = 1; return;                                                    // SECF
    case 0x0a2: s.ifl[0] = 1; return;                                                // SEIF0
    case 0x0a4: s.ie = 1; return;                                                    // SEIE
    case 0x0a5: s.tf = 1; return;                                                    // SETF
    case 0x110: case 0x111: ramW(s.a); iy(); xsp(); return;                          // LMAIY
    case 0x114: case 0x115: ramW(s.a); dy(); xsp(); return;                          // LMADY
    case 0x118: s.a = s.y; return;                                                   // LAY
    case 0x120: s.a |= s.b; return;                                                  // OR
    case 0x124: s.s = s.a != ramR(); return;                                         // ANEM
    case 0x1a0: s.s = s.ifl[1]; return;                                              // TIF1
    case 0x1a1: s.s = s.intl[1]; return;                                             // TI1
    case 0x1a2: s.s = s.ifl[0]; return;                                              // TIF0
    case 0x1a3: s.s = s.intl[0]; return;                                             // TI0
    case 0x1a5: s.s = s.tf; return;                                                  // TTF
    case 0x224: { uint8_t c = s.a & 1; s.a = uint8_t(s.a >> 1 | s.c << 3); s.c = c; return; }   // ROTR
    case 0x225: s.a = uint8_t(s.a << 1 | s.c); s.c = s.a >> 4 & 1; s.a &= 0xf; return;           // ROTL
    case 0x230: { int v = int(ramR()) - s.a - (s.c ^ 1); s.c = uint8_t((~v >> 4) & 1); s.s = s.c; s.a = uint8_t(v & 0xf); return; }   // SMC
    case 0x234: s.s = s.a <= ramR(); return;                                         // ALEM
    case 0x23c: s.a = s.tc; return;                                                  // LAT
    case 0x240: s.a = s.spx; return;                                                 // LASPX
    case 0x244: s.a = (0 - s.a) & 0xf; return;                                       // NEGA
    case 0x24f: s.s = s.c; return;                                                   // TC
    case 0x250: s.a = s.spy; return;                                                 // LASPY
    case 0x254: dy(); return;                                                        // DY
    case 0x258: { int v = int(s.y) - s.a; s.s = uint8_t((~v >> 4) & 1); s.y = uint8_t(v & 0xf); return; }   // SYY
    case 0x260: s.a = s.b; return;                                                   // LAB
    case 0x267: s.b = (s.b - 1) & 0xf; s.s = s.b != 0xf; return;                     // DB
    case 0x290: writeD(s.y, 0); return;                                              // RED
    case 0x2a0: s.ifl[1] = 0; return;                                                // REIF1
    case 0x2a1: s.cf = 0; return;                                                    // RECF
    case 0x2a2: s.ifl[0] = 0; return;                                                // REIF0
    case 0x2a4: s.ie = 0; return;                                                    // REIE
    case 0x2a5: s.tf = 0; return;                                                    // RETF
    case 0x320: s.b ^= 0xf; return;                                                  // COMB
    case 0x324: s.s = s.b != ramR(); return;                                         // BNEM
    case 0x3a4: s.ie = 1; rtn(); return;                                             // RTNI
    case 0x3a7: rtn(); return;                                                       // RTN
    default: return;                                                                 // illegal
    }
}
