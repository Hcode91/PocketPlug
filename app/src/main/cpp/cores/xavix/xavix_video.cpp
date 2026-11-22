// PocketPlug - XaviX video: two tilemaps and 256 sprites, line by line
// SPDX-License-Identifier: BSD-3-Clause
//
// Port of MAME's tvgames/xavix_v.cpp (David Haywood), BSD-3-Clause. Lines are
// drawn when MAME would draw them: when a palette entry or the raster
// interrupt asks for a partial update, and the rest at the start of vblank.

#include "xavix.h"
#include <algorithm>

void XavixCore::updatePen(int pen)
{
    // pens 256-511: the SuperXaviX bitmap layer's palette ($6C00 / $6D00)
    uint16_t dat = pen < 256 ? (m_s.palSh[pen] | (m_s.palL[pen] << 8))
                             : (m_s.sxRam[0x200 + (pen & 0xff)] | (m_s.sxRam[0x300 + (pen & 0xff)] << 8));
    int yRaw = (((dat & 0x1f00) >> 7) | ((dat & 0x8000) >> 15)) >> 1;
    int cRaw = (((dat & 0x00e0) >> 4) | ((dat & 0x4000) >> 14)) >> 1;
    int hRaw = (((dat & 0x001f) << 1) | ((dat & 0x2000) >> 13)) >> 1;
    double y = yRaw / 20.0;
    double c = cRaw / 5.0;
    static const double hues[32][3] = {
        { 1.00, 0.00, 0.00 }, { 1.00, 0.25, 0.00 }, { 1.00, 0.50, 0.00 }, { 1.00, 0.75, 0.00 },
        { 1.00, 1.00, 0.00 }, { 0.75, 1.00, 0.00 }, { 0.50, 1.00, 0.00 }, { 0.25, 1.00, 0.00 },
        { 0.00, 1.00, 0.00 }, { 0.00, 1.00, 0.25 }, { 0.00, 1.00, 0.50 }, { 0.00, 1.00, 0.75 },
        { 0.00, 1.00, 1.00 }, { 0.00, 0.75, 1.00 }, { 0.00, 0.50, 1.00 }, { 0.00, 0.25, 1.00 },
        { 0.00, 0.00, 1.00 }, { 0.25, 0.00, 1.00 }, { 0.50, 0.00, 1.00 }, { 0.75, 0.00, 1.00 },
        { 1.00, 0.00, 1.00 }, { 1.00, 0.00, 0.75 }, { 1.00, 0.00, 0.50 }, { 1.00, 0.00, 0.25 },
        { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 }, { 0, 0, 0 },
    };
    const double wr = 0.299, wg = 0.587, wb = 0.114;
    double r0 = hues[hRaw][0], g0 = hues[hRaw][1], b0 = hues[hRaw][2];
    double z = wr * r0 + wg * g0 + wb * b0;
    if (y < z) c *= y / z;
    else if (z < 1) c *= (1 - y) / (1 - z);
    double rgb[3] = { (r0 - z) * c + y, (g0 - z) * c + y, (b0 - z) * c + y };
    int out[3];
    for (int i = 0; i < 3; i++) {
        double v = rgb[i] * 0.92f;
        if (v < 0) v = 0; else if (v > 1) v = 1.0;
        out[i] = int(v * 255.0f);
    }
    m_pens[pen] = 0xff000000u | (out[0] << 16) | (out[1] << 8) | out[2];
}

uint8_t XavixCore::nextBit()
{
    if (m_tmpBit == 0) {
        uint32_t a = m_tmpAddr;
        if (m_cfg.superXavix && (a & 0x2000000)) { a &= 0x1ffffff; a ^= 0x0600000; }
        m_bit = readBypass(a);
    }
    uint8_t r = (m_bit >> m_tmpBit) & 1;
    if (++m_tmpBit == 8) { m_tmpBit = 0; m_tmpAddr++; }
    return r;
}

uint8_t XavixCore::nextByte()
{
    uint8_t d = 0;
    for (int i = 0; i < 8; i++) d |= nextBit() << i;
    return d;
}

void XavixCore::renderUpTo(int line)
{
    if (line > 239) line = 239;
    for (int l = m_rendered + 1; l <= line; l++)
        if (l >= 16) renderLine(l);
    if (line > m_rendered) m_rendered = line;
}

namespace { int g_clipMin, g_clipMax, g_hres; uint32_t* g_dst; }

