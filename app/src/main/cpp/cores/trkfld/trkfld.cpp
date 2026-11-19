// PocketPlug - "trkfldch" SoC (65C816-based TV game chip: 4 Player System 10-in-1)
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Port of MAME's trkfldch.cpp (David Haywood, BSD-3-Clause). See trkfld.h.

#include "trkfld.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
constexpr int CPU_HZ = 20000000;
constexpr int LINES = 240;            // MAME: 320x240, vblank time 0, a callback every line
constexpr uint32_t REGION = 0x1000000;

// gfx_trkfldch: 0 = 8x8x8, 1 = 16x16x8, 2 = 8x8x6 (from 0x40), 3 = 16x16x6 (from 0x40)
struct Layout {
    int w, h, planes;
    int planeoff[8];
    int xoff[16];
    int yinc;            // STEPn(0, yinc)
    int charinc;         // bits per element
    uint32_t start;      // region offset in bytes
    uint32_t total;      // number of elements
};
const Layout kLayouts[4] = {
    { 8, 8, 8, { 48, 49, 32, 33, 16, 17, 0, 1 }, { 8, 10, 12, 14, 0, 2, 4, 6 }, 64, 512, 0, REGION * 8 / 512 },
    { 16, 16, 8, { 96, 97, 64, 65, 32, 33, 0, 1 }, { 8, 10, 12, 14, 0, 2, 4, 6, 24, 26, 28, 30, 16, 18, 20, 22 }, 128, 128 * 16, 0, REGION * 8 / (128 * 16) },
    { 8, 8, 6, { 32, 33, 16, 17, 0, 1 }, { 8, 10, 12, 14, 0, 2, 4, 6 }, 48, 48 * 8, 0x40, 0x5500 * 4 },
    { 16, 16, 6, { 64, 65, 32, 33, 0, 1 }, { 8, 10, 12, 14, 0, 2, 4, 6, 24, 26, 28, 30, 16, 18, 20, 22 }, 96, 96 * 16, 0x40, 0x5500 },
};
}

TrkfldCore::TrkfldCore(const std::string& romName, uint32_t romSize)
    : m_romName(romName), m_romSize(romSize)
{
    std::memset(&m_s, 0, sizeof(m_s));
    std::memset(m_out, 0, sizeof(m_out));
    std::memset(m_bitmap, 0, sizeof(m_bitmap));
    std::memset(m_pal, 0, sizeof(m_pal));
    m_cpu.m_bus = this;
    m_cpu.device_start();
}

bool TrkfldCore::load(const std::vector<RomFile>& roms, std::string& error)
{
    for (auto& r : roms) {
        if (r.name != m_romName) continue;
        if (r.data.size() != m_romSize) { error = m_romName + " has the wrong size"; return false; }
        m_rom.assign(REGION, 0);
        std::memcpy(m_rom.data(), r.data.data(), r.data.size());
        reset();
        return true;
    }
    error = "Missing " + m_romName;
    return false;
}

void TrkfldCore::reset()
{
    std::memset(&m_s, 0, sizeof(m_s));
    m_s.rng = 0x1234567;
    m_s.whichVector = 0x06;
    std::memset(m_s.unkregs, 0x90, sizeof(m_s.unkregs));
    for (int j = 0; j < 2; j++) {
        m_s.tmapscroll[j][0x02] = 0x1e;
        m_s.tmapscroll[j][0x03] = 0x1e;
        m_s.tmapscroll[j][0x05] = 0x28;
    }
    m_s.dmaregs[0x07] = 0x01;
    m_s.dmaregs[0x0b] = 0x01;
    m_cycleDebt = 0;
    m_cpu.device_reset();
}

