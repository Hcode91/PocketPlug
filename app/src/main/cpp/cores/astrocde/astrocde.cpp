// PocketPlug - Bally Astrocade (port of MAME's bally/astrohome.cpp, astrocde_v.cpp and
// sound/astrocde.cpp). See astrocde.h.
// SPDX-License-Identifier: GPL-2.0-or-later

#include "astrocde.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
constexpr int LINE = 455;                 // pixel clocks a scanline
constexpr int LINES = 262;
constexpr int64_t FRAME = int64_t(LINE) * LINES;
constexpr int VERT_OFFSET = 22;
constexpr int HORZ_OFFSET = 16;
constexpr double CPU_HZ = 14318181.0 / 8.0;
constexpr int AUDIO_HZ = 44100;

uint8_t zRead(void* u, uint16_t a) { return static_cast<AstrocadeCore*>(u)->memRead(a); }
void zWrite(void* u, uint16_t a, uint8_t d) { static_cast<AstrocadeCore*>(u)->memWrite(a, d); }
uint8_t zIn(z80* z, uint8_t p) { return static_cast<AstrocadeCore*>(z->userdata)->ioRead(uint16_t(z->port_hi << 8 | p)); }
void zOut(z80* z, uint8_t p, uint8_t d) { static_cast<AstrocadeCore*>(z->userdata)->ioWrite(uint16_t(z->port_hi << 8 | p), d); }
}

bool AstrocadeCore::load(const std::vector<RomFile>& roms, std::string& error)
{
    bool bios = false;
    m_cart.clear();
    for (auto& r : roms) {
        if (r.name == m_biosName && r.data.size() == 0x2000) { std::memcpy(m_bios, r.data.data(), 0x2000); bios = true; }
        else if (!r.data.empty()) m_cart = r.data;
    }
    if (!bios) { error = std::string("Missing the BIOS (") + m_biosName + ")"; return false; }
    if (m_cart.size() > 0x2000) { error = "Cartridge is larger than 8K (bank-switched carts aren't supported yet)"; return false; }

    // MAME's astrocade_palette: 32 colours round the YUV circle x 16 luma steps
    for (int color = 0; color < 32; color++) {
        double angle = (color / 32.0) * (2.0 * M_PI);
        float ry = color ? float(0.75 * std::sin(angle)) : 0.f;
        float by = color ? float(1.15 * std::cos(angle)) : 0.f;
        for (int luma = 0; luma < 16; luma++) {
            float y = luma / 15.0f;
            int r = int((ry + y) * 255);
            int g = int(((y - 0.299f * (ry + y) - 0.114f * (by + y)) / 0.587f) * 255);
            int b = int((by + y) * 255);
            r = std::clamp(r, 0, 255); g = std::clamp(g, 0, 255); b = std::clamp(b, 0, 255);
            m_palette[color * 16 + luma] = 0xff000000u | uint32_t(r) << 16 | uint32_t(g) << 8 | uint32_t(b);
        }
    }
    std::memset(m_fb, 0, sizeof(m_fb));
    reset();
    return true;
}

void AstrocadeCore::reset()
{
    std::memset(&m_s, 0, sizeof(m_s));
    m_s.intOffAt = -1;
    m_s.knob = 128;
    z80_init(&m_z80);
    m_z80.read_byte = zRead; m_z80.write_byte = zWrite;
    m_z80.port_in = zIn; m_z80.port_out = zOut;
    m_z80.userdata = this;
}

void AstrocadeCore::setAnalog(int stick, float x, float)
{
    if (stick != 0) return;
    // the knob reads inverted: turned right = low, centre = 128 (as MAME starts it)
    m_s.knob = uint8_t(std::clamp<long>(std::lround(127.5 - double(std::clamp(x, -1.f, 1.f)) * 127.5), 0, 255));
}

uint8_t AstrocadeCore::memRead(uint16_t a)
{
    if (a < 0x2000) return m_bios[a];
    if (a < 0x4000) { uint16_t o = uint16_t(a - 0x2000); return m_cart.empty() ? 0 : (o < m_cart.size() ? m_cart[o] : 0xff); }
    if (a < 0x5000) return m_s.vram[a & 0xfff];
    return 0;
}

