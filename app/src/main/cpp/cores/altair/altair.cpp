// PocketPlug - Altair 8800b and 8800b Turnkey
// SPDX-License-Identifier: GPL-2.0-or-later
#include "altair.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
// panel momentary bits
enum { P_STOP = 16, P_RUN, P_STEP, P_EXAM, P_EXAMNEXT, P_DEP, P_DEPNEXT, P_RESET, P_CLR, P_PROT, P_UNPROT,
       P_DISPACC, P_LOADACC, P_INACC, P_OUTACC };
// status LEDs (index into the tally / row 25 offset 24)
enum { S_INTE, S_PROT, S_MEMR, S_INP, S_M1, S_OUT, S_HLTA, S_STACK, S_WO, S_INT, S_WAIT, S_HLDA };
const int TX_CYCLES = 2000000 / 960;    // one character at 9600 baud
}

bool AltairCore::load(const std::vector<RomFile>& roms, std::string& error)
{
    auto find = [&](const char* name, size_t idx) -> const RomFile* {
        for (auto& r : roms) if (r.name == name) return &r;
        return idx < roms.size() ? &roms[idx] : nullptr;
    };
    if (m_turnkey) {
        const RomFile* mon = find("turnmon.bin", 0);
        const RomFile* dbl = find("88dskrom.bin", 1);
        if (!mon || mon->data.size() < 256 || !dbl || dbl->data.size() < 256) { error = "turnmon.bin and 88dskrom.bin are needed"; return false; }
        std::memcpy(m_monRom, mon->data.data(), 256);
        std::memcpy(m_dblRom, dbl->data.data(), 256);
    } else {
        // the panel's microcode PROM isn't run: its behaviour is built in
        std::memset(m_monRom, 0xff, 256);
        std::memset(m_dblRom, 0xff, 256);
    }
    m_cpu.ctx = this;
    m_cpu.rd = rdThunk;
    m_cpu.wr = wrThunk;
    m_cpu.in = inThunk;
    m_cpu.out = outThunk;
    std::memset(&m_s, 0, sizeof(m_s));
    reset();
    return true;
}

void AltairCore::reset()
{
    std::memset(m_s.ram, 0, sizeof(m_s.ram));
    std::memset(m_s.screen, ' ', sizeof(m_s.screen));
    std::memset(m_s.attr, 0, sizeof(m_s.attr));
    m_s.cx = m_s.cy = 0;
    m_s.esc = 0;
    m_s.top = 0; m_s.bottom = 23; m_s.curAttr = 0;
    m_s.aciaRdrf = 0;
    m_s.aciaTxBusy = 0;
    m_s.aciaCtrl = 0;
    m_s.dsel = -1;
    for (int d = 0; d < 4; d++) { m_s.track[d] = 0; m_s.sector[d] = 0; m_s.byte[d] = 0xff; m_s.writeIdx[d] = SECLEN; m_s.flag[d] = 0; }
    m_keys.clear();
    m_cpu.reset();
    if (m_turnkey) { m_cpu.r.pc = 0xfd00; m_s.running = 1; }
    else { m_cpu.r.pc = 0; m_s.running = 0; }
    render();
}

// ---------------- memory ----------------
uint8_t AltairCore::peek(uint16_t a) const
{
    if (m_turnkey && a >= 0xfd00) {
        if (a < 0xfe00) return m_monRom[a & 0xff];
        if (a >= 0xff00) return m_dblRom[a & 0xff];
        return 0xff;
    }
    return m_s.ram[a];
}

void AltairCore::poke(uint16_t a, uint8_t d)
{
    if (m_turnkey && a >= 0xfd00) return;
    m_s.ram[a] = d;
}

void AltairCore::tally(uint16_t a, uint8_t d, int status)
{
    for (int i = 0; i < 16; i++) m_hitA[i] += (a >> i) & 1;
    for (int i = 0; i < 8; i++) m_hitD[i] += (d >> i) & 1;
    for (int i = 0; i < 12; i++) m_hitS[i] += (status >> i) & 1;
    m_hitN++;
}

uint8_t AltairCore::busRead(uint16_t a, I8080::Access k)
{
    uint8_t d = peek(a);
    int st = (1 << S_MEMR) | (1 << S_WO);
    if (k == I8080::FETCH) st |= 1 << S_M1;
    if (k == I8080::STACK_READ) st |= 1 << S_STACK;
    tally(a, d, st);
    return d;
}

