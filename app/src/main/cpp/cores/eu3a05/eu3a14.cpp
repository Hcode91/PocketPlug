// PocketPlug - Elan EU3A14 TV game SoC (see eu3a14.h)
// SPDX-License-Identifier: GPL-2.0-or-later
#include "eu3a14.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>

namespace {
const int8_t kIndexShift[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };
int g_diff[49 * 16];
bool g_tables = false;
void buildTables()
{
    if (g_tables) return;
    g_tables = true;
    static const int8_t nbl2bit[16][4] = {
        { 1, 0, 0, 0}, { 1, 0, 0, 1}, { 1, 0, 1, 0}, { 1, 0, 1, 1},
        { 1, 1, 0, 0}, { 1, 1, 0, 1}, { 1, 1, 1, 0}, { 1, 1, 1, 1},
        {-1, 0, 0, 0}, {-1, 0, 0, 1}, {-1, 0, 1, 0}, {-1, 0, 1, 1},
        {-1, 1, 0, 0}, {-1, 1, 0, 1}, {-1, 1, 1, 0}, {-1, 1, 1, 1} };
    for (int step = 0; step <= 48; step++) {
        int stepval = int(std::floor(16.0 * std::pow(11.0 / 10.0, double(step))));
        for (int nib = 0; nib < 16; nib++)
            g_diff[step * 16 + nib] = nbl2bit[nib][0] * (stepval * nbl2bit[nib][1] + stepval / 2 * nbl2bit[nib][2]
                + stepval / 4 * nbl2bit[nib][3] + stepval / 8);
    }
}
double hue2rgb(double p, double q, double t)
{
    if (t < 0) t += 1;
    if (t > 1) t -= 1;
    if (t < 1 / 6.0) return p + (q - p) * 6 * t;
    if (t < 1 / 2.0) return q;
    if (t < 2 / 3.0) return p + (q - p) * (2 / 3.0 - t) * 6;
    return p;
}
// video register offsets (5100 + n)
enum { TILECFG = 0x10, ROWSCROLLCFG = 0x16, ROWSCROLLSPLIT = 0x1a, SCROLL = 0x21, ROWSCROLL = 0x25, RAMTILECFG = 0x40, SPRITEADDR = 0x50, SPRITEBASE = 0x51 };
}

Eu3a14Core::Eu3a14Core(const std::string& game) : m_game(game)
{
    buildTables();
    m_frame.assign(256 * 224, 0xff000000u);
    std::memset(&m_s, 0, sizeof(m_s));
    std::memset(m_pen, 0, sizeof(m_pen));
}

bool Eu3a14Core::load(const std::vector<RomFile>& roms, std::string& error)
{
    if (roms.empty() || roms[0].data.size() < 0x10000) { error = "The game ROM is needed"; return false; }
    // the external bus: 8 MB (smaller ROMs mirrored)
    const auto& d = roms[0].data;
    m_rom.assign(0x800000, 0xff);
    for (size_t i = 0; i < m_rom.size(); i++) m_rom[i] = d[i % d.size()];
    m_cpu.setBus(this, rdThunk, wrThunk);
    std::memset(&m_s, 0, sizeof(m_s));
    m_s.rng = 0x1234567;
    reset();
    return true;
}

