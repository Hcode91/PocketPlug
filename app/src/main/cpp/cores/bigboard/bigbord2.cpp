// PocketPlug - Big Board II (port of MAME's xerox/bigbord2.cpp)
// SPDX-License-Identifier: BSD-3-Clause

#include "bigbord2.h"
#include <cstring>
#include <fstream>
#include <iterator>
#include <algorithm>

namespace {
const int CPU_HZ = 4000000;
const RomFile* find(const std::vector<RomFile>& roms, const char* name)
{
    for (auto& r : roms) if (r.name == name) return &r;
    return nullptr;
}
uint8_t rd(void* u, uint16_t a) { return static_cast<BigBoard2Core*>(u)->memRead(a); }
void wr(void* u, uint16_t a, uint8_t d) { static_cast<BigBoard2Core*>(u)->memWrite(a, d); }
uint8_t pin(z80* z, uint8_t p) { return static_cast<BigBoard2Core*>(z->userdata)->ioRead(uint16_t(z->port_hi << 8 | p)); }
void pout(z80* z, uint8_t p, uint8_t d) { static_cast<BigBoard2Core*>(z->userdata)->ioWrite(uint16_t(z->port_hi << 8 | p), d); }
}

BigBoard2Core::BigBoard2Core()
{
    m_out.assign(640 * 288, 0xff000000u);
    m_chain[0] = &m_ctc1; m_chain[1] = &m_ctc2; m_chain[2] = &m_sio;   // (the DMA is first on the real chain)
    m_ctc2.onZero = [this](int ch) { if (ch == 2) m_ctc2.trigger(3); };
    m_fdc.sideFromCommand = false;
    m_fdc.onIntrq = [this](bool v) { m_fdcIrq = v; };
    m_fdc.onDrq = [this](bool v) { m_fdcDrq = v; };
}

bool BigBoard2Core::load(const std::vector<RomFile>& roms, std::string& error)
{
    m_rom.assign(0x6000, 0xff);
    m_chr.assign(0x800, 0);
    const RomFile* r = find(roms, "bigbrdii.u85");
    const RomFile* c = find(roms, "8002.u52");
    if (!r) { error = "Missing bigbrdii.u85"; return false; }
    if (!c) { error = "Missing 8002.u52"; return false; }
    std::memcpy(m_rom.data(), r->data.data(), std::min<size_t>(r->data.size(), 0x1000));
    std::memcpy(m_chr.data(), c->data.data(), std::min<size_t>(c->data.size(), 0x800));
    reset();
    return true;
}

void BigBoard2Core::reset()
{
    std::memset(m_ram, 0, sizeof(m_ram));
    std::memset(m_vram, 0, sizeof(m_vram));
    std::memset(m_aram, 0, sizeof(m_aram));
    std::memset(m_crtc, 0, sizeof(m_crtc));
    z80_init(&m_z80);
    m_z80.read_byte = rd; m_z80.write_byte = wr; m_z80.port_in = pin; m_z80.port_out = pout; m_z80.userdata = this;
    m_ctc1.reset(); m_ctc2.reset(); m_sio.reset(); m_fdc.reset();
    m_latch1 = m_latch2 = 0;
    m_termData = m_termStatus = 0;
    m_nmiLine = false; m_fdcIrq = m_fdcDrq = false;
    m_keys.clear(); m_keyGap = 0;
    selectDrive();
}

// D_S (latch 1 Q0) = 0: 0000-5FFF ROM (writes lost), 6000-6FFF characters, 7000-7FFF attributes; 1: all RAM
uint8_t BigBoard2Core::memRead(uint16_t a)
{
    if (a < 0x8000 && !(m_latch1 & 1)) {
        if (a < 0x6000) return m_rom[a];
        if (a < 0x7000) return m_vram[a & 0x7ff];
        return m_aram[a & 0x7ff];
    }
    return m_ram[a];
}

void BigBoard2Core::memWrite(uint16_t a, uint8_t d)
{
    if (a < 0x8000 && !(m_latch1 & 1)) {
        if (a < 0x6000) return;
        if (a < 0x7000) m_vram[a & 0x7ff] = d; else m_aram[a & 0x7ff] = d;
        return;
    }
    m_ram[a] = d;
}

void BigBoard2Core::selectDrive()
{
    // latch 2: DS0 (bit 7) drive 0, DS1 (bit 6) drive 1; latch 1 Q1: side
    int drv = (m_latch2 & 0x80) ? 0 : (m_latch2 & 0x40) ? 1 : -1;
    m_fdc.setDisk(drv >= 0 ? m_disk[drv].get() : nullptr);
    m_fdc.setSide((m_latch1 >> 1) & 1);
}

