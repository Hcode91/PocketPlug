// PocketPlug - Sega Mega Drive / Genesis VDP (315-5313)
// SPDX-License-Identifier: BSD-3-Clause
//
// Written for PocketPlug from the documented behaviour of the Mega Drive VDP (Charles
// MacDonald's "Sega Genesis VDP documentation", the Sega "Genesis Software Manual"):
// control / data ports, the 24 registers, 64 KB VRAM, CRAM, VSRAM, the three DMA modes
// (68000 to VDP, fill, VRAM copy; done at once), H / V interrupts, the HV counter, and a
// line renderer for planes A and B, the window, sprites (link list, per-line limits,
// masking, collision), full / cell / line horizontal scroll, full / 2-cell vertical
// scroll, H32 / H40, and shadow / highlight.

#pragma once
#include <cstdint>
#include <cstring>
#include <functional>

class MdVdp {
public:
    static constexpr int W = 320, H = 240;
    bool pal = true;
    std::function<uint16_t(uint32_t)> busRead16 = [](uint32_t) { return uint16_t(0); };

    struct State {
        uint8_t vram[0x20000];      // 128 KB on the Firecore (A16 in bit 2 of the second control word)
        uint16_t cram[64], vsram[40];
        uint16_t xcram[1024];      // Firecore: CRAM writes beyond 0x80 (24-bit palette at 0x400)
        uint8_t reg[32];
        uint32_t addr;
        uint8_t code, pending, fillPending;
        uint8_t vintPending, hintPending;
        int16_t hcounter;          // H interrupt counter
        int16_t line;              // current scanline
        uint16_t readBuffer;
        uint8_t inVblank, odd;
    } s;

    MdVdp() { reset(); }
    void reset() { std::memset(&s, 0, sizeof(s)); }

    int lines() const { return pal ? 313 : 262; }
    int visibleLines() const { return (s.reg[1] & 8) && pal ? 240 : 224; }
    bool h40() const { return s.reg[12] & 0x81; }
    int width() const { return h40() ? 320 : 256; }

    // interrupt level the 68000 sees (0 none)
    int irqLevel() const
    {
        if (s.vintPending && (s.reg[1] & 0x20)) return 6;
        if (s.hintPending && (s.reg[0] & 0x10)) return 4;
        return 0;
    }
    void ackIrq(int level) { if (level == 6) s.vintPending = 0; else if (level == 4) s.hintPending = 0; }

    int hpos = 0;          // 68000 cycles into the line (0..488), for HBLANK / the H counter
    uint16_t status() const
    {
        uint16_t st = 0x3400 | 0x0200;   // FIFO empty
        if (hpos >= 400 || hpos < 8) st |= 0x04;   // HBLANK
        if (s.vintPending) st |= 0x80;
        if (s.inVblank || !(s.reg[1] & 0x40)) st |= 0x08;
        if (s.odd) st |= 0x10;
        if (pal) st |= 0x01;
        return st;
    }

    uint16_t hvCounter(int hpos) const
    {
        int v = s.line;
        if (pal) { if (v > 0x102) v -= 57; } else if (v > 0xea) v -= 6;
        int h = h40() ? (hpos * 0xb6 / 420) : (hpos * 0x94 / 342);   // rough
        return uint16_t((v & 0xff) << 8 | (h & 0xff));
    }

    uint16_t readControl() { s.pending = 0; return status(); }

    uint16_t readData()
    {
        s.pending = 0;
        uint16_t r = 0;
        switch (s.code & 0x0f) {
        case 0x00: r = uint16_t(s.vram[s.addr & 0x1fffe] << 8 | s.vram[(s.addr & 0x1fffe) | 1]); break;
        case 0x08: r = s.cram[(s.addr >> 1) & 63]; break;
        case 0x04: { int i = (s.addr >> 1) & 63; r = i < 40 ? s.vsram[i] : s.vsram[0]; break; }
        default: break;
        }
        s.addr = (s.addr + s.reg[15]) & 0x1ffff;
        return r;
    }

