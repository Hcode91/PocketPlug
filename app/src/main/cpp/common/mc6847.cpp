// PocketPlug - Motorola MC6847 VDG (NTSC)
// SPDX-License-Identifier: BSD-3-Clause
//
// Port of MAME's mc6847.cpp (0.264, copyright-holders Nathan Woods). See mc6847.h.

#include "mc6847.h"
#include <cstring>
#include <algorithm>

#include "mc6847_fonts.inc"

namespace {
const int LINES_TOP_BORDER = 25, LINES_ACTIVE_VIDEO = 192, LINES_BOTTOM_BORDER = 26;
const int LINES_VERTICAL_RETRACE = 6, LINES_VBLANK = 13;
const int LINES_UNTIL_RETRACE = LINES_TOP_BORDER + LINES_ACTIVE_VIDEO + LINES_BOTTOM_BORDER;   // 243
const int LINES_UNTIL_VBLANK = LINES_UNTIL_RETRACE + LINES_VERTICAL_RETRACE;                   // 249
const int FSYNC_FALLING_LINE = LINES_TOP_BORDER + LINES_ACTIVE_VIDEO - 1;                        // 216
const int TIMER_FSYNC_TIME = 212;
const int BMP_BORDER = 29 * 2, BMP_ACTIVE = 128 * 2;
enum { ZONE_TOP, ZONE_BODY, ZONE_BOTTOM, ZONE_RETRACE, ZONE_VBLANK };

inline uint32_t rgb(int r, int g, int b) { return 0xff000000u | uint32_t(r << 16) | uint32_t(g << 8) | uint32_t(b); }
const uint32_t PALETTE[16] = {
    rgb(0x30, 0xd2, 0x00), rgb(0xc1, 0xe5, 0x00), rgb(0x4c, 0x3a, 0xb4), rgb(0x9a, 0x32, 0x36),   // green yellow blue red
    rgb(0xbf, 0xc8, 0xad), rgb(0x41, 0xaf, 0x71), rgb(0xc8, 0x4e, 0xf0), rgb(0xd4, 0x7f, 0x00),   // buff cyan magenta orange
    rgb(0x26, 0x30, 0x16), rgb(0x30, 0xd2, 0x00), rgb(0x26, 0x30, 0x16), rgb(0xbf, 0xc8, 0xad),   // black green black buff
    rgb(0x00, 0x7c, 0x00), rgb(0x30, 0xd2, 0x00), rgb(0x6b, 0x27, 0x00), rgb(0xff, 0xb7, 0x00),   // text dark/bright green, orange
};

inline uint8_t simplifyMode(uint8_t mode)
{
    return mode & ~((mode & MC6847::MODE_AG) ? (MC6847::MODE_AS | MC6847::MODE_INV) : 0);
}
}

MC6847::MC6847(Host* h) : m_h(h)
{
    std::memset(&s, 0, sizeof(s));
    for (int i = 0; i < 64 * 12; i++) m_textInverse[i] = vdg_fontdata8x12[i] ^ 0xff;
    for (int mode = 0; mode < 128; mode++) {
        Entry e{};
        if ((mode & (MODE_INTEXT | MODE_AS)) == MODE_AS) {
            e.font = semigraphics4_fontdata8x12; e.charMask = 0x0f; e.base0 = 8; e.base1 = 0; e.shift1 = 4; e.mask1 = 0x07;
        } else if ((mode & (MODE_INTEXT | MODE_AS)) == (MODE_INTEXT | MODE_AS)) {
            e.font = semigraphics6_fontdata8x12; e.charMask = 0x3f; e.base0 = 8; e.base1 = (mode & MODE_CSS) ? 4 : 0;
            e.shift1 = 6; e.mask1 = 0x03;
        } else {
            // INTEXT without an external ROM ("stripes") is not used by the machines here: drawn as text
            e.font = (mode & MODE_INV) ? m_textInverse : vdg_fontdata8x12;
            e.charMask = 0x3f;
            e.base0 = (mode & MODE_CSS) ? 14 : 12;
            e.base1 = (mode & MODE_CSS) ? 15 : 13;
        }
        m_entries[mode] = e;
    }
}