uint8_t BigBoard2Core::ioRead(uint16_t port)
{
    uint8_t p = port & 0xff;
    switch (p & 0xfc) {
    case 0x80: return (p & 1) ? m_sio.readCtl((p >> 1) & 1) : m_sio.readData((p >> 1) & 1);   // ba_cd: 80 A data, 81 A ctl, 82 B data, 83 B ctl
    case 0x84: return m_ctc1.read(p & 3);
    case 0x88: return m_ctc2.read(p & 3);
    case 0xc4: {
        // RXDA, RXDB idle, MOTOR (latch 1 Q6), KBDSTB, DIP switches 1-4 (4 off, the others on)
        uint8_t v = uint8_t(m_termStatus | 3 | (((m_latch1 >> 6) & 1) << 2) | 0x10);
        m_termStatus = 0;
        return v;
    }
    case 0xd0: { uint8_t v = m_termData; m_termData = 0; return v; }
    case 0xd4: m_busyFrames = 6; return m_fdc.read(p & 3);
    case 0xd8: return (p & 3) == 1 ? 0x00 : 0xff;      // SASI: nothing there
    case 0xdc: return (p & 1) ? (m_crtcIdx >= 12 && m_crtcIdx <= 17 ? m_crtc[m_crtcIdx] : 0) : 0;
    }
    return 0xff;
}

void BigBoard2Core::ioWrite(uint16_t port, uint8_t d)
{
    uint8_t p = port & 0xff;
    switch (p & 0xfc) {
    case 0x80: if (p & 1) m_sio.writeCtl((p >> 1) & 1, d); else m_sio.writeData((p >> 1) & 1, d); break;
    case 0x84: m_ctc1.write(p & 3, d); break;
    case 0x88: m_ctc2.write(p & 3, d); break;
    case 0xc8: {   // 74LS259: data bits 0-2 pick the output, bit 3 is its level
        int q = d & 7;
        m_latch1 = uint8_t((m_latch1 & ~(1 << q)) | (((d >> 3) & 1) << q));
        if (q == 1) selectDrive();
        break;
    }
    case 0xcc: m_latch2 = d; selectDrive(); break;
    case 0xd4: m_busyFrames = 6; m_fdc.write(p & 3, d); break;
    case 0xdc: if (p & 1) m_crtc[m_crtcIdx & 31] = d; else m_crtcIdx = d & 31; break;
    }
}

void BigBoard2Core::keyEvent(int code, bool down)
{
    if (down && code > 0 && code < 0x80 && m_keys.size() < 64) m_keys.push_back(uint8_t(code));
}

void BigBoard2Core::step()
{
    if (memRead(m_z80.pc) == 0xed && memRead(uint16_t(m_z80.pc + 1)) == 0x4d)
        for (int i = 0; i < 3; i++) if (m_chain[i]->reti()) break;
    m_ackFrom = -1;
    m_z80.int_pending = 0;
    for (int i = 0; i < 3; i++) {
        if (m_chain[i]->inService()) break;
        int v = m_chain[i]->pending();
        if (v >= 0) { m_z80.int_pending = 1; m_z80.int_data = uint8_t(v); m_ackFrom = i; break; }
    }
    bool wanted = m_z80.int_pending;
    unsigned long before = m_z80.cyc;
    z80_step(&m_z80);
    int cyc = int(m_z80.cyc - before); if (cyc <= 0) cyc = 4;
    if (wanted && !m_z80.int_pending && m_ackFrom >= 0) m_chain[m_ackFrom]->ack();
    m_ctc1.clock(cyc); m_ctc2.clock(cyc);
    m_fdcCarry += cyc;
    // 8" at 2 MHz: double density (DDEN, latch 1 Q4, low) 16 us a byte, single 32 (the FDC core counts 32)
    if (m_fdcCarry >= 4) { int us = m_fdcCarry / 4; m_fdcCarry -= us * 4; m_fdc.advance((m_latch1 & 0x10) ? us : us * 2); }
    bool nmi = m_z80.halted && (m_fdcIrq || m_fdcDrq);
    if (nmi && !m_nmiLine) z80_gen_nmi(&m_z80);
    m_nmiLine = nmi;
    m_sampleAcc += cyc * 44100.0;
    while (m_sampleAcc >= CPU_HZ) {
        m_sampleAcc -= CPU_HZ;
        int16_t s = 0;
        if (m_latch1 & 0x80) { m_beepPhase += 950.0 / 44100.0; if (m_beepPhase >= 1) m_beepPhase -= 1; s = m_beepPhase < 0.5 ? 5000 : -5000; }
        m_audio.push_back(s);
    }
}

void BigBoard2Core::runFrame()
{
    // the keyboard: one character per read; its strobe pulses CTC 1 channel 0
    if (m_keyGap > 0) m_keyGap--;
    else if (!m_keys.empty()) {     // (as MAME: a new key replaces one nobody read)
        m_termData = m_keys.front(); m_keys.pop_front();
        m_termStatus = 8;
        m_ctc1.trigger(0);
        m_keyGap = 3;
    }
    int budget = CPU_HZ / 60;
    while (budget > 0) {
        unsigned long c0 = m_z80.cyc;
        step();
        int used = int(m_z80.cyc - c0);
        budget -= used > 0 ? used : 4;
    }
    m_ctc1.trigger(3);     // the 6845's VSYNC
    if (m_busyFrames > 0) m_busyFrames--;
    m_cnt++;
    render();
}

