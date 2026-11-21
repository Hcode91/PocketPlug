// PocketPlug - GeneralPlus GPL32600 picture processing unit
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The PPU at 0xd0500000 is a 32-bit descendant of the GPL162xx/SPG2xx PPU: register n of the
// 16-bit parts (0x7000 + n) sits at 0xd0500000 + n*4, and PPU RAM keeps one 16-bit entry per
// 32-bit slot. Worked out from what the Arcade Genius application does:
//   * four text layers: text 0 regs at 0x40 (X, Y, attribute, control, map, attribute map),
//     text 1 at 0x58, text 2 at 0x00 (attribute 0x10) and text 3 at 0x20 (attribute 0x30).
//     Tile data bases: text 0 0x80, text 1 0x84, text 2 0x8c, text 3 0x90.
//     Control: bit 0 bitmap, bit 1 attributes from the register, bit 3 enable, bit 7 true colour.
//     Attribute: bits 0-1 colour depth (2/4/6/8 bpp), 2/3 flips, 4-7 cell size (8 << n),
//     8-11 palette, 12-13 depth (drawing order).
//   * true-colour layers: maps hold a 32-bit pixel address per cell (0 = empty); bitmaps hold
//     one 32-bit address per line. Pixels are 32-bit, stored as the bytes A, R, G, B.
//   * indexed layers: maps hold 16-bit tile numbers (0 = empty), pixel data packed MSB first,
//     colours from palette RAM at 0x1000 (RGB555, bit 15 = transparent).
//   * sprites: 256 entries of eight slots in PPU RAM at 0x2000: address bits 0-15, X, Y,
//     attribute, address bits 16-31 (the address counts 16-bit units). Register 0x1fc bit 2
//     switches sprites to 32-bit pixels.
// PPU_GO (0x1f0 bit 0) renders into the frame buffer at 0x1e0 (RGB565); the result is shown
// on the 320x240 panel.
#include "gpl32.h"
#include <cstring>

namespace {
inline void blendArgb(uint32_t& dst, const uint8_t* p)
{
    uint32_t a = p[0];
    if (a == 0) return;
    uint32_t r = p[1], g = p[2], b = p[3];
    if (a < 255) {
        uint32_t dr = (dst >> 16) & 0xff, dg = (dst >> 8) & 0xff, db = dst & 0xff;
        r = (r * a + dr * (255 - a)) / 255;
        g = (g * a + dg * (255 - a)) / 255;
        b = (b * a + db * (255 - a)) / 255;
    }
    dst = 0xff000000u | (r << 16) | (g << 8) | b;
}

inline uint32_t rgb555(uint32_t c)
{
    uint32_t r = (c >> 10) & 31, g = (c >> 5) & 31, b = c & 31;
    return 0xff000000u | (((r << 3) | (r >> 2)) << 16) | (((g << 3) | (g >> 2)) << 8) | ((b << 3) | (b >> 2));
}

// pixel n of a row packed MSB first at bpp bits per pixel
inline uint32_t packedPixel(const uint8_t* row, int n, int bpp)
{
    uint32_t bit = uint32_t(n * bpp);
    uint32_t w = (uint32_t(row[bit >> 3]) << 8) | row[(bit >> 3) + 1];   // callers allow one spare byte
    return (w >> (16 - bpp - (bit & 7))) & ((1u << bpp) - 1);
}
}

const uint8_t* GplCore::ramPtr(uint32_t addr, uint32_t len) const
{
    if (addr >= RAM_SIZE || len > RAM_SIZE - addr) return nullptr;
    return m_ram.data() + addr;
}

void GplCore::ppuIndexed(uint32_t& dst, uint32_t index)
{
    uint32_t c = m_ppuRam[(0x1000 >> 2) + (index & 0x3ff)] & 0xffff;
    if (c & 0x8000) return;
    dst = rgb555(c);
}