void MC6847::reset()
{
    // device_start (timers start at time 0) and device_reset
    std::memset(&s, 0, sizeof(s));
    s.fsyncAt = -1;
    s.lastHsyncOn = 0;
    // set_geometry() at start: the field sync goes high 212 clocks in
    s.fsyncAt = TIMER_FSYNC_TIME;
    s.fsyncValue = 1;
    s.mode = m_fixedMode;
    m_frameReady = false;
}

void MC6847::changeMode(uint8_t bits, int state)
{
    uint8_t nm = state ? uint8_t(s.mode | bits) : uint8_t(s.mode & ~bits);
    if (nm != s.mode) {
        videoFlush();
        s.mode = nm;
    }
}

void MC6847::videoFlush()
{
    if (!s.recording && s.zone == ZONE_BODY) {
        int32_t now = int32_t(m_h->vdgNow() - s.lastHsyncOn);
        if (s.partialClocks < now) {
            s.recording = 1;
            recordPartial(s.logical, s.partialClocks, now);
            s.recording = 0;
            s.partialClocks = now;
        }
    }
}

void MC6847::hsyncOn(int64_t now)
{
    // rising edge: record the line that just ended
    if (!s.hsync) {
        if (s.zone == ZONE_BODY) {
            s.recording = 1;
            if (s.partialClocks > 0) recordPartial(s.logical, s.partialClocks, 228);
            else recordFull(s.logical);
            s.recording = 0;
        }
        nextScanline();
        // update_field_sync_timer()
        bool expected = s.physical < FSYNC_FALLING_LINE || s.physical >= LINES_UNTIL_VBLANK - 1;
        if (expected != bool(s.fieldSync)) { s.fsyncAt = now + TIMER_FSYNC_TIME; s.fsyncValue = expected; }
        if (s.physical == LINES_UNTIL_RETRACE) m_frameReady = true;
    }
    s.hsync = 1;
    s.lastHsyncOn = now;
    // hsync off follows 212 clocks into the next period; nothing is wired to it here,
    // so it is folded into the next rising edge.
    s.hsync = 0;
}

void MC6847::fireFsync()
{
    uint8_t line = s.fsyncValue;
    s.fsyncAt = -1;
    if (line != s.fieldSync) {
        s.fieldSync = line;
        m_h->vdgFsync(line);
        // field_sync_changed(): DA lines go Hi-Z
        if (line) m_h->vdgInput(0xffff);
    }
}

void MC6847::nextScanline()
{
    s.physical++;
    s.logical++;
    s.partialClocks = 0;
    if (s.zone == ZONE_VBLANK && s.logical >= LINES_VBLANK) {
        s.physical = 0; s.logical = 0; s.zone = ZONE_TOP;
    } else if (s.zone < ZONE_VBLANK && s.physical >= LINES_UNTIL_VBLANK) {
        s.logical = 0; s.zone = ZONE_VBLANK;
    } else if (s.zone < ZONE_RETRACE && s.physical >= LINES_UNTIL_RETRACE) {
        s.logical = 0; s.zone = ZONE_RETRACE;
    } else if (s.zone == ZONE_TOP && s.logical >= LINES_TOP_BORDER) {
        s.logical = 0; s.zone = ZONE_BODY;
    } else if (s.zone == ZONE_BODY && s.logical >= LINES_ACTIVE_VIDEO) {
        s.logical = 0; s.zone = ZONE_BOTTOM;
    }
}

template<int sampleCount, int yres>
void MC6847::recordRes(int scanline, int32_t startPos, int32_t endPos)
{
    const int modulo = 32 / sampleCount;
    Line& L = s.lines[scanline];
    uint8_t count = startPos > 0 ? L.count : 0;
    for (int32_t pos = startPos; pos < endPos; pos++) {
        if (pos == 0) s.videoAddress = uint16_t(scanline / (LINES_ACTIVE_VIDEO / yres) * sampleCount);
        if (pos % modulo == 0) {
            uint8_t data = m_h->vdgInput(s.videoAddress++);
            if (pos < 32) {
                if (count < 32) { L.mode[count] = simplifyMode(s.mode); L.data[count] = data; }
                count++;
            }
        }
    }
    L.count = count;
}