uint8_t BigBoard2Core::crt8002(int ra, uint8_t chr, uint8_t attr, bool cursor)
{
    uint8_t gfx = 0;
    switch (attr & 3) {
    case 0: {   // block graphics
        int b = ra < 3 ? 0 : ra < 6 ? 1 : ra < 9 ? 2 : 3;
        gfx = uint8_t((((chr >> (7 - b)) & 1) ? 0xf8 : 0) | (((chr >> (3 - b)) & 1) ? 7 : 0));
        break;
    }
    case 1: { uint8_t v = 0; for (int i = 0; i < 8; i++) if (chr & (1 << i)) v |= uint8_t(0x80 >> i); gfx = v; break; }
    case 2: break;
    case 3: gfx = m_chr[((chr & 0x7f) << 4 | ra) & 0x7ff]; break;
    }
    if ((attr & 0x08) && ra == 11) gfx = 0xff;
    if ((attr & 0x04) && (ra == 5 || ra == 6)) gfx = 0xff;
    if ((attr & 0x40) && (m_cnt & 0x20)) gfx = 0;
    if (attr & 0x20) gfx = 0;
    if (cursor && (m_cnt & 0x10)) gfx ^= 0xff;
    if (attr & 0x10) gfx ^= 0xff;
    return gfx;
}

void BigBoard2Core::render()
{
    std::fill(m_out.begin(), m_out.end(), 0xff000000u);
    int cols = std::min<int>(m_crtc[1], 80), rows = std::min<int>(m_crtc[6], 24);
    int rasters = std::min<int>((m_crtc[9] & 31) + 1, 12);
    if (cols <= 0 || rows <= 0) return;
    int start = (m_crtc[12] << 8 | m_crtc[13]) & 0x3fff;
    int cursor = (m_crtc[14] << 8 | m_crtc[15]) & 0x3fff;
    bool curOn = (m_crtc[10] & 0x60) != 0x20;
    int ox = (80 - cols) * 4, oy = (24 - rows) * 6;
    for (int r = 0; r < rows; r++) {
        int ma = start + r * cols;
        for (int ra = 0; ra < rasters; ra++) {
            int y = oy + r * 12 + ra;
            if (y < 0 || y >= 288) continue;
            uint32_t* line = &m_out[size_t(y) * 640];
            for (int x = 0; x < cols; x++) {
                int mem = (ma + x) & 0x7ff;
                bool cur = curOn && ((ma + x) & 0x3fff) == cursor && ra >= (m_crtc[10] & 31) && ra <= (m_crtc[11] & 31);
                uint8_t g = crt8002(ra, m_vram[mem], m_aram[mem], cur);
                uint32_t* px = line + ox + x * 8;
                for (int b = 0; b < 8; b++) px[b] = ((g >> (7 - b)) & 1) ? 0xffe8f0e8u : 0xff000000u;
            }
        }
    }
}

size_t BigBoard2Core::readAudio(int16_t* out, size_t maxSamples)
{
    size_t n = std::min(maxSamples, m_audio.size());
    std::memcpy(out, m_audio.data(), n * sizeof(int16_t));
    m_audio.erase(m_audio.begin(), m_audio.begin() + long(n));
    return n;
}

bool BigBoard2Core::mediaInsert(int drive, const std::string& path, std::string& error)
{
    if (drive < 0 || drive > 1) { error = "No such drive"; return false; }
    std::ifstream f(path, std::ios::binary);
    if (!f) { error = "Can't read " + path; return false; }
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    std::unique_ptr<DiskImage> d(new DiskImage());
    bool ok = d->loadImd(data);
    if (!ok && data.size() == 77 * 26 * 128) ok = d->loadRaw(data, 77, 1, 26, 128);
    if (!ok) { error = "Unrecognised disk image (use .imd)"; return false; }
    d->path = path;
    m_disk[drive] = std::move(d);
    selectDrive();
    return true;
}

bool BigBoard2Core::mediaSave(int drive, const std::string& path)
{
    if (drive < 0 || drive > 1 || !m_disk[drive] || !m_disk[drive]->dirty) return false;
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    auto img = m_disk[drive]->saveImd();
    f.write(reinterpret_cast<const char*>(img.data()), std::streamsize(img.size()));
    m_disk[drive]->dirty = false;
    return true;
}

void BigBoard2Core::mediaEject(int drive)
{
    if (drive < 0 || drive > 1) return;
    m_disk[drive].reset();
    selectDrive();
}
