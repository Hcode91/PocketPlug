// PocketPlug - SSD XaviX 2
// SPDX-License-Identifier: BSD-3-Clause
//
// Port of MAME's tvgames/xavix2.cpp and cpu/xavix2/xavix2.cpp (Olivier
// Galibert, David Haywood), BSD-3-Clause; the 24Cxx EEPROM after MAME's i2cmem.

#include <set>
#include <map>
#include "xavix2.h"
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>

namespace {
const double CPU_CLOCK = 98000000.0;
const uint8_t BPO[8] = { 4, 3, 3, 2, 2, 2, 2, 1 };
const int SDW = 0x800, SDH = 0x400;

inline int r1(uint32_t op) { return (op >> 22) & 7; }
inline int r2(uint32_t op) { return (op >> 19) & 7; }
inline int r3(uint32_t op) { return (op >> 16) & 7; }
inline uint32_t val24u(uint32_t op) { return op & 0x00ffffff; }
inline uint32_t val22h(uint32_t op) { return op << 10; }
inline uint32_t val22s(uint32_t op) { return op & 0x200000 ? op | 0xffc00000 : op & 0x3fffff; }
inline uint32_t val19s(uint32_t op) { return op & 0x40000 ? op | 0xfff80000 : op & 0x7ffff; }
inline uint32_t val19u(uint32_t op) { return op & 0x0007ffff; }
inline uint32_t val16s(uint32_t op) { return uint32_t(int32_t(int16_t(op >> 8))); }
inline uint32_t val14h(uint32_t op) { return (op << 10) & 0xfffc0000; }
inline uint32_t val14s(uint32_t op) { return op & 0x200000 ? (op >> 8) | 0xffffc000 : (op >> 8) & 0x3fff; }
inline uint32_t val11s(uint32_t op) { return op & 0x40000 ? (op >> 8) | 0xfffff800 : (op >> 8) & 0x7ff; }
inline uint32_t val11u(uint32_t op) { return (op >> 8) & 0x000007ff; }
inline uint32_t val8s(uint32_t op) { return uint32_t(int32_t(int8_t(op >> 16))); }
inline uint32_t val6u(uint32_t op) { return (op >> 16) & 0x3f; }
inline uint32_t val6s(uint32_t op) { return op & 0x200000 ? (op >> 16) | 0xffffffc0 : (op >> 16) & 0x3f; }
inline uint32_t val3u(uint32_t op) { return (op >> 16) & 7; }
inline uint32_t val3s(uint32_t op) { return op & 0x40000 ? (op >> 16) | 0xfffffff8 : (op >> 16) & 7; }
}

Xavix2Core::Xavix2Core(const Config& c) : m_cfg(c)
{
    m_sd.assign(size_t(SDW) * SDH, 0xff000000u);
    m_out.assign(640 * 400, 0xff000000u);
}

bool Xavix2Core::load(const std::vector<RomFile>& roms, std::string& error)
{
    for (const auto& r : roms)
        if (r.name == m_cfg.romName) {
            m_rom.assign(0x1000000, 0);
            std::memcpy(m_rom.data(), r.data.data(), std::min<size_t>(r.data.size(), m_rom.size()));
            // a smaller chip repeats through the 16 MB window (the Dragon Ball
            // games jump into the upper half of an 8 MB ROM)
            if (!r.data.empty() && r.data.size() < m_rom.size() && (m_rom.size() % r.data.size()) == 0)
                for (size_t o = r.data.size(); o < m_rom.size(); o += r.data.size()) std::memcpy(m_rom.data() + o, r.data.data(), r.data.size());
        }
    if (m_rom.empty()) { error = "Missing " + m_cfg.romName; return false; }
    reset();
    return true;
}

void Xavix2Core::reset()
{
    static uint8_t keep[sizeof(m_s.eep)];
    if (m_eepInit) std::memcpy(keep, m_s.eep, sizeof(keep));
    std::memset(&m_s, 0, sizeof(m_s));
    if (m_eepInit) std::memcpy(m_s.eep, keep, sizeof(keep));
    else std::memset(m_s.eep, 0xff, sizeof(m_s.eep));
    m_eepInit = true;
    m_s.sdar = 1;
    m_s.pc = 0x40000000;
    std::fill(m_sd.begin(), m_sd.end(), 0xff000000u);
}

// ---------------------------------------------------------------- memory

static int x2Frame = 0;

uint8_t Xavix2Core::rd8(uint32_t a)
{
#ifdef XAVIX_DEBUG
    static int wlo = -2, whi = 0;
    if (wlo == -2) { const char* e = getenv("X2RW"); if (e) sscanf(e, "%x-%x", &wlo, &whi); else wlo = -1; }
    if (wlo >= 0 && a >= uint32_t(wlo) && a <= uint32_t(whi)) { static std::set<uint64_t> seen; if (seen.insert((uint64_t(a) << 32) | m_s.pc).second) fprintf(stderr, "rd %04x pc %08x\n", a, m_s.pc); }
#endif
    if (a < 0x10000) return m_s.ram[a];
    if (a < 0x01000000) return m_rom[a];
    if (a >= 0x40000000 && a < 0x41000000) return m_rom[a - 0x40000000];
    if (a >= 0xc0000000 && a < 0xc0000800) return uint8_t(m_s.palette[(a & 0x7ff) >> 2] >> ((a & 3) * 8));
    if (a >= 0xc0000800 && a < 0xc0020000) return m_s.vram[a - 0xc0000000];
    if (a >= 0xffffe000) return uint8_t(ioRead(a, 1));
    return 0;
}

void Xavix2Core::wr8(uint32_t a, uint8_t d)
{
#ifdef XAVIX_DEBUG
    static int wlo = -2, whi = 0;
    if (wlo == -2) { const char* e = getenv("X2WW"); if (e) sscanf(e, "%x-%x", &wlo, &whi); else wlo = -1; }
    if (wlo >= 0 && a >= uint32_t(wlo) && a <= uint32_t(whi)) { static std::set<uint64_t> seen; if (seen.insert((uint64_t(a) << 32) | m_s.pc).second) fprintf(stderr, "wr %04x = %02x pc %08x\n", a, d, m_s.pc); }
#endif
    if (a < 0x10000) { m_s.ram[a] = d; return; }
#ifdef XAVIX_DEBUG
    if (const char* e = getenv("X2SNDV")) { uint32_t v = atoi(e), b = 0xc0010000 + v * 64; if (a >= b && a < b + 64) fprintf(stderr, "f%d vw +%02x = %02x\n", x2Frame, a - b, d); }
#endif
    if (a >= 0xc0000800 && a < 0xc0020000) { m_s.vram[a - 0xc0000000] = d; return; }
    if (a >= 0xffffe000) ioWrite(a, d, 1);
}

void Xavix2Core::wr16(uint32_t a, uint16_t d)
{
    if (a >= 0xc0000000 && a < 0xc0000800) { m_s.palette[(a & 0x7ff) >> 2] = (d & 0x8000) ? 0 : (rgb555(d) | 0xff000000u); return; }
    if (a >= 0xffffe000) { ioWrite(a, d, 2); return; }
    wr8(a, uint8_t(d)); wr8(a + 1, uint8_t(d >> 8));
}