void Eu3a14Core::reset()
{
    std::memset(m_s.ram, 0, sizeof(m_s.ram));
    m_s.intmask[0] = m_s.intmask[1] = 0;
    m_s.customNmi = 0;
    m_s.nmiVector = 0;
    m_s.rombankLo = 0x7f;
    m_s.rombankHi = 0;
    m_s.bank = 0x01;          // elan_eu3a14_soc_device::device_reset
    std::memset(m_s.portDir, 0, sizeof(m_s.portDir));
    std::memset(m_s.dma, 0, sizeof(m_s.dma));
    std::memset(m_s.vid, 0, sizeof(m_s.vid));
    m_s.vid[SPRITEADDR] = uint8_t(m_defaultSpriteAddr);
    for (int i = 0; i < 6; i++) { m_s.sndAddr[i] = m_s.sndLen[i] = m_s.sndPos[i] = 0; m_s.adSignal[i] = m_s.adStep[i] = 0; }
    m_s.stopped = 0x3f;
    m_s.trigger = 0;
    m_s.volumes[0] = 0xff;
    m_s.volumes[1] = 0x0f;
    m_s.timerCycles = CPU_HZ / 4096;
    m_s.sndCycles = 0;
    m_s.serLine = 1; m_s.serWait = 0; m_s.serPos = m_s.serLen = 0;
    m_cpu.m_irqLine = false;
    m_cpu.m_nmiPending = false;
    m_cpu.reset();
    m_cpu.s = 0xff;
}

// ---------------- memory ----------------
uint8_t Eu3a14Core::portRead(int port)
{
    // active low. batvgc (tsbuzz): IN1 bit 0 pause, bit 1 skip, bit 2 the controller's serial line
    if (port == 1) {
        m_s.rng = m_s.rng * 1103515245u + 12345u;
        uint8_t v = 0xff;
        if (m_inputs & 0x01) v &= ~0x01;            // the base unit's PAUSE key
        if (!m_s.serLine) v &= ~0x04;
        return v;
    }
    return 0xff;
}

uint8_t Eu3a14Core::read(uint16_t a)
{
    if (a < 0x4000) return m_s.ram[a];
    if (a >= 0xe000) {
        if (a == 0xfffa || a == 0xfffb) return m_s.customNmi ? uint8_t(m_s.nmiVector >> ((a & 1) * 8)) : 0;
        return ext(a - 0xe000);
    }
    if (a >= 0x6000) return ext(uint32_t(m_s.bank) * 0x8000u + (a - 0x6000));
    if (a >= 0x4800 && a < 0x4c00) return m_s.palram[a - 0x4800];
    if (a >= 0x5000 && a < 0x5020) return sysRead(a - 0x5000);
    if (a >= 0x5040 && a < 0x5046) {
        int o = a - 0x5040, port = o / 2;
        if (!(o & 1)) return m_s.portDir[port];
#ifndef __ANDROID__
        if (getenv("EAIO")) { static int n = 0; if (n++ < atoi(getenv("EAIO"))) fprintf(stderr, "R %04x pc %04x dir %02x out %02x\n", a, m_cpu.pc, m_s.portDir[port], m_s.portOut[port]); }
#endif
        return portRead(port);
    }
    if (a >= 0x5080 && a < 0x50c0) {
        int o = a - 0x5080;
        if (o < 0x12) return uint8_t(m_s.sndAddr[o / 3] >> ((o % 3) * 8));
        if (o < 0x24) { o -= 0x12; return uint8_t(m_s.sndLen[o / 3] >> ((o % 3) * 8)); }
        switch (o) {
        case 0x24: return m_s.r50a4;
        case 0x25: return m_s.trigger;
        case 0x26: case 0x27: return m_s.volumes[o - 0x26];
        case 0x28: return uint8_t(m_s.stopped | 0xc0);
        case 0x29: return m_s.r50a9;
        default: return 0;
        }
    }
    if (a >= 0x5100 && a < 0x5180) {
        int o = a - 0x5100;
        if (o <= 0x09 || (o >= 0x10 && o <= 0x17) || (o >= 0x1a && o <= 0x1e) || (o >= 0x21 && o <= 0x2c) || (o >= 0x40 && o <= 0x4b) || (o >= 0x50 && o <= 0x53))
            return m_s.vid[o];
        return 0;
    }
    return 0;
}

