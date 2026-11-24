// PocketPlug - Unicard Bridge Companion (port of MAME's unicard/bbcbc.cpp). See bbcbc.h.
// SPDX-License-Identifier: GPL-2.0-or-later

#include "bbcbc.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace {
uint8_t rd(void* u, uint16_t a) { return static_cast<BbcbcCore*>(u)->memRead(a); }
void wr(void* u, uint16_t a, uint8_t d) { static_cast<BbcbcCore*>(u)->memWrite(a, d); }
uint8_t in(z80* z, uint8_t p) { return static_cast<BbcbcCore*>(z->userdata)->ioRead(p); }
void out(z80* z, uint8_t p, uint8_t d) { static_cast<BbcbcCore*>(z->userdata)->ioWrite(p, d); }
const RomFile* file(const std::vector<RomFile>& roms, const char* name)
{
    for (auto& r : roms) if (r.name == name) return &r;
    return nullptr;
}
// 10.6875 MHz / 3, 50 Hz, 313 lines
constexpr double CPU_HZ = 10687500.0 / 3;
constexpr int LINES = 313;
}

bool BbcbcCore::load(const std::vector<RomFile>& roms, std::string& error)
{
    const RomFile* a = file(roms, "br_4_1.ic3");
    const RomFile* b = file(roms, "br_4_2.ic4");
    if (!a || !b) { error = "Missing br_4_1.ic3 / br_4_2.ic4"; return false; }
    std::memset(m_rom, 0xff, sizeof(m_rom));
    std::memcpy(m_rom, a->data.data(), std::min<size_t>(a->data.size(), 0x2000));
    std::memcpy(m_rom + 0x2000, b->data.data(), std::min<size_t>(b->data.size(), 0x2000));
    // anything else is the cartridge (its chips joined in order), at 4000-BFFF
    m_cart.clear();
    for (auto& r : roms)
        if (r.name != "br_4_1.ic3" && r.name != "br_4_2.ic4") m_cart.insert(m_cart.end(), r.data.begin(), r.data.end());
    if (m_cart.size() > 0x8000) m_cart.resize(0x8000);
    reset();
    return true;
}

void BbcbcCore::reset()
{
    std::memset(&m_s, 0, sizeof(m_s));
    m_s.select = 0xff;
    for (auto& p : m_s.port) { p.mode = 1; p.ior = 0xff; }    // input mode after reset
    std::memset(&m_z80, 0, sizeof(m_z80));
    z80_init(&m_z80);
    m_z80.read_byte = rd; m_z80.write_byte = wr;
    m_z80.port_in = in; m_z80.port_out = out;
    m_z80.userdata = this;
    std::memset(m_vdp.vram, 0, sizeof(m_vdp.vram));
    m_vdp.reset();
}

uint8_t BbcbcCore::memRead(uint16_t a)
{
    if (a < 0x4000) return m_rom[a];
    if (a < 0xc000) { size_t o = a - 0x4000; return o < m_cart.size() ? m_cart[o] : 0xff; }
    if (a >= 0xe000 && a < 0xe800) return m_s.ram[a - 0xe000];
    return 0xff;
}

void BbcbcCore::memWrite(uint16_t a, uint8_t d)
{
    if (a >= 0xe000 && a < 0xe800) m_s.ram[a - 0xe000] = d;
}

uint8_t BbcbcCore::buttons() const
{
    // the PIO's port B high nibble picks a row of four buttons (active low)
    int row = m_s.select == 0xef ? 0 : m_s.select == 0xdf ? 1 : m_s.select == 0xbf ? 2 : -1;
    if (row < 0) return 0xff;
    return uint8_t(~(m_in >> (row * 4)) & 0x0f) | 0xf0;
}

uint8_t BbcbcCore::pioRead(int reg)
{
    Port& p = m_s.port[reg & 1];
    if (reg & 2) return 0xff;   // control registers can't be read
    switch (p.mode) {
    case 0: return p.output;
    case 1: if (reg & 1) p.input = buttons(); return p.input;
    case 3: if (reg & 1) p.input = buttons(); return uint8_t((p.input & p.ior) | (p.output & ~p.ior));
    default: return p.input;
    }
}