void AltairCore::busWrite(uint16_t a, uint8_t d, I8080::Access k)
{
    int st = k == I8080::STACK_WRITE ? 1 << S_STACK : 0;
    tally(a, d, st);
    poke(a, d);
}

// ---------------- I/O ----------------
uint8_t AltairCore::ioIn(uint8_t p)
{
    uint8_t v = 0xff;
    if ((p & 0xef) == 0x00) {           // 2SIO status
        v = uint8_t((m_s.aciaRdrf ? 0x01 : 0) | (m_s.aciaTxBusy <= 0 ? 0x02 : 0)
            | ((m_s.aciaCtrl & 0x80) && m_s.aciaRdrf ? 0x80 : 0));
    } else if ((p & 0xef) == 0x01) {    // 2SIO data
        v = m_s.aciaRx;
        m_s.aciaRdrf = 0;
    } else if (p == 0x08) {             // disk status
        v = m_s.dsel < 0 ? 0xff : uint8_t(~m_s.flag[m_s.dsel]);
    } else if (p == 0x09) {             // sector position
        int d = m_s.dsel;
        if (d >= 0 && (m_s.flag[d] & 0x04)) {
            m_s.sectorTrue[d] ^= 1;
            if (m_s.sectorTrue[d] == 0) {
                diskFlush();
                m_s.sector[d] = (m_s.sector[d] + 1) % SECTORS;
                m_s.byte[d] = 0xff;
            }
            v = uint8_t(((m_s.sector[d] << 1) & 0x3e) | 0xc0 | m_s.sectorTrue[d]);
        }
    } else if (p == 0x0a) {             // read data
        int d = m_s.dsel;
        if (d >= 0 && !m_disk[d].empty()) {
            if (m_s.byte[d] >= SECLEN) {
                size_t off = (size_t(m_s.track[d]) * SECTORS + m_s.sector[d]) * SECLEN;
                std::memcpy(m_s.buf[d], m_disk[d].data() + off, SECLEN);
                m_s.byte[d] = 0;
            }
            v = m_s.buf[d][m_s.byte[d]++];
            m_diskLight = 8;
        } else v = 0;
    } else if (p == 0xff) {             // sense switches
        v = m_turnkey ? 0x00 : uint8_t(m_inputs >> 8);
    }
    tally(uint16_t(p << 8 | p), v, (1 << S_INP) | (1 << S_WO));
    return v;
}

void AltairCore::ioOut(uint8_t p, uint8_t d)
{
    tally(uint16_t(p << 8 | p), d, 1 << S_OUT);
    if ((p & 0xef) == 0x00) {
        if ((d & 3) == 3) { m_s.aciaRdrf = 0; m_s.aciaTxBusy = 0; }
        m_s.aciaCtrl = d;
    } else if ((p & 0xef) == 0x01) {
        termPut(d);
        m_s.aciaTxBusy = TX_CYCLES;
    } else if (p == 0x08) {             // drive select
        diskFlush();
        if (d & 0x80) { m_s.dsel = -1; return; }
        int n = d & 0x0f;
        if (n > 3 || m_disk[n].empty()) { m_s.dsel = -1; return; }
        m_s.dsel = n;
        m_s.sector[n] = 0;
        m_s.byte[n] = 0xff;
        m_s.flag[n] = 0x1a;
        if (m_s.track[n] == 0) m_s.flag[n] |= 0x40;
    } else if (p == 0x09) {             // disk control
        int n = m_s.dsel;
        if (n < 0) return;
        if (d & 0x01) {                 // step in
            diskFlush();
            if (m_s.track[n] < TRACKS - 1) m_s.track[n]++;
            m_s.flag[n] &= ~0x40;
            m_s.byte[n] = 0xff;
        }
        if (d & 0x02) {                 // step out
            diskFlush();
            if (m_s.track[n] > 0) m_s.track[n]--;
            if (m_s.track[n] == 0) m_s.flag[n] |= 0x40;
            m_s.byte[n] = 0xff;
        }
        if (d & 0x04) m_s.flag[n] |= 0x84;                  // head load: data available
        if (d & 0x08) { diskFlush(); m_s.flag[n] &= ~0x84; m_s.byte[n] = 0xff; }
        if (d & 0x80) { m_s.writeIdx[n] = 0; m_s.flag[n] |= 0x01; }   // write enable
    } else if (p == 0x0a) {             // write data
        int n = m_s.dsel;
        if (n < 0 || m_s.writeIdx[n] >= SECLEN) return;
        m_s.wbuf[m_s.writeIdx[n]++] = d;
        m_diskLight = 8;
        if (m_s.writeIdx[n] >= SECLEN) diskFlush();
    }
}

