// PocketPlug - Rockwell AIM 65
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Port of MAME's aim65.cpp / aim65_m.cpp (GPL-2.0+). See aim65.h.

#include "aim65.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace {
constexpr int CPU_HZ = 1000000;
// DL1416T character set (MAME's dl1416.cpp), led16seg bit order
const uint16_t kSeg[64] = {
    0x0000, 0x2421, 0x0480, 0x0f3c, 0x0fbb, 0x5f99, 0xa579, 0x4000,
    0xc000, 0x3000, 0xff00, 0x0f00, 0x1000, 0x0300, 0x0020, 0x5000,
    0x0ce1, 0x0c00, 0x0561, 0x0d21, 0x0d80, 0x09a1, 0x09e1, 0x0c01,
    0x0de1, 0x0da1, 0x0021, 0x1001, 0x5030, 0x0330, 0xa030, 0x0a07,
    0x097f, 0x03cf, 0x0e3f, 0x00f3, 0x0c3f, 0x01f3, 0x01c3, 0x02fb,
    0x03cc, 0x0c33, 0x0c63, 0xc1c0, 0x00f0, 0x60cc, 0xa0cc, 0x00ff,
    0x03c7, 0x80ff, 0x83c7, 0x03bb, 0x0c03, 0x00fc, 0x50c0, 0x90cc,
    0xf000, 0x6800, 0x5033, 0x00e1, 0xa000, 0x001e, 0x9000, 0x0030 };
}

Aim65Core::Aim65Core()
{
    std::memset(m_rom, 0xff, sizeof(m_rom));
    std::memset(&m_s, 0, sizeof(m_s));
    m_out.assign(160 * 201, 0);
    m_cpu.setBus(this, rdThunk, wrThunk);
    m_via0.inB = [this]() -> uint8_t {
        // d6: TTY in (high, unused); d3: KB/TTY switch (KB)
        return uint8_t(0x08 | 0x40 | (m_s.pbSave & 0x37));
    };
    m_via0.outA = [this](uint8_t d) { printerPa(d); };
    m_via0.outB = [this](uint8_t d) { printerPb(d); };
    m_via0.ca2Out = [this](int s) { m_s.ca2 = uint8_t(s); };
    m_via0.cb2Out = [this](int s) { printerCb2(s); };
    m_via0.irqOut = [this](bool) { updateIrq(); };
    m_via1.irqOut = [this](bool) { updateIrq(); };
}

bool Aim65Core::load(const std::vector<RomFile>& roms, std::string& error)
{
    bool z23 = false, z22 = false;
    for (auto& r : roms) {
        if (r.name == "aim65mon.z23" && r.data.size() == 0x1000) { std::memcpy(m_rom, r.data.data(), 0x1000); z23 = true; }
        if (r.name == "aim65mon.z22" && r.data.size() == 0x1000) { std::memcpy(m_rom + 0x1000, r.data.data(), 0x1000); z22 = true; }
    }
    if (!z23 || !z22) { error = "Missing the monitor ROMs (aim65mon.z23, aim65mon.z22)"; return false; }
    reset();
    return true;
}

bool Aim65Core::mediaInsert(int drive, const std::string& path, std::string& error)
{
    if (drive < 3 || drive > 5) { error = "No such socket"; return false; }
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) { error = "Can't open the ROM"; return false; }
    std::vector<uint8_t> d(0x1001);
    size_t n = fread(d.data(), 1, d.size(), f);
    fclose(f);
    if (n == 0 || n > 0x1000) { error = "A socket ROM is at most 4 KB"; return false; }
    d.resize(n);
    m_sockets[drive - 3] = d;
    reset();
    return true;
}

void Aim65Core::mediaEject(int drive)
{
    if (drive >= 3 && drive <= 5) { m_sockets[drive - 3].clear(); reset(); }
}

void Aim65Core::reset()
{
    uint8_t keys[8];
    std::memcpy(keys, m_s.keys, 8);
    uint16_t paper[64 * 100];
    std::memcpy(paper, m_s.printerRam, sizeof(paper));
    int32_t py = m_s.printerY;
    std::memset(&m_s, 0, sizeof(m_s));
    std::memcpy(m_s.keys, keys, 8);
    std::memcpy(m_s.printerRam, paper, sizeof(paper));    // the paper stays
    m_s.printerY = py;
    m_s.riotPa = 0xff;
    m_s.riotTimer = 0x7fffffff;
    m_s.printerTimer = -1;
    m_s.ca2 = 1; m_s.cb2 = 1;
    for (int i = 0; i < 5; i++) { m_s.dsWr[i] = 1; m_s.dsCeIn[i] = 1; m_s.dsCeLatch[i] = 1; }
    m_s.dsCu = 1;
    m_via0.reset(); m_via1.reset();
    m_cpu.reset();
}

