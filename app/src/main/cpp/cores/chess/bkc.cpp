// PocketPlug - Fidelity Backgammon Challenger (port of MAME's fidelity/cc7.cpp). See bkc.h.
// SPDX-License-Identifier: GPL-2.0-or-later

#include "bkc.h"
#include <algorithm>
#include <cstring>

namespace {
constexpr int CPU_HZ = 3579545;
uint8_t zRead(void* u, uint16_t a) { return static_cast<BkcCore*>(u)->memRead(a); }
void zWrite(void* u, uint16_t a, uint8_t d) { static_cast<BkcCore*>(u)->memWrite(a, d); }
uint8_t zIn(z80* z, uint8_t p) { return static_cast<BkcCore*>(z->userdata)->ioRead(p); }
void zOut(z80* z, uint8_t p, uint8_t d) { static_cast<BkcCore*>(z->userdata)->ioWrite(p, d); }
}

bool BkcCore::load(const std::vector<RomFile>& roms, std::string& error)
{
    const RomFile* rom = nullptr;
    for (auto& r : roms) if (r.name == "cn19255n_101-32012" && r.data.size() == 0x1000) rom = &r;
    if (!rom) { error = "Missing cn19255n_101-32012 (backgamc zip)"; return false; }
    std::memcpy(m_rom, rom->data.data(), 0x1000);
    std::memset(&m_s, 0, sizeof(m_s));
    std::memset(m_acc, 0, sizeof(m_acc));
    z80_init(&m_z80);
    m_z80.read_byte = zRead; m_z80.write_byte = zWrite;
    m_z80.port_in = zIn; m_z80.port_out = zOut;
    m_z80.userdata = this;
    return true;
}

void BkcCore::reset()
{
    auto keep = m_z80;
    z80_init(&m_z80);
    m_z80.read_byte = keep.read_byte; m_z80.write_byte = keep.write_byte;
    m_z80.port_in = keep.port_in; m_z80.port_out = keep.port_out; m_z80.userdata = keep.userdata;
}

uint8_t BkcCore::memRead(uint16_t a)
{
    a &= 0x3fff;
    if (a < 0x1000) return m_rom[a];
    if (a >= 0x3000) return m_s.ram[a & 0xff];
    return 0xff;
}

void BkcCore::memWrite(uint16_t a, uint8_t d)
{
    a &= 0x3fff;
    if (a >= 0x3000) m_s.ram[a & 0xff] = d;
}

uint8_t BkcCore::ioRead(uint8_t)
{
    uint8_t data = 0;
    for (int i = 0; i < 4; i++) if (m_s.select >> i & 1) data |= uint8_t(m_keys >> (i * 4) & 0xf);
    return data;
}

void BkcCore::ioWrite(uint8_t p, uint8_t d)
{
    flushDisplay();
    // a0-a2, d7: segment data through an NE591 latch; d0-d3 digit select and key
    // mux, d4/d5 the two lamps
    uint8_t mask = uint8_t(1 << (p & 7));
    m_s.segData = uint8_t((m_s.segData & ~mask) | ((d & 0x80) ? mask : 0));
    m_s.select = d & 0x3f;
}

void BkcCore::flushDisplay()
{
    uint64_t dt = m_time - m_lastFlush;
    m_lastFlush = m_time;
    if (!dt) return;
    for (int r = 0; r < 6; r++) {
        if (!(m_s.select >> r & 1)) continue;
        for (int s = 0; s < 8; s++) if (m_s.segData >> s & 1) m_acc[r * 8 + s] += dt;
    }
}

void BkcCore::runFrame()
{
    const uint64_t end = m_time + CPU_HZ / 60;
    m_lastFlush = m_time;
    while (m_time < end) {
        unsigned long before = m_z80.cyc;
        z80_step(&m_z80);
        int cyc = int(m_z80.cyc - before);
        m_time += uint64_t(cyc > 0 ? cyc : 4);
    }
    flushDisplay();
    m_audioPending = 735;
    std::memset(m_out, 0, sizeof(m_out));
    // segments within a digit are compared with that digit's brightest one, and a
    // digit keeps what it last showed for a few frames, so the multiplex doesn't flicker
    for (int d = 0; d < 6; d++) {
        uint8_t m = 0; uint64_t mx = 0;
        for (int sg = 0; sg < 8; sg++) mx = std::max(mx, m_acc[d * 8 + sg]);
        for (int sg = 0; sg < 8; sg++) if (mx && m_acc[d * 8 + sg] * 3 >= mx) m |= uint8_t(1 << sg);
        if (mx) { m_held[d] = m; m_age[d] = 0; }
        else if (m_age[d] < 255) m_age[d]++;
        uint8_t show = m_age[d] < 8 ? m_held[d] : 0;
        if (d < 4) for (int sg = 0; sg < 8; sg++) m_out[130 + d * 8 + sg] = (show >> sg & 1) ? 255 : 0;
        else m_out[170 + d - 4] = (show & 1) ? 255 : 0;
    }
    std::memset(m_acc, 0, sizeof(m_acc));
}

size_t BkcCore::readAudio(int16_t* out, size_t maxSamples)
{
    size_t n = std::min<size_t>(maxSamples, m_audioPending);   // no sound hardware: silence
    std::memset(out, 0, n * 2);
    m_audioPending -= n;
    return n;
}

namespace { constexpr uint32_t MAGIC = 0x31434b42; }  // 'BKC1'

bool BkcCore::saveState(std::vector<uint8_t>& out)
{
    out.clear();
    auto put = [&](const void* p, size_t n) { const uint8_t* b = (const uint8_t*)p; out.insert(out.end(), b, b + n); };
    uint32_t magic = MAGIC;
    put(&magic, 4); put(&m_s, sizeof(m_s)); put(&m_z80, sizeof(m_z80));
    return true;
}

bool BkcCore::loadState(const std::vector<uint8_t>& in)
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
