// PocketPlug - Texas Instruments TMS9918 video display processor
// SPDX-License-Identifier: BSD-3-Clause
//
// The original (non-A) 9918: text, graphics I and multicolour modes, 32
// sprites with 4-per-line limit, magnification and collision. Rendering and
// palette follow MAME's tms9928a.cpp (BSD-3, Sean Young et al.); only the
// 256 x 192 active area is produced.

#pragma once
#include <cstdint>
#include <cstring>

class Tms9918 {
public:
    uint8_t vram[0x4000];
    uint8_t regs[8];
    uint8_t status = 0, readAhead = 0, latch = 0, fifth = 0;
    uint16_t addrReg = 0;
    uint32_t out[256 * 192];

    void reset()
    {
        memset(regs, 0, sizeof regs);
        status = readAhead = latch = 0;
        fifth = 0;
        addrReg = 0;
    }

    uint8_t vramRead()
    {
        uint8_t d = readAhead;
        readAhead = vram[addrReg];
        addrReg = (addrReg + 1) & 0x3fff;
        latch = 0;
        return d;
    }
    void vramWrite(uint8_t d)
    {
        vram[addrReg] = d;
        addrReg = (addrReg + 1) & 0x3fff;
        readAhead = d;
        latch = 0;
    }
    uint8_t registerRead()
    {
        uint8_t d = status;
        status = fifth;
        latch = 0;
        return d;
    }
    void registerWrite(uint8_t d)
    {
        if (latch) {
            addrReg = uint16_t(((d << 8) | (addrReg & 0xff)) & 0x3fff);
            if (d & 0x80) setReg(d & 7, uint8_t(addrReg & 0xff));
            else if (!(d & 0x40)) vramRead();
            latch = 0;
        } else {
            addrReg = uint16_t(((addrReg & 0xff00) | d) & 0x3fff);
            latch = 1;
        }
    }

    // draws active line y (0-191); at y == 192 the frame flag is raised
    void line(int y)
    {
        if (y == 193) { status |= 0x80; return; }
        if (y < 0 || y >= 192) return;
        uint32_t* p = &out[y * 256];
        int back = regs[7] & 15;
        if (!(regs[1] & 0x40)) { for (int i = 0; i < 256; i++) p[i] = pen(back); return; }
        int mode = ((regs[1] & 0x10) >> 4) | ((regs[1] & 8) >> 1);
        uint16_t name = uint16_t((regs[2] * 1024) & 0x3fff);
        uint16_t colour = uint16_t((regs[3] * 64) & 0x3fff);
        uint16_t pattern = uint16_t((regs[4] * 2048) & 0x3fff);
        switch (mode) {
        case 0: {
            uint16_t a = uint16_t(name + ((y & 0xf8) << 2));
            for (int x = 0; x < 256; x += 8, a++) {
                uint8_t ch = vram[a & 0x3fff];
                uint8_t pat = vram[(pattern + (ch << 3) + (y & 7)) & 0x3fff];
                uint8_t col = vram[(colour + (ch >> 3)) & 0x3fff];
                uint32_t fg = pen((col >> 4) ? (col >> 4) : back), bg = pen((col & 15) ? (col & 15) : back);
                for (int i = 0; i < 8; pat <<= 1, i++) p[x + i] = (pat & 0x80) ? fg : bg;
            }
            break;
        }
        case 1: {
            uint16_t a = uint16_t(name + (y >> 3) * 40);
            uint32_t fg = pen((regs[7] >> 4) ? (regs[7] >> 4) : back), bg = pen(back);
            for (int x = 0; x < 6; x++) p[x] = bg;
            for (int x = 6; x < 246; x += 6, a++) {
                uint8_t ch = vram[a & 0x3fff];
                uint8_t pat = vram[(pattern + (ch << 3) + (y & 7)) & 0x3fff];
                for (int i = 0; i < 6; pat <<= 1, i++) p[x + i] = (pat & 0x80) ? fg : bg;
            }
            for (int x = 246; x < 256; x++) p[x] = bg;
            break;
        }
        case 4: {
            uint16_t a = uint16_t(name + (y >> 3) * 32);
            for (int x = 0; x < 256; x += 8, a++) {
                uint8_t ch = vram[a & 0x3fff];
                uint8_t col = vram[(pattern + (ch << 3) + ((y >> 2) & 7)) & 0x3fff];
                uint32_t fg = pen((col >> 4) ? (col >> 4) : back), bg = pen((col & 15) ? (col & 15) : back);
                p[x] = p[x + 1] = p[x + 2] = p[x + 3] = fg;
                p[x + 4] = p[x + 5] = p[x + 6] = p[x + 7] = bg;
            }
            break;
        }
        default: {
            uint32_t fg = pen((regs[7] >> 4) ? (regs[7] >> 4) : back), bg = pen(back);
            for (int x = 0; x < 6; x++) p[x] = bg;
            for (int x = 6; x < 246; x += 6) { p[x] = p[x + 1] = p[x + 2] = p[x + 3] = fg; p[x + 4] = p[x + 5] = bg; }
            for (int x = 246; x < 256; x++) p[x] = bg;
            break;
        }
        }
        sprites(y, p);
    }

