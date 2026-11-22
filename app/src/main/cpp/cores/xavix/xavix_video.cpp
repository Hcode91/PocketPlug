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
    uint16_t dat = m_s.palSh[pen] | (m_s.palL[pen] << 8);
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
    if (m_tmpBit == 0) m_bit = readBypass(m_tmpAddr);
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

namespace { int g_clipMin, g_clipMax; uint32_t* g_dst; }

void XavixCore::renderLine(int line)
{
    g_dst = &m_out[(line - 16) * 256];
    for (int x = 0; x < 256; x++) g_dst[x] = 0xff000000u;
    int cmin = 0, cmax = 255;
    if (m_s.arenaControl & 0x01) {
        if (m_s.arenaStart != 0x00 && m_s.arenaEnd != 0x00 && m_s.arenaStart != 0xff && m_s.arenaEnd != 0xff) {
            cmin = std::max(0, m_s.arenaEnd - 2);
            cmax = std::min(255, m_s.arenaStart - 3);
        }
    }
    g_clipMin = cmin; g_clipMax = cmax;
    for (int x = cmin; x <= cmax; x++) { g_dst[x] = m_pens[0]; m_z[x] = 0; }
    if (cmin > cmax) return;
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
                tile = 0;     // SuperXaviX only
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
    if (xpos + drawwidth - 1 < g_clipMin || xpos > g_clipMax) return;
    if (bpp > 4) pal &= (0xf << (bpp - 4));
    int bitsPerLine = drawwidth * bpp;
    if (flipy) line = (drawheight - 1) - line;
    m_tmpAddr = uint32_t(tile + (line * bitsPerLine) / 8);
    m_tmpBit = (line * bitsPerLine) % 8;
    for (int x = 0; x < drawwidth; x++) {
        int col = flipx ? xpos + (drawwidth - 1) - x : xpos + x;
        uint8_t dat = 0;
        for (int i = 0; i < bpp; i++) dat |= nextBit() << i;
        if (col < g_clipMin || col > g_clipMax) continue;
        if (zval >= m_z[col]) {
            int pen = (dat + (pal << 4)) & 0xff;
            if ((m_s.palSh[pen] & 0x1f) < 24) {
                g_dst[col] = m_pens[pen];
                m_z[col] = uint16_t(zval);
            }
        }
    }
}