    void writeData(uint16_t d)
    {
        s.pending = 0;
        if (s.fillPending) {
            s.fillPending = 0;
            uint32_t len = uint32_t(s.reg[19] | s.reg[20] << 8); if (!len) len = 0x10000;
            writeTarget(d);
            uint8_t hi = uint8_t(d >> 8);
            for (uint32_t i = 0; i < len; i++) {
                if ((s.code & 0x0f) == 1) s.vram[(s.addr ^ 1) & 0x1ffff] = hi;
                else writeTarget(d);
                s.addr = (s.addr + s.reg[15]) & 0x1ffff;
            }
            s.reg[19] = s.reg[20] = 0;
            return;
        }
        writeTarget(d);
        s.addr = (s.addr + s.reg[15]) & 0x1ffff;
    }

    void writeControl(uint16_t d)
    {
        if (traceWrite) traceWrite(0xfe, 0, d);
        if (!s.pending) {
            if ((d & 0xc000) == 0x8000) {
                int r = (d >> 8) & 0x1f;
                if (r < 32) s.reg[r] = uint8_t(d);
                if (traceWrite) traceWrite(0xff, uint16_t(r), uint16_t(d & 0xff));
                s.code = 0;
                return;
            }
            s.code = uint8_t((s.code & 0x3c) | (d >> 14));
            s.addr = (s.addr & 0x1c000) | (d & 0x3fff);
            s.pending = 1;
            return;
        }
        s.pending = 0;
        s.code = uint8_t((s.code & 0x03) | ((d >> 2) & 0x3c));
        s.addr = (s.addr & 0x3fff) | uint32_t(d & 3) << 14 | uint32_t((d >> 2) & 1) << 16;
        if ((s.code & 0x20) && (s.reg[1] & 0x10)) dma();
    }

    // call at the start of each line (before running the CPUs for it); returns true at vblank start
    bool beginLine(int line)
    {
        s.line = int16_t(line);
        int vis = visibleLines();
        bool vb = false;
        if (line <= vis) {
            if (line == 0) s.hcounter = s.reg[10];
            else if (--s.hcounter < 0) { s.hcounter = s.reg[10]; s.hintPending = 1; }
        } else s.hcounter = s.reg[10];
        if (line == vis) { s.vintPending = 1; s.inVblank = 1; vb = true; }
        if (line == 0) { s.inVblank = 0; s.odd ^= 1; }
        return vb;
    }

    void renderLine(int y, uint32_t* out);
    bool extMode() const { return s.reg[24] & 0x80; }

private:
public:
    std::function<void(uint8_t code, uint16_t addr, uint16_t d)> traceWrite;
private:
    void writeTarget(uint16_t d)
    {
        if (traceWrite) traceWrite(s.code, uint16_t(s.addr), d);
        switch (s.code & 0x0f) {
        case 0x01:
            if (s.addr & 1) d = uint16_t(d << 8 | d >> 8);
            s.vram[s.addr & 0x1fffe] = uint8_t(d >> 8); s.vram[(s.addr & 0x1fffe) | 1] = uint8_t(d);
            break;
        case 0x03:
            s.xcram[(s.addr >> 1) & 1023] = d;
            if (s.addr < 0x80) s.cram[(s.addr >> 1) & 63] = d & 0x0eee;
            break;
        case 0x05: { int i = (s.addr >> 1) & 63; if (i < 40) s.vsram[i] = d & 0x07ff; break; }
        default: break;
        }
    }

    void dma()
    {
        uint32_t len = uint32_t(s.reg[19] | s.reg[20] << 8); if (!len) len = 0x10000;
        int mode = s.reg[23] >> 6;
        if (mode < 2) {   // 68000 -> VDP
            uint32_t src = (uint32_t(s.reg[21]) | uint32_t(s.reg[22]) << 8 | uint32_t(s.reg[23] & 0x7f) << 16) << 1;
            for (uint32_t i = 0; i < len; i++) {
                writeTarget(busRead16(src));
                s.addr = (s.addr + s.reg[15]) & 0x1ffff;
                src = (src & 0xfe0000) | ((src + 2) & 0x1ffff);
            }
            uint32_t ns = src >> 1;
            s.reg[21] = uint8_t(ns); s.reg[22] = uint8_t(ns >> 8);
            s.reg[19] = s.reg[20] = 0;
            s.code &= 0x1f;
        } else if (mode == 2) {
            s.fillPending = 1;   // on the next data write
        } else {   // VRAM copy
            uint16_t src = uint16_t(s.reg[21] | s.reg[22] << 8);
            for (uint32_t i = 0; i < len; i++) {
                s.vram[(s.addr ^ 1) & 0x1ffff] = s.vram[src ^ 1];   // byte-wide, on the swapped bus
                src++;
                s.addr = (s.addr + s.reg[15]) & 0x1ffff;
            }
            s.reg[21] = uint8_t(src); s.reg[22] = uint8_t(src >> 8);
            s.reg[19] = s.reg[20] = 0;
            s.code &= 0x1f;
        }
    }