void Xavix2Core::wr32(uint32_t a, uint32_t d)
{
    if (a >= 0xc0000000 && a < 0xc0000800) { m_s.palette[(a & 0x7ff) >> 2] = (d & 0x8000) ? 0 : (rgb555(uint16_t(d)) | 0xff000000u); return; }
    if (a >= 0xffffe000) { ioWrite(a, d, 4); return; }
    wr16(a, uint16_t(d)); wr16(a + 2, uint16_t(d >> 16));
}

uint32_t Xavix2Core::rgb555(uint16_t c)
{
    uint32_t r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
    r = (r << 3) | (r >> 2); g = (g << 3) | (g >> 2); b = (b << 3) | (b >> 2);
    return (r << 16) | (g << 8) | b;
}

#ifdef XAVIX_DEBUG
static uint32_t* x2Pch = nullptr;
#endif

uint32_t Xavix2Core::ioRead(uint32_t a, int size)
{
#ifdef XAVIX_DEBUG
    if (getenv("X2IO") && x2Frame >= atoi(getenv("X2IO"))) { static std::set<uint64_t> seen; if (seen.insert((uint64_t(a) << 32) | m_s.pc).second) fprintf(stderr, "R%d %08x pc %08x\n", size, a, m_s.pc); }
#endif
    switch (a & ~(size == 4 ? 3u : 0u)) {
    case 0xffffe010: return (m_s.intActive & (1 << IRQ_DMA)) ? 6 : 0;
    case 0xffffe200: return m_s.pioMode[0];
    case 0xffffe204: return m_s.pioMode[1];
    case 0xffffe208: {
        uint32_t in = m_inputs & ~((1u << m_cfg.sdaBit) | (1u << m_cfg.sclBit));
        int sda = m_s.est == 0 ? m_s.sdaw : m_s.sdar;
        in |= uint32_t(sda) << m_cfg.sdaBit;
        uint32_t v = (in & ~m_s.pioMaskOut) | (m_s.pioData & m_s.pioMaskOut);
        return size == 4 ? v : (v >> ((a & 3) * 8)) & (size == 2 ? 0xffff : 0xff);
    }
    case 0xffffe209: case 0xffffe20a: case 0xffffe20b: {
        uint32_t in = m_inputs & ~((1u << m_cfg.sdaBit) | (1u << m_cfg.sclBit));
        int sda = m_s.est == 0 ? m_s.sdaw : m_s.sdar;
        in |= uint32_t(sda) << m_cfg.sdaBit;
        uint32_t v = (in & ~m_s.pioMaskOut) | (m_s.pioData & m_s.pioMaskOut);
        return (v >> ((a & 3) * 8)) & (size == 2 ? 0xffff : 0xff);
    }
    case 0xffffe859: {
#ifdef XAVIX_DEBUG
        if (const char* e = getenv("X2CAMST")) return uint32_t(strtoul(e, 0, 16));
#endif
        return 0;
    }
    case 0xffffe244: return m_s.linkStart;
    case 0xffffe246: return m_s.linkEnd;
    case 0xffffe248: return m_s.linkLen;
    case 0xffffe24a: case 0xffffe24b: return m_s.linkStart;   // receive pointer: back at the start once a packet is in
    case 0xffffe270: case 0xffffe271: return m_s.e270;
    case 0xffffe238: return 0;
    case 0xffffe239: return 2;
    case 0xffffe600: return m_s.gpu0bAdr;
    case 0xffffe602: return m_s.gpu0bCount;
    case 0xffffe604: return m_s.gpu1bAdr;
    case 0xffffe606: return m_s.gpu1bCount;
    case 0xffffe60a: return 0x240;           // NTSC
    case 0xffffe630: case 0xffffe632: return 0x210;
    case 0xfffffc00: for (int i = 0; i <= 12; i++) if (m_s.intActive & (1 << i)) return uint32_t(i); return 0xff;
    case 0xfffffc08: return m_s.intNmi;
    case 0xfffffc0a: return m_s.intEnabled;
    }
    return 0;
}

void Xavix2Core::ioWrite(uint32_t a, uint32_t d, int size)
{
#ifdef XAVIX_DEBUG
    if (getenv("X2SND") && a >= 0xffffea00 && a < 0xffffec00) fprintf(stderr, "f%d snd W%d %08x = %08x pc %08x\n", x2Frame, size, a, d, m_s.pc);
    if (getenv("X2IO") && x2Frame >= atoi(getenv("X2IO"))) { static std::set<uint64_t> seen; if (seen.insert((uint64_t(a) << 32) | m_s.pc).second) fprintf(stderr, "W%d %08x = %08x pc %08x\n", size, a, d, m_s.pc); }
#endif
    auto combine32 = [&](uint32_t& reg) {
        if (size == 4) reg = d;
        else { int sh = (a & 3) * 8; uint32_t m = (size == 2 ? 0xffffu : 0xffu) << sh; reg = (reg & ~m) | ((d << sh) & m); }
    };
    auto combine16 = [&](uint16_t& reg) {
        if (size >= 2) reg = uint16_t(d);
        else { int sh = (a & 1) * 8; uint16_t m = uint16_t(0xff << sh); reg = uint16_t((reg & ~m) | ((d << sh) & m)); }
    };
    uint32_t base = a;
    if (a >= 0xffffe000 && a < 0xffffe004) base = 0xffffe000;
    else if (a >= 0xffffe200 && a < 0xffffe204) base = 0xffffe200;
    else if (a >= 0xffffe204 && a < 0xffffe208) base = 0xffffe204;
    else if (a >= 0xffffe208 && a < 0xffffe20c) base = 0xffffe208;
    else if ((a & 1) && size == 1) base = a & ~1u;
#ifdef XAVIX_DEBUG
    if (a == 0xffffe858) if (const char* e = getenv("X2CAMIRQ")) { static int n = 0; if (n++ < 3) fprintf(stderr, "cam cmd %02x\n", d & 0xff); irqRaise(atoi(e)); }
#endif
    switch (base) {
    case 0xffffea0a: if (size >= 2) soundCommand(uint16_t(d)); break;
    case 0xffffea18: combine16(m_s.sndVol); break;
    case 0xffffea1c: if (a == 0xffffea1c) m_s.sndPanL = uint8_t(d); else m_s.sndPanR = uint8_t(d); break;   // $EA1D folds into here as a byte write
    case 0xffffe244: combine16(m_s.linkStart); break;
    case 0xffffe246: combine16(m_s.linkEnd); break;
    case 0xffffe248: combine16(m_s.linkLen); break;
    case 0xffffe270: combine16(m_s.e270); break;
    case 0xffffe234: if (a == 0xffffe235) irqClear((1u << IRQ_LINKPOLL) | (1u << IRQ_LINKDONE)); break;   // the link's interrupts are acknowledged here
    case 0xffffe220:
        if ((d & 0xff) == 0x0f) {   // request a packet from the unit
            buildPacket();
            for (int i = 0; i < 16; i++) m_s.ram[(m_s.linkStart + i) & 0xffff] = m_pkt[i];
            m_s.linkLeft = 20000;
        }
        break;
    case 0xffffe000: combine32(m_s.dmaSrc); break;
    case 0xffffe004: combine16(m_s.dmaDst); break;
    case 0xffffe008: combine16(m_s.dmaCount); break;
    case 0xffffe00c: {
        uint8_t v = uint8_t(d);
        if (v == 3 || v == 7) {
            uint32_t s = m_s.dmaSrc | (v == 3 ? 0 : 0x40000000);
            for (uint32_t i = 0; i < m_s.dmaCount; i++) wr8(m_s.dmaDst + i, rd8(s + i));
            m_s.dmaLeft = m_s.dmaCount ? m_s.dmaCount : 1;
        }
        break; }
    case 0xffffe010: if ((d & 0xff) == 2) irqClear(1u << IRQ_DMA); break;
    case 0xffffe200: case 0xffffe204: {
        combine32(m_s.pioMode[base == 0xffffe200 ? 0 : 1]);
        m_s.pioMaskOut = 0;
        for (int i = 0; i < 32; i++)
            if (((m_s.pioMode[i / 16] >> ((i % 16) * 2)) & 3) == 3) m_s.pioMaskOut |= 1u << i;
        pioUpdate();
        break; }
    case 0xffffe208: combine32(m_s.pioData); pioUpdate(); break;
    case 0xffffe400: combine16(m_s.gpu0Adr); break;
    case 0xffffe404: combine16(m_s.gpu0Count); break;
    case 0xffffe408: gpuUpdate(m_s.gpu0Count, m_s.gpu0Adr); break;
    case 0xffffe40c: combine16(m_s.gpu1Adr); break;
    case 0xffffe410: combine16(m_s.gpu1Count); break;
    case 0xffffe414: gpuUpdate(m_s.gpu1Count, m_s.gpu1Adr); break;
    case 0xffffe600: combine16(m_s.gpu0bAdr); break;
    case 0xffffe602: combine16(m_s.gpu0bCount); break;
    case 0xffffe604: combine16(m_s.gpu1bAdr); break;
    case 0xffffe606: combine16(m_s.gpu1bCount); break;
    case 0xffffe608: combine16(m_s.descSizeAdr); break;
    case 0xffffe60e: combine16(m_s.bgColor); break;
    case 0xffffe622: combine16(m_s.descDataAdr); break;
    case 0xfffffc04: m_s.intActive &= ~uint32_t(uint16_t(d)); updateIrqLine(); break;
    case 0xfffffc08: combine16(m_s.intNmi); break;
    case 0xfffffc0a: { uint16_t v = m_s.intEnabled; combine16(v); irqClear(~uint32_t(v)); m_s.intEnabled = v; break; }
    }
}

