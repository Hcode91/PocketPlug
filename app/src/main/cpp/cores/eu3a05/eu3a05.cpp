// PocketPlug - Elan EU3A05 TV game SoC (Air-Blaster Joystick)
// SPDX-License-Identifier: GPL-2.0-or-later
#include "eu3a05.h"
#include <algorithm>
#include <cmath>
#include <cstring>

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
}

Eu3a05Core::Eu3a05Core()
{
    buildTables();
    m_frame.assign(256 * 224, 0xff000000u);
    std::memset(&m_s, 0, sizeof(m_s));
}

bool Eu3a05Core::load(const std::vector<RomFile>& roms, std::string& error)
{
    if (roms.empty() || roms[0].data.size() < 0x8000) { error = "airblsjs.bin is needed"; return false; }
    // the external bus is 4 MB, mirrored
    m_rom.assign(0x400000, 0xff);
    const auto& d = roms[0].data;
    for (size_t i = 0; i < m_rom.size(); i++) m_rom[i] = d[i % d.size()];
    m_cpu.setBus(this, rdThunk, wrThunk);
    std::memset(&m_s, 0, sizeof(m_s));
    m_s.rng = 0x1234567;
    reset();
    return true;
}

void Eu3a05Core::reset()
{
    std::memset(m_s.ram, 0, sizeof(m_s.ram));
    m_s.intmask[0] = m_s.intmask[1] = 0;
    m_s.customNmi = 0;
    m_s.nmiVector = 0;
    m_s.rombankLo = 0x7f;
    m_s.rombankHi = 0;
    m_s.bank = 0x7f;
    std::memset(m_s.dma, 0, sizeof(m_s.dma));
    std::memset(m_s.gpioDdr, 0, sizeof(m_s.gpioDdr));
    m_s.vidctrl = 0;
    std::memset(m_s.tileScroll, 0, sizeof(m_s.tileScroll));
    m_s.tileBaseLo = m_s.tileBaseHi = m_s.sprBaseLo = m_s.sprBaseHi = 0;
    m_s.splitpos[0] = m_s.splitpos[1] = 0;
    m_s.transpen = 0;
    for (int i = 0; i < 6; i++) { m_s.sndAddr[i] = m_s.sndLen[i] = m_s.sndPos[i] = 0; m_s.adSignal[i] = m_s.adStep[i] = 0; }
    m_s.stopped = 0x3f;
    m_s.trigger = 0;
    m_s.volumes[0] = 0xff;
    m_s.volumes[1] = 0x0f;
    m_s.r50a4 = m_s.r50a9 = 0;
    m_s.timerCycles = CPU_HZ / 4096;
    m_s.sndCycles = 0;
    m_cpu.m_irqLine = false;
    m_cpu.m_nmiPending = false;
    m_cpu.reset();
    m_cpu.s = 0xff;            // the stack starts at 01FF (Tetris needs it)
}

// ---------------- memory ----------------
uint8_t Eu3a05Core::read(uint16_t a)
{
    if (a < 0x4000) return m_s.ram[a];
    if (a >= 0xe000) {
        if ((a == 0xfffa || a == 0xfffb)) return m_s.customNmi ? uint8_t(m_s.nmiVector >> ((a & 1) * 8)) : 0;
        return ext(0x3f8000u + (a - 0xe000));
    }
    if (a >= 0x6000) return ext(uint32_t(m_s.bank) * 0x8000u + (a - 0x6000));
    if (a >= 0x4800 && a < 0x4a00) return m_s.palram[a - 0x4800];
    if (a >= 0x5000 && a < 0x5020) return sysRead(a - 0x5000);
    if (a >= 0x5020 && a < 0x5040) return vidRead(a - 0x5020);
    if (a >= 0x5040 && a < 0x5047) {
        int o = a - 0x5040, port = o / 2;
        if (!(o & 1)) return m_s.gpioDdr[port];
        if (port == 2) return uint8_t(~m_inputs);
        return 0xff;
    }
    if (a >= 0x5060 && a < 0x506e) return m_s.ram5060[a - 0x5060];
    if (a >= 0x5080 && a < 0x50c0) return sndRead(a - 0x5080);
    return 0;
}