    static uint32_t palette(int c)
    {
        static const uint32_t pal[16] = {
            0xff000000, 0xff000000, 0xff21c842, 0xff5edc78, 0xff5455ed, 0xff7d76fc, 0xffd4524d, 0xff42ebf5,
            0xfffc5554, 0xffff7978, 0xffd4c154, 0xffe6ce80, 0xff21b03b, 0xffc95bba, 0xffcccccc, 0xffffffff };
        return pal[c & 15];
    }

    void setReg(int r, uint8_t v)
    {
        static const uint8_t mask[8] = { 0x03, 0xfb, 0x0f, 0xff, 0x07, 0x7f, 0x07, 0xff };
        regs[r] = v & mask[r];
    }

private:
    // colour 0 (transparent) with a transparent backdrop shows black
    uint32_t pen(int c) const { return palette(c); }

    void sprites(int y, uint32_t* p)
    {
        if ((regs[1] & 0x50) != 0x40) { fifth = 31; return; }
        uint16_t attr = uint16_t((regs[5] * 128) & 0x3fff);
        uint16_t spat = uint16_t((regs[6] * 2048) & 0x3fff);
        int size = (regs[1] & 2) ? 16 : 8;
        int mag = regs[1] & 1;
        int height = size * (mag + 1);
        uint8_t drawn[32 + 256 + 32];
        memset(drawn, 0, sizeof drawn);
        int num = 0;
        bool fifthHit = false;
        for (int sa = 0; sa < 128; sa += 4) {
            int sy = vram[(attr + sa) & 0x3fff];
            fifth = uint8_t(sa / 4);
            if (sy == 208) break;
            if (sy > 0xe0) sy -= 256;
            sy++;
            if (sy <= y && y < sy + height) {
                int sx = vram[(attr + sa + 1) & 0x3fff];
                uint8_t code = vram[(attr + sa + 2) & 0x3fff];
                uint8_t col = vram[(attr + sa + 3) & 0x3fff];
                uint16_t pa = uint16_t(spat + ((size == 16) ? (code & ~3) : code) * 8);
                if (++num == 5) { fifthHit = true; break; }
                pa = uint16_t(pa + (mag ? (((y - sy) & 0x1f) >> 1) : ((y - sy) & 0x0f)));
                uint8_t pat = vram[pa & 0x3fff];
                if (col & 0x80) sx -= 32;
                col &= 15;
                for (int s = 0; s < size; s += 8) {
                    for (int i = 0; i < 8; pat <<= 1, i++) {
                        int ci = sx + (mag ? i * 2 : i) + 32;
                        for (int z = 0; z <= mag; ci++, z++) {
                            if (!(pat & 0x80) || ci < 32 || ci >= 32 + 256) continue;
                            if (drawn[ci]) status |= 0x20;
                            drawn[ci] |= 1;
                            if (col && !(drawn[ci] & 2)) { drawn[ci] |= 2; p[ci - 32] = palette(col); }
                        }
                    }
                    pat = vram[(pa + 16) & 0x3fff];
                    sx += mag ? 16 : 8;
                }
            }
        }
        if (~status & 0x40) {
            status = uint8_t((status & 0xe0) | fifth);
            if (fifthHit && (~status & 0x80)) status |= 0x40;
        }
    }
};