void Eu3a14Core::write(uint16_t a, uint8_t d)
{
    if (a < 0x4000) { m_s.ram[a] = d; return; }
    if (a >= 0x6000 && a < 0xe000) return;                     // ROM
    if (a >= 0x4800 && a < 0x4c00) { m_s.palram[a - 0x4800] = d; updatePen((a - 0x4800) / 2); return; }
    if (a >= 0x5000 && a < 0x5020) { sysWrite(a - 0x5000, d); return; }
    if (a >= 0x5040 && a < 0x5046) {
#ifndef __ANDROID__
        if (getenv("EAIO")) { static int n = 0; if (n++ < atoi(getenv("EAIO"))) fprintf(stderr, "W %04x = %02x pc %04x\n", a, d, m_cpu.pc); }
#endif
        int o = a - 0x5040; if (!(o & 1)) m_s.portDir[o / 2] = d; else m_s.portOut[o / 2] = d; return; }
    if (a >= 0x5080 && a < 0x50c0) {
        int o = a - 0x5080;
        if (o < 0x12) { int sh = (o % 3) * 8; m_s.sndAddr[o / 3] = (m_s.sndAddr[o / 3] & ~(0xffu << sh)) | uint32_t(d) << sh; return; }
        if (o < 0x24) { o -= 0x12; int sh = (o % 3) * 8; m_s.sndLen[o / 3] = (m_s.sndLen[o / 3] & ~(0xffu << sh)) | uint32_t(d) << sh; return; }
        switch (o) {
        case 0x24: m_s.r50a4 = d; return;
        case 0x25:
            m_s.trigger = d;
            for (int ch = 0; ch < 6; ch++) {
                if (!((d >> ch) & 1)) continue;
                if ((m_s.stopped & (1 << ch)) && m_s.sndLen[ch]) {
                    m_s.sndPos[ch] = 0;
                    m_s.stopped &= uint8_t(~(1 << ch));
                    m_s.adSignal[ch] = m_s.adStep[ch] = 0;
                }
            }
            return;
        case 0x26: case 0x27: m_s.volumes[o - 0x26] = d; return;
        case 0x29: m_s.r50a9 = d; return;
        default: return;
        }
    }
    if (a >= 0x5100 && a < 0x5180) { m_s.vid[a - 0x5100] = d; return; }
}

// ---------------- system: interrupts, banking, DMA ----------------
uint8_t Eu3a14Core::sysRead(int o)
{
    switch (o) {
    case 0x00: case 0x01: case 0x04: case 0x06: case 0x0a: return m_s.sysRam[o];
    case 0x03: case 0x09:
        m_s.rng = m_s.rng * 1103515245u + 12345u;
        return uint8_t(m_s.rng >> 16);
    case 0x07: return m_s.intmask[0];
    case 0x08: return m_s.intmask[1];
    case 0x0b: return 0xff;                 // NTSC
    case 0x0c: return m_s.rombankHi;
    case 0x0d: return m_s.rombankLo;
    default:
        if (o >= 0x0f && o <= 0x17) return m_s.dma[o - 0x0f];
        return 0;
    }
}

void Eu3a14Core::sysWrite(int o, uint8_t d)
{
    switch (o) {
    case 0x00: case 0x01: case 0x04: case 0x06: case 0x0a: m_s.sysRam[o] = d; return;
    case 0x07: m_s.intmask[0] = d; return;
    case 0x08: m_s.intmask[1] = d; return;
    case 0x0c: m_s.rombankHi = d; m_s.bank = uint16_t(m_s.rombankLo | m_s.rombankHi << 8); return;
    case 0x0d: m_s.rombankLo = d; return;
    case 0x18:
        if (d == 0x08) {
            uint32_t src = uint32_t(m_s.dma[0] | m_s.dma[1] << 8 | m_s.dma[2] << 16);
            uint32_t dst = uint32_t(m_s.dma[3] | m_s.dma[4] << 8 | m_s.dma[5] << 16);
            uint32_t len = uint32_t(m_s.dma[6] | m_s.dma[7] << 8 | m_s.dma[8] << 16);
            for (uint32_t i = 0; i < len; i++) write(uint16_t(dst + i), ext(src + i));
        }
        return;
    default:
        if (o >= 0x0f && o <= 0x17) m_s.dma[o - 0x0f] = d;
        return;
    }
}