    uint32_t color(int idx, int mode) const   // mode 0 normal, 1 shadow, 2 highlight
    {
        uint16_t c = s.cram[idx & 63];
        int r = (c >> 1) & 7, g = (c >> 5) & 7, b = (c >> 9) & 7;
        auto lvl = [mode](int v) {
            if (mode == 1) return v * 255 / 14;
            if (mode == 2) return 255 * 7 / 14 + v * 255 / 14;
            return v * 255 / 7;
        };
        return 0xff000000u | uint32_t(lvl(r)) << 16 | uint32_t(lvl(g)) << 8 | uint32_t(lvl(b));
    }
};

// Firecore extended mode (register 24 bit 7), worked out from the AtGames games that use it:
// tiles are 6 bits a pixel (48 bytes; a row is three VRAM words, pixels taken from the low
// bits up), a pixel's colour is (palette << 6 | pixel) in a 256-entry 24-bit palette (two
// CRAM words: GGRR, 00BB) - planes from CRAM 0x000, sprites from CRAM 0x400 - and the name
// and sprite tables sit 0x6000 above where the registers would put them in 128 KB VRAM.
inline void renderLineExt(MdVdp& v, int y, uint32_t* out);

inline void MdVdp::renderLine(int y, uint32_t* out)
{
    if (extMode()) { renderLineExt(*this, y, out); return; }
    const int w = width();
    const int vis = visibleLines();
    uint8_t pix[320];      // colour index | 0x80 high priority (planes)
    uint8_t spr[320];      // sprite colour index, bit 7 priority, bit 6 present
    uint32_t bgc = uint32_t(s.reg[7] & 0x3f);
    if (y >= vis || !(s.reg[1] & 0x40)) {
        uint32_t c = color(int(bgc), 0);
        for (int x = 0; x < W; x++) out[x] = c;
        return;
    }
    // scroll sizes
    static const int sz[4] = { 32, 64, 32, 128 };
    int pw = sz[s.reg[16] & 3], ph = sz[(s.reg[16] >> 4) & 3];
    if (pw == 128 && ph > 32) ph = 32;
    if (pw == 64 && ph > 64) ph = 64;
    uint16_t aBase = uint16_t((s.reg[2] & 0x38) << 10);
    uint16_t bBase = uint16_t((s.reg[4] & 0x07) << 13);
    uint16_t wBase = uint16_t(h40() ? (s.reg[3] & 0x3c) << 10 : (s.reg[3] & 0x3e) << 10);
    uint16_t hsBase = uint16_t((s.reg[13] & 0x3f) << 10);
    int hsMode = s.reg[11] & 3;
    bool vs2 = s.reg[11] & 4;
    int hsLine = hsMode == 0 ? 0 : hsMode == 2 ? (y & ~7) : y;
    uint16_t hsa = uint16_t(hsBase + hsLine * 4);
    int hsA = (s.vram[hsa] << 8 | s.vram[hsa + 1]) & 0x3ff;
    int hsB = (s.vram[(hsa + 2) & 0xffff] << 8 | s.vram[(hsa + 3) & 0xffff]) & 0x3ff;

    // window region on this line
    int winV = s.reg[18] & 0x1f, winH = (s.reg[17] & 0x1f) * 16;
    bool winDown = s.reg[18] & 0x80, winRight = s.reg[17] & 0x80;
    bool winLine = winDown ? (y >= winV * 8) : (y < winV * 8);
    auto inWindow = [&](int x) {
        if (winLine) return true;
        if (winH == 0 && !winRight) return false;
        return winRight ? (x >= winH) : (x < winH);
    };

    auto planePixel = [&](uint16_t base, int px, int py, uint8_t& outPix) {
        int cx = (px >> 3) & (pw - 1), cy = (py >> 3) & (ph - 1);
        uint16_t ea = uint16_t(base + (cy * pw + cx) * 2);
        uint16_t e = uint16_t(s.vram[ea] << 8 | s.vram[(ea + 1) & 0xffff]);
        int tile = e & 0x7ff, pal = (e >> 13) & 3;
        int ty = py & 7, tx = px & 7;
        if (e & 0x1000) ty = 7 - ty;
        if (e & 0x0800) tx = 7 - tx;
        uint8_t b = s.vram[(tile * 32 + ty * 4 + (tx >> 1)) & 0xffff];
        int c = (tx & 1) ? (b & 15) : (b >> 4);
        outPix = uint8_t(c ? (pal * 16 + c) | ((e & 0x8000) ? 0x80 : 0) : ((e & 0x8000) ? 0x80 : 0));
    };

    uint8_t a[320], bpl[320];
    for (int x = 0; x < w; x++) {
        // plane B
        int vsB = vs2 ? s.vsram[((x >> 4) * 2 + 1) % 40] : s.vsram[1];
        planePixel(bBase, (x - hsB) & 0x3ff, (y + vsB) & 0x3ff, bpl[x]);
        // plane A or window
        if (inWindow(x)) {
            int ww = h40() ? 64 : 32;
            int cx = x >> 3, cy = y >> 3;
            uint16_t ea = uint16_t(wBase + (cy * ww + cx) * 2);
            uint16_t e = uint16_t(s.vram[ea] << 8 | s.vram[(ea + 1) & 0xffff]);
            int tile = e & 0x7ff, pal = (e >> 13) & 3;
            int ty = y & 7, tx = x & 7;
            if (e & 0x1000) ty = 7 - ty;
            if (e & 0x0800) tx = 7 - tx;
            uint8_t b = s.vram[(tile * 32 + ty * 4 + (tx >> 1)) & 0xffff];
            int c = (tx & 1) ? (b & 15) : (b >> 4);
            a[x] = uint8_t(c ? (pal * 16 + c) | ((e & 0x8000) ? 0x80 : 0) : ((e & 0x8000) ? 0x80 : 0));
        } else {
            int vsA = vs2 ? s.vsram[((x >> 4) * 2) % 40] : s.vsram[0];
            planePixel(aBase, (x - hsA) & 0x3ff, (y + vsA) & 0x3ff, a[x]);
        }
    }

    // sprites
    std::memset(spr, 0, sizeof(spr));
    uint16_t sat = uint16_t(h40() ? (s.reg[5] & 0x7e) << 9 : (s.reg[5] & 0x7f) << 9);
    int maxS = h40() ? 80 : 64, maxLine = h40() ? 20 : 16, maxDots = w;
    int link = 0, count = 0, dots = 0;
    bool prevNonZeroX = false, masked = false;
    for (int n = 0; n < maxS; n++) {
        uint16_t e = uint16_t(sat + link * 8);
        int sy = ((s.vram[e] << 8 | s.vram[(e + 1) & 0xffff]) & 0x3ff) - 128;
        int size = s.vram[(e + 2) & 0xffff];
        int nlink = s.vram[(e + 3) & 0xffff] & 0x7f;
        int hc = ((size >> 2) & 3) + 1, vc = (size & 3) + 1;
        if (y >= sy && y < sy + vc * 8) {
            uint16_t attr = uint16_t(s.vram[(e + 4) & 0xffff] << 8 | s.vram[(e + 5) & 0xffff]);
            int sx = ((s.vram[(e + 6) & 0xffff] << 8 | s.vram[(e + 7) & 0xffff]) & 0x1ff) - 128;
            if (++count > maxLine) break;
            if (sx == -128 && prevNonZeroX) masked = true;
            if (sx != -128) prevNonZeroX = true;
            int tile = attr & 0x7ff, pal = (attr >> 13) & 3;
            bool hf = attr & 0x800, vf = attr & 0x1000, pr = attr & 0x8000;
            int row = y - sy; if (vf) row = vc * 8 - 1 - row;
            for (int px = 0; px < hc * 8; px++) {
                if (dots >= maxDots) break;
                dots++;
                if (masked) continue;
                int x = sx + px;
                if (x < 0 || x >= w) continue;
                int col = hf ? hc * 8 - 1 - px : px;
                int t = tile + (col >> 3) * vc + (row >> 3);
                uint8_t b = s.vram[(t * 32 + (row & 7) * 4 + ((col & 7) >> 1)) & 0xffff];
                int c = (col & 1) ? (b & 15) : (b >> 4);
                if (!c) continue;
                if (spr[x] & 0x40) continue;   // a lower-numbered sprite is already here
                spr[x] = uint8_t(0x40 | (pr ? 0x80 : 0) | (pal * 16 + c));
            }
            if (dots >= maxDots) break;
        }
        link = nlink;
        if (!link) break;
    }

    bool shl = s.reg[12] & 0x08;
    uint32_t line[320];
    for (int x = 0; x < w; x++) {
        int pa = a[x], pb = bpl[x], ps = spr[x];
        int idx = int(bgc); int mode = 0;
        bool sp = ps & 0x40, spHi = ps & 0x80;
        int sc = ps & 0x3f;
        // priority: hi sprite > hi A > hi B > lo sprite > lo A > lo B > backdrop
        int ca = pa & 0x3f, cb = pb & 0x3f;
        bool aHi = pa & 0x80, bHi = pb & 0x80;
        if (sp && spHi && (sc & 15)) idx = sc;
        else if (aHi && (ca & 15)) idx = ca;
        else if (bHi && (cb & 15)) idx = cb;
        else if (sp && (sc & 15)) idx = sc;
        else if (ca & 15) idx = ca;
        else if (cb & 15) idx = cb;
        if (shl) {
            bool anyHi = aHi || bHi;
            mode = anyHi ? 0 : 1;
            if (idx == sc && sp && (sc & 15)) {
                if (sc == 0x3e) { mode = mode == 1 ? 0 : 2; idx = (ca & 15) ? ca : (cb & 15) ? cb : int(bgc); }
                else if (sc == 0x3f) { mode = 1; idx = (ca & 15) ? ca : (cb & 15) ? cb : int(bgc); }
                else if ((sc & 15) == 14) mode = 0;
            }
        }
        line[x] = color(idx, mode);
    }
    if (s.reg[0] & 0x20) for (int x = 0; x < 8 && x < w; x++) line[x] = color(int(bgc), 0);
    if (w == W) std::memcpy(out, line, sizeof(uint32_t) * W);
    else for (int x = 0; x < W; x++) out[x] = line[x * w / W];
}