void Xavix2Core::irqRaise(int level)
{
    uint32_t line = 1u << level;
    if ((m_s.intEnabled | m_s.intNmi) & line) { m_s.intActive |= line; updateIrqLine(); }
}

void Xavix2Core::irqClear(uint32_t mask) { m_s.intActive &= ~mask; updateIrqLine(); }

// ---------------------------------------------------------------- GPU

void Xavix2Core::gpuUpdate(uint16_t count, uint16_t adr)
{
    std::vector<int> list(count);
    for (uint32_t i = 0; i < count; i++) {
        uint64_t cmd = rd64(adr + 8 * i);
        list[i] = int((cmd & 0x1fe00000) | i);
    }
    std::sort(list.begin(), list.end(), std::greater<int>());
    for (uint32_t i = 0; i < count; i++) {
        uint64_t cmd = rd64(adr + 8 * (list[i] & 0xffff));
        uint32_t idx = (cmd >> 30) & 0x3f, idx2 = (cmd >> 58) & 0x3f;
        uint32_t descsize = rd32(m_s.descSizeAdr + 4 * idx);
        uint16_t descdata = rd16(m_s.descDataAdr + 2 * idx2);
        uint32_t sadr = (uint32_t(descdata) << 14) + ((cmd >> 43) & 0x7fe0);
        uint32_t x = cmd & 0x7ff, y = (cmd >> 11) & 0x3ff;
        uint32_t sx = 1 + (descsize & 0xff), sy = 1 + ((descsize >> 8) & 0xff);
        uint32_t scalex = (cmd >> 40) & 3, scaley = (cmd >> 46) & 3;
        uint32_t bpp = 1 + ((descsize >> 24) & 7);
        if (x + sx > SDW) sx = SDW - x;
        if (y + sy > SDH) sy = SDH - y;
        uint32_t mask = (1u << bpp) - 1;
        uint32_t palette = ((descsize >> 27) & 0x1f) << bpp;
        for (uint32_t yy = 0; yy < sy; yy++) {
            uint64_t v = rd64(sadr); sadr += 8;
            uint32_t avail = 64;
            for (uint32_t xx = 0; xx < sx; xx++) {
                if (avail < bpp) { v = rd64(sadr); sadr += 8; avail = 64; }
                uint32_t color = m_s.palette[(palette | (v & mask)) & 511];
                if (color)
                    for (uint32_t a = 0; a < scaley; a++)
                        for (uint32_t b = 0; b < scalex; b++) {
                            uint32_t py = y + yy * scaley + a, px = x + xx * scalex + b;
                            if (py < uint32_t(SDH) && px < uint32_t(SDW)) m_sd[size_t(py) * SDW + px] = color;
                        }
                v >>= bpp;
                avail -= bpp;
            }
        }
    }
}

// ---------------------------------------------------------------- PIO / EEPROM

// The unit's camera finds the two reflective stickers on the gloves itself and
// sends their positions: 43 bits (header 101, then x, y and size of each spot in
// 6 bits, then footer 101), coded with a variable-length bit code that the game
// unpacks through a 16-entry table (sign = output bit, size = input bits used).
static const int8_t kNaruCode[16] = { 2, -1, -2, -2, 2, 2, 1, -3, 3, -1, -2, -2, 2, -1, 1, -2 };