void Eu3a14Core::customInterrupt(int level)
{
    uint16_t mask = uint16_t(m_s.intmask[0] << 8 | m_s.intmask[1]);
    if (mask & (1 << level)) {
        m_s.customNmi = 1;
        m_s.nmiVector = uint16_t(0xffb0 + level * 4);
        m_cpu.pulseNmi();
    }
}

// ---------------- video ----------------
void Eu3a14Core::updatePen(int pen)
{
    uint16_t dat = uint16_t(m_s.palram[pen * 2] << 8 | m_s.palram[pen * 2 + 1]);
    double l = ((dat & 0xf800) >> 11) / 31.0;
    double s = ((dat & 0x0700) >> 8) / 7.0;
    double h = (dat & 0x001f) / 24.0;
    double r, g, b;
    if (s == 0) r = g = b = l;
    else {
        double q = l < 0.5 ? l * (1 + s) : l + s - l * s;
        double p = 2 * l - q;
        r = hue2rgb(p, q, h + 1 / 3.0);
        g = hue2rgb(p, q, h);
        b = hue2rgb(p, q, h - 1 / 3.0);
    }
    int ri = int(r * 255.0), gi = int(g * 255.0), bi = int(b * 255.0);
    m_pen[pen] = 0xff000000u | uint32_t(std::clamp(ri, 0, 255)) << 16 | uint32_t(std::clamp(gi, 0, 255)) << 8 | uint32_t(std::clamp(bi, 0, 255));
}

int Eu3a14Core::xscrollFor(int y) const
{
    if (y < 0 || y >= 224) return 0;
    const uint8_t* v = m_s.vid;
    int xscroll = v[SCROLL] | v[SCROLL + 1] << 8;
    if (v[ROWSCROLLCFG + 1] == 0x01) {
        const uint8_t* sp = &v[ROWSCROLLSPLIT];
        const uint8_t* rs = &v[ROWSCROLL];
        if (y < sp[0]) return xscroll;
        for (int i = 0; i < 4; i++) if (y < sp[i + 1]) return rs[i * 2] | rs[i * 2 + 1] << 8;
        return 0;
    }
    return xscroll;
}

void Eu3a14Core::drawTile(int bpp, int tile, int palette, int priority, int xpos, int ypos, int size, int base, bool fromRam)
{
    int baseaddr = base * 256;
    int xstride;
    if (bpp == 8) { xstride = size; baseaddr += tile * (size == 8 ? 64 : 256); palette &= 0x100; }
    else if (bpp == 4) { xstride = size / 2; baseaddr += tile * (size == 8 ? 32 : 128); }
    else if (bpp == 2) { xstride = size / 4; baseaddr += tile * 64; }
    else return;
    int count = 0;
    for (int y = 0; y < size; y++) {
        int realy = ypos + y;
        if (!fromRam) realy = ((realy % m_wrapH) + m_wrapH) % m_wrapH;   // the tilemap wraps
        int xs = fromRam ? xpos : xpos - xscrollFor(realy);
        bool rowOk = realy >= 0 && realy < 224;
        uint32_t* dst = rowOk ? &m_frame[size_t(realy) * 256] : nullptr;
        uint8_t* pri = rowOk ? &m_pri[size_t(realy) * 256] : nullptr;
        for (int x = 0; x < xstride; x++, count++) {
            if (!rowOk) continue;
            uint8_t pix = fromRam ? vram((baseaddr + count) & 0x3fff) : ext(uint32_t(baseaddr + count) & 0x7fffff);
            if (bpp == 8) {
                int rx = wrapX(x + xs, fromRam);
                if (rx >= 0 && rx < 256 && pix && pri[rx] <= priority) { dst[rx] = m_pen[(pix | palette) & 0x1ff]; pri[rx] = uint8_t(priority); }
            } else if (bpp == 4) {
                for (int i = 0; i < 2; i++) {
                    int rx = wrapX(x * 2 + xs + i, fromRam);
                    uint8_t p = i ? (pix & 0x0f) : (pix >> 4);
                    if (rx >= 0 && rx < 256 && p && pri[rx] <= priority) { dst[rx] = m_pen[(p | palette) & 0x1ff]; pri[rx] = uint8_t(priority); }
                }
            } else {
                for (int i = 0; i < 4; i++) {
                    int rx = wrapX(x * 4 + xs + i, fromRam);
                    uint8_t p = uint8_t(pix >> (6 - i * 2) & 3);
                    // MAME tests the top pair for every pixel here
                    if (rx >= 0 && rx < 256 && (pix & 0xc0) && pri[rx] <= priority) { dst[rx] = m_pen[(p | palette) & 0x1ff]; pri[rx] = uint8_t(priority); }
                }
            }
        }
    }
}