void AstrocadeCore::memWrite(uint16_t a, uint8_t d)
{
    if (a < 0x1000) funcgenWrite(a, d);
    else if (a >= 0x4000 && a < 0x5000) m_s.vram[a & 0xfff] = d;
}

void AstrocadeCore::funcgenWrite(uint16_t offset, uint8_t data)
{
    // control: d0-d1 shift, d2 rotate, d3 expand, d4 OR, d5 XOR, d6 flop
    if (m_s.fgControl & 0x08) {
        m_s.fgExpandCount ^= 1;
        data = uint8_t(data >> (4 * m_s.fgExpandCount));
        data = uint8_t((m_s.fgExpandColor[(data >> 3) & 1] << 6) | (m_s.fgExpandColor[(data >> 2) & 1] << 4) |
                       (m_s.fgExpandColor[(data >> 1) & 1] << 2) | (m_s.fgExpandColor[data & 1]));
    }
    uint8_t prev = m_s.fgShiftPrev;
    m_s.fgShiftPrev = data;
    if (m_s.fgControl & 0x04) {
        if ((m_s.fgRotateCount & 4) == 0) { m_s.fgRotateData[m_s.fgRotateCount++ & 3] = data; return; }
        int shift = 2 * (~m_s.fgRotateCount++ & 3);
        data = uint8_t((((m_s.fgRotateData[3] >> shift) & 3) << 6) | (((m_s.fgRotateData[2] >> shift) & 3) << 4) |
                       (((m_s.fgRotateData[1] >> shift) & 3) << 2) | ((m_s.fgRotateData[0] >> shift) & 3));
    } else {
        int shift = 2 * (m_s.fgControl & 3);
        data = uint8_t((data >> shift) | (prev << (8 - shift)));
    }
    if (m_s.fgControl & 0x40) data = uint8_t((data >> 6) | ((data >> 2) & 0x0c) | ((data << 2) & 0x30) | (data << 6));
    uint16_t addr = uint16_t(0x4000 + offset);
    if (m_s.fgControl & 0x30) {
        uint8_t old = memRead(addr);
        m_s.fgIntercept &= 0x0f;
        if ((old & 0xc0) && (data & 0xc0)) m_s.fgIntercept |= 0x11;
        if ((old & 0x30) && (data & 0x30)) m_s.fgIntercept |= 0x22;
        if ((old & 0x0c) && (data & 0x0c)) m_s.fgIntercept |= 0x44;
        if ((old & 0x03) && (data & 0x03)) m_s.fgIntercept |= 0x88;
        if (m_s.fgControl & 0x10) data |= old;
        else if (m_s.fgControl & 0x20) data ^= old;
    }
    memWrite(addr, data);
}

uint8_t AstrocadeCore::ioRead(uint16_t port)
{
    uint8_t lo = uint8_t(port);
    if (lo < 0x10) {
        switch (lo) {
        case 0x08: { uint8_t r = m_s.fgIntercept; m_s.fgIntercept = 0; return r; }
        case 0x0e: return m_s.vertFeedback;
        case 0x0f: return m_s.horizFeedback;
        default: return 0xff;
        }
    }
    if (lo < 0x20) {
        int o = lo & 0x0f;
        if (o < 8) {
            if (o & 4) {   // keypad column (o & 3): rows 0-5
                int col = o & 3;
                return uint8_t(m_keys >> (8 + col * 6) & 0x3f);
            }
            if ((o & 3) == 0) return uint8_t(m_keys & 0x1f);   // hand controller 1
            return 0;
        }
        if (o >= 0x0c) return (o & 3) == 0 ? m_s.knob : 0;
        return 0xff;
    }
    return 0;
}