void Eu3a05Core::write(uint16_t a, uint8_t d)
{
    if (a < 0x4000) { m_s.ram[a] = d; return; }
    if (a >= 0x4800 && a < 0x4a00) { m_s.palram[a - 0x4800] = d; updatePen((a - 0x4800) / 2); return; }
    if (a >= 0x5000 && a < 0x5020) { sysWrite(a - 0x5000, d); return; }
    if (a >= 0x5020 && a < 0x5040) { vidWrite(a - 0x5020, d); return; }
    if (a >= 0x5040 && a < 0x5047) { int o = a - 0x5040; if (!(o & 1)) m_s.gpioDdr[o / 2] = d; return; }
    if (a >= 0x5060 && a < 0x506e) { m_s.ram5060[a - 0x5060] = d; return; }
    if (a >= 0x5080 && a < 0x50c0) { sndWrite(a - 0x5080, d); return; }
}

// ---------------- system: interrupts, banking, DMA ----------------
uint8_t Eu3a05Core::sysRead(int o)
{
    switch (o) {
    case 0x00: case 0x01: case 0x04: case 0x06: case 0x0a: return m_s.sysRam[o];
    case 0x03: case 0x09:
        m_s.rng = m_s.rng * 1103515245u + 12345u;
        return uint8_t(m_s.rng >> 16);
    case 0x07: return m_s.intmask[0];
    case 0x08: return m_s.intmask[1];
    case 0x0b: return 0x00;                 // PAL
    case 0x0c: return m_s.rombankHi;
    case 0x0d: return m_s.rombankLo;
    default:
        if (o >= 0x0f && o <= 0x15) return m_s.dma[o - 0x0f];
        return 0;
    }
}

void Eu3a05Core::sysWrite(int o, uint8_t d)
{
    switch (o) {
    case 0x00: case 0x01: case 0x04: case 0x06: case 0x0a: m_s.sysRam[o] = d; return;
    case 0x07: m_s.intmask[0] = d; return;
    case 0x08: m_s.intmask[1] = d; return;
    case 0x0c: m_s.rombankHi = d; m_s.bank = uint16_t(m_s.rombankLo | m_s.rombankHi << 8); return;
    case 0x0d: m_s.rombankLo = d; return;
    case 0x16:
        if (d) {
            uint32_t src = m_s.dma[0] | m_s.dma[1] << 8 | m_s.dma[2] << 16;
            uint16_t dest = uint16_t(m_s.dma[3] | m_s.dma[4] << 8);
            uint16_t size = uint16_t(m_s.dma[5] | m_s.dma[6] << 8);
            for (int i = 0; i < size; i++) write(uint16_t(dest + i), ext(src + i));
        }
        return;
    default:
        if (o >= 0x0f && o <= 0x15) m_s.dma[o - 0x0f] = d;
        return;
    }
}

void Eu3a05Core::customInterrupt(int level)
{
    uint16_t mask = uint16_t(m_s.intmask[0] << 8 | m_s.intmask[1]);
    if (mask & (1 << level)) {
        m_s.customNmi = 1;
        m_s.nmiVector = uint16_t(0xffb0 + level * 4);
        m_cpu.pulseNmi();
    }
}