void MC6847::recordBody(uint8_t mode, int scanline, int32_t startPos, int32_t endPos)
{
    if (mode & MODE_AG) {
        switch (mode & (MODE_GM2 | MODE_GM1 | MODE_GM0)) {
        case 0: case MODE_GM0: recordRes<16, 64>(scanline, startPos, endPos); break;
        case MODE_GM1: recordRes<32, 64>(scanline, startPos, endPos); break;
        case MODE_GM1 | MODE_GM0: recordRes<16, 96>(scanline, startPos, endPos); break;
        case MODE_GM2: recordRes<32, 96>(scanline, startPos, endPos); break;
        case MODE_GM2 | MODE_GM0: recordRes<16, 192>(scanline, startPos, endPos); break;
        default: recordRes<32, 192>(scanline, startPos, endPos); break;
        }
    } else recordRes<32, 16>(scanline, startPos, endPos);
}

void MC6847::recordFull(int scanline) { recordBody(s.mode, scanline, 0, 32); }

void MC6847::recordPartial(int scanline, int32_t startClock, int32_t endClock)
{
    int32_t startPos = std::max((startClock - 25) / 4, 0);
    int32_t endPos = std::min((endClock - 25) / 4, 42);
    if (startPos < endPos) recordBody(s.mode, scanline, startPos, endPos);
}

uint8_t MC6847::borderValue(uint8_t mode) const
{
    if (mode & MODE_AG) return (~mode & MODE_CSS) ? 0 : 4;
    return 8;
}

uint32_t MC6847::emit(uint8_t mode, const uint8_t* data, int length, uint32_t* pixels, int y)
{
    auto graphics = [&](int bpp, int xscale, int base) {
        for (int i = 0; i < length; i++)
            for (int j = 0; j < 8 / bpp; j++)
                for (int k = 0; k < xscale; k++)
                    pixels[(i * (8 / bpp) + j) * xscale + k] = PALETTE[base + ((data[i] >> (8 - (j + 1) * bpp)) & ((1 << bpp) - 1))];
    };
    if (mode & MODE_AG) {
        switch (mode & (MODE_GM2 | MODE_GM1 | MODE_GM0)) {
        case 0:
            graphics(2, 4, (mode & MODE_CSS) ? 4 : 0); return uint32_t(length * 16);
        case MODE_GM0: case MODE_GM1 | MODE_GM0: case MODE_GM2 | MODE_GM0:
            graphics(1, 2, (mode & MODE_CSS) ? 10 : 8); return uint32_t(length * 16);
        case MODE_GM1: case MODE_GM2: case MODE_GM2 | MODE_GM1:
            graphics(2, 2, (mode & MODE_CSS) ? 4 : 0); return uint32_t(length * 8);
        default:
            graphics(1, 1, (mode & MODE_CSS) ? 10 : 8); return uint32_t(length * 8);
        }
    }
    const Entry& e = m_entries[mode % 128];
    for (int i = 0; i < length; i++) {
        uint8_t ch = data[i];
        uint8_t slice = e.font[(ch & e.charMask) * 12 + y % 12];
        uint32_t c0 = PALETTE[e.base0];
        uint32_t c1 = PALETTE[e.base1 + ((ch >> e.shift1) & e.mask1)];
        for (int j = 0; j < 8; j++) pixels[i * 8 + j] = (slice & (0x80 >> j)) ? c1 : c0;
    }
    return uint32_t(length * 8);
}