void Eu3a14Core::drawPage(int ramstart, int ramend, int xbase, int ybase, int size, int bpp, int base, int pagewidth, int bytespertile, uint8_t palettepri, bool fromRam)
{
    int palette = (((palettepri & 0xf0) >> 4) | ((palettepri & 0x08) << 1)) << 4;
    int priority = palettepri & 0x07;
    int xdraw = xbase, ydraw = ybase, count = 0;
    for (int i = ramstart; i < ramend; i += bytespertile) {
        int tile = vram(i) | vram(i + 1) << 8;
        int rp = palette, rpri = priority, rbpp = bpp;
        if (bytespertile == 4) {
            uint8_t a2 = vram(i + 2);
            rp = (((a2 & 0xf0) >> 4) | ((a2 & 0x08) << 1)) << 4;
            rpri = a2 & 7;
            rbpp = vram(i + 3) & 7;
            if (!rbpp) rbpp = 8;
        }
        drawTile(rbpp, tile, rp, rpri, xdraw, ydraw, size, base, fromRam);
        xdraw += size;
        if (++count % pagewidth == 0) { xdraw -= size * pagewidth; ydraw += size; }
    }
}

void Eu3a14Core::drawRamLayer()
{
    const uint8_t* c = &m_s.vid[RAMTILECFG];
    if (!(c[0] & 0x80)) return;
    int size, pw, ph;
    if (c[0] & 0x10) { size = 8; pw = 32; ph = 28; } else { size = 16; pw = 16; ph = 14; }
    int bpp = c[2] & 7; if (!bpp) bpp = 8;
    int base = (0x2000 - 0x200) / 256;
    // MAME's guess is the map 0x700 bytes in; the Thinkway games keep their 16 x 16 RAM-tile
    // map (the menu text) 0x380 in, two 16 x 14 pages after the background's
    int rs = m_tileRamBase + (size == 16 && m_game == "batvgc" ? 0x380 : 0x700), re = rs + 0x700;
    if (c[5] == 0x06) { rs = 0x3980 - 0x200; re = rs + 0x700; }
    for (int k = 0; k < 4; k++)
        drawPage(rs, re, (k & 1) ? size * pw : 0, (k & 2) ? size * ph : 0, size, bpp, base, pw, 2, c[1], true);
}

void Eu3a14Core::drawBackground()
{
    const uint8_t* t = &m_s.vid[TILECFG];
    int yscroll = m_s.vid[SCROLL + 2] | m_s.vid[SCROLL + 3] << 8;
    int base = t[5] << 8 | t[4];
    int size, pw, ph;
    if (t[0] & 0x10) { size = 8; pw = 32; ph = 28; } else { size = 16; pw = 16; ph = 14; }
    int bpt = (t[0] & 0x80) ? 4 : 2;
    int bpp = t[2] & 7; if (!bpp) bpp = 8;
    int pagesize = pw * ph * 2 * (bpt == 4 ? 2 : 1);
    auto page = [&](int n, int x, int y) {
        int rs = m_tileRamBase + pagesize * n;
        drawPage(rs, rs + pagesize, x, y - yscroll, size, bpp, base, pw, bpt, t[1], false);
    };
    const int W = size * pw, H = size * ph;
    // each page is drawn once; the pixels wrap round the whole map
    switch (t[0] & 3) {
    case 0:   // 2 x 2 pages
        m_wrapW = 2 * W; m_wrapH = 2 * H;
        for (int n = 0; n < 4; n++) page(n, (n & 1) ? W : 0, (n & 2) ? H : 0);
        break;
    case 1:   // 2 x 1
        m_wrapW = 2 * W; m_wrapH = H;
        page(0, 0, 0); page(1, W, 0);
        break;
    case 3:   // 1 x 1
        m_wrapW = W; m_wrapH = H;
        page(0, 0, 0);
        drawRamLayer();
        break;
    default: break;
    }
}