// ---- memory map ----
uint8_t TrkfldCore::read(uint32_t a)
{
    a &= 0xffffff;
    if (a >= 0x8000) return romByte(a - 0x8000);
    if (a < 0x4000) return m_s.ram[a];
    if (a >= 0x6800 && a < 0x6d00) return m_s.spriteram[a - 0x6800];
    if (a >= 0x7000 && a < 0x7300) return m_s.palram[a - 0x7000];
    if (a >= 0x7800 && a < 0x7810) {
        int o = a - 0x7800;
        switch (o) {
        case 0: case 1:   // IRQ status? (MAME: random)
            m_s.rng = m_s.rng * 1103515245u + 12345u;
            return uint8_t(m_s.rng >> 16);
        case 4: return 0x80;   // ready after a write to 78b6
        case 5: return 0x20;   // DMA done
        default: return m_s.sysregs[o];
        }
    }
    if (a >= 0x7810 && a < 0x781b) return m_s.modebank[a - 0x7810];
    if (a >= 0x7820 && a < 0x7832) return m_s.tmapscroll[0][a - 0x7820];
    if (a >= 0x7832 && a < 0x7844) return m_s.tmapscroll[1][a - 0x7832];
    if (a >= 0x7854 && a < 0x7857) return m_s.tilemapbase[a - 0x7854];
    if (a >= 0x7860 && a < 0x786e) {
        int o = a - 0x7860;
        if (o == 5 || o == 6) return 0;   // DMA length counts down to 0
        return m_s.dmaregs[o];
    }
    if (a >= 0x7870 && a < 0x7900) {
        int o = a - 0x7870;
        // pads, active low
        switch (o) {
        case 0x00: return uint8_t(~m_inputs);
        case 0x03: return uint8_t(~((m_inputs >> 8) & 0x3f));
        case 0x07: return uint8_t(~((m_inputs >> 16) & 0x3f));
        case 0x0b: return uint8_t(~((m_inputs >> 24) & 0x3f));
        default: return m_s.unkregs[o];
        }
    }
    return 0;
}

void TrkfldCore::write(uint32_t a, uint8_t d)
{
    a &= 0xffffff;
    if (a >= 0x8000) return;
    if (a < 0x4000) { m_s.ram[a] = d; return; }
    if (a >= 0x6800 && a < 0x6d00) { m_s.spriteram[a - 0x6800] = d; return; }
    if (a >= 0x7000 && a < 0x7300) { m_s.palram[a - 0x7000] = d; return; }
    if (a >= 0x7800 && a < 0x7810) { m_s.sysregs[a - 0x7800] = d; return; }
    if (a >= 0x7810 && a < 0x781b) { m_s.modebank[a - 0x7810] = d; return; }
    if (a >= 0x7820 && a < 0x7832) { m_s.tmapscroll[0][a - 0x7820] = d; return; }
    if (a >= 0x7832 && a < 0x7844) { m_s.tmapscroll[1][a - 0x7832] = d; return; }
    if (a >= 0x7854 && a < 0x7857) { m_s.tilemapbase[a - 0x7854] = d; return; }
    if (a >= 0x7860 && a < 0x786e) {
        m_s.dmaregs[a - 0x7860] = d;
        if (a == 0x7865) doDma();
        return;
    }
    if (a >= 0x7870 && a < 0x7900) { m_s.unkregs[a - 0x7870] = d; return; }
}

uint8_t TrkfldCore::vector(uint32_t a)
{
    // the NMI vector is picked by whichever interrupt the chip raised; the
    // table sits at the start of the ROM
    if (a == 0x0b) return romByte(m_s.whichVector + 1);
    if (a == 0x0a) return romByte(m_s.whichVector);
    return romByte(a);
}

void TrkfldCore::doDma()
{
    const uint8_t* r = m_s.dmaregs;
    uint16_t len = uint16_t((r[6] << 8) | r[5]);
    uint32_t src = (uint32_t(r[2]) << 16) | (r[1] << 8) | r[0];
    uint16_t dst = uint16_t((r[4] << 8) | r[3]);
    int woff = 0, wdo = r[0x0d];
    int roff = 0, rdo = r[0x09];
    for (uint32_t j = 0; j < len; j++) {
        uint8_t b = read(src + roff);
        if (--rdo < 0) { rdo = r[0x09]; roff += r[0x07] | (r[0x08] << 8); }
        else roff++;
        write(uint16_t(dst + woff), b);
        if (--wdo < 0) { wdo = r[0x0d]; woff += r[0x0b] | (r[0x0c] << 8); }
        else woff++;
    }
}