void AltairCore::diskFlush()
{
    int n = m_s.dsel;
    if (n < 0 || m_s.writeIdx[n] == SECLEN && !(m_s.flag[n] & 0x01)) return;
    if (m_s.writeIdx[n] > 0 && !m_disk[n].empty()) {
        // a short write is padded with zeros, as SIMH does
        for (int i = m_s.writeIdx[n]; i < SECLEN; i++) m_s.wbuf[i] = 0;
        size_t off = (size_t(m_s.track[n]) * SECTORS + m_s.sector[n]) * SECLEN;
        std::memcpy(m_disk[n].data() + off, m_s.wbuf, SECLEN);
        m_diskDirty[n] = true;
    }
    m_s.writeIdx[n] = SECLEN;
    m_s.flag[n] &= ~0x01;
}

// ---------------- terminal ----------------
// VT100 (the default: most CP/M disks around are set up for it) or ADM-3A
// style codes (ESC = row col, ^Z clear, ^K up, ^L right, Soroc ESC * etc.)
void AltairCore::termClear(int r0, int c0, int r1, int c1)
{
    for (int r = r0; r <= r1; r++)
        for (int c = (r == r0 ? c0 : 0); c <= (r == r1 ? c1 : 79); c++) { m_s.screen[r][c] = ' '; m_s.attr[r][c] = 0; }
}

void AltairCore::termScroll(int dir)
{
    int t = m_s.top, b = m_s.bottom;
    if (dir > 0) {
        std::memmove(m_s.screen[t], m_s.screen[t + 1], size_t(b - t) * 80);
        std::memmove(m_s.attr[t], m_s.attr[t + 1], size_t(b - t) * 80);
        termClear(b, 0, b, 79);
    } else {
        std::memmove(m_s.screen[t + 1], m_s.screen[t], size_t(b - t) * 80);
        std::memmove(m_s.attr[t + 1], m_s.attr[t], size_t(b - t) * 80);
        termClear(t, 0, t, 79);
    }
}

void AltairCore::termLf()
{
    if (m_s.cy == m_s.bottom) termScroll(1);
    else if (m_s.cy < 23) m_s.cy++;
}