void Eu3a14Core::spritePix(int realx, int y, int priority, uint8_t pix, uint8_t mask, uint8_t shift, int palette)
{
    if (realx < 0 || realx >= 256) return;
    uint8_t& p = m_pri[size_t(y) * 256 + realx];
    if (p <= priority && (pix & mask)) {
        m_frame[size_t(y) * 256 + realx] = m_pen[(((pix & mask) >> shift) | palette) & 0x1ff];
        p = uint8_t(priority);
    }
}

void Eu3a14Core::spriteLine(int offset, int line, int palette, int flipx, int priority, int xpos, int ypos, int bpp)
{
    offset *= 2;
    int bppdiv;
    switch (bpp) {
    default: offset += line * 8; bppdiv = 1; break;
    case 4: case 3: offset += line * 4; bppdiv = 2; break;
    case 2: offset += line * 2; bppdiv = 4; break;
    case 1: offset += line * 1; bppdiv = 8; break;
    }
    if (ypos < 0 || ypos >= 224) return;
    auto gfx = [&](int x) { return ext(uint32_t(offset + x) & 0x7fffff); };
    for (int x = 0, count = 0; x < 8 / bppdiv; x++, count++) {
        if (bpp == 8 || bpp == 7) {
            uint8_t pix = gfx(flipx ? 7 - count : count);
            spritePix(xpos + x, ypos, priority, pix, bpp == 8 ? 0xff : 0x7f, 0, palette);
        } else if (bpp == 4) {
            uint8_t pix = gfx(flipx ? 3 - count : count);
            int rx = xpos + x * 2;
            spritePix(rx, ypos, priority, pix, flipx ? 0x0f : 0xf0, flipx ? 0 : 4, palette);
            spritePix(rx + 1, ypos, priority, pix, flipx ? 0xf0 : 0x0f, flipx ? 4 : 0, palette);
        } else if (bpp == 2) {
            uint8_t pix = gfx(flipx ? 1 - count : count);
            int rx = xpos + x * 4;
            for (int i = 0; i < 4; i++) {
                int sh = flipx ? i * 2 : 6 - i * 2;
                spritePix(rx + i, ypos, priority, pix, uint8_t(3 << sh), uint8_t(sh), palette);
            }
        }
    }
}

void Eu3a14Core::drawSprites()
{
    int base = m_s.vid[SPRITEADDR] * 0x200 - 0x200;
    int spritebase = m_s.vid[SPRITEBASE + 1] << 8 | m_s.vid[SPRITEBASE];
    for (int i = base; i < base + 0x800; i += 8) {
        int attr = vram(i);
        if (!(attr & 0x80)) continue;
        int y = vram(i + 1), x = vram(i + 2), pp = vram(i + 3);
        static const int hs[4] = { 2, 4, 8, 16 }, ws[4] = { 1, 2, 4, 8 };
        int height = hs[(attr >> 2) & 3], width = ws[attr & 3];
        int flipx = (attr >> 4) & 1, flipy = (attr >> 5) & 1;
        int pri = pp & 7;
        int palette = (((pp & 0xf0) >> 4) | ((pp & 0x08) << 1)) << 4;
        y -= height * 2 - 4;
        x -= width * 4 - 4;
        height *= 4;
        x -= 6;
        y -= 4;
        int offset = vram(i + 5) << 8 | vram(i + 4);
        int extra = vram(i + 6);
        offset += (extra & 0xf8) << 13;
        offset += spritebase << 7;
        int bpp = extra & 7; if (!bpp) bpp = 8;
        int count = 0;
        for (int yy = 0; yy < height; yy++) {
            int yoff = flipy ? height - 1 - yy : yy;
            for (int xx = 0; xx < width; xx++) {
                int xoff = flipx ? (width - 1) * 8 - xx * 8 : xx * 8;
                spriteLine(offset, count, palette, flipx, pri, x + xoff, y + yoff, bpp);
                count++;
            }
        }
    }
}