void Xavix2Core::buildPacket()
{
    uint64_t v = 5;
    for (int g = 0; g < 2; g++) {
        int x = std::clamp(int((m_ax[g] + 1.0f) * 31.5f), 0, 63);
        int y = std::clamp(int((1.0f - m_ay[g]) * 31.5f), 0, 63);   // the unit counts y upwards
        int size = 10 + int(m_reach[g] * 40.0f);                      // the game ignores sizes of 56 and up
#ifdef XAVIX_DEBUG
        if (getenv("X2HIDE") && (m_inputs >> (9 + g) & 1)) size = 0;   // experiment: hand turned away
#endif
        v |= uint64_t(x) << (3 + g * 18) | uint64_t(y) << (9 + g * 18) | uint64_t(size) << (15 + g * 18);
    }
    v |= uint64_t(5) << 39;
#ifdef XAVIX_DEBUG
    if (const char* e = getenv("X2FUZZ")) {   // random walk of both spots, a new step every N packets
        static uint32_t seed = 12345, n = 0; static int f[6] = { 20, 30, 12, 44, 30, 12 };
        if (n++ % uint32_t(atoi(e)) == 0) for (int i = 0; i < 6; i++) { seed = seed * 1103515245u + 12345u; int r = (seed >> 16) % 100;
            if (i % 3 == 2) f[i] = r < 15 ? 0 : 4 + r % 50; else f[i] = std::clamp(f[i] + (r % 21) - 10, 0, 63); }
        v = 5 | uint64_t(5) << 39; for (int i = 0; i < 6; i++) v |= uint64_t(f[i] & 63) << (3 + 6 * i);
    }
    if (const char* e = getenv("X2FIELDS")) { unsigned f[6]; if (sscanf(e, "%u,%u,%u,%u,%u,%u", &f[0], &f[1], &f[2], &f[3], &f[4], &f[5]) == 6) { v = 5 | uint64_t(5) << 39; for (int i = 0; i < 6; i++) v |= uint64_t(f[i] & 63) << (3 + 6 * i); } }
#endif
    // encode: pick for each output bit a 4-bit window whose table entry gives that bit,
    // looking ahead (by a backwards pass) so the bits it leaves behind can still work
    const int N = 43;
    bool ok[N + 1][4][8];
    for (int k = 0; k < 4; k++) for (int p = 0; p < 8; p++) ok[N][k][p] = true;
    auto fits = [&](int n, int k, int pref, int bit) { return (n & ((1 << k) - 1)) == pref && ((kNaruCode[n] < 0) == bool(bit)); };
    for (int i = N - 1; i >= 0; i--)
        for (int k = 0; k < 4; k++)
            for (int p = 0; p < (1 << k); p++) {
                ok[i][k][p] = false;
                for (int n = 0; n < 16 && !ok[i][k][p]; n++)
                    if (fits(n, k, p, int(v >> i & 1))) { int l = std::abs(kNaruCode[n]); if (ok[i + 1][4 - l][n >> l]) ok[i][k][p] = true; }
            }
    std::memset(m_pkt, 0, sizeof(m_pkt));
    int out = 0, k = 0, pref = 0;
    for (int i = 0; i < N; i++)
        for (int n = 0; n < 16; n++) {
            if (!fits(n, k, pref, int(v >> i & 1))) continue;
            int l = std::abs(kNaruCode[n]);
            if (!ok[i + 1][4 - l][n >> l]) continue;
            for (int j = k; j < 4; j++, out++) if (out < 128 && (n >> j & 1)) m_pkt[out >> 3] |= uint8_t(1 << (out & 7));
            k = 4 - l; pref = n >> l;
            break;
        }
}

void Xavix2Core::pioUpdate()
{
    i2cSda((m_s.pioMaskOut >> m_cfg.sdaBit & 1) ? (m_s.pioData >> m_cfg.sdaBit & 1) : 1);
    i2cScl((m_s.pioMaskOut >> m_cfg.sclBit & 1) ? (m_s.pioData >> m_cfg.sclBit & 1) : 0);
}

enum { E_IDLE, E_DEVSEL, E_ADDRHIGH, E_ADDRLOW, E_DATAIN, E_READSELACK, E_DATAOUT, E_RESET };

void Xavix2Core::i2cSda(int st)
{
    st &= 1;
    if (m_s.sdaw == st) return;
    m_s.sdaw = uint8_t(st);
    if (m_s.scl && m_s.sdar) {
        if (m_s.sdaw) {
            if (m_s.pageWritten > 0) {
                int mask = m_cfg.i2cSize - 1, base = m_s.byteaddr & mask, root = base & ~(m_cfg.i2cPage - 1);
                for (int i = 0; i < m_s.pageWritten; i++)
                    m_s.eep[(root | ((base + i) & (m_cfg.i2cPage - 1))) & mask] = m_s.page[i];
                m_s.pageWritten = 0;
            }
            m_s.est = E_IDLE;
        } else { m_s.est = E_DEVSEL; m_s.bits = 0; }
    }
}

void Xavix2Core::i2cScl(int st)
{
    st &= 1;
    if (m_s.scl == st) return;
    m_s.scl = uint8_t(st);
    int mask = m_cfg.i2cSize - 1;
    switch (m_s.est) {
    case E_DEVSEL: case E_ADDRHIGH: case E_ADDRLOW: case E_DATAIN:
        if (m_s.bits < 8) {
            if (m_s.scl) { m_s.shift = uint8_t((m_s.shift << 1) | m_s.sdaw); m_s.bits++; }
        } else if (m_s.scl) {
            m_s.bits++;
        } else if (m_s.bits == 8) {
            switch (m_s.est) {
            case E_DEVSEL: {
                m_s.devsel = m_s.shift;
                int amask = 0xfe & ~(mask >> 7);
                if (m_s.devsel == 0) m_s.est = E_RESET;
                else if ((m_s.devsel & amask) != (0xa0 & amask)) m_s.est = E_IDLE;
                else if (!(m_s.devsel & 1)) m_s.est = E_ADDRLOW;
                else m_s.est = E_READSELACK;
                break; }
            case E_ADDRLOW:
                m_s.byteaddr = m_s.shift | (((m_s.devsel & 0xfe) << 7) & mask);
                m_s.pageOffset = 0; m_s.pageWritten = 0;
                m_s.est = E_DATAIN;
                break;
            case E_DATAIN:
                m_s.page[m_s.pageOffset] = m_s.shift;
                if (++m_s.pageOffset == m_cfg.i2cPage) m_s.pageOffset = 0;
                if (++m_s.pageWritten > m_cfg.i2cPage) m_s.pageWritten = m_cfg.i2cPage;
                break;
            }
            if (m_s.est != E_IDLE) m_s.sdar = 0;
        } else { m_s.bits = 0; m_s.sdar = 1; }
        break;
    case E_READSELACK: m_s.bits = 0; m_s.est = E_DATAOUT; break;
    case E_DATAOUT:
        if (m_s.bits < 8) {
            if (m_s.scl) m_s.bits++;
            else {
                if (m_s.bits == 0) { m_s.shift = m_s.eep[m_s.byteaddr & mask]; m_s.byteaddr++; }
                m_s.sdar = (m_s.shift >> 7) & 1;
                m_s.shift = uint8_t(m_s.shift << 1);
            }
        } else {
            if (m_s.scl) { if (m_s.sdaw) m_s.est = E_IDLE; m_s.bits = 0; }
            else m_s.sdar = 1;
        }
        break;
    case E_RESET:
        if (m_s.scl) { if (m_s.bits > 8) { m_s.est = E_IDLE; m_s.sdar = 1; } m_s.bits++; }
        break;
    }
}

// ---------------------------------------------------------------- CPU

uint32_t Xavix2Core::doAdd(uint32_t v1, uint32_t v2)
{
    uint32_t r = v1 + v2, f = 0;
    if (!r) f |= F_Z;
    if (r & 0x80000000) f |= F_N;
    if (((v1 & v2) | ((~r) & (v1 | v2))) & 0x80000000) f |= F_C;
    if (((v1 ^ r) & (v2 ^ r)) & 0x80000000) f |= F_V;
    m_s.hr[4] = (m_s.hr[4] & ~F_MASK) | f;
    return r;
}

uint32_t Xavix2Core::doSub(uint32_t v1, uint32_t v2)
{
    uint32_t r = v1 - v2, f = 0;
    if (!r) f |= F_Z;
    if (r & 0x80000000) f |= F_N;
    if (((v2 & r) | ((~v1) & (v2 | r))) & 0x80000000) f |= F_C;
    if (((v1 ^ v2) & (v1 ^ r)) & 0x80000000) f |= F_V;
    m_s.hr[4] = (m_s.hr[4] & ~F_MASK) | f;
    return r;
}