void AltairCore::termCsi(uint8_t f)
{
    auto P = [&](int i, int def) { int v = i < m_s.npar ? m_s.par[i] : 0; return v ? v : def; };
    int n = P(0, 1);
    switch (f) {
    case 'H': case 'f':
        m_s.cy = std::clamp(P(0, 1) - 1, 0, 23);
        m_s.cx = std::clamp(P(1, 1) - 1, 0, 79);
        break;
    case 'A': m_s.cy = std::max(m_s.cy - n, 0); break;
    case 'B': m_s.cy = std::min(m_s.cy + n, 23); break;
    case 'C': m_s.cx = std::min(m_s.cx + n, 79); break;
    case 'D': m_s.cx = std::max(std::min(m_s.cx, 79) - n, 0); break;
    case 'J': {
        int cx = std::min<int>(m_s.cx, 79);
        int m = m_s.npar ? m_s.par[0] : 0;
        if (m == 0) termClear(m_s.cy, cx, 23, 79);
        else if (m == 1) termClear(0, 0, m_s.cy, cx);
        else termClear(0, 0, 23, 79);
        break;
    }
    case 'K': {
        int cx = std::min<int>(m_s.cx, 79);
        int m = m_s.npar ? m_s.par[0] : 0;
        if (m == 0) termClear(m_s.cy, cx, m_s.cy, 79);
        else if (m == 1) termClear(m_s.cy, 0, m_s.cy, cx);
        else termClear(m_s.cy, 0, m_s.cy, 79);
        break;
    }
    case 'm':
        if (!m_s.npar) m_s.curAttr = 0;
        for (int i = 0; i < m_s.npar; i++) {
            int v = m_s.par[i];
            if (v == 0) m_s.curAttr = 0;
            else if (v == 1) m_s.curAttr |= 2;
            else if (v == 4) m_s.curAttr |= 4;
            else if (v == 7) m_s.curAttr |= 1;
        }
        break;
    case 'r':
        m_s.top = std::clamp(P(0, 1) - 1, 0, 23);
        m_s.bottom = std::clamp(P(1, 24) - 1, 0, 23);
        if (m_s.bottom <= m_s.top) { m_s.top = 0; m_s.bottom = 23; }
        m_s.cx = 0; m_s.cy = 0;
        break;
    case 'L': case 'M': {            // insert / delete lines within the scroll region
        if (m_s.cy < m_s.top || m_s.cy > m_s.bottom) break;
        int t = m_s.top;
        m_s.top = m_s.cy;
        for (int i = 0; i < n; i++) termScroll(f == 'L' ? -1 : 1);
        m_s.top = t;
        break;
    }
    case 'P': {                      // delete characters
        int cx = std::min<int>(m_s.cx, 79);
        n = std::min(n, 80 - cx);
        std::memmove(&m_s.screen[m_s.cy][cx], &m_s.screen[m_s.cy][cx + n], size_t(80 - cx - n));
        std::memmove(&m_s.attr[m_s.cy][cx], &m_s.attr[m_s.cy][cx + n], size_t(80 - cx - n));
        termClear(m_s.cy, 80 - n, m_s.cy, 79);
        break;
    }
    case '@': {                      // insert blanks
        int cx = std::min<int>(m_s.cx, 79);
        n = std::min(n, 80 - cx);
        std::memmove(&m_s.screen[m_s.cy][cx + n], &m_s.screen[m_s.cy][cx], size_t(80 - cx - n));
        std::memmove(&m_s.attr[m_s.cy][cx + n], &m_s.attr[m_s.cy][cx], size_t(80 - cx - n));
        termClear(m_s.cy, cx, m_s.cy, cx + n - 1);
        break;
    }
    case 'n':                         // cursor position report
        if (P(0, 0) == 6) {
            char rep[32];
            snprintf(rep, sizeof(rep), "\x1b[%d;%dR", m_s.cy + 1, std::min<int>(m_s.cx, 79) + 1);
            for (char* q = rep; *q; q++) m_keys.push_back(uint8_t(*q));
        } else if (P(0, 0) == 5) { m_keys.push_back(27); m_keys.push_back('['); m_keys.push_back('0'); m_keys.push_back('n'); }
        break;
    case 'c':                         // identify: a VT100
        for (const char* q = "\x1b[?1;0c"; *q; q++) m_keys.push_back(uint8_t(*q));
        break;
    default: break;
    }
}