void MC6847::updateArtifactColors(uint32_t c0, uint32_t c1)
{
    static const double artifact_colors[14 * 3] = {
        0.157, 0.000, 0.157, 0.000, 0.157, 0.000, 1.000, 0.824, 1.000, 0.824, 1.000, 0.824,
        0.706, 0.236, 0.118, 0.000, 0.197, 0.471, 1.000, 0.550, 0.393, 0.275, 0.785, 1.000,
        0.000, 0.500, 1.000, 1.000, 0.500, 0.000, 1.000, 0.942, 0.785, 0.393, 0.942, 1.000,
        0.236, 0.000, 0.000, 0.000, 0.000, 0.236,
    };
    static const uint8_t artifact_correction[128] = {
        0, 0, 0, 0, 0, 6, 0, 2, 5, 7, 5, 7, 1, 3, 1, 11, 8, 6, 8, 14, 8, 9, 8, 9, 4, 4, 4, 15, 12, 12, 12, 15,
        5, 13, 5, 13, 13, 0, 13, 2, 10, 10, 10, 10, 10, 15, 10, 11, 3, 1, 3, 1, 15, 9, 15, 9, 11, 11, 11, 11, 15, 15, 15, 15,
        14, 0, 14, 0, 14, 6, 14, 2, 0, 7, 0, 7, 1, 3, 1, 11, 9, 6, 9, 14, 9, 9, 9, 9, 15, 4, 15, 15, 12, 12, 12, 15,
        2, 13, 2, 13, 2, 0, 2, 2, 10, 10, 10, 10, 10, 15, 10, 11, 12, 1, 12, 1, 12, 9, 12, 9, 15, 11, 15, 11, 15, 15, 15, 15,
    };
    if (c0 == m_savedC0 && c1 == m_savedC1 && m_expanded[127]) return;
    m_savedC0 = c0; m_savedC1 = c1;
    auto mix = [](double f, uint8_t a, uint8_t b) { return uint32_t(uint8_t(a * (1.0 - f) + b * f + 0.5)); };
    uint32_t colors[16];
    colors[0] = c0; colors[15] = c1;
    for (int i = 1; i <= 14; i++) {
        // artifacting "standard" (1): factors index (i - 1) ^ 1
        const double* f = &artifact_colors[((i - 1) ^ 1) * 3];
        colors[i] = 0xff000000u | (mix(f[0], uint8_t(c0 >> 16), uint8_t(c1 >> 16)) << 16)
                  | (mix(f[1], uint8_t(c0 >> 8), uint8_t(c1 >> 8)) << 8) | mix(f[2], uint8_t(c0), uint8_t(c1));
    }
    for (int i = 0; i < 128; i++) m_expanded[i] = colors[artifact_correction[i]];
}

void MC6847::render(uint32_t* out)
{
    const int W = WIDTH, baseX = BMP_BORDER, baseY = LINES_TOP_BORDER;
    for (int y = 0; y < baseY; y++)
        for (int x = 0; x < W; x++) out[y * W + x] = PALETTE[borderValue(s.lines[0].mode[0])];
    for (int y = 0; y < LINES_ACTIVE_VIDEO; y++) {
        const Line& L = s.lines[y];
        uint32_t* row = out + (y + baseY) * W;
        for (int x = 0; x < baseX; x++) row[x] = PALETTE[borderValue(L.mode[0])];
        int width = std::min<int>(L.count, 32);
        uint32_t* px = row + baseX;
        // the body only covers what was fetched; anything not drawn keeps the last frame (as MAME's bitmap)
        for (int x = 0; x < width;) {
            int x2 = x + 1;
            while (x2 < width && L.mode[x] == L.mode[x2]) x2++;
            px += emit(L.mode[x], &L.data[x], x2 - x, px, y);
            x = x2;
        }
        if (width)
            for (int x = baseX + BMP_ACTIVE; x < W; x++) row[x] = PALETTE[borderValue(L.mode[width - 1])];
        // NTSC artifacting of the 256-dot mode
        uint8_t m0 = L.mode[0];
        if ((m0 & (MODE_AG | MODE_GM2 | MODE_GM1 | MODE_GM0)) == (MODE_AG | MODE_GM2 | MODE_GM1 | MODE_GM0)) {
            uint32_t c0 = PALETTE[(m0 & MODE_CSS) ? 10 : 8], c1 = PALETTE[(m0 & MODE_CSS) ? 11 : 9];
            updateArtifactColors(c0, c1);
            uint32_t* p = row + baseX;
            uint32_t nl[256];
            for (int x = 0; x < 256; x += 2) {
                uint8_t v = uint8_t(((p[x - 2] == c1) ? 0x20 : 0) | ((p[x - 1] == c1) ? 0x10 : 0) | ((p[x] == c1) ? 0x08 : 0)
                    | ((p[x + 1] == c1) ? 0x04 : 0) | ((p[x + 2] == c1) ? 0x02 : 0) | ((p[x + 3] == c1) ? 0x01 : 0));
                nl[x] = m_expanded[v * 2]; nl[x + 1] = m_expanded[v * 2 + 1];
            }
            std::memcpy(p, nl, sizeof(nl));
        }
    }
    const Line& last = s.lines[LINES_ACTIVE_VIDEO - 1];
    int width = std::min<int>(last.count, 32);
    if (width)
        for (int y = baseY + LINES_ACTIVE_VIDEO; y < HEIGHT; y++)
            for (int x = 0; x < W; x++) out[y * W + x] = PALETTE[borderValue(last.mode[width - 1])];
}