// ---- timing ----
void TrkfldCore::scanline(int line)
{
    auto pulse = [&](uint8_t vec, bool on) {
        m_s.whichVector = vec;
        m_cpu.setLine(G65816_LINE_NMI, on ? ASSERT_LINE : CLEAR_LINE);
    };
    if (line == 200) pulse(0x06, true);
    else if (line == 201) pulse(0x06, false);
    if (line == 20) pulse(0x14, true);
    else if (line == 21) pulse(0x14, false);
    // timer interrupt
    if (line >= 80 && line < 120) pulse(0x18, (line & 1) == 0);
}

void TrkfldCore::runFrame()
{
    const double perLine = double(CPU_HZ) / 60.0 / LINES;
    for (int line = 0; line < LINES; line++) {
        scanline(line);
        m_frac += perLine;
        int n = int(m_frac);
        m_frac -= n;
        int budget = n - m_cycleDebt;
        if (budget > 0) m_cycleDebt = -m_cpu.run(budget);
        else m_cycleDebt = -budget;
        if (m_cycleDebt < 0) m_cycleDebt = 0;
    }
    render();
    m_pendingSamples = std::min<size_t>(m_pendingSamples + 735, 735 * 4);
}

size_t TrkfldCore::readAudio(int16_t* out, size_t maxSamples)
{
    // silent, but paced like real audio: 735 samples a frame
    size_t n = std::min<size_t>(maxSamples, m_pendingSamples);
    std::memset(out, 0, n * sizeof(int16_t));
    m_pendingSamples -= n;
    return n;
}

// ---- video ----
int TrkfldCore::pixel(int layout, uint32_t code, int x, int y) const
{
    const Layout& L = kLayouts[layout];
    uint64_t base = uint64_t(L.start) * 8 + uint64_t(code % L.total) * L.charinc + uint64_t(L.xoff[x]) + uint64_t(y) * L.yinc;
    int v = 0;
    for (int p = 0; p < L.planes; p++) {
        uint64_t b = base + L.planeoff[p];
        uint32_t byte = uint32_t(b >> 3);
        int bit = byte < REGION ? (m_rom[byte] >> (7 - (b & 7))) & 1 : 0;
        v = (v << 1) | bit;
    }
    return v;
}

void TrkfldCore::drawTile(int layout, uint32_t code, int color, bool flipx, bool flipy, int sx, int sy, int scale)
{
    const Layout& L = kLayouts[layout];
    const int gran = 1 << L.planes;
    int dw = L.w * scale, dh = L.h * scale;
    for (int dy = 0; dy < dh; dy++) {
        int y = sy + dy;
        if (y < 0 || y >= 240) continue;
        int ty = dy / scale; if (flipy) ty = L.h - 1 - ty;
        for (int dx = 0; dx < dw; dx++) {
            int x = sx + dx;
            if (x < 0 || x >= 320) continue;
            int tx = dx / scale; if (flipx) tx = L.w - 1 - tx;
            int p = pixel(layout, code, tx, ty);
            if (p == 0) continue;
            m_bitmap[y * 320 + x] = uint8_t((color * gran + p) & 0xff);
        }
    }
}

void TrkfldCore::renderTileLayer(int which)
{
    int base, gfxbase, layout;
    if (which == 0) { base = m_s.tilemapbase[0] << 8; gfxbase = m_s.modebank[1] * 0x2000; }
    else { base = m_s.tilemapbase[1] << 8; gfxbase = m_s.modebank[3] * 0x2000; }
    if (m_s.modebank[0] & 1) { gfxbase -= 0x200; layout = 0; }
    else { gfxbase -= 0x2ac; layout = 2; }
    for (int y = 0; y < 30; y++)
        for (int x = 0; x < 41; x++) {
            int addr = (y * 41) + x;
            uint8_t lo = read(uint32_t(base + addr * 2));
            uint8_t hi = read(uint32_t(base + addr * 2 + 1));
            int tile = (hi << 8) | lo;
            bool flipx = (tile & 0x8000) != 0;
            int pal = (tile & 0x6000) >> 13;
            tile &= 0x1fff;
            drawTile(layout, uint32_t(tile + gfxbase), layout == 0 ? 0 : pal, flipx, false, x * 8, y * 8, 1);
        }
}