void AltairCore::termPut(uint8_t c)
{
    c &= 0x7f;
    if (c == 0 || c == 0x7f) return;
    bool vt = m_s.vtMode == 0;
    if (m_s.esc == 4) {               // CSI parameters
        if (c >= '0' && c <= '9') {
            if (m_s.npar == 0) m_s.npar = 1;
            int& p = m_s.par[m_s.npar - 1];
            p = std::min(p * 10 + (c - '0'), 999);
            return;
        }
        if (c == ';') { if (m_s.npar == 0) m_s.npar = 1; if (m_s.npar < 8) m_s.par[m_s.npar++] = 0; return; }
        if (c == '?') { m_s.priv = 1; return; }
        if (c < 0x20) { /* control inside a sequence: act on it */ }
        else { m_s.esc = 0; if (!m_s.priv) termCsi(c); return; }
    }
    if (m_s.esc == 5) { m_s.esc = 0; return; }          // character set selection
    if (m_s.esc == 1) {
        m_s.esc = 0;
        if (vt) {
            switch (c) {
            case '[': m_s.esc = 4; m_s.npar = 0; m_s.priv = 0; std::memset(m_s.par, 0, sizeof(m_s.par)); return;
            case '7': m_s.saveX = m_s.cx; m_s.saveY = m_s.cy; return;
            case '8': m_s.cx = m_s.saveX; m_s.cy = m_s.saveY; return;
            case 'D': termLf(); return;
            case 'E': m_s.cx = 0; termLf(); return;
            case 'M': if (m_s.cy == m_s.top) termScroll(-1); else if (m_s.cy > 0) m_s.cy--; return;
            case 'c': termClear(0, 0, 23, 79); m_s.cx = m_s.cy = 0; m_s.top = 0; m_s.bottom = 23; m_s.curAttr = 0; return;
            case '(': case ')': m_s.esc = 5; return;
            default: return;          // ESC = / ESC > keypad modes and the rest
            }
        }
        switch (c) {
        case '=': m_s.esc = 2; return;                       // cursor address: row, column + 32
        case '*': case '+': case ':':                        // clear screen
            termClear(0, 0, 23, 79); m_s.cx = m_s.cy = 0; return;
        case 'T': case 't': termClear(m_s.cy, std::min<int>(m_s.cx, 79), m_s.cy, 79); return;
        case 'Y': case 'y': termClear(m_s.cy, std::min<int>(m_s.cx, 79), 23, 79); return;
        default: return;
        }
    }
    if (m_s.esc == 2) { m_s.escRow = c - 32; m_s.esc = 3; return; }
    if (m_s.esc == 3) {
        m_s.esc = 0;
        m_s.cy = std::clamp<int32_t>(m_s.escRow, 0, 23);
        m_s.cx = std::clamp<int32_t>(c - 32, 0, 79);
        return;
    }
    switch (c) {
    case 0x07: m_s.bell = 4410; m_s.bellCount++; return;
    case 0x08: if (m_s.cx > 0) m_s.cx = std::min<int>(m_s.cx, 79) - 1; return;
    case 0x09: m_s.cx = std::min(79, (std::min<int>(m_s.cx, 79) + 8) & ~7); return;
    case 0x0a: termLf(); return;
    case 0x0b: if (vt) termLf(); else if (m_s.cy > 0) m_s.cy--; return;
    case 0x0c: if (vt) termLf(); else if (m_s.cx < 79) m_s.cx++; return;
    case 0x0d: m_s.cx = 0; return;
    case 0x1a: if (!vt) { termClear(0, 0, 23, 79); m_s.cx = m_s.cy = 0; } return;
    case 0x1b: m_s.esc = 1; return;
    case 0x1e: if (!vt) m_s.cx = m_s.cy = 0; return;
    default: break;
    }
    if (c < 0x20) return;
    if (m_s.cx > 79) {                  // wrap
        m_s.cx = 0;
        termLf();
    }
    m_s.attr[m_s.cy][m_s.cx] = uint8_t(m_s.curAttr);
    m_s.screen[m_s.cy][m_s.cx++] = c;
}

void AltairCore::setOption(int id, int value)
{
    if (id == 0) m_s.vtMode = value ? 1 : 0;
}

void AltairCore::keyEvent(int code, bool down)
{
    if (!down || code <= 0 || code > 0x7f) return;
    if (m_keys.size() < 256) m_keys.push_back(uint8_t(code));
}

// ---------------- front panel ----------------
void AltairCore::setInputs(uint32_t pressed)
{
    uint32_t rise = pressed & ~m_inputs;
    m_inputs = pressed;
    for (int b = 16; b < 31; b++) if (rise & (1u << b)) panelEdge(b);
}

void AltairCore::panelEdge(int bit)
{
    uint16_t sw = uint16_t(m_inputs);
    bool stopped = !m_s.running;
    switch (bit) {
    case P_STOP: m_s.running = 0; break;
    case P_RUN: m_s.running = 1; break;
    case P_STEP: if (stopped) { m_cpu.r.halted = 0; m_cpu.step(); } break;
    case P_EXAM: if (stopped) { m_cpu.r.pc = sw; m_cpu.r.halted = 0; } break;
    case P_EXAMNEXT: if (stopped) m_cpu.r.pc++; break;
    case P_DEP: if (stopped && !m_s.protect) poke(m_cpu.r.pc, uint8_t(sw)); break;
    case P_DEPNEXT: if (stopped) { m_cpu.r.pc++; if (!m_s.protect) poke(m_cpu.r.pc, uint8_t(sw)); } break;
    case P_RESET:
        m_cpu.reset();
        if (m_turnkey) m_cpu.r.pc = 0xfd00;
        m_s.aciaRdrf = 0; m_s.aciaTxBusy = 0;
        break;
    case P_CLR: m_s.aciaRdrf = 0; m_s.aciaTxBusy = 0; m_s.aciaCtrl = 0; break;
    case P_PROT: m_s.protect = 1; break;
    case P_UNPROT: m_s.protect = 0; break;
    case P_LOADACC: if (stopped) m_cpu.r.a = uint8_t(sw); break;
    case P_INACC: if (stopped) m_cpu.r.a = ioIn(uint8_t(sw >> 8)); break;
    case P_OUTACC: if (stopped) ioOut(uint8_t(sw >> 8), m_cpu.r.a); break;
    default: break;
    }
}