void AstrocadeCore::ioWrite(uint16_t port, uint8_t d)
{
    uint8_t lo = uint8_t(port);
    if (lo < 0x10) {
        switch (lo) {
        case 0x00: case 0x01: case 0x02: case 0x03: case 0x04: case 0x05: case 0x06: case 0x07:
            m_s.colors[lo] = d; break;
        case 0x08: m_s.videoMode = d & 1; break;
        case 0x09: m_s.colorsplit = uint8_t(2 * (d & 0x3f)); m_s.bgdata = uint8_t(((d & 0xc0) >> 6) * 0x55); break;
        case 0x0a: m_s.vblank = d; break;
        case 0x0b: m_s.colors[(port >> 8) & 7] = d; break;
        case 0x0c: m_s.fgControl = d; m_s.fgExpandCount = 0; m_s.fgRotateCount = 0; m_s.fgShiftPrev = 0; break;
        case 0x0d: m_s.intFeedback = d; m_s.intLine = 0; break;
        case 0x0e: m_s.intEnable = d; m_s.intLine = 0; break;
        case 0x0f: m_s.intScanline = d; m_s.intLine = 0; break;
        }
        return;
    }
    if (lo <= 0x18) {
        int reg = (lo & 8) ? (port >> 8) & 7 : lo & 7;
        m_s.reg[reg] = d;
        return;
    }
    if (lo == 0x19) { m_s.fgExpandColor[0] = d & 3; m_s.fgExpandColor[1] = (d >> 2) & 3; }
}

void AstrocadeCore::renderLine(int y)
{
    if (y < 0 || y >= H) return;
    int effy = y - VERT_OFFSET;
    if (effy < 0) effy += LINES;
    int xystep = 2 - m_s.videoMode;
    uint16_t offset = uint16_t((effy / xystep) * (80 / xystep));
    uint32_t* dest = m_fb + y * W;
    int n = 0;
    for (int x = 0; x < 456 / 4 && n < W; x += xystep) {
        int effx = x - HORZ_OFFSET / 4;
        const uint8_t* colorbase = &m_s.colors[(effx < m_s.colorsplit) ? 4 : 0];
        uint8_t data = (effx >= 0 && effx < 80 && effy < m_s.vblank) ? m_s.vram[offset++ & 0xfff] : m_s.bgdata;
        for (int xx = 0; xx < 4; xx++) {
            int pix = (data >> 6) & 3;
            int colordata = colorbase[pix] << 1;
            uint32_t c = m_palette[(colordata & 0x1f0) | (colordata & 0x0f)];
            if (n < W) dest[n++] = c;
            if (xystep == 2 && n < W) dest[n++] = c;
            data = uint8_t(data << 2);
        }
    }
}

void AstrocadeCore::sound(int cycles)
{
    const double perSample = CPU_HZ / AUDIO_HZ;
    while (cycles > 0) {
        int n = std::min(cycles, 256 - m_s.masterCount);
        n = std::min(n, 64 - m_s.noiseClock);
        int toSample = int(std::ceil(perSample - m_s.audioCycles));
        if (toSample < 1) toSample = 1;
        n = std::min(n, toSample);
        int cur = 0;
        if (m_s.aState) cur += m_s.reg[6] & 0x0f;
        if (m_s.bState) cur += m_s.reg[6] >> 4;
        if (m_s.cState) cur += m_s.reg[5] & 0x0f;
        if ((m_s.reg[5] & 0x20) && (m_s.noiseState & 0x4000)) cur += m_s.reg[7] >> 4;
        m_s.audioAcc += double(cur) * n;
        m_s.audioCycles += n;
        if (m_s.audioCycles >= perSample) {
            double v = m_s.audioAcc / m_s.audioCycles;      // 0..60
            m_audio.push_back(int16_t(std::clamp(v * 32767.0 / 60.0, 0.0, 32767.0)));
            m_s.audioAcc = 0; m_s.audioCycles = 0;
        }
        m_s.noiseClock = uint8_t(m_s.noiseClock + n);
        if (m_s.noiseClock >= 64) {
            m_s.noiseState = uint16_t((m_s.noiseState << 1) | (~((m_s.noiseState >> 14) ^ (m_s.noiseState >> 13)) & 1));
            m_s.noiseClock = uint8_t(m_s.noiseClock - 64);
            m_s.vibratoClock++;
        }
        int mc = m_s.masterCount + n;
        m_s.masterCount = uint8_t(mc);
        if (mc == 256) {
            m_s.masterCount = uint8_t(~m_s.reg[0]);
            if ((m_s.reg[5] & 0x10) == 0) {
                if (!((m_s.vibratoClock >> (m_s.reg[4] >> 6)) & 0x0200)) m_s.masterCount = uint8_t(m_s.masterCount + (m_s.reg[4] & 0x3f));
            } else {
                uint8_t nb = uint8_t(m_s.noiseState >> 7), sw = 0;
                for (int i = 0; i < 8; i++) if (nb >> i & 1) sw |= uint8_t(0x80 >> i);
                m_s.masterCount = uint8_t(m_s.masterCount + (sw & m_s.reg[7]));
            }
            if (++m_s.aCount == 0) { m_s.aState ^= 1; m_s.aCount = uint8_t(~m_s.reg[1]); }
            if (++m_s.bCount == 0) { m_s.bState ^= 1; m_s.bCount = uint8_t(~m_s.reg[2]); }
            if (++m_s.cCount == 0) { m_s.cState ^= 1; m_s.cCount = uint8_t(~m_s.reg[3]); }
        }
        cycles -= n;
    }
}