void Eu3a14Core::render()
{
    std::fill(m_frame.begin(), m_frame.end(), m_pen[0]);
    std::memset(m_pri, 0, sizeof(m_pri));
    drawBackground();
    drawSprites();
}

// ---------------- sound: six ADPCM channels at 8 kHz ----------------
int Eu3a14Core::adpcmClock(int ch, uint8_t nib)
{
    int32_t& sig = m_s.adSignal[ch];
    int32_t& step = m_s.adStep[ch];
    sig += g_diff[step * 16 + (nib & 15)];
    sig = std::clamp(sig, -2048, 2047);
    step = std::clamp(step + kIndexShift[nib & 7], 0, 48);
    return sig;
}

void Eu3a14Core::soundSample()
{
    int volume = m_s.volumes[0] | m_s.volumes[1] << 8;
    int total = 0;
    for (int ch = 0; ch < 6; ch++) {
        if ((m_s.stopped >> ch) & 1) continue;
        uint32_t off = m_s.sndAddr[ch] + m_s.sndPos[ch] / 2;
        uint8_t nib = ext(off);
        nib = uint8_t(nib >> ((m_s.sndPos[ch] & 1) ? 0 : 4));
        int v = adpcmClock(ch, nib & 0x0f);
        total += v * ((volume >> (ch * 2)) & 3) / 3;
        if (++m_s.sndPos[ch] >= m_s.sndLen[ch] * 2) {
            m_s.sndPos[ch] = 0;
            m_s.adSignal[ch] = m_s.adStep[ch] = 0;
            m_s.stopped |= uint8_t(1 << ch);
            customInterrupt(2 + ch);          // sound end
        }
    }
    int16_t out = int16_t(std::clamp(total * 4, -32768, 32767));
    m_s.resampleAcc += 44100;
    while (m_s.resampleAcc >= 8000) {
        m_s.resampleAcc -= 8000;
        if (m_audio.size() < 44100) m_audio.push_back(out);
    }
}

size_t Eu3a14Core::readAudio(int16_t* out, size_t maxSamples)
{
    size_t n = std::min(maxSamples, m_audio.size());
    std::copy(m_audio.begin(), m_audio.begin() + long(n), out);
    m_audio.erase(m_audio.begin(), m_audio.begin() + long(n));
    return n;
}

// ---------------- the motion gear's radio link ----------------
// Read on port B bit 2 by the 2048 Hz timer: a '1' is two short gaps between edges (2 ticks
// each), a '0' one long gap (6 ticks); 12 quiet ticks end a frame. A frame is 18 bits, MSB
// first: 111101, the 4-bit move code twice, then (code + code) & 15.
int Eu3a14Core::gearCode() const
{
    // the moves seen in Villains of Gotham City: 4 punch, 10 kick, 6 guard, 11 jab, 7 the
    // fourth move; bits 16-31 send any code (testing)
    if (m_inputs & 0x10) return 4;
    if (m_inputs & 0x20) return 10;
    if (m_inputs & 0x40) return 6;
    if (m_inputs & 0x80) return 11;
    if (m_inputs & 0x200) return 7;
    for (int v = 0; v < 16; v++) if (m_inputs >> (16 + v) & 1) return v;
    return -1;
}