// ---------------- frame ----------------
void AltairCore::runCycles(int cycles)
{
    int done = 0;
    while (done < cycles) {
        m_cpu.r.irq = (m_s.aciaCtrl & 0x80) && m_s.aciaRdrf ? 1 : 0;
        m_cpu.r.irqVector = 0xff;      // RST 7
        int n = m_cpu.step();
        if (m_cpu.r.halted) tally(0xffff, 0xff, 1 << S_HLTA | 1 << S_WO);
        done += n;
        if (m_s.aciaTxBusy > 0) m_s.aciaTxBusy -= n;
        if (m_s.aciaRxWait > 0) m_s.aciaRxWait -= n;
        if (!m_s.aciaRdrf && m_s.aciaRxWait <= 0 && !m_keys.empty()) {
            m_s.aciaRx = m_keys.front();
            m_keys.pop_front();
            m_s.aciaRdrf = 1;
            m_s.aciaRxWait = TX_CYCLES;
        }
    }
}

void AltairCore::runFrame()
{
    std::fill(m_hitA, m_hitA + 16, 0u);
    std::fill(m_hitD, m_hitD + 8, 0u);
    std::fill(m_hitS, m_hitS + 12, 0u);
    m_hitN = 0;
    if (m_s.running) runCycles(CPU_HZ / 60);
    if (m_diskLight > 0) m_diskLight--;
    m_audioDue += 735;
    render();
}

void AltairCore::render()
{
    for (int r = 0; r < 24; r++)
        for (int c = 0; c < 80; c++) m_out[r * 80 + c] = m_s.screen[r][c] | uint32_t(m_s.attr[r][c]) << 8;
    uint32_t* st = &m_out[24 * 80];
    std::fill(st, st + 80, 0u);
    st[0] = uint32_t(m_s.cx > 79 ? 79 : m_s.cx);
    st[1] = uint32_t(m_s.cy);
    st[2] = m_s.running;
    st[3] = m_diskLight > 0;
    st[4] = m_s.protect;
    st[5] = m_s.bellCount;
    uint32_t* led = &m_out[25 * 80];
    std::fill(led, led + 160, 0u);
    auto lvl = [](uint32_t hits, uint32_t n) -> uint32_t {
        if (!n || !hits) return 0;
        return uint32_t(std::lround(255.0 * std::pow(double(hits) / n, 0.6)));
    };
    if (m_s.running && m_hitN) {
        for (int i = 0; i < 16; i++) led[i] = lvl(m_hitA[i], m_hitN);
        for (int i = 0; i < 8; i++) led[16 + i] = lvl(m_hitD[i], m_hitN);
        for (int i = 0; i < 12; i++) led[24 + i] = lvl(m_hitS[i], m_hitN);
        led[24 + S_INTE] = m_cpu.r.inte ? 255 : 0;
        led[24 + S_PROT] = m_s.protect ? 255 : 0;
        led[24 + S_WAIT] = 0;
    } else {
        uint16_t a = m_cpu.r.pc;
        uint8_t d = (m_inputs & (1u << P_DISPACC)) ? m_cpu.r.a : peek(a);
        for (int i = 0; i < 16; i++) led[i] = (a >> i) & 1 ? 255 : 0;
        for (int i = 0; i < 8; i++) led[16 + i] = (d >> i) & 1 ? 255 : 0;
        led[24 + S_MEMR] = led[24 + S_M1] = led[24 + S_WO] = led[24 + S_WAIT] = 255;
        led[24 + S_INTE] = m_cpu.r.inte ? 255 : 0;
        led[24 + S_PROT] = m_s.protect ? 255 : 0;
        led[24 + S_HLTA] = m_cpu.r.halted ? 255 : 0;
    }
}