uint32_t Xavix2Core::snz(uint32_t r)
{
    uint32_t f = 0;
    if (!r) f |= F_Z;
    if (r & 0x80000000) f |= F_N;
    m_s.hr[4] = (m_s.hr[4] & ~F_MASK) | f;
    return r;
}

uint32_t Xavix2Core::doLsl(uint32_t v1, uint32_t s)
{
    uint32_t& h = m_s.hr[4];
    if (!s) { h = (h & ~F_MASK) | (v1 ? (v1 & 0x80000000 ? F_N : 0) : F_Z); return v1; }
    if (s < 32) {
        uint32_t r = v1 << s, f = r ? (r & 0x80000000 ? F_N : 0) : F_Z;
        if (v1 & (1u << (32 - s))) f |= F_C;
        h = (h & ~F_MASK) | f; return r;
    }
    if (s == 32) { h = (h & ~F_MASK) | (v1 & 1 ? F_C | F_Z : F_Z); return 0; }
    h = (h & ~F_MASK) | F_Z; return 0;
}

uint32_t Xavix2Core::doLsr(uint32_t v1, uint32_t s)
{
    uint32_t& h = m_s.hr[4];
    if (!s) { h = (h & ~F_MASK) | (v1 ? (v1 & 0x80000000 ? F_N : 0) : F_Z); return v1; }
    if (s < 32) {
        uint32_t r = v1 >> s, f = r ? 0 : F_Z;
        if (v1 & (1u << (s - 1))) f |= F_C;
        h = (h & ~F_MASK) | f; return r;
    }
    if (s == 32) { h = (h & ~F_MASK) | (v1 & 0x80000000 ? F_C | F_Z : F_Z); return 0; }
    h = (h & ~F_MASK) | F_Z; return 0;
}

uint32_t Xavix2Core::doAsr(uint32_t v1, uint32_t s)
{
    uint32_t& h = m_s.hr[4];
    if (!s) { h = (h & ~F_MASK) | (v1 ? (v1 & 0x80000000 ? F_N : 0) : F_Z); return v1; }
    if (s < 32) {
        uint32_t r = uint32_t(int32_t(v1) >> s), f = r ? (r & 0x80000000 ? F_N : 0) : F_Z;
        if (v1 & (1u << (s - 1))) f |= F_C;
        h = (h & ~F_MASK) | f; return r;
    }
    if (v1 & 0x80000000) { h = (h & ~F_MASK) | F_C; return 0xffffffff; }
    h = (h & ~F_MASK) | F_Z; return 0;
}

uint32_t Xavix2Core::checkInterrupt(uint32_t cpc)
{
    if (m_s.intLine && ((m_s.hr[4] & F_I) || m_s.wait)) {
        m_s.ilr1 = m_s.wait ? cpc + 1 : cpc;
        m_s.wait = 0;
        m_s.eiCount = 0;
        m_s.if1 = uint8_t(m_s.hr[4]);
        m_s.hr[4] &= ~F_I;
        return 0x40000010;
    }
    return cpc;
}