void Eu3a14Core::serialTick()
{
    if (m_s.serWait > 0) { m_s.serWait--; return; }
    if (m_s.serPos >= m_s.serLen) {
        // build the next frame (or stay quiet)
        int code = gearCode();
        m_s.serLen = m_s.serPos = 0;
        if (code < 0) { m_s.serWait = 12; return; }
        uint32_t bits = 0x3du << 12 | uint32_t(code) << 8 | uint32_t(code) << 4 | uint32_t((code * 2) & 15);
        m_s.serGap[m_s.serLen++] = 12;
        for (int b = 17; b >= 0; b--) {
            if (bits >> b & 1) { m_s.serGap[m_s.serLen++] = 2; m_s.serGap[m_s.serLen++] = 2; }
            else m_s.serGap[m_s.serLen++] = 6;
        }
    }
    // toggle the line, then wait out this gap
    m_s.serLine ^= 1;
    m_s.serWait = m_s.serGap[m_s.serPos++] - 1;
}

// ---------------- frame ----------------
void Eu3a14Core::setInputs(uint32_t pressed)
{
    bool rst = (pressed & 0x100) && !(m_inputs & 0x100);
    m_inputs = pressed;
    if (rst) reset();
}

void Eu3a14Core::runFrame()
{
    const int perFrame = CPU_HZ / 60;
    const int timerPeriod = CPU_HZ / 2048;
    const int sndPeriod = CPU_HZ / 8000;
    int done = 0;
    while (done < perFrame) {
        int n = m_cpu.step();
        done += n;
        m_s.timerCycles -= n;
        while (m_s.timerCycles <= 0) { m_s.timerCycles += timerPeriod; serialTick(); customInterrupt(0); }
        m_s.sndCycles += n;
        while (m_s.sndCycles >= sndPeriod) { m_s.sndCycles -= sndPeriod; soundSample(); }
    }
    render();
#ifndef __ANDROID__
    if (const char* f = getenv("EARAM")) { static int n = 0; if (++n == atoi(getenv("EAFRAME") ? getenv("EAFRAME") : "300")) { FILE* fp = fopen(f, "wb"); if (fp) { fwrite(m_s.ram, 1, sizeof(m_s.ram), fp); fwrite(m_s.vid, 1, sizeof(m_s.vid), fp); fclose(fp); } } }
#endif
    customInterrupt(9);        // vblank
}

// ---------------- save states ----------------
bool Eu3a14Core::saveState(std::vector<uint8_t>& out)
{
    struct Regs { uint16_t pc; uint8_t a, x, y, s, p, irq, nmi; } c = { m_cpu.pc, m_cpu.a, m_cpu.x, m_cpu.y, m_cpu.s, m_cpu.p, m_cpu.m_irqLine, m_cpu.m_nmiPending };
    out.clear();
    uint32_t magic = 0x34314145;   // 'EA14'
    auto put = [&](const void* p, size_t n) { const uint8_t* b = (const uint8_t*)p; out.insert(out.end(), b, b + n); };
    put(&magic, 4); put(&m_s, sizeof(m_s)); put(&c, sizeof(c));
    return true;
}

bool Eu3a14Core::loadState(const std::vector<uint8_t>& in)
{
    struct Regs { uint16_t pc; uint8_t a, x, y, s, p, irq, nmi; } c;
    if (in.size() != 4 + sizeof(m_s) + sizeof(c)) return false;
    uint32_t magic; std::memcpy(&magic, in.data(), 4);
    if (magic != 0x34314145) return false;
    std::memcpy(&m_s, in.data() + 4, sizeof(m_s));
    std::memcpy(&c, in.data() + 4 + sizeof(m_s), sizeof(c));
    m_cpu.pc = c.pc; m_cpu.a = c.a; m_cpu.x = c.x; m_cpu.y = c.y; m_cpu.s = c.s; m_cpu.p = c.p;
    m_cpu.m_irqLine = c.irq; m_cpu.m_nmiPending = c.nmi;
    for (int i = 0; i < 512; i++) updatePen(i);
    return true;
}