void Aim65Core::setInputs(uint32_t pressed)
{
    if ((pressed & 1) && !(m_resetBtn & 1)) {
        m_via0.reset(); m_via1.reset();
        m_s.piaOra = m_s.piaOrb = m_s.piaDdra = m_s.piaDdrb = m_s.piaCra = m_s.piaCrb = 0;
        m_s.riotPaDdr = m_s.riotPbDdr = 0; m_s.riotIeTimer = m_s.riotIeEdge = 0;
        m_cpu.reset();
    }
    m_resetBtn = pressed;
}

void Aim65Core::keyEvent(int code, bool down)
{
    if (code < 0 || code >= 64) return;
    if (down) m_s.keys[code >> 3] |= uint8_t(1 << (code & 7));
    else m_s.keys[code >> 3] &= uint8_t(~(1 << (code & 7)));
}

// ---------------- bus ----------------
uint8_t Aim65Core::read(uint16_t a)
{
    if (a < 0x1000) return m_s.ram[a];
    if (a >= 0xe000) return m_rom[a - 0xe000];
    if (a >= 0xb000 && a < 0xe000) {
        auto& sk = m_sockets[(a - 0xb000) >> 12];        // B000 = Z26 (drive 3) ... D000 = Z24 (drive 5)
        return (a & 0xfff) < sk.size() ? sk[a & 0xfff] : 0xff;
    }
    if (a >= 0xa000 && a < 0xa400) return m_via1.read(a & 15);
    if (a >= 0xa400 && a < 0xa480) return m_s.riotRam[a & 0x7f];
    if (a >= 0xa480 && a < 0xa498) return riotRead((a - 0xa480) & 0x1f);
    if (a >= 0xa800 && a < 0xac00) return m_via0.read(a & 15);
    if (a >= 0xac00 && a < 0xac04) return piaRead(a & 3);
    if (a >= 0xac04 && a < 0xac44) return m_s.piaRam[a - 0xac04];
    return 0xff;
}

void Aim65Core::write(uint16_t a, uint8_t d)
{
    if (a < 0x1000) { m_s.ram[a] = d; return; }
    if (a >= 0xa000 && a < 0xa400) { m_via1.write(a & 15, d); return; }
    if (a >= 0xa400 && a < 0xa480) { m_s.riotRam[a & 0x7f] = d; return; }
    if (a >= 0xa480 && a < 0xa498) { riotWrite((a - 0xa480) & 0x1f, d); return; }
    if (a >= 0xa800 && a < 0xac00) { m_via0.write(a & 15, d); return; }
    if (a >= 0xac00 && a < 0xac04) { piaWrite(a & 3, d); return; }
    if (a >= 0xac04 && a < 0xac44) { m_s.piaRam[a - 0xac04] = d; return; }
}

void Aim65Core::updateIrq()
{
    bool riot = (m_s.riotIeTimer && m_s.riotIrqTimer) || (m_s.riotIeEdge && m_s.riotIrqEdge);
    m_cpu.setIrq(riot || m_via0.s.irq || m_via1.s.irq);
}

// ---------------- 6532 RIOT (keyboard) ----------------
uint8_t Aim65Core::riotRead(int off)
{
    switch (off & 0x07) {
    case 0x00: return uint8_t((m_s.riotPa & m_s.riotPaDdr) | (0xff & ~m_s.riotPaDdr));
    case 0x01: return m_s.riotPaDdr;
    case 0x02: {
        // keyboard columns of the rows selected by port A (active low)
        uint8_t sel = uint8_t((m_s.riotPa & m_s.riotPaDdr) | ~m_s.riotPaDdr);
        uint8_t d = 0xff;
        for (int i = 0; i < 8; i++) if (!(sel >> i & 1)) d &= uint8_t(~m_s.keys[i]);
        return uint8_t((d & ~m_s.riotPbDdr) | (m_s.riotPb & m_s.riotPbDdr));
    }
    case 0x03: return m_s.riotPbDdr;
    case 0x04: case 0x06: {
        // timer read (A3 = interrupt enable)
        int shift = m_s.riotSpinning ? 0 : m_s.riotShift;
        int remain = m_s.riotTimer;
        uint8_t val = uint8_t(remain >> shift);
        if (!(remain & ((1 << shift) - 1))) val = uint8_t(val - 1);
        m_s.riotIrqTimer = 0;
        if (m_s.riotSpinning) { m_s.riotSpinning = 0; m_s.riotTimer = 1 + (val << m_s.riotShift); }
        m_s.riotIeTimer = (off & 0x08) ? 1 : 0;
        updateIrq();
        return val;
    }
    default: {
        uint8_t d = uint8_t((m_s.riotIrqTimer ? 0x80 : 0) | (m_s.riotIrqEdge ? 0x40 : 0));
        m_s.riotIrqEdge = 0;
        updateIrq();
        return d;
    }
    }
}