void Xavix2Core::execute(int64_t cycles)
{
    m_icount += cycles;
    uint32_t* r = m_s.r;
    uint32_t* hr = m_s.hr;
    if (m_s.wait) {
        // halted until an interrupt
        uint32_t npc = checkInterrupt(m_s.pc);
        if (m_s.wait) { m_icount = 0; return; }
        m_s.pc = npc;
    }
    if (!m_s.eiCount) m_s.pc = checkInterrupt(m_s.pc);
    while (m_icount > 0) {
        if (m_s.eiCount) {
            if (!--m_s.eiCount) m_s.pc = checkInterrupt(m_s.pc);
        } else if (m_s.intLine && (hr[4] & F_I)) {
            m_s.pc = checkInterrupt(m_s.pc);
        }
        uint32_t pc = m_s.pc;
#ifdef XAVIX_DEBUG
        if (x2Pch) x2Pch[(pc & 0xffffff) >> 4]++;
        { static int wa = -2; static uint8_t last; if (wa == -2) { const char* e = getenv("X2CHG"); wa = e ? int(strtoul(e, 0, 16)) : -1; if (wa >= 0) last = m_s.ram[wa]; }
          if (wa >= 0 && m_s.ram[wa] != last) { fprintf(stderr, "chg %04x %02x->%02x before pc %08x prev %08x f%d\n", wa, last, m_s.ram[wa], pc, m_prevPc, x2Frame); last = m_s.ram[wa]; } m_prevPc = pc; }
        { // X2IRQTR=n: print the PCs of the first n instructions after each of the first interrupts
            static int n = -1, left = 0, shown = 0;
            if (n < 0) n = getenv("X2IRQTR") ? atoi(getenv("X2IRQTR")) : 0;
            if (n && pc == 0x40000010 && shown < 3) { left = n; shown++; fprintf(stderr, "\nirq f%d act=%04x:", x2Frame, m_s.intActive); }
            if (left > 0) { left--; fprintf(stderr, " %x", pc); }
        }
        { // X2TRAP: when execution leaves ROM / RAM, print the last PCs before it
            static int on = -1; static uint32_t ring[256]; static unsigned ri;
            if (on < 0) on = getenv("X2TRAP") ? 1 : 0;
            if (on == 1) {
                ring[ri++ & 255] = pc;
                bool ok = (pc < 0x00800000 || (pc >= 0x40000000 && pc < 0x40800000)) && !(pc >= 0x10000 && pc < 0x10000 + 0x10 && 0);
                if (!ok) {
                    on = 2;
                    fprintf(stderr, "trap: pc %08x frame %d\n", pc, x2Frame);
                    for (unsigned k = ri - 64; k != ri; k++) fprintf(stderr, " %08x", ring[k & 255]);
                    fprintf(stderr, "\n r:"); for (int k = 0; k < 8; k++) fprintf(stderr, " %08x", r[k]); fprintf(stderr, "\n");
                }
            }
        }
        { static int brk = -2; if (brk == -2) { const char* e = getenv("X2BRK"); brk = e ? int(strtoul(e, 0, 16)) : -1; }
          if (brk >= 0 && (pc & 0xffffff) == uint32_t(brk)) { static std::set<uint32_t> seen; if (seen.insert(r[7]).second) fprintf(stderr, "brk %08x from lnk %08x r0=%08x r6=%08x ram1a0=%02x%02x f%d\n", pc, r[7], r[0], r[6], m_s.ram[0x1a1], m_s.ram[0x1a0], x2Frame); } }
#endif
        // code in the low 64 KB is fetched from the ROM image there: the DBZ games point their timer callback at a
        // routine whose RAM copy their start-up code wipes (and then fills with a table), which only works this way
        uint32_t fpc = pc < 0x10000 ? pc | 0x40000000 : pc;
        uint32_t op = uint32_t(rd8(fpc)) << 24;
        uint8_t nb = BPO[op >> 29];
        uint32_t npc = pc + nb;
        for (uint8_t i = 1; i != nb; i++) op |= uint32_t(rd8(fpc + i)) << (24 - 8 * i);
        m_icount -= nb;
        if (m_s.dmaLeft > 0 && (m_s.dmaLeft -= nb) <= 0) { m_s.dmaLeft = 0; irqRaise(IRQ_DMA); }
        switch (op >> 24) {
        case 0x00: case 0x01: r[r1(op)] = doAdd(r[r2(op)], val19s(op)); break;
        case 0x02: case 0x03: r[r1(op)] = val22h(op); break;
        case 0x04: case 0x05: r[r1(op)] = doSub(r[r2(op)], val19s(op)); break;
        case 0x06: case 0x07: r[r1(op)] = val22s(op); break;
        case 0x08: npc = val24u(op) | (pc & 0xff000000); break;
        case 0x09: r[7] = npc; npc = val24u(op) | (pc & 0xff000000);
#ifdef XAVIX_DEBUG
            if (getenv("X2JSR") && x2Frame >= atoi(getenv("X2JSR"))) { static std::map<uint64_t, int> cnt; static int lf = -1; cnt[(uint64_t(npc) << 32) | pc]++;
                if (x2Frame != lf && lf >= 0) { for (auto& kv : cnt) fprintf(stderr, "jsr %08x from %08x x%d\n", uint32_t(kv.first >> 32), uint32_t(kv.first), kv.second); cnt.clear(); fprintf(stderr, "---- f%d\n", x2Frame); } lf = x2Frame; }
#endif
            break;
        case 0x0a: case 0x0b: r[r1(op)] = snz(r[r2(op)] & val19u(op)); break;
        case 0x0c: case 0x0d: r[r1(op)] = snz(r[r2(op)] | val19u(op)); break;
        case 0x0e: case 0x0f: r[r1(op)] = snz(r[r2(op)] ^ val19u(op)); break;
        case 0x10: case 0x11: r[r1(op)] = uint32_t(int32_t(int8_t(rd8(r[r2(op)] + val19s(op))))); break;
        case 0x12: case 0x13: r[r1(op)] = rd8(r[r2(op)] + val19s(op)); break;
        case 0x14: case 0x15: r[r1(op)] = uint32_t(int32_t(int16_t(rd16(r[r2(op)] + val19s(op))))); break;
        case 0x16: case 0x17: r[r1(op)] = rd16(r[r2(op)] + val19s(op)); break;
        case 0x18: case 0x19: r[r1(op)] = rd32(r[r2(op)] + val19s(op)); break;
        case 0x1a: case 0x1b: wr8(r[r2(op)] + val19s(op), uint8_t(r[r1(op)])); break;
        case 0x1c: case 0x1d: wr16(r[r2(op)] + val19s(op), uint16_t(r[r1(op)])); break;
        case 0x1e: case 0x1f: wr32(r[r2(op)] + val19s(op), r[r1(op)]); break;
        case 0x20: case 0x21: r[r1(op)] = doAdd(r[r2(op)], val11s(op)); break;
        case 0x22: case 0x23: r[r1(op)] = val14h(op); break;
        case 0x24: case 0x25: r[r1(op)] = doSub(r[r2(op)], val11s(op)); break;
        case 0x26: case 0x27: doSub(r[r1(op)], val14s(op)); break;
        case 0x28: npc = pc + val16s(op); break;
        case 0x29: r[7] = npc; npc = pc + val16s(op); break;
        case 0x2a: case 0x2b: r[r1(op)] = snz(r[r2(op)] & val11u(op)); break;
        case 0x2c: case 0x2d: r[r1(op)] = snz(r[r2(op)] | val11u(op)); break;
        case 0x2e: case 0x2f: r[r1(op)] = snz(r[r2(op)] ^ val11u(op)); break;
        case 0x30: case 0x31: r[r1(op)] = uint32_t(int32_t(int8_t(rd8(r[6] + val14s(op))))); break;
        case 0x32: case 0x33: r[r1(op)] = rd8(r[6] + val14s(op)); break;
        case 0x34: case 0x35: r[r1(op)] = uint32_t(int32_t(int16_t(rd16(r[6] + val14s(op))))); break;
        case 0x36: case 0x37: r[r1(op)] = rd16(r[6] + val14s(op)); break;
        case 0x38: case 0x39: r[r1(op)] = rd32(r[6] + val14s(op)); break;
        case 0x3a: case 0x3b: wr8(r[6] + val14s(op), uint8_t(r[r1(op)])); break;
        case 0x3c: case 0x3d: wr16(r[6] + val14s(op), uint16_t(r[r1(op)])); break;
        case 0x3e: case 0x3f: wr32(r[6] + val14s(op), r[r1(op)]); break;
        case 0x40: case 0x41: r[r1(op)] = uint32_t(int32_t(int8_t(rd8(r[r2(op)] + val11s(op))))); break;
        case 0x42: case 0x43: r[r1(op)] = rd8(r[r2(op)] + val11s(op)); break;
        case 0x44: case 0x45: r[r1(op)] = uint32_t(int32_t(int16_t(rd16(r[r2(op)] + val11s(op))))); break;
        case 0x46: case 0x47: r[r1(op)] = rd16(r[r2(op)] + val11s(op)); break;
        case 0x48: case 0x49: r[r1(op)] = rd32(r[r2(op)] + val11s(op)); break;
        case 0x4a: case 0x4b: wr8(r[r2(op)] + val11s(op), uint8_t(r[r1(op)])); break;
        case 0x4c: case 0x4d: wr16(r[r2(op)] + val11s(op), uint16_t(r[r1(op)])); break;
        case 0x4e: case 0x4f: wr32(r[r2(op)] + val11s(op), r[r1(op)]); break;
        case 0x50: case 0x51: r[r1(op)] = uint32_t(int32_t(int8_t(rd8(val14s(op))))); break;
        case 0x52: case 0x53: r[r1(op)] = rd8(val14s(op)); break;
        case 0x54: case 0x55: r[r1(op)] = uint32_t(int32_t(int16_t(rd16(val14s(op))))); break;
        case 0x56: case 0x57: r[r1(op)] = rd16(val14s(op)); break;
        case 0x58: case 0x59: r[r1(op)] = rd32(val14s(op)); break;
        case 0x5a: case 0x5b: wr8(val14s(op), uint8_t(r[r1(op)])); break;
        case 0x5c: case 0x5d: wr16(val14s(op), uint16_t(r[r1(op)])); break;
        case 0x5e: case 0x5f: wr32(val14s(op), r[r1(op)]); break;
        case 0x60: case 0x61: r[r1(op)] = doAdd(r[r1(op)], val6s(op)); break;
        case 0x62: case 0x63: r[r1(op)] = val6s(op); break;
        case 0x64: case 0x65: r[r1(op)] = doSub(r[r1(op)], val6s(op)); break;
        case 0x66: case 0x67: doSub(r[r1(op)], val6s(op)); break;
        case 0x6a: case 0x6b: r[r1(op)] = doAsr(r[r2(op)], val3u(op)); break;
        case 0x6c: case 0x6d: r[r1(op)] = doLsr(r[r2(op)], val3u(op)); break;
        case 0x6e: case 0x6f: r[r1(op)] = doLsl(r[r2(op)], val3u(op)); break;
        case 0x70: case 0x71: r[r1(op)] = uint32_t(int32_t(int8_t(rd8(r[6] + val6s(op))))); break;
        case 0x72: case 0x73: r[r1(op)] = rd8(r[6] + val6s(op)); break;
        case 0x74: case 0x75: r[r1(op)] = uint32_t(int32_t(int16_t(rd16(r[6] + val6s(op))))); break;
        case 0x76: case 0x77: r[r1(op)] = rd16(r[6] + val6s(op)); break;
        case 0x78: case 0x79: r[r1(op)] = rd32(r[6] + val6s(op)); break;
        case 0x7a: case 0x7b: wr8(r[6] + val6s(op), uint8_t(r[r1(op)])); break;
        case 0x7c: case 0x7d: wr16(r[6] + val6s(op), uint16_t(r[r1(op)])); break;
        case 0x7e: case 0x7f: wr32(r[6] + val6s(op), r[r1(op)]); break;
        case 0x80: case 0x81: r[r1(op)] = doAdd(r[r2(op)], r[r3(op)]); break;
        case 0x84: case 0x85: r[r1(op)] = doSub(r[r2(op)], r[r3(op)]); break;
        case 0x88: npc = r[r2(op)]; break;
        case 0x8a: case 0x8b: r[r1(op)] = snz(r[r2(op)] & r[r3(op)]); break;
        case 0x8c: case 0x8d: r[r1(op)] = snz(r[r2(op)] | r[r3(op)]); break;
        case 0x8e: case 0x8f: r[r1(op)] = snz(r[r2(op)] ^ r[r3(op)]); break;
        case 0x90: case 0x91: r[r1(op)] = uint32_t(int32_t(int8_t(rd8(r[r2(op)] + val3s(op))))); break;
        case 0x92: case 0x93: r[r1(op)] = rd8(r[r2(op)] + val3s(op)); break;
        case 0x94: case 0x95: r[r1(op)] = uint32_t(int32_t(int16_t(rd16(r[r2(op)] + val3s(op))))); break;
        case 0x96: case 0x97: r[r1(op)] = rd16(r[r2(op)] + val3s(op)); break;
        case 0x98: case 0x99: r[r1(op)] = rd32(r[r2(op)] + val3s(op)); break;
        case 0x9a: case 0x9b: wr8(r[r2(op)] + val3s(op), uint8_t(r[r1(op)])); break;
        case 0x9c: case 0x9d: wr16(r[r2(op)] + val3s(op), uint16_t(r[r1(op)])); break;
        case 0x9e: case 0x9f: wr32(r[r2(op)] + val3s(op), r[r1(op)]); break;
        case 0xa0: case 0xa1: r[r1(op)] = ~r[r2(op)]; break;
        case 0xa2: case 0xa3: r[r1(op)] = r[r2(op)]; break;
        case 0xa4: case 0xa5: r[r1(op)] = uint32_t(-int32_t(r[r2(op)])); break;
        case 0xa6: case 0xa7: doSub(r[r1(op)], r[r2(op)]); break;
        case 0xa8: { uint32_t t = r[r2(op)]; r[7] = npc; npc = t; break; }
        case 0xaa: case 0xab: r[r1(op)] = doAsr(r[r2(op)], r[r3(op)]); break;
        case 0xac: case 0xad: r[r1(op)] = doLsr(r[r2(op)], r[r3(op)]); break;
        case 0xae: case 0xaf: r[r1(op)] = doLsl(r[r2(op)], r[r3(op)]); break;
        case 0xb0: case 0xb1: hr[0] = r[r1(op)] * r[r2(op)]; break;
        case 0xb2: case 0xb3: hr[0] = uint32_t(int32_t(r[r1(op)]) * int32_t(r[r2(op)])); break;
        case 0xb4: case 0xb5: { uint64_t res = uint64_t(r[r1(op)]) * r[r2(op)]; hr[0] = uint32_t(res); hr[1] = uint32_t(res >> 32); break; }
        case 0xb6: case 0xb7: { int64_t res = int64_t(int32_t(r[r1(op)])) * int32_t(r[r2(op)]); hr[0] = uint32_t(res); hr[1] = uint32_t(uint64_t(res) >> 32); break; }
        case 0xbc: case 0xbd: { int32_t a = int32_t(r[r1(op)]), b = int32_t(r[r2(op)]);
            if (b && !(a == INT32_MIN && b == -1)) { hr[2] = uint32_t(a / b); hr[3] = uint32_t(a % b); } break; }
        case 0xbe: case 0xbf: { uint32_t a = r[r1(op)], b = r[r2(op)]; if (b) { hr[2] = a / b; hr[3] = a % b; } break; }
        case 0xc8: case 0xc9: r[r1(op)] = hr[val6u(op)]; break;
        case 0xca: case 0xcb: hr[val6u(op)] = r[r1(op)]; break;
        case 0xd0: if (hr[4] & F_V) npc = pc + val8s(op); break;
        case 0xd1: if (hr[4] & F_C) npc = pc + val8s(op); break;
        case 0xd2: if (hr[4] & F_Z) npc = pc + val8s(op); break;
        case 0xd3: if ((hr[4] & F_Z) || (hr[4] & F_C)) npc = pc + val8s(op); break;
        case 0xd4: if (hr[4] & F_N) npc = pc + val8s(op); break;
        case 0xd5: npc = pc + val8s(op); break;
        case 0xd6: if (bool(hr[4] & F_N) != bool(hr[4] & F_V)) npc = pc + val8s(op); break;
        case 0xd7: if ((hr[4] & F_Z) || bool(hr[4] & F_N) != bool(hr[4] & F_V)) npc = pc + val8s(op); break;
        case 0xd8: if (!(hr[4] & F_V)) npc = pc + val8s(op); break;
        case 0xd9: if (!(hr[4] & F_C)) npc = pc + val8s(op); break;
        case 0xda: if (!(hr[4] & F_Z)) npc = pc + val8s(op); break;
        case 0xdb: if (!(hr[4] & F_Z) && !(hr[4] & F_C)) npc = pc + val8s(op); break;
        case 0xdc: if (!(hr[4] & F_N)) npc = pc + val8s(op); break;
        case 0xdd: break;
        case 0xde: if (bool(hr[4] & F_N) == bool(hr[4] & F_V)) npc = pc + val8s(op); break;
        case 0xdf: if (!(hr[4] & F_Z) && bool(hr[4] & F_N) == bool(hr[4] & F_V)) npc = pc + val8s(op); break;
        case 0xe0: npc = r[7]; break;
        case 0xe1: hr[4] = m_s.if1; npc = m_s.ilr1; break;
        case 0xe3: break;
        case 0xf0: hr[4] &= ~F_C; break;
        case 0xf1: hr[4] |= F_C; break;
        // the game's own routines return yes / no with f7 / f6 and the callers test Z,
        // so f6 / f7 are the Z ops and f2 / f3 the V ops (MAME has them the other way)
        case 0xf2: hr[4] &= ~F_V; break;
        case 0xf3: hr[4] |= F_V; break;
        case 0xf4: hr[4] &= ~F_N; break;
        case 0xf5: hr[4] |= F_N; break;
        case 0xf6: hr[4] &= ~F_Z; break;
        case 0xf7: hr[4] |= F_Z; break;
        case 0xf8: hr[4] &= ~F_I; break;
        case 0xf9: hr[4] |= F_I; m_s.eiCount = 2; break;
        case 0xfc: break;
        case 0xfe:
            m_s.wait = 1;
            npc = checkInterrupt(npc - 1);
            if (m_s.wait) { m_s.pc = npc; m_icount = 0; return; }
            break;
        }
        m_s.pc = npc;
    }
}