void AstrocadeCore::cpuRun(int64_t until)
{
    while (m_s.px < until) {
        if (m_s.intOffAt >= 0 && m_s.px >= m_s.intOffAt) { m_s.intLine = 0; m_s.intOffAt = -1; }
        m_z80.int_pending = m_s.intLine != 0;
        m_z80.int_data = m_s.intVector;
        unsigned long before = m_z80.cyc;
        z80_step(&m_z80);
        int cyc = int(m_z80.cyc - before);
        if (cyc <= 0) cyc = 4;
        if (m_s.intLine && !m_z80.int_pending && m_s.intClearOnAck) m_s.intLine = 0;   // acknowledged
        m_z80.int_pending = 0;
        m_s.px += int64_t(cyc) * 4;
        sound(cyc);
    }
}

void AstrocadeCore::runFrame()
{
    for (int s = 0; s < LINES; s++) {
        cpuRun(int64_t(s) * LINE);
        if (s > 0) renderLine(s - 1);
        int ascan = s - VERT_OFFSET;
        if (ascan < 0) ascan += LINES;
        if (ascan == m_s.intScanline && (m_s.intEnable & 0x08)) {
            m_s.intVector = m_s.intFeedback;
            m_s.intLine = 1;
            if ((m_s.intEnable & 0x04) == 0) { m_s.intClearOnAck = 1; m_s.intOffAt = FRAME; }   // until vblank ends
            else { m_s.intClearOnAck = 0; m_s.intOffAt = m_s.px + 4; }                           // for one cycle
        }
    }
    cpuRun(FRAME);
    m_s.px -= FRAME;
    if (m_s.intOffAt >= 0) m_s.intOffAt = std::max<int64_t>(0, m_s.intOffAt - FRAME);
    if (m_audio.size() > 44100) m_audio.erase(m_audio.begin(), m_audio.end() - 4410);
}

size_t AstrocadeCore::readAudio(int16_t* out, size_t maxSamples)
{
    size_t n = std::min(maxSamples, m_audio.size());
    std::memcpy(out, m_audio.data(), n * sizeof(int16_t));
    m_audio.erase(m_audio.begin(), m_audio.begin() + long(n));
    return n;
}

namespace { constexpr uint32_t MAGIC = 0x31545341; }   // 'AST1'

bool AstrocadeCore::saveState(std::vector<uint8_t>& out)
{
    out.clear();
    auto put = [&](const void* p, size_t n) { const uint8_t* b = (const uint8_t*)p; out.insert(out.end(), b, b + n); };
    uint32_t magic = MAGIC;
    put(&magic, 4); put(&m_s, sizeof(m_s)); put(&m_z80, sizeof(m_z80));
    return true;
}

bool AstrocadeCore::loadState(const std::vector<uint8_t>& in)
{
    if (in.size() != 4 + sizeof(m_s) + sizeof(m_z80)) return false;
    uint32_t magic; std::memcpy(&magic, in.data(), 4);
    if (magic != MAGIC) return false;
    std::memcpy(&m_s, in.data() + 4, sizeof(m_s));
    auto keep = m_z80;
    std::memcpy(&m_z80, in.data() + 4 + sizeof(m_s), sizeof(m_z80));
    m_z80.read_byte = keep.read_byte; m_z80.write_byte = keep.write_byte;
    m_z80.port_in = keep.port_in; m_z80.port_out = keep.port_out; m_z80.userdata = keep.userdata;
    return true;
}