void TrkfldCore::drawSprites(int pri)
{
    const uint8_t* s = m_s.spriteram;
    for (int i = 0x500 - 5; i >= 0; i -= 5) {
        if (((s[i + 4] & 0x20) >> 5) != pri) continue;
        int tilegfxbase = m_s.modebank[5] * 0x800;
        int y = s[i + 1], x = s[i + 3], tile = s[i + 2];
        bool dbl = s[i + 4] & 0x02;
        if (s[i + 4] & 0x04) tile += 0x100;
        if (s[i + 0] & 0x04) tile += 0x200;
        if (s[i + 0] & 0x08) tile += 0x400;
        bool flipx = s[i + 4] & 0x10, flipy = s[i + 4] & 0x08;
        x |= (s[i + 4] & 0x01) << 8;
        y |= (s[i + 0] & 0x01) << 8;
        y -= 0x100; y -= 16; x -= 16;
        int pal = 0, layout;
        if (m_s.modebank[0] & 1) { layout = 1; tilegfxbase -= 0x80; }
        else { pal = (s[i + 0] & 0x30) >> 4; layout = 3; tilegfxbase -= 0x40; tilegfxbase -= 0x6b; }
        drawTile(layout, uint32_t(tile + tilegfxbase), pal, flipx, flipy, x, y, dbl ? 2 : 1);
    }
}

void TrkfldCore::render()
{
    std::memset(m_bitmap, 0, sizeof(m_bitmap));
    // YUV palette (as MAME borrows from tetrisp2.cpp)
    for (int i = 0; i < 256; i++) {
        int u = m_s.palram[i], y1 = m_s.palram[0x100 + i], v = m_s.palram[0x200 + i];
        double bf = y1 + 1.772 * (u - 128);
        double gf = y1 - 0.334 * (u - 128) - 0.714 * (v - 128);
        double rf = y1 + 1.772 * (v - 128);
        auto c = [](double f) { return uint32_t(std::clamp(f, 0.0, 255.0)); };
        m_pal[i] = 0xff000000u | (c(rf) << 16) | (c(gf) << 8) | c(bf);
    }
    renderTileLayer(0);
    drawSprites(0);
    if (m_s.modebank[0] & 0x10) renderTileLayer(1);
    drawSprites(1);
    for (int i = 0; i < 320 * 240; i++) m_out[i] = m_pal[m_bitmap[i]];
}

// ---- save states ----
bool TrkfldCore::saveState(std::vector<uint8_t>& out)
{
    out.clear();
    uint32_t magic = 0x44464b54;   // 'TKFD'
    auto put = [&](const void* p, size_t n) { const uint8_t* b = (const uint8_t*)p; out.insert(out.end(), b, b + n); };
    put(&magic, 4);
    put(&m_s, sizeof(m_s));
    put(m_cpu.stateBegin(), m_cpu.stateSize());
    put(&m_cycleDebt, sizeof(m_cycleDebt));
    return true;
}

bool TrkfldCore::loadState(const std::vector<uint8_t>& in)
{
    if (in.size() != 4 + sizeof(m_s) + m_cpu.stateSize() + sizeof(m_cycleDebt)) return false;
    uint32_t magic; std::memcpy(&magic, in.data(), 4);
    if (magic != 0x44464b54) return false;
    const uint8_t* p = in.data() + 4;
    std::memcpy(&m_s, p, sizeof(m_s)); p += sizeof(m_s);
    std::memcpy(m_cpu.stateBegin(), p, m_cpu.stateSize()); p += m_cpu.stateSize();
    std::memcpy(&m_cycleDebt, p, sizeof(m_cycleDebt));
    m_cpu.g65816_restore_state();
    return true;
}