inline void renderLineExt(MdVdp& v, int y, uint32_t* out)
{
    auto& s = v.s;
    const int w = v.width();
    const int outY = y;
    auto color = [&](int bank, int idx) -> uint32_t {
        uint16_t w0 = s.xcram[(bank * 512 + idx * 2) & 1023], w1 = s.xcram[(bank * 512 + idx * 2 + 1) & 1023];
        return 0xff000000u | uint32_t(w0 & 0xff) << 16 | uint32_t(w0 >> 8) << 8 | uint32_t(w1 & 0xff);
    };
    auto vw = [&](uint32_t a) -> uint32_t { a &= 0x1fffe; return uint32_t(s.vram[a] << 8 | s.vram[a + 1]); };
    auto tilePix = [&](int tile, int tx, int ty) -> int {
        uint32_t a = uint32_t(tile) * 48 + uint32_t(ty) * 6;
        uint64_t row = uint64_t(vw(a)) | uint64_t(vw(a + 2)) << 16 | uint64_t(vw(a + 4)) << 32;
        return int((row >> (tx * 6)) & 63);
    };
    const uint32_t OFS = 0x6000;
    int bgIdx = s.reg[7] & 0x3f;
    uint32_t bg = color(0, bgIdx);
    if (outY >= v.visibleLines() || !(s.reg[1] & 0x40)) { for (int x = 0; x < MdVdp::W; x++) out[x] = bg; return; }
    static const int sz[4] = { 32, 64, 32, 128 };
    int pw = sz[s.reg[16] & 3], ph = sz[(s.reg[16] >> 4) & 3];
    uint32_t aBase = (uint32_t((s.reg[2] & 0x38) << 10) | uint32_t(s.reg[14] & 1) << 16) + OFS;
    uint32_t bBase = (uint32_t((s.reg[4] & 0x07) << 13) | uint32_t((s.reg[14] >> 4) & 1) << 16) + OFS;
    uint32_t hsBase = (uint32_t((s.reg[13] & 0x3f) << 10) | uint32_t((s.reg[13] >> 6) & 1) << 16) + OFS;
    int hsMode = s.reg[11] & 3;
    int hsLine = hsMode == 0 ? 0 : hsMode == 2 ? (y & ~7) : y;
    int hsA = vw(hsBase + hsLine * 4) & 0x3ff, hsB = vw(hsBase + hsLine * 4 + 2) & 0x3ff;
    bool vs2 = s.reg[11] & 4;
    uint8_t a[320], b[320];
    uint8_t aHi[320], bHi[320];
    auto plane = [&](uint32_t base, int px, int py, uint8_t& c, uint8_t& hi) {
        int cx = (px >> 3) & (pw - 1), cy = (py >> 3) & (ph - 1);
        uint32_t e = vw(base + uint32_t(cy * pw + cx) * 2);
        int tile = e & 0x7ff, pal = (e >> 13) & 3;
        int ty = py & 7, tx = px & 7;
        if (e & 0x1000) ty = 7 - ty;
        if (e & 0x0800) tx = 7 - tx;
        int p = tilePix(tile, tx, ty);
        c = uint8_t(p ? (pal << 6 | p) : 0);
        hi = (e & 0x8000) ? 1 : 0;
    };
    for (int x = 0; x < w; x++) {
        int vsA = vs2 ? s.vsram[((x >> 4) * 2) % 40] : s.vsram[0];
        int vsB = vs2 ? s.vsram[((x >> 4) * 2 + 1) % 40] : s.vsram[1];
        plane(bBase, (x - hsB) & 0x3ff, (y + vsB) & 0x3ff, b[x], bHi[x]);
        plane(aBase, (x - hsA) & 0x3ff, (y + vsA) & 0x3ff, a[x], aHi[x]);
    }
    // sprites
    uint8_t spr[320], sprOn[320], sprHi[320];
    std::memset(sprOn, 0, sizeof(sprOn));
    uint32_t sat = (uint32_t((v.h40() ? (s.reg[5] & 0x7e) : (s.reg[5] & 0x7f)) << 9) | uint32_t((s.reg[5] >> 7) & 1) << 16) + OFS;
    int maxS = v.h40() ? 80 : 64, maxLine = v.h40() ? 20 : 16;
    int link = 0, count = 0, dots = 0;
    for (int n = 0; n < maxS; n++) {
        uint32_t e = sat + uint32_t(link) * 8;
        int sy = int(vw(e) & 0x3ff) - 128;
        uint32_t sl = vw(e + 2);
        int size = sl >> 8, nlink = sl & 0x7f;
        int hc = ((size >> 2) & 3) + 1, vc = (size & 3) + 1;
        if (y >= sy && y < sy + vc * 8) {
            uint32_t attr = vw(e + 4);
            int sx = int(vw(e + 6) & 0x1ff) - 128;
            if (++count > maxLine) break;
            int tile = attr & 0x7ff, pal = (attr >> 13) & 3;
            bool hf = attr & 0x800, vf = attr & 0x1000, pr = attr & 0x8000;
            int row = y - sy; if (vf) row = vc * 8 - 1 - row;
            for (int px = 0; px < hc * 8 && dots < w; px++, dots++) {
                int x = sx + px;
                if (x < 0 || x >= w) continue;
                int col = hf ? hc * 8 - 1 - px : px;
                int p = tilePix(tile + (col >> 3) * vc + (row >> 3), col & 7, row & 7);
                if (!p || sprOn[x]) continue;
                sprOn[x] = 1; spr[x] = uint8_t(pal << 6 | p); sprHi[x] = pr ? 1 : 0;
            }
        }
        link = nlink;
        if (!link) break;
    }
    uint32_t line[320];
    for (int x = 0; x < w; x++) {
        uint32_t c = bg;
        if (sprOn[x] && sprHi[x]) c = color(1, spr[x]);
        else if (a[x] && aHi[x]) c = color(0, a[x]);
        else if (b[x] && bHi[x]) c = color(0, b[x]);
        else if (sprOn[x]) c = color(1, spr[x]);
        else if (a[x]) c = color(0, a[x]);
        else if (b[x]) c = color(0, b[x]);
        line[x] = c;
    }
    if (w == MdVdp::W) std::memcpy(out, line, sizeof(uint32_t) * MdVdp::W);
    else for (int x = 0; x < MdVdp::W; x++) out[x] = line[x * w / MdVdp::W];
}