void XavixCore::renderLine(int line)
{
    const int w = frameWidth();
    g_hres = m_cfg.superXavix ? 2 : 1;
    g_dst = &m_out[(line - 16) * w];
    for (int x = 0; x < w; x++) g_dst[x] = 0xff000000u;
    int cmin = 0, cmax = w - 1;
    if (m_s.arenaControl & 0x01) {
        if (m_s.arenaStart != 0x00 && m_s.arenaEnd != 0x00 && m_s.arenaStart != 0xff && m_s.arenaEnd != 0xff) {
            cmin = std::max(0, (m_s.arenaEnd - 2) * g_hres);
            cmax = std::min(w - 1, (m_s.arenaStart - 3) * g_hres);
        }
    }
    g_clipMin = cmin; g_clipMax = cmax;
    for (int x = cmin; x <= cmax; x++) { g_dst[x] = m_pens[0]; m_z[x] = 0; }
    if (cmin > cmax) return;
    if (m_cfg.superXavix) drawBitmapLine(line);
    drawTilemapLine(0, line);
    drawTilemapLine(1, line);
    drawSpritesLine(line);
}

void XavixCore::decodeInlineHeader(int& flipx, int& flipy, int& test, int& pal)
{
    flipx = flipy = test = 0;
    bool first = true;
    for (;;) {
        uint8_t b = nextByte();
        if (first) {
            pal = (b & 0xf0) >> 4;
            switch (b & 0x0f) {
            case 0x1: case 0x3: flipx = 0; flipy = 0; break;
            case 0x5: case 0x7: flipx = 1; flipy = 0; break;
            case 0x9: case 0xb: flipx = 0; flipy = 1; break;
            case 0xd: case 0xf: flipx = 1; flipy = 1; break;
            default: break;
            }
            first = false;
        }
        if ((b & 0x0f) == 0x06) break;
    }
}

void XavixCore::drawTilemapLine(int which, int line)
{
    const uint8_t* r = which ? m_s.tmap2 : m_s.tmap1;
    if (!(r[7] & 0x80)) return;
    int ydim = 32, xdim = 32, ytile = 8, xtile = 8, yshift = 3;
    switch (r[3] & 0x30) {
    case 0x00: ydim = 32; xdim = 32; ytile = 8; xtile = 8; yshift = 3; break;
    case 0x10: ydim = 32; xdim = 16; ytile = 8; xtile = 16; yshift = 3; break;
    case 0x20: ydim = 16; xdim = 32; ytile = 16; xtile = 8; yshift = 4; break;
    case 0x30: ydim = 16; xdim = 16; ytile = 16; xtile = 16; yshift = 4; break;
    }
    int inlineHeader = (r[7] & 0x10) ? 1 : 0;
    int alt = 0;
    if (r[7] & 0x02) alt = 1;
    if ((r[7] & 0x7f) == 0x04) alt = 2;
    if ((r[7] & 0x7f) == 0x0d) alt = 3;

    int drawline = (line + r[5]) & ((ydim * ytile) - 1);
    int y = drawline >> yshift;
    int yyline = drawline & (ytile - 1);

    for (int x = 0; x < xdim; x++) {
        int count = y * xdim + x;
        int tile = 0;
        if (r[0] != 0) tile |= readBypass((r[0] << 8) + count);
        if ((r[7] & 0x7f) != 0x00 && (r[7] & 0x7f) != 0x08) tile |= readBypass((r[1] << 8) + count) << 8;
        if (alt == 2) tile |= readBypass((r[2] << 8) + count) << 16;

        int bpp = ((r[3] & 0x0e) >> 1) + 1;
        int pal = (r[6] & 0xf0) >> 4;
        int zval = r[6] & 0x0f;
        int scrollx = r[4];
        int flipx = (r[3] & 0x40) >> 6;
        int flipy = (r[3] & 0x80) >> 7;
        if (tile == 0) continue;
        int test = 0;
        if (!inlineHeader) {
            if (alt == 0) {
                int mult = (ytile * xtile) / 8;
                int gfxbase = (m_s.segment[1] << 16) | (m_s.segment[0] << 8);
                tile = tile * (mult * bpp) + gfxbase;
            } else if (alt == 1) {
                int basereg;
                if (r[7] & 0x01) {
                    basereg = (tile & 0xf000) >> 12;
                    tile &= 0x0fff;
                } else {
                    basereg = (tile & 0xe000) >> 13;
                    tile = (tile & 0x1fff) * 8;
                }
                tile += (m_s.segment[basereg * 2 + 1] << 16) | (m_s.segment[basereg * 2] << 8);
            } else if (alt == 3) {
                // SuperXaviX: tile number from the 32-bit extended segment 0
                if (m_cfg.superXavix) tile = int(tile * ((ytile * xtile) / 8) * bpp + extSegment(0));
                else tile = 0;
            }
            if (r[7] & 0x08) {
                uint8_t extra = readBypass((r[2] << 8) + count);
                pal = (extra & 0xf0) >> 4;
                zval = extra & 0x0f;
            }
        } else {
            if (r[7] == 0x94) {
                tile += readBypass((r[2] << 8) + count) << 16;
            } else {
                int basereg = (tile & 0xf000) >> 12;
                tile &= 0x0fff;
                tile += (m_s.segment[basereg * 2 + 1] << 16) | (m_s.segment[basereg * 2] << 8);
            }
            m_tmpAddr = tile; m_tmpBit = 0;
            decodeInlineHeader(flipx, flipy, test, pal);
            tile = int(m_tmpAddr);
        }
        if (test) { m_s.rng = m_s.rng * 1103515245u + 12345u; pal = (m_s.rng >> 16) & 0xf; }
        drawTileLine(tile, bpp, x * xtile + scrollx, line, ytile, xtile, flipx, flipy, pal, zval, yyline);
        drawTileLine(tile, bpp, x * xtile + scrollx - 256, line, ytile, xtile, flipx, flipy, pal, zval, yyline);
    }
}