size_t AltairCore::readAudio(int16_t* out, size_t maxSamples)
{
    size_t n = std::min<size_t>(maxSamples, m_audioDue);
    for (size_t i = 0; i < n; i++) {
        int16_t v = 0;
        if (m_s.bell > 0) {
            m_s.bell--;
            m_bellPhase += 880;
            v = (m_bellPhase / 22050) & 1 ? 2500 : -2500;
        }
        out[i] = v;
    }
    m_audioDue -= uint32_t(n);
    return n;
}

// ---------------- media ----------------
bool AltairCore::mediaInsert(int drive, const std::string& path, std::string& error)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) { error = "Can't open the file"; return false; }
    std::vector<uint8_t> d;
    uint8_t buf[65536]; size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0 && d.size() < 4u << 20) d.insert(d.end(), buf, buf + n);
    fclose(f);
    if (d.empty()) { error = "The file is empty"; return false; }
    if (drive == 8) {
        size_t limit = m_turnkey ? 0xfd00 : 0x10000;
        if (d.size() > limit) { error = "The program doesn't fit in memory"; return false; }
        std::memcpy(m_s.ram, d.data(), d.size());
        m_cpu.reset();
        m_cpu.r.pc = 0;
        m_s.running = 1;
        return true;
    }
    if (drive == 10) {                  // a boot PROM image at FF00, run from there (front panel model)
        if (m_turnkey || d.size() > 256) { error = "Not a boot PROM"; return false; }
        std::memcpy(m_s.ram + 0xff00, d.data(), d.size());
        m_cpu.reset();
        m_cpu.r.pc = 0xff00;
        m_s.running = 1;
        return true;
    }
    if (drive < 0 || drive > 3) { error = "No such drive"; return false; }
    const size_t full = size_t(TRACKS) * SECTORS * SECLEN;
    if (d.size() == 76720) { error = "5.25\" minidisk images aren't supported, only 8\" disks"; return false; }
    if (d.size() > full + 4096) { error = "Not an Altair 8\" disk image (337,568 bytes)"; return false; }
    d.resize(full, 0xe5);
    m_disk[drive] = std::move(d);
    m_diskPath[drive] = path;
    m_diskDirty[drive] = false;
    return true;
}

bool AltairCore::mediaSave(int drive, const std::string& path)
{
    if (drive < 0 || drive > 3 || m_disk[drive].empty()) return false;
    if (m_s.dsel == drive) diskFlush();
    if (!m_diskDirty[drive]) return false;
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = fwrite(m_disk[drive].data(), 1, m_disk[drive].size(), f) == m_disk[drive].size();
    fclose(f);
    if (ok) m_diskDirty[drive] = false;
    return ok;
}

void AltairCore::mediaEject(int drive)
{
    if (drive < 0 || drive > 3) return;
    if (m_s.dsel == drive) { diskFlush(); m_s.dsel = -1; }
    m_disk[drive].clear();
    m_diskPath[drive].clear();
}

// ---------------- save states ----------------
bool AltairCore::saveState(std::vector<uint8_t>& out)
{
    out.clear();
    uint32_t magic = m_turnkey ? 0x544c4141u : 0x504c4141u;
    auto put = [&](const void* p, size_t n) { const uint8_t* b = (const uint8_t*)p; out.insert(out.end(), b, b + n); };
    put(&magic, 4);
    put(&m_s, sizeof(m_s));
    put(&m_cpu.r, sizeof(m_cpu.r));
    return true;
}

bool AltairCore::loadState(const std::vector<uint8_t>& in)
{
    if (in.size() != 4 + sizeof(m_s) + sizeof(m_cpu.r)) return false;
    uint32_t magic;
    std::memcpy(&magic, in.data(), 4);
    if (magic != (m_turnkey ? 0x544c4141u : 0x504c4141u)) return false;
    std::memcpy(&m_s, in.data() + 4, sizeof(m_s));
    std::memcpy(&m_cpu.r, in.data() + 4 + sizeof(m_s), sizeof(m_cpu.r));
    m_keys.clear();
    render();
    return true;
}
