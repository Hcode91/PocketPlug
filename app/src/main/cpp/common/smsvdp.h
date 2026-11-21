// PocketPlug - Sega Master System / Game Gear VDP (315-5124 / 315-5246 / 315-5378), mode 4
// SPDX-License-Identifier: BSD-3-Clause
//
// Written for PocketPlug from the documented behaviour of the SMS VDP (Charles MacDonald's
// "SMS/GG hardware notes"): the control / data ports with the read buffer, the 11
// registers, 16 KB VRAM, 32-byte (SMS) or 64-byte (Game Gear, 12-bit) CRAM, the
// line counter and frame interrupts, NTSC V counter, and the mode 4 renderer
// (scrolling with the top-row / right-column locks, the left column blank, 8 sprites
// a line with overflow and collision, 8x16 and zoomed sprites).

#pragma once
#include <cstdint>
#include <cstring>

class SmsVdp {
public:
    static constexpr int W = 256, H = 192, LINES = 262;
    bool gg = false;               // Game Gear CRAM (12-bit colour, 64 bytes)

    struct State {
        uint8_t vram[0x4000], cram[64], reg[16];
        uint16_t addr;
        uint8_t code, latch, pending, buffer, status, ggLatch;
        int16_t lineCounter;
        uint8_t lineIrq, frameIrq, hcount;
        int16_t line;              // current scanline 0..261
    } s;

    SmsVdp() { reset(); }
    void reset()
    {
        std::memset(&s, 0, sizeof(s));
        static const uint8_t init[11] = { 0x36, 0x80, 0xff, 0xff, 0xff, 0xff, 0xfb, 0x00, 0x00, 0x00, 0xff };
        std::memcpy(s.reg, init, sizeof(init));
    }

    bool irq() const { return ((s.status & 0x80) && (s.reg[1] & 0x20)) || (s.lineIrq && (s.reg[0] & 0x10)); }

    uint8_t vcounter() const
    {
        int l = s.line;
        return uint8_t(l <= 0xda ? l : l - 6);   // NTSC 192 lines: 00-DA, D5-FF
    }

    uint8_t readData()
    {
        s.pending = 0;
        uint8_t r = s.buffer;
        s.buffer = s.vram[s.addr & 0x3fff];
        s.addr = uint16_t((s.addr + 1) & 0x3fff);
        return r;
    }
    uint8_t readControl()
    {
        uint8_t r = uint8_t(s.status | 0x1f);
        s.status = 0;
        s.lineIrq = 0;
        s.pending = 0;
        return r;
    }
    void writeData(uint8_t d)
    {
        s.pending = 0;
        if (s.code == 3) {
            if (gg) {
                if (s.addr & 1) { s.cram[(s.addr & 0x3e)] = s.ggLatch; s.cram[(s.addr & 0x3e) | 1] = d; }
                else s.ggLatch = d;
            } else s.cram[s.addr & 0x1f] = d;
        } else s.vram[s.addr & 0x3fff] = d;
        s.buffer = d;
        s.addr = uint16_t((s.addr + 1) & 0x3fff);
    }
    void writeControl(uint8_t d)
    {
        if (!s.pending) { s.latch = d; s.addr = uint16_t((s.addr & 0x3f00) | d); s.pending = 1; return; }
        s.pending = 0;
        s.code = d >> 6;
        s.addr = uint16_t(((d & 0x3f) << 8) | s.latch);
        if (s.code == 0) { s.buffer = s.vram[s.addr]; s.addr = uint16_t((s.addr + 1) & 0x3fff); }
        else if (s.code == 2) s.reg[d & 0x0f] = s.latch;
    }

    // ARGB of a CRAM entry
    uint32_t color(int i) const
    {
        if (gg) {
            uint16_t c = uint16_t(s.cram[i * 2] | s.cram[i * 2 + 1] << 8);
            int r = c & 15, g = (c >> 4) & 15, b = (c >> 8) & 15;
            return 0xff000000u | uint32_t(r * 17) << 16 | uint32_t(g * 17) << 8 | uint32_t(b * 17);
        }
        uint8_t c = s.cram[i & 31];
        int r = c & 3, g = (c >> 2) & 3, b = (c >> 4) & 3;
        return 0xff000000u | uint32_t(r * 85) << 16 | uint32_t(g * 85) << 8 | uint32_t(b * 85);
    }