// ---------------------------------------------------------------- frame

void Xavix2Core::runFrame()
{
    for (int g = 0; g < 2; g++) {
        float target = ((m_inputs >> (9 + g)) & 1) ? 1.0f : 0.0f;
        m_reach[g] += std::clamp(target - m_reach[g], -0.25f, 0.25f);
    }
    const int64_t perFrame = int64_t(CPU_CLOCK / 60.0);
    const int64_t slice = perFrame / 64;
    for (int i = 0; i < 64; i++) {
        execute(slice);
        if (m_s.linkLeft > 0) { m_s.linkLeft -= slice; if (m_s.linkLeft <= 0) { m_s.linkLeft = 0; irqRaise(IRQ_LINKDONE); } }
        if (m_s.wait && m_s.dmaLeft > 0) { m_s.dmaLeft -= slice; if (m_s.dmaLeft <= 0) { m_s.dmaLeft = 0; irqRaise(IRQ_DMA); } }
    }
    // vblank: show the 640x400 window, clear the surface to the background colour, timer interrupt
    const int dx = 0x400 - 320, dy = 0x200 - 200;
    for (int y = 0; y < 400; y++)
        for (int x = 0; x < 640; x++) {
            uint32_t c = m_sd[size_t(y + dy) * SDW + x + dx];
            m_out[size_t(y) * 640 + x] = c | 0xff000000u;
        }
    uint32_t bg = rgb555(m_s.bgColor) | 0xff000000u;
    std::fill(m_sd.begin(), m_sd.end(), bg);
#ifdef XAVIX_DEBUG
    if (!(getenv("X2NOTIMER") && x2Frame < atoi(getenv("X2NOTIMER"))))
#endif
    irqRaise(IRQ_TIMER);
    mixAudio(800);
#ifdef XAVIX_DEBUG
    x2Frame++;
#ifdef XAVIX_DEBUG
    if (const char* e = getenv("X2PCH")) {   // "from-to": count executed PCs (16-byte buckets) between those frames
        int a = 0, b = 0; sscanf(e, "%d-%d", &a, &b);
        if (x2Frame == a) x2Pch = (uint32_t*)calloc(1 << 20, 4);
        if (x2Frame == b && x2Pch) { FILE* f = fopen("/tmp/x2pch.txt", "w"); for (int i = 0; i < (1 << 20); i++) if (x2Pch[i]) fprintf(f, "%06x %u\n", i << 4, x2Pch[i]); fclose(f); free(x2Pch); x2Pch = nullptr; }
    }
    if (const char* e = getenv("X2DUMP")) if (x2Frame == atoi(e)) { FILE* f = fopen("/tmp/x2ram.bin", "wb"); fwrite(m_s.ram, 1, sizeof(m_s.ram), f); fclose(f); f = fopen("/tmp/x2vram.bin", "wb"); fwrite(m_s.vram, 1, sizeof(m_s.vram), f); fclose(f); }
#endif
    if (const char* e = getenv("X2IRQ")) { static int fr2 = 0; if ((++fr2 % 4) == 0) irqRaise(atoi(e)); }
#endif
#ifdef XAVIX_DEBUG
    static int fr = 0;
    if (getenv("X2DBG") && (++fr % atoi(getenv("X2DBG"))) == 0) fprintf(stderr, "f%d hr4=%02x en=%04x nmi=%04x act=%04x pc=%08x pio mode %08x %08x data %08x\n", fr, m_s.hr[4], m_s.intEnabled, m_s.intNmi, m_s.intActive, m_s.pc, m_s.pioMode[0], m_s.pioMode[1], m_s.pioData);
#endif
}