void Aim65Core::riotWrite(int off, uint8_t d)
{
    if (off & 0x04) {
        if (off & 0x10) {
            static const uint8_t shift[4] = { 0, 3, 6, 10 };
            m_s.riotShift = shift[off & 3];
            m_s.riotSpinning = 0;
            m_s.riotTimer = 1 + (d << m_s.riotShift);
            m_s.riotIrqTimer = 0;
            m_s.riotIeTimer = (off & 0x08) ? 1 : 0;
        } else {
            m_s.riotIeEdge = (off >> 1) & 1;   // PA7 edge control (PA7 is a key row here)
        }
        updateIrq();
        return;
    }
    switch (off & 3) {
    case 0: m_s.riotPa = d; break;
    case 1: m_s.riotPaDdr = d; break;
    case 2: m_s.riotPb = d; break;
    case 3: m_s.riotPbDdr = d; break;
    }
}

void Aim65Core::riotTick(int n)
{
    m_s.riotTimer -= n;
    while (m_s.riotTimer <= 0) {
        if (!m_s.riotSpinning) { m_s.riotIrqTimer = 1; updateIrq(); }
        m_s.riotSpinning = 1;
        m_s.riotTimer += 256;
    }
}

// ---------------- 6821 PIA (displays) ----------------
uint8_t Aim65Core::piaRead(int r)
{
    switch (r) {
    case 0: return (m_s.piaCra & 4) ? uint8_t(m_s.piaOra | ~m_s.piaDdra) : m_s.piaDdra;
    case 1: return m_s.piaCra & 0x3f;
    case 2: return (m_s.piaCrb & 4) ? uint8_t(m_s.piaOrb | ~m_s.piaDdrb) : m_s.piaDdrb;
    default: return m_s.piaCrb & 0x3f;
    }
}

void Aim65Core::piaWrite(int r, uint8_t d)
{
    switch (r) {
    case 0:
        if (m_s.piaCra & 4) { m_s.piaOra = d; displayPa(uint8_t((d & m_s.piaDdra) | ~m_s.piaDdra)); }
        else m_s.piaDdra = d;
        break;
    case 1: m_s.piaCra = d & 0x3f; break;
    case 2:
        if (m_s.piaCrb & 4) { m_s.piaOrb = d; displayPb(uint8_t((d & m_s.piaDdrb) | ~m_s.piaDdrb)); }
        else m_s.piaDdrb = d;
        break;
    default: m_s.piaCrb = d & 0x3f; break;
    }
}

void Aim65Core::displayPa(uint8_t d)
{
    // as MAME's u1_pa_w: WR first (latching the previous CE / address), then CE and address
    for (int i = 0; i < 5; i++) {
        int wr = (d >> 7) & 1;
        if (wr != m_s.dsWr[i]) {
            m_s.dsWr[i] = uint8_t(wr);
            if (wr) {
                if (!m_s.dsCeLatch[i]) {
                    int off = m_s.dsAddrLatch[i];
                    if (!m_s.dsCu) for (int k = 0; k < 4; k++) m_s.dsCursor[i][k] = (m_s.dsData >> k) & 1;
                    else m_s.dsRam[i][off] = m_s.dsData & 0x7f;
                }
            } else {
                m_s.dsCeLatch[i] = m_s.dsCeIn[i];
                m_s.dsAddrLatch[i] = m_s.dsAddrIn[i];
            }
        }
        m_s.dsCeIn[i] = (d >> (2 + i)) & 1;
        m_s.dsAddrIn[i] = d & 3;
    }
}

void Aim65Core::displayPb(uint8_t d)
{
    m_s.dsCu = (d >> 7) & 1;
    m_s.dsData = d & 0x7f;
}

// ---------------- thermal printer (MAME's model) ----------------
void Aim65Core::printerPb(uint8_t d)
{
    if ((d & 0x30) == 0x30) d &= ~0x30;
    m_s.pbSave = d;
    d &= 0x03;
    if (m_s.printerFlag == 1 && !m_s.cb2) m_s.printerRam[m_s.printerY * 100 + m_s.printerX] |= uint16_t(d << 8);
}

void Aim65Core::printerCb2(int state)
{
    m_s.cb2 = uint8_t(state);
    if (!state) {
        m_s.printerX = 0;
        m_s.printerY = (m_s.printerY + 1) & 63;
        for (int i = 0; i < 100; i++) m_s.printerRam[m_s.printerY * 100 + i] = 0;
        m_s.printerFlag = 0;
        m_via0.setCb1(1);
        m_s.printerTimer = 0;           // fires now, then every millisecond
        m_s.printerLevel = 1;
    } else m_s.printerTimer = -1;
}