// ---------------- video ----------------
void Eu3a05Core::updatePen(int pen)
{
    uint16_t dat = uint16_t(m_s.palram[pen * 2] << 8 | m_s.palram[pen * 2 + 1]);
    // llll lsss ---h hhhh
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

uint8_t Eu3a05Core::vidRead(int o)
{
    switch (o) {
    case 0x07: return m_s.vidctrl;
    case 0x09: return m_s.tileBaseLo;
    case 0x0a: return m_s.tileBaseHi;
    case 0x0b: return m_s.sprBaseLo;
    case 0x0c: return m_s.sprBaseHi;
    case 0x0d: case 0x0e: return m_s.splitpos[o - 0x0d];
    case 0x18: return m_s.transpen;
    default:
        if (o <= 0x06 || o == 0x08 || o == 0x17) return m_s.vidRam[o];
        if (o >= 0x0f && o <= 0x16) return m_s.tileScroll[o - 0x0f];
        return 0;
    }
}

void Eu3a05Core::vidWrite(int o, uint8_t d)
{
    switch (o) {
    case 0x07: m_s.vidctrl = d; return;
    case 0x09: m_s.tileBaseLo = d; return;
    case 0x0a: m_s.tileBaseHi = d; return;
    case 0x0b: m_s.sprBaseLo = d; return;
    case 0x0c: m_s.sprBaseHi = d; return;
    case 0x0d: case 0x0e: m_s.splitpos[o - 0x0d] = d; return;
    case 0x18: m_s.transpen = d; return;
    default:
        if (o <= 0x06 || o == 0x08 || o == 0x17) m_s.vidRam[o] = d;
        else if (o >= 0x0f && o <= 0x16) m_s.tileScroll[o - 0x0f] = d;
        return;
    }
}

// tilemaps at 0600 in RAM, sprites at 3E00
bool Eu3a05Core::tileData(int base, int drawpri, int& tile, int& attr, int& unk2)
{
    auto vram = [&](int off) -> uint8_t { int a = off + 0x600; return a < 0x4000 ? m_s.ram[a] : 0; };
    tile = vram(base * 4) | vram(base * 4 + 1) << 8;
    attr = vram(base * 4 + 2);
    unk2 = vram(base * 4 + 3);
    if (unk2 == 0x07) attr = 0;
    int priority = attr & 0x0f;
    if ((drawpri == 0 && priority == 0x0f) || (drawpri == 1 && priority != 0x0f)) return false;
    return true;
}

void Eu3a05Core::tileLine(int tile, int attr, int tilexsize, int i, int xpos, uint32_t* row)
{
    int colour = attr & 0xf0;
    const int perLine = 256 / tilexsize;
    const int mask = perLine - 1;
    tile = (tile & mask) + ((tile & ~mask) * tilexsize);
    if (!(m_s.vidctrl & 0x20)) tile <<= 1;     // 8bpp
    if (!(m_s.vidctrl & 0x40)) tile >>= 1;     // 8x8
    tile += (m_s.tileBaseLo | m_s.tileBaseHi << 8) << 5;
    if (m_s.vidctrl & 0x20) {                  // 4bpp
        for (int xx = 0; xx < tilexsize; xx += 2) {
            uint8_t pix = ext(uint32_t(((tile + i * 16) << 3) + (xx >> 1)));
            int x0 = xpos + xx;
            if (x0 >= 0 && x0 < 256) row[x0] = m_pen[((pix & 0xf0) >> 4) + colour];
            if (x0 + 1 >= 0 && x0 + 1 < 256) row[x0 + 1] = m_pen[(pix & 0x0f) + colour];
        }
    } else {
        for (int xx = 0; xx < tilexsize; xx++) {
            uint8_t pix = ext(uint32_t(((tile + i * 32) << 3) + xx));
            int x0 = xpos + xx;
            if (x0 >= 0 && x0 < 256) row[x0] = m_pen[(pix + ((colour & 0x70) << 1)) & 0xff];
        }
    }
}

uint16_t Eu3a05Core::tilemapIndex(uint16_t x, uint16_t y)
{
    uint16_t sizeY = 0, sizeX = 0, pageY = 14, pageX = 16;
    switch (m_s.vidctrl & 0x03) {
    case 0x00: sizeY = 28; sizeX = 32; break;
    case 0x02: sizeY = 28; sizeX = 16; break;
    case 0x01: sizeY = 14; sizeX = 32; break;
    case 0x03: sizeY = 14; sizeX = 16; break;
    }
    if (!(m_s.vidctrl & 0x40)) { pageY <<= 1; pageX <<= 1; sizeY <<= 1; sizeX <<= 1; }
    y %= sizeY;
    x %= sizeX;
    int page = 0;
    switch (m_s.vidctrl & 0x03) {
    case 0x00: page = (y < pageY ? 0 : 2) + (x < pageX ? 0 : 1); break;
    case 0x02: page = y < pageY ? 0 : 1; break;
    case 0x01: page = x < pageX ? 0 : 1; break;
    default: page = 0; break;
    }
    y %= pageY;
    x %= pageX;
    return uint16_t(x + y * pageX + page * pageY * pageX);
}

void Eu3a05Core::drawTilemaps(int drawpri)
{
    int scrolly = scroll(1);
    for (int line = 0; line < 224; line++) {
        int scrollx;
        if (line < m_s.splitpos[0]) scrollx = scroll(0);
        else if (line < m_s.splitpos[1]) scrollx = scroll(2);
        else scrollx = scroll(3);
        int realline = line + scrolly;
        uint32_t* row = &m_frame[line * 256];
        bool big = m_s.vidctrl & 0x40;
        int xtiles = big ? 16 : 32, xsize = big ? 16 : 8;
        int coarse = big ? scrollx >> 4 : scrollx >> 3;
        int fine = big ? scrollx & 0xf : scrollx & 7;
        int maprow = big ? realline >> 4 : realline >> 3;
        int tline = big ? realline & 0xf : realline & 7;
        for (int xt = 0; xt <= xtiles; xt++) {
            int idx = tilemapIndex(uint16_t(xt + coarse), uint16_t(maprow));
            int tile, attr, unk2;
            if (!tileData(idx, drawpri, tile, attr, unk2)) continue;
            tileLine(tile, attr, xsize, tline, xt * xsize - fine, row);
        }
    }
}

void Eu3a05Core::drawSprites()
{
    auto spr = [&](int off) -> uint8_t { int a = off + 0x3e00; return a < 0x4000 ? m_s.ram[a] : 0; };
    int base0 = (m_s.sprBaseLo | m_s.sprBaseHi << 8) * 0x100;
    for (int i = 512 - 8; i >= 0; i -= 8) {
        uint8_t x = spr(i + 2), y = spr(i + 1);
        if (y == 255) continue;
        uint8_t texX = spr(i + 4), texY = spr(i + 5), flags = spr(i + 0), attr = spr(i + 3);
        const int dblX = (flags & 0x10) >> 4, dblY = (flags & 0x20) >> 5;
        int priority = (attr & 0x0f) ^ 0xf;
        int colour = attr & 0xf0;
        if (colour & 0x80) colour = 0;
        if (!(flags & 0x80)) continue;
        int sx = (flags & 0x01) ? 16 : 8, sy = (flags & 0x02) ? 16 : 8;
        if (dblX) sx *= 2;
        if (dblY) sy *= 2;
        for (int yy = 0; yy < sy; yy++) {
            int dy = (flags & 0x08) ? (y + (sy - 1 - yy)) & 0xff : (y + yy) & 0xff;
            if (dy >= 224) continue;
            uint32_t* row = &m_frame[dy * 256];
            uint8_t* rowpri = &m_pri[dy * 256];
            for (int xx = 0; xx < sx; xx++) {
                int addr = base0 + ((texX + (dblX ? xx >> 1 : xx)) & 0xff);
                addr += ((texY + (dblY ? yy >> 1 : yy)) & 0xff) * 256;
                uint8_t pix = ext(uint32_t(addr));
                if (pix == m_s.transpen) continue;
                int dx = (flags & 0x04) ? (x + (sx - 1 - xx)) & 0xff : (x + xx) & 0xff;
                if (rowpri[dx] > priority) {
                    rowpri[dx] = uint8_t(priority);
                    row[dx] = m_pen[(pix + ((colour & 0x70) << 1)) & 0xff];
                }
            }
        }
    }
}

void Eu3a05Core::render()
{
    std::fill(m_frame.begin(), m_frame.end(), m_pen[0]);
    std::memset(m_pri, 0xff, sizeof(m_pri));
    drawTilemaps(0);
    drawSprites();
    drawTilemaps(1);
}

// ---------------- sound: six ADPCM channels at 8 kHz ----------------
uint8_t Eu3a05Core::sndRead(int o)
{
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

void Eu3a05Core::sndWrite(int o, uint8_t d)
{
    if (o < 0x12) {
        int sh = (o % 3) * 8;
        m_s.sndAddr[o / 3] = (m_s.sndAddr[o / 3] & ~(0xffu << sh)) | uint32_t(d) << sh;
        return;
    }
    if (o < 0x24) {
        o -= 0x12;
        int sh = (o % 3) * 8;
        m_s.sndLen[o / 3] = (m_s.sndLen[o / 3] & ~(0xffu << sh)) | uint32_t(d) << sh;
        return;
    }
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

int Eu3a05Core::adpcmClock(int ch, uint8_t nib)
{
    int32_t& sig = m_s.adSignal[ch];
    int32_t& step = m_s.adStep[ch];
    sig += g_diff[step * 16 + (nib & 15)];
    sig = std::clamp(sig, -2048, 2047);
    step = std::clamp(step + kIndexShift[nib & 7], 0, 48);
    return sig;
}

void Eu3a05Core::soundSample()
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
        }
    }
    int16_t out = int16_t(std::clamp(total * 4, -32768, 32767));
    // 8 kHz -> 44.1 kHz
    m_s.resampleAcc += 44100;
    while (m_s.resampleAcc >= 8000) {
        m_s.resampleAcc -= 8000;
        if (m_audio.size() < 44100) m_audio.push_back(out);
    }
}

