// PocketPlug - AtGames SMS / Game Gear plug & play. See atgsms.h.
// SPDX-License-Identifier: GPL-2.0-or-later

#include "atgsms.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
constexpr int CPU_HZ = 3579545;
constexpr int LINE_CYC = 228;            // CPU cycles a scanline
constexpr uint32_t MENU = 0x200000;
uint8_t zRead(void* u, uint16_t a) { return static_cast<AtgSmsCore*>(u)->memRead(a); }
void zWrite(void* u, uint16_t a, uint8_t d) { static_cast<AtgSmsCore*>(u)->memWrite(a, d); }
uint8_t zIn(z80* z, uint8_t p) { return static_cast<AtgSmsCore*>(z->userdata)->ioRead(p); }
void zOut(z80* z, uint8_t p, uint8_t d) { static_cast<AtgSmsCore*>(z->userdata)->ioWrite(p, d); }
bool dbg() { static int v = getenv("ATGDBG") ? 1 : 0; return v; }
}

bool AtgSmsCore::load(const std::vector<RomFile>& roms, std::string& error)
{
    m_flash.clear();
    for (auto& r : roms) if (r.name == "atgames_sms.u5" && r.data.size() == 0x400000) m_flash = r.data;
    if (m_flash.empty()) { error = "Missing atgames_sms.u5"; return false; }
    m_psg.setSega();
    reset();
    return true;
}

void AtgSmsCore::reset()
{
    std::memset(&m_s, 0, sizeof(m_s));
    m_s.base = MENU; m_s.size = 0;   // size 0: the menu
    m_s.bank[0] = 0; m_s.bank[1] = 1; m_s.bank[2] = 2;
    m_vdp.reset(); m_vdp.gg = true;
    m_psg.reset();
    z80_init(&m_z80);
    m_z80.read_byte = zRead; m_z80.write_byte = zWrite;
    m_z80.port_in = zIn; m_z80.port_out = zOut;
    m_z80.userdata = this;
    std::memset(m_fb, 0, sizeof(m_fb));
}

void AtgSmsCore::setInputs(uint32_t pressed)
{
    // pad: A = button 1, B and C = button 2; START is the Game Gear START, or PAUSE (NMI) in a
    // Master System game
    uint32_t k = pressed & 0x3f;
    if (pressed & 0x80) k |= 0x20;
    bool start = pressed & 0x40;
    if (start && !m_s.pauseHeld && m_s.size && !m_s.gg) z80_gen_nmi(&m_z80);
    m_s.pauseHeld = start ? 1 : 0;
    if (start) k |= 0x40;
    m_keys = k;
}

uint8_t AtgSmsCore::memRead(uint16_t a)
{
    if (a >= 0xc000) return m_s.ram[a & 0x1fff];
    uint32_t bank = a < 0x400 ? 0 : m_s.bank[a >> 14];
    uint32_t mask = m_s.size ? m_s.size : 0x10000;
    uint32_t off = (bank * 0x4000 + (a & 0x3fff)) & (mask - 1);
    uint32_t p = m_s.base + off;
    return p < m_flash.size() ? m_flash[p] : 0xff;
}

void AtgSmsCore::memWrite(uint16_t a, uint8_t d)
{
    if (a >= 0xc000) {
        m_s.ram[a & 0x1fff] = d;
        if (a >= 0xfffc) {
            if (a == 0xfffc) m_s.ctrl = d;
            else m_s.bank[a - 0xfffd] = d;
        }
        return;
    }
    if (dbg()) fprintf(stderr, "rom write %04x=%02x pc=%04x\n", a, d, m_z80.pc);
}

uint8_t AtgSmsCore::ioRead(uint8_t port)
{
    switch (port) {
    case 0x7e: return m_vdp.vcounter();
    case 0x7f: return m_vdp.s.hcount;
    case 0xbe: return m_vdp.readData();
    case 0xbf: return m_vdp.readControl();
    case 0xdc: case 0xc0: return uint8_t(~(m_keys & 0x3f));
    case 0xdd: case 0xc1: return 0xff;
    case 0x93: return uint8_t(m_vdp.s.line >= SmsVdp::H ? 0x08 : 0x00);   // bit 3: vertical blank (the launch stub waits on it)
    case 0x00: return uint8_t((m_keys & 0x40) ? 0x40 : 0xc0);   // Game Gear START (bit 7, active low)
    default: break;
    }
    if (dbg()) fprintf(stderr, "in  %02x -> %02x pc=%04x\n", port, m_s.custom[port], m_z80.pc);
    return m_s.custom[port];
}

void AtgSmsCore::ioWrite(uint8_t port, uint8_t d)
{
    switch (port) {
    case 0x7e: case 0x7f: m_psg.write(d); return;
    case 0xbe: m_vdp.writeData(d); return;
    case 0xbf: m_vdp.writeControl(d); return;
    case 0x3f: m_s.ioCtrl = d; return;
    default: break;
    }
    if (dbg()) fprintf(stderr, "out %02x <- %02x pc=%04x\n", port, d, m_z80.pc);
    uint8_t old = m_s.custom[port];
    m_s.custom[port] = d;
    // the menu's launch stub ends with port 0x8c bit 4 set, then JP 0: the flash base the
    // menu set up (0x61 = A8-A15, 0x62 = A16-A20, 0xde bit 3 = A21) goes live
    if (port == 0x8c && (d & 0x10) && !(old & 0x10) && !m_s.size) launch(0);
}