size_t Xavix2Core::readAudio(int16_t* out, size_t maxSamples)
{
    size_t n = std::min(maxSamples, m_audio.size());
    std::copy(m_audio.begin(), m_audio.begin() + n, out);
    m_audio.erase(m_audio.begin(), m_audio.begin() + n);
    if (m_audio.size() > 24000) m_audio.clear();
    return n;
}

// ---------------------------------------------------------------- sound
//
// Worked out from the game (MAME has no sound for this chip). Each voice is 16
// registers at $C0010000 + voice * 64, a 16-bit value in the top half of each word:
//   0/1 start address (high, low)   3/4 loop address   5 volume   7 mode ($72 = 8-bit)
//   8 pitch (4-bit octave, 12-bit fraction)   12 pan (left byte, right byte)
// Samples are signed 8-bit, ended by $80. Commands at $EA0A, voice in the low 6 bits:
//   $40 start   $240 start, looping to the loop address at the end   $80 stop
//   $C0 set volume / pan from $EA18 / $EA1C / $EA1D

void Xavix2Core::soundCommand(uint16_t cmd)
{
    int v = cmd & 0x3f;
    auto& vc = m_s.voice[v];
    const uint8_t* r = &m_s.vram[0x10000 + v * 64];
    auto w = [&](int i) { return uint16_t(r[i * 4 + 2] | (r[i * 4 + 3] << 8)); };
    switch (cmd & 0x3c0) {
    case 0x040: case 0x240: {
        if ((w(7) & 0xff) != 0x72) { vc.on = 0; break; }   // other modes aren't understood yet
        vc.pos = (uint32_t(w(0)) << 16) | w(1);
        vc.loop = (uint32_t(w(3)) << 16) | w(4);
        vc.frac = 0;
        uint16_t p = w(8);
        vc.step = uint32_t(std::ldexp(1.0 + (p & 0xfff) / 4096.0, (p >> 12) - 5) * 65536.0);
        vc.vol = uint8_t(w(5));
        vc.panL = r[0x32]; vc.panR = r[0x33];
        vc.looping = (cmd & 0x200) ? 1 : 0;
        vc.on = 1;
        break;
    }
    case 0x080: vc.on = 0; break;
    case 0x0c0: vc.vol = uint8_t(m_s.sndVol); vc.panL = m_s.sndPanL; vc.panR = m_s.sndPanR; break;
    }
}

void Xavix2Core::mixAudio(int n)
{
    for (int i = 0; i < n; i++) {
        int32_t acc = 0;
        for (auto& vc : m_s.voice) {
            if (!vc.on) continue;
            uint8_t b = vc.pos < m_rom.size() ? m_rom[vc.pos] : 0x80;
            if (b == 0x80) {
                if (vc.looping && vc.loop < m_rom.size() && m_rom[vc.loop] != 0x80) { vc.pos = vc.loop; b = m_rom[vc.pos]; }
                else { vc.on = 0; continue; }
            }
            acc += int32_t(int8_t(b)) * vc.vol * (vc.panL + vc.panR) / 2;
            vc.frac += vc.step;
            vc.pos += vc.frac >> 16;
            vc.frac &= 0xffff;
        }
        acc >>= 5;
        m_audio.push_back(int16_t(std::clamp(acc, -32768, 32767)));
    }
}

bool Xavix2Core::saveState(std::vector<uint8_t>& out)
{
    out.resize(sizeof(m_s));
    std::memcpy(out.data(), &m_s, sizeof(m_s));
    return true;
}

bool Xavix2Core::loadState(const std::vector<uint8_t>& in)
{
    if (in.size() != sizeof(m_s)) return false;
    std::memcpy(&m_s, in.data(), sizeof(m_s));
    return true;
}