void Aim65Core::printerPa(uint8_t d)
{
    if (m_s.printerFlag == 1) m_s.printerRam[m_s.printerY * 100 + m_s.printerX] |= d;
    if (++m_s.printerFlag > 2) { m_s.printerFlag = 0; m_s.printerX++; }
    if ((m_s.printerX % 10) == 0) { m_via0.setCa1(m_s.printerLevel); m_s.printerLevel ^= 1; }
    if (m_s.printerX == 100) { m_s.printerX = 0; m_via0.setCb1(0); }
}

void Aim65Core::renderPrinter()
{
    const int H = 200;
    for (int y = 0; y < H; y++) {
        uint32_t* dst = &m_out[y * 160];
        std::fill(dst, dst + 160, 0u);
        uint16_t sy = uint16_t(m_s.printerY * 10 - H + 10 + y);
        if (sy > 0xfe80) sy = uint16_t(sy + 0x280);
        for (int x = 0; x < 10; x++) {
            int idx = (sy & 1) ? sy * 10 + (9 - x) : sy * 10 + x;
            uint16_t data = (idx >= 0 && idx < 64 * 100) ? m_s.printerRam[idx] : 0;
            for (int b = 0; b < 10; b++) {
                int px = 120 - (b * 12) - ((x > 4) ? x + 1 : x);
                if (px >= 0 && px < 160) dst[px] = (data >> b) & 1;
            }
        }
    }
}

// ---------------- frame ----------------
void Aim65Core::runFrame()
{
    const int perFrame = CPU_HZ / 60;
    int done = 0;
    while (done < perFrame) {
        int n = m_cpu.step();
        done += n;
        m_via0.tick(n);
        m_via1.tick(n);
        riotTick(n);
        if (m_s.printerTimer >= 0) {
            m_s.printerTimer -= n;
            while (m_s.printerTimer <= 0) {
                m_via0.setCb1(m_s.printerLevel);
                m_via0.setCa1(m_s.printerLevel);
                m_s.printerLevel ^= 1;
                m_s.printerTimer += 1000;          // every millisecond while CB2 is low
            }
        }
    }
    renderPrinter();
    for (int d = 0; d < 20; d++) {
        int disp = d >> 2, off = (d & 3) ^ 3;
        uint8_t ch = m_s.dsRam[disp][off];
        bool cur = m_s.dsCursor[disp][off];
        uint32_t seg = (cur && ch >= 0x20 && ch <= 0x5f) ? 0xffff : (ch >= 0x20 && ch < 0x60 ? kSeg[ch - 0x20] : 0);
        m_out[200 * 160 + d] = seg;
    }
    m_audioDue += 735;
}

size_t Aim65Core::readAudio(int16_t* out, size_t maxSamples)
{
    size_t n = std::min<size_t>(maxSamples, m_audioDue);
    std::memset(out, 0, n * sizeof(int16_t));
    m_audioDue -= uint32_t(n);
    return n;
}

namespace { struct Regs { uint16_t pc; uint8_t a, x, y, s, p, irq, nmi; }; }

bool Aim65Core::saveState(std::vector<uint8_t>& out)
{
    out.clear();
    uint32_t magic = 0x35364d41;   // 'AM65'
    Regs c = { m_cpu.pc, m_cpu.a, m_cpu.x, m_cpu.y, m_cpu.s, m_cpu.p, m_cpu.m_irqLine, m_cpu.m_nmiPending };
    auto put = [&](const void* p, size_t n) { const uint8_t* b = (const uint8_t*)p; out.insert(out.end(), b, b + n); };
    put(&magic, 4); put(&m_s, sizeof(m_s)); put(&c, sizeof(c)); put(&m_via0.s, sizeof(m_via0.s)); put(&m_via1.s, sizeof(m_via1.s));
    return true;
}

bool Aim65Core::loadState(const std::vector<uint8_t>& in)
{
    Regs c;
    if (in.size() != 4 + sizeof(m_s) + sizeof(c) + 2 * sizeof(m_via0.s)) return false;
    uint32_t magic; std::memcpy(&magic, in.data(), 4);
    if (magic != 0x35364d41) return false;
    const uint8_t* p = in.data() + 4;
    std::memcpy(&m_s, p, sizeof(m_s)); p += sizeof(m_s);
    std::memcpy(&c, p, sizeof(c)); p += sizeof(c);
    std::memcpy(&m_via0.s, p, sizeof(m_via0.s)); p += sizeof(m_via0.s);
    std::memcpy(&m_via1.s, p, sizeof(m_via1.s));
    m_cpu.pc = c.pc; m_cpu.a = c.a; m_cpu.x = c.x; m_cpu.y = c.y; m_cpu.s = c.s; m_cpu.p = c.p;
    m_cpu.m_irqLine = c.irq; m_cpu.m_nmiPending = c.nmi;
    return true;
}