void XavixCore::drawSpritesLine(int line)
{
    int alt = 0;
    m_s.spritereg &= 0x7f;
    uint8_t sr = m_s.spritereg;
    if (sr == 0x00 || sr == 0x01 || sr == 0x05) alt = 1;
    else if (sr == 0x02) alt = 2;
    else if (sr == 0x04) alt = 0;
    else if (sr == 0x07) alt = 3;

    const uint8_t* spr = m_s.sprite;
    for (int i = 0xff; i >= 0; i--) {
        int ypos = spr[0x200 + i];
        int xpos = spr[0x300 + i];
        int tile = 0;
        if (sr == 0x04 || sr == 0x07 || sr == 0x15) tile |= spr[0x700 + i] << 16;
        if (sr != 0x00) tile |= spr[0x600 + i] << 8;
        tile |= spr[0x500 + i];
        int attr0 = spr[i], attr1 = spr[0x100 + i];
        int pal = (attr0 & 0xf0) >> 4;
        int zval = (attr1 & 0xf0) >> 4;
        int flipx = attr1 & 0x01;
        int flipy = attr1 & 0x02;
        int dh = (attr1 & 0x08) ? 16 : 8;
        int dw = (attr1 & 0x04) ? 16 : 8;
        int xadj = -(dw / 2), yadj = -(dh / 2);
        ypos ^= 0xff;
        if (ypos & 0x80) ypos = -0x80 + (ypos & 0x7f);
        else ypos &= 0x7f;
        ypos += 128 + 1 + yadj;
        if (line < ypos || line >= ypos + dh) continue;
        int drawline = line - ypos;
        xpos |= (spr[0x400 + i] & 1) << 8;
        xpos ^= 0x100;
        xpos += xadj - 0x80;
        int bpp = ((attr0 & 0x0e) >> 1) + 1;
        if (alt == 1) {
            tile = (tile * dh * dw * bpp) / 8;
            tile += (m_s.segment[1] << 16) | (m_s.segment[0] << 8);
            if (m_cfg.superXavix) tile += int(extSegment(0));
        } else if (alt == 2) {
            tile *= 8;
            int basereg = (tile & 0xf0000) >> 16;
            tile &= 0xffff;
            tile += (m_s.segment[basereg * 2 + 1] << 16) | (m_s.segment[basereg * 2] << 8);
        } else if (alt == 3) {
            tile *= 8;
        }
        drawTileLine(tile, bpp, xpos, line, dh, dw, flipx, flipy, pal, zval, drawline);
    }
}

void XavixCore::drawTileLine(int tile, int bpp, int xpos, int ypos, int drawheight, int drawwidth, int flipx, int flipy, int pal, int zval, int line)
{
    (void)ypos;
    if ((xpos + drawwidth) * g_hres - 1 < g_clipMin || xpos * g_hres > g_clipMax) return;
    if (bpp > 4) pal &= (0xf << (bpp - 4));
    int bitsPerLine = drawwidth * bpp;
    if (flipy) line = (drawheight - 1) - line;
    m_tmpAddr = uint32_t(tile + (line * bitsPerLine) / 8);
    m_tmpBit = (line * bitsPerLine) % 8;
    for (int x = 0; x < drawwidth; x++) {
        int col = flipx ? xpos + (drawwidth - 1) - x : xpos + x;
        uint8_t dat = 0;
        for (int i = 0; i < bpp; i++) dat |= nextBit() << i;
        col *= g_hres;
        if (col < g_clipMin || col > g_clipMax) continue;
        if (zval >= m_z[col]) {
            int pen = (dat + (pal << 4)) & 0xff;
            if ((m_s.palSh[pen] & 0x1f) < 24) {
                g_dst[col] = m_pens[pen];
                m_z[col] = uint16_t(zval);
                if (g_hres == 2 && col + 1 <= g_clipMax) { g_dst[col + 1] = m_pens[pen]; m_z[col + 1] = uint16_t(zval); }
            }
        }
    }
}