void GplCore::ppuDrawLayer(int layer)
{
    static const uint32_t xyOff[4] = { 0x40, 0x58, 0x00, 0x20 };
    static const uint32_t attrOff[4] = { 0x48, 0x60, 0x10, 0x30 };
    static const uint32_t baseOff[4] = { 0x80, 0x84, 0x8c, 0x90 };
    const uint32_t b = 0xd0500000u;
    uint32_t ctrl = m_regs[b + attrOff[layer] + 4];
    if (!(ctrl & 8)) return;
    uint32_t attr = m_regs[b + attrOff[layer]];
    uint32_t map = m_regs[b + attrOff[layer] + 8];
    uint32_t attrMap = m_regs[b + attrOff[layer] + 12];
    uint32_t tileBase = m_regs[b + baseOff[layer]];
    bool trueColour = ctrl & 0x80;
    int sx = int(m_regs[b + xyOff[layer]] & 0x3ff), sy = int(m_regs[b + xyOff[layer] + 4] & 0x3ff);
    if (ctrl & 1) {                                  // bitmap: one line address per screen line
        for (int y = 0; y < H; y++) {
            uint32_t lineAddr = 0;
            int ly = (y + sy) & 0x3ff;
            if (const uint8_t* lp = ramPtr(map + uint32_t(ly) * 4, 4)) std::memcpy(&lineAddr, lp, 4);
            if (!lineAddr) continue;
            uint32_t* out = &m_fb[size_t(y) * W];
            if (trueColour) {
                const uint8_t* line = ramPtr(lineAddr, 1024 * 4);
                if (!line) continue;
                for (int x = 0; x < W; x++) blendArgb(out[x], line + size_t((x + sx) & 1023) * 4);
            } else {
                int bpp = int((attr & 3) + 1) * 2;
                const uint8_t* line = ramPtr(lineAddr, 1024 * uint32_t(bpp) / 8 + 1);
                if (!line) continue;
                uint32_t pal = ((attr >> 8) & 15) << 4;
                for (int x = 0; x < W; x++) ppuIndexed(out[x], pal + packedPixel(line, (x + sx) & 1023, bpp));
            }
        }
        return;
    }
    int tw = 8 << ((attr >> 4) & 3), th = 8 << ((attr >> 6) & 3);
    int mapW = 512 / tw;
    for (int y = 0; y < H; y++) {
        int vy = (y + sy) & 255;
        int row = vy / th, ty = vy % th;
        uint32_t* out = &m_fb[size_t(y) * W];
        for (int x = 0; x < W; x++) {
            int vx = (x + sx) & 511;
            int col = vx / tw, tx = vx % tw;
            uint32_t idx = uint32_t(row * mapW + col);
            if (trueColour) {
                uint32_t cell = 0;
                if (const uint8_t* mp = ramPtr(map + idx * 4, 4)) std::memcpy(&cell, mp, 4);
                if (!cell) continue;
                if (const uint8_t* px = ramPtr(cell + uint32_t(ty * tw + tx) * 4, 4)) blendArgb(out[x], px);
                continue;
            }
            uint16_t tile = 0;
            if (const uint8_t* mp = ramPtr(map + idx * 2, 2)) std::memcpy(&tile, mp, 2);
            if (!tile) continue;
            uint32_t a = attr;
            if (!(ctrl & 2)) {                       // per-cell attributes
                uint16_t cellAttr = 0;
                if (const uint8_t* ap = ramPtr(attrMap + idx * 2, 2)) std::memcpy(&cellAttr, ap, 2);
                a = (attr & ~0xf0cu) | (uint32_t(cellAttr) & 0xf0c);
            }
            int bpp = int((a & 3) + 1) * 2;
            uint32_t tileBytes = uint32_t(tw * th * bpp / 8);
            int px = (a & 4) ? tw - 1 - tx : tx, py = (a & 8) ? th - 1 - ty : ty;
            const uint8_t* rowp = ramPtr(tileBase + tile * tileBytes + uint32_t(py * tw * bpp / 8), uint32_t(tw * bpp / 8) + 1);
            if (!rowp) continue;
            ppuIndexed(out[x], (((a >> 8) & 15) << 4) + packedPixel(rowp, px, bpp));
        }
    }
}

void GplCore::ppuDrawSprites(int depth)
{
    bool trueColour = (m_regs[0xd05001fc] & 4) != 0;
    for (int i = 0; i < 256; i++) {
        const uint32_t* e = &m_ppuRam[(0x2000 >> 2) + size_t(i) * 8];
        uint32_t attr = e[3] & 0xffff;
        if (int((attr >> 12) & 3) != depth) continue;
        uint32_t addr = (((e[4] & 0xffff) << 16) | (e[0] & 0xffff)) * 2;
        if (!addr) continue;
        int x0 = int(int16_t(e[1])), y0 = int(int16_t(e[2]));
        int sw = 8 << ((attr >> 4) & 3), sh = 8 << ((attr >> 6) & 3);
        bool hflip = attr & 4, vflip = attr & 8;
        int bpp = trueColour ? 32 : int((attr & 3) + 1) * 2;
        const uint8_t* gfx = ramPtr(addr, uint32_t(sw * sh * bpp / 8) + 1);
        if (!gfx) continue;
        uint32_t pal = ((attr >> 8) & 15) << 4;
        for (int yy = 0; yy < sh; yy++) {
            int y = y0 + yy;
            if (y < 0 || y >= H) continue;
            int srcY = vflip ? sh - 1 - yy : yy;
            uint32_t* out = &m_fb[size_t(y) * W];
            const uint8_t* row = gfx + size_t(srcY * sw * bpp / 8);
            for (int xx = 0; xx < sw; xx++) {
                int x = x0 + xx;
                if (x < 0 || x >= W) continue;
                int srcX = hflip ? sw - 1 - xx : xx;
                if (trueColour) blendArgb(out[x], row + size_t(srcX) * 4);
                else ppuIndexed(out[x], pal + packedPixel(row, srcX, bpp));
            }
        }
    }
}

void GplCore::ppuRender()
{
    std::fill(m_fb.begin(), m_fb.end(), 0xff000000u);
    bool spritesOn = (m_regs[0xd0500108] & 1) != 0;
    static const uint32_t attrOff[4] = { 0x48, 0x60, 0x10, 0x30 };
    for (int depth = 0; depth < 4; depth++) {
        for (int layer = 0; layer < 4; layer++)
            if (int((m_regs[0xd0500000u + attrOff[layer]] >> 12) & 3) == depth) ppuDrawLayer(layer);
        if (spritesOn) ppuDrawSprites(depth);
    }
    // Write the frame to the frame buffer the application asked for, as RGB565.
    uint32_t fb = m_regs[0xd05001e0];
    if (uint8_t* dst = const_cast<uint8_t*>(ramPtr(fb, W * H * 2))) {
        for (size_t i = 0; i < size_t(W) * H; i++) {
            uint32_t c = m_fb[i];
            uint16_t v = uint16_t(((c >> 8) & 0xf800) | ((c >> 5) & 0x07e0) | ((c >> 3) & 0x001f));
            std::memcpy(dst + i * 2, &v, 2);
        }
    }
}