void BbcbcCore::pioWrite(int reg, uint8_t d)
{
    Port& p = m_s.port[reg & 1];
    if (reg & 2) {
        // control word
        if (p.next == 1) { p.ior = d; p.next = 0; if (reg & 1) m_s.select = uint8_t(p.ior | (p.output & ~p.ior)); return; }
        if (p.next == 2) { p.next = 0; return; }                   // interrupt mask
        if ((d & 0x0f) == 0x0f) { p.mode = d >> 6; if (p.mode == 3) p.next = 1; return; }
        if ((d & 0x0f) == 0x07) { if (d & 0x10) p.next = 2; return; }
        return;                                                   // vector / interrupt enable
    }
    p.output = d;
    if (!(reg & 1)) return;
    if (p.mode == 0) m_s.select = d;
    else if (p.mode == 3) m_s.select = uint8_t(p.ior | (d & ~p.ior));
}

uint8_t BbcbcCore::ioRead(uint8_t a)
{
    if (a < 0x80) return pioRead(a >> 5);
    if (a < 0x82) return (a & 1) ? m_vdp.registerRead() : m_vdp.vramRead();
    return 0xff;
}

void BbcbcCore::ioWrite(uint8_t a, uint8_t d)
{
    if (a < 0x80) pioWrite(a >> 5, d);
    else if (a < 0x82) { if (a & 1) m_vdp.registerWrite(d); else m_vdp.vramWrite(d); }
}

void BbcbcCore::runFrame()
{
    const double perLine = CPU_HZ / 50.0 / LINES;
    for (int line = 0; line < LINES; line++) {
        m_carry += perLine;
        unsigned long target = m_z80.cyc + (unsigned long)m_carry;
        m_carry -= (unsigned long)m_carry;
        while (m_z80.cyc < target) {
            // the VDP's interrupt is level triggered on INT
            if (m_vdp.irq()) z80_gen_int(&m_z80, 0xff); else m_z80.int_pending = 0;
            z80_step(&m_z80);
        }
        m_vdp.line(line);
    }
    uint32_t back = Tms9928a::palette(m_vdp.regs[7] & 15);
    std::fill(m_frame.begin(), m_frame.end(), back);
    for (int y = 0; y < 192; y++) std::memcpy(&m_frame[(y + 8) * W + 8], &m_vdp.out[y * 256], 256 * 4);
    m_audio.insert(m_audio.end(), 882, 0);
    if (m_audio.size() > 44100) m_audio.erase(m_audio.begin(), m_audio.end() - 4410);
}

size_t BbcbcCore::readAudio(int16_t* out, size_t maxSamples)
{
    size_t n = std::min(maxSamples, m_audio.size());
    std::memcpy(out, m_audio.data(), n * sizeof(int16_t));
    m_audio.erase(m_audio.begin(), m_audio.begin() + long(n));
    return n;
}

bool BbcbcCore::mediaInsert(int drive, const std::string& path, std::string& error)
{
    (void)drive;
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) { error = "Can't open the cartridge"; return false; }
    std::vector<uint8_t> d(0x8000);
    size_t n = fread(d.data(), 1, d.size(), f);
    fclose(f);
    if (!n) { error = "Empty cartridge"; return false; }
    d.resize(n);
    m_cart = d;
    reset();
    return true;
}

void BbcbcCore::mediaEject(int drive) { (void)drive; m_cart.clear(); reset(); }

bool BbcbcCore::saveState(std::vector<uint8_t>& out)
{
    out.clear();
    uint32_t magic = 0x43424242;
    auto put = [&](const void* p, size_t n) { const uint8_t* b = (const uint8_t*)p; out.insert(out.end(), b, b + n); };
    put(&magic, 4); put(&m_s, sizeof(m_s)); put(&m_z80, sizeof(m_z80)); put(&m_vdp, sizeof(m_vdp));
    return true;
}

bool BbcbcCore::loadState(const std::vector<uint8_t>& in)
{
    if (in.size() != 4 + sizeof(m_s) + sizeof(m_z80) + sizeof(m_vdp)) return false;
    uint32_t magic; std::memcpy(&magic, in.data(), 4);
    if (magic != 0x43424242) return false;
    const uint8_t* p = in.data() + 4;
    std::memcpy(&m_s, p, sizeof(m_s)); p += sizeof(m_s);
    z80 keep = m_z80;
    std::memcpy(&m_z80, p, sizeof(m_z80)); p += sizeof(m_z80);
    m_z80.read_byte = keep.read_byte; m_z80.write_byte = keep.write_byte;
    m_z80.port_in = keep.port_in; m_z80.port_out = keep.port_out; m_z80.userdata = this;
    std::memcpy(&m_vdp, p, sizeof(m_vdp));
    return true;
}