// SuperXaviX bitmap layer (MAME superxavix_state::draw_bitmap_layer); registers at $6FB0-$6FC7
void XavixCore::drawBitmapLine(int y)
{
    const uint8_t* b = &m_s.sxRam[0x5b0];
    uint8_t mode = b[0x14];
    if (!(mode & 0x01)) return;
    int top = b[0x00] | (b[0x01] << 8);
    int bot = b[0x02] | (b[0x03] << 8);
    int lft = b[0x04] | (b[0x05] << 8);
    int rgt = b[0x06] | (b[0x07] << 8);
    int topadr = b[0x08] | (b[0x09] << 8);
    int start = b[0x10] | (b[0x11] << 8);
    uint8_t step = b[0x12], size = b[0x13];
    int base = start * 0x800, base2 = topadr * 8;
    int bpp = ((mode & 0x0e) >> 1) + 1;
    int zval = (mode & 0xf0) >> 4;
    bool highres = (size & 0x70) != 0x70;
    int hposadjust = highres ? 0x90 : 0x46;
    top += 6; bot += 6;
    int line = y - top;
    if (line <= 0 || y >= bot) return;
    m_tmpAddr = uint32_t(base + base2 + ((line * (step * 64)) / 8));
    m_tmpBit = 0;
    for (int x = lft - hposadjust; x < rgt - hposadjust; x++) {
        uint8_t dat = 0;
        for (int i = 0; i < bpp; i++) dat |= nextBit() << i;
        int realx = highres ? x : x * 2;
        for (int k = 0; k < (highres ? 1 : 2); k++, realx++) {
            if (realx < g_clipMin || realx > g_clipMax) continue;
            if ((m_s.sxRam[0x200 + dat] & 0x1f) < 24 && zval >= m_z[realx]) {
                g_dst[realx] = m_pens[0x100 + dat];
                m_z[realx] = uint16_t(zval);
            }
        }
    }
}

// $6F78-$6F7B (characters) and $6F7C-$6F7F (bitmap): index, then hue / saturation / lightness of that entry
void XavixCore::sxPalWrite(int reg, uint8_t d)
{
    bool bmp = reg >= 4;
    int r = reg & 3;
    uint8_t& idx = m_s.sxRam[0x578 + (bmp ? 4 : 0)];
    if (r == 0) { idx = d; return; }
    uint8_t* sh = bmp ? &m_s.sxRam[0x200] : m_s.palSh;
    uint8_t* sl = bmp ? &m_s.sxRam[0x300] : m_s.palL;
    uint16_t dat = sh[idx] | (sl[idx] << 8);
    if (r == 1) { uint16_t hue = (d >> 2) & 0x3f; dat = uint16_t((dat & ~0x201f) | ((hue & 0x3e) >> 1) | ((hue & 1) << 13)); }
    if (r == 2) { uint16_t sat = (d >> 4) & 0x0f; dat = uint16_t((dat & ~0x40e0) | ((sat & 0x0e) << 4) | ((sat & 1) << 14)); }
    if (r == 3) { uint16_t lt = (d >> 2) & 0x3f; dat = uint16_t((dat & ~0x9f00) | ((lt & 0x3e) << 7) | ((lt & 1) << 15)); }
    sh[idx] = uint8_t(dat); sl[idx] = uint8_t(dat >> 8);
    updatePen(idx + (bmp ? 256 : 0));
}

uint8_t XavixCore::sxPalRead(int reg)
{
    bool bmp = reg >= 4;
    int r = reg & 3;
    uint8_t idx = m_s.sxRam[0x578 + (bmp ? 4 : 0)];
    if (r == 0) return idx;
    const uint8_t* sh = bmp ? &m_s.sxRam[0x200] : m_s.palSh;
    const uint8_t* sl = bmp ? &m_s.sxRam[0x300] : m_s.palL;
    uint16_t dat = sh[idx] | (sl[idx] << 8);
    if (r == 1) return uint8_t((((dat & 0x1f) << 1) | ((dat & 0x2000) >> 13)) << 2);
    if (r == 2) return uint8_t((((dat & 0xe0) >> 4) | ((dat & 0x4000) >> 14)) << 4);
    return uint8_t((((dat & 0x1f00) >> 7) | ((dat & 0x8000) >> 15)) << 2);
}