    // run the end-of-line bookkeeping for line s.line (call after rendering it), then advance
    void endLine()
    {
        int l = s.line;
        if (l <= H) {
            if (--s.lineCounter < 0) { s.lineCounter = s.reg[10]; s.lineIrq = 1; }
        } else s.lineCounter = s.reg[10];
        if (l == H) s.status |= 0x80;
        s.line = int16_t(l + 1 >= LINES ? 0 : l + 1);
    }

    // render line y (0..191) into 256 ARGB pixels
    void renderLine(int y, uint32_t* out)
    {
        if (y < 0 || y >= H) return;
        uint32_t backdrop = color(16 + (s.reg[7] & 15));
        if (!(s.reg[1] & 0x40)) { for (int x = 0; x < W; x++) out[x] = backdrop; return; }
        uint8_t prio[W]; uint8_t bgIdx[W];
        // background
        uint16_t nameBase = uint16_t((s.reg[2] & 0x0e) << 10);
        int hs = (s.reg[0] & 0x40) && y < 16 ? 0 : s.reg[8];
        for (int x = 0; x < W; x++) {
            int vs = ((s.reg[0] & 0x80) && x >= 192) ? 0 : s.reg[9];
            int row = (y + vs) % 224;
            int mx = (x - hs) & 0xff;
            uint16_t ea = uint16_t(nameBase + ((row >> 3) * 32 + (mx >> 3)) * 2);
            uint16_t e = uint16_t(s.vram[ea & 0x3fff] | s.vram[(ea + 1) & 0x3fff] << 8);
            int tile = e & 0x1ff; bool hf = e & 0x200, vf = e & 0x400; int pal = (e & 0x800) ? 16 : 0; bool pr = e & 0x1000;
            int ty = row & 7; if (vf) ty = 7 - ty;
            uint16_t ta = uint16_t(tile * 32 + ty * 4);
            int px = mx & 7;
            int bit = hf ? px : 7 - px;
            int c = (s.vram[ta & 0x3fff] >> bit & 1) | (s.vram[(ta + 1) & 0x3fff] >> bit & 1) << 1 |
                    (s.vram[(ta + 2) & 0x3fff] >> bit & 1) << 2 | (s.vram[(ta + 3) & 0x3fff] >> bit & 1) << 3;
            out[x] = color(pal + c); bgIdx[x] = uint8_t(c); prio[x] = pr && c ? 1 : 0;
        }
        // sprites
        uint16_t sat = uint16_t((s.reg[5] & 0x7e) << 7);
        int h = (s.reg[1] & 2) ? 16 : 8;
        int zoom = (s.reg[1] & 1) ? 2 : 1;
        uint16_t pbase = (s.reg[6] & 4) ? 0x2000 : 0;
        int shift = (s.reg[0] & 8) ? 8 : 0;
        uint8_t used[W]; std::memset(used, 0, sizeof(used));
        int count = 0;
        for (int i = 0; i < 64; i++) {
            int sy = s.vram[(sat + i) & 0x3fff];
            if (sy == 0xd0) break;
            sy += 1; if (sy > 240) sy -= 256;
            if (y < sy || y >= sy + h * zoom) continue;
            if (++count > 8) { s.status |= 0x40; break; }
            int sx = s.vram[(sat + 0x80 + i * 2) & 0x3fff] - shift;
            int n = s.vram[(sat + 0x81 + i * 2) & 0x3fff];
            if (h == 16) n &= 0xfe;
            int ly = (y - sy) / zoom;
            uint16_t ta = uint16_t(pbase + (n + (ly >> 3)) * 32 + (ly & 7) * 4);
            uint8_t p0 = s.vram[ta & 0x3fff], p1 = s.vram[(ta + 1) & 0x3fff], p2 = s.vram[(ta + 2) & 0x3fff], p3 = s.vram[(ta + 3) & 0x3fff];
            for (int px = 0; px < 8 * zoom; px++) {
                int x = sx + px;
                if (x < 0 || x >= W) continue;
                int bit = 7 - px / zoom;
                int c = (p0 >> bit & 1) | (p1 >> bit & 1) << 1 | (p2 >> bit & 1) << 2 | (p3 >> bit & 1) << 3;
                if (!c) continue;
                if (used[x]) { s.status |= 0x20; continue; }
                used[x] = 1;
                if (!prio[x]) out[x] = color(16 + c);
            }
        }
        if (s.reg[0] & 0x20) for (int x = 0; x < 8; x++) out[x] = backdrop;
        (void)bgIdx;
    }
};