void AtgSmsCore::launch(int)
{
    m_s.base = uint32_t(((m_s.custom[0xde] >> 3) & 1) << 21 | (m_s.custom[0x62] & 0x1f) << 16 | m_s.custom[0x61] << 8);
    m_s.size = 0x80000;
    m_s.gg = (m_s.custom[0x88] & 0x80) ? 1 : 0;
    m_vdp.gg = m_s.gg;
    m_s.bank[0] = 0; m_s.bank[1] = 1; m_s.bank[2] = 2;
    if (dbg()) fprintf(stderr, "launch base %06x gg %d\n", m_s.base, m_s.gg);
}

void AtgSmsCore::runFrame()
{
    const double perSample = double(CPU_HZ) / 44100.0;
    for (int line = 0; line < SmsVdp::LINES; line++) {
        int done = m_lineCarry;
        while (done < LINE_CYC) {
            m_z80.int_pending = m_vdp.irq();
            m_z80.int_data = 0xff;
            unsigned long before = m_z80.cyc;
            z80_step(&m_z80);
            int c = int(m_z80.cyc - before);
            if (c <= 0) c = 4;
            done += c;
            m_vdp.s.hcount = uint8_t(std::min(255, done * 342 / LINE_CYC / 2));
            for (int i = 0; i < c; i++) {
                int v = m_psg.clock();
                m_s.audioAcc += v; m_s.audioN += 1;
                m_s.audioPos += 1;
                if (m_s.audioPos >= perSample) {
                    m_s.audioPos -= perSample;
                    m_audio.push_back(int16_t(std::min(32767.0, m_s.audioAcc / m_s.audioN)));
                    m_s.audioAcc = 0; m_s.audioN = 0;
                }
            }
        }
        m_lineCarry = done - LINE_CYC;
        if (line < SmsVdp::H) m_vdp.renderLine(line, m_fb + line * SmsVdp::W);
        m_vdp.endLine();
    }
    m_z80.int_pending = 0;
    if (m_s.gg) {
        // Game Gear games: the 160 x 144 window at (48, 24), scaled to fill the TV picture
        static uint32_t tmp[SmsVdp::W * SmsVdp::H];
        std::memcpy(tmp, m_fb, sizeof(tmp));
        for (int y = 0; y < SmsVdp::H; y++) {
            const uint32_t* src = tmp + (24 + y * 144 / SmsVdp::H) * SmsVdp::W + 48;
            for (int x = 0; x < SmsVdp::W; x++) m_fb[y * SmsVdp::W + x] = src[x * 160 / SmsVdp::W];
        }
    }
    if (m_audio.size() > 44100) m_audio.erase(m_audio.begin(), m_audio.end() - 4410);
}

size_t AtgSmsCore::readAudio(int16_t* out, size_t maxSamples)
{
    size_t n = std::min(maxSamples, m_audio.size());
    std::memcpy(out, m_audio.data(), n * sizeof(int16_t));
    m_audio.erase(m_audio.begin(), m_audio.begin() + long(n));
    return n;
}

namespace { constexpr uint32_t MAGIC = 0x31534741; }   // 'AGS1'

bool AtgSmsCore::saveState(std::vector<uint8_t>& out)
{
    out.clear();
    auto put = [&](const void* p, size_t n) { const uint8_t* b = (const uint8_t*)p; out.insert(out.end(), b, b + n); };
    uint32_t magic = MAGIC;
    put(&magic, 4); put(&m_s, sizeof(m_s)); put(&m_vdp.s, sizeof(m_vdp.s)); put(&m_psg.s, sizeof(m_psg.s)); put(&m_z80, sizeof(m_z80));
    return true;
}

bool AtgSmsCore::loadState(const std::vector<uint8_t>& in)
{
    if (in.size() != 4 + sizeof(m_s) + sizeof(m_vdp.s) + sizeof(m_psg.s) + sizeof(m_z80)) return false;
    uint32_t magic; std::memcpy(&magic, in.data(), 4);
    if (magic != MAGIC) return false;
    const uint8_t* p = in.data() + 4;
    std::memcpy(&m_s, p, sizeof(m_s)); p += sizeof(m_s);
    std::memcpy(&m_vdp.s, p, sizeof(m_vdp.s)); p += sizeof(m_vdp.s);
    std::memcpy(&m_psg.s, p, sizeof(m_psg.s)); p += sizeof(m_psg.s);
    auto keep = m_z80;
    std::memcpy(&m_z80, p, sizeof(m_z80));
    m_z80.read_byte = keep.read_byte; m_z80.write_byte = keep.write_byte;
    m_z80.port_in = keep.port_in; m_z80.port_out = keep.port_out; m_z80.userdata = keep.userdata;
    return true;
}