size_t Eu3a05Core::readAudio(int16_t* out, size_t maxSamples)
{
    size_t n = std::min(maxSamples, m_audio.size());
    std::copy(m_audio.begin(), m_audio.begin() + long(n), out);
    m_audio.erase(m_audio.begin(), m_audio.begin() + long(n));
    return n;
}

// ---------------- frame ----------------
void Eu3a05Core::setInputs(uint32_t pressed)
{
    bool rst = (pressed & 0x100) && !(m_inputs & 0x100);
    m_inputs = pressed;
    if (rst) reset();
}

void Eu3a05Core::runFrame()
{
    const int perFrame = CPU_HZ / 50;
    const int timerPeriod = CPU_HZ / 2048;
    const int sndPeriod = CPU_HZ / 8000;
    int done = 0;
    while (done < perFrame) {
        int n = m_cpu.step();
        done += n;
        m_s.timerCycles -= n;
        while (m_s.timerCycles <= 0) { m_s.timerCycles += timerPeriod; customInterrupt(0); }
        m_s.sndCycles += n;
        while (m_s.sndCycles >= sndPeriod) { m_s.sndCycles -= sndPeriod; soundSample(); }
    }
    render();
    customInterrupt(9);        // vblank
}

// ---------------- save states ----------------
bool Eu3a05Core::saveState(std::vector<uint8_t>& out)
{
    struct Regs { uint16_t pc; uint8_t a, x, y, s, p, irq, nmi; } c = { m_cpu.pc, m_cpu.a, m_cpu.x, m_cpu.y, m_cpu.s, m_cpu.p, m_cpu.m_irqLine, m_cpu.m_nmiPending };
    out.clear();
    uint32_t magic = 0x35304145;   // 'EA05'
    auto put = [&](const void* p, size_t n) { const uint8_t* b = (const uint8_t*)p; out.insert(out.end(), b, b + n); };
    put(&magic, 4); put(&m_s, sizeof(m_s)); put(&c, sizeof(c));
    return true;
}

bool Eu3a05Core::loadState(const std::vector<uint8_t>& in)
{
    struct Regs { uint16_t pc; uint8_t a, x, y, s, p, irq, nmi; } c;
    if (in.size() != 4 + sizeof(m_s) + sizeof(c)) return false;
    uint32_t magic; std::memcpy(&magic, in.data(), 4);
    if (magic != 0x35304145) return false;
    std::memcpy(&m_s, in.data() + 4, sizeof(m_s));
    std::memcpy(&c, in.data() + 4 + sizeof(m_s), sizeof(c));
    m_cpu.pc = c.pc; m_cpu.a = c.a; m_cpu.x = c.x; m_cpu.y = c.y; m_cpu.s = c.s; m_cpu.p = c.p;
    m_cpu.m_irqLine = c.irq; m_cpu.m_nmiPending = c.nmi;
    for (int i = 0; i < 256; i++) updatePen(i);
    return true;
}
