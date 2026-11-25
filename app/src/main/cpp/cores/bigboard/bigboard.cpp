// PocketPlug - Big Board family CP/M computers (port of MAME's xerox/xerox820.cpp)
// SPDX-License-Identifier: BSD-3-Clause

#include "bigboard.h"
#include <cstring>
#include <fstream>
#include <iterator>

namespace {
const int CPU_HZ = 2500000;   // 20 MHz / 8
const RomFile* find(const std::vector<RomFile>& roms, const char* name)
{
    for (auto& r : roms) if (r.name == name) return &r;
    return nullptr;
}
uint8_t rd(void* u, uint16_t a) { return static_cast<BigBoardCore*>(u)->memRead(a); }
void wr(void* u, uint16_t a, uint8_t d) { static_cast<BigBoardCore*>(u)->memWrite(a, d); }
uint8_t pin(z80* z, uint8_t p) { return static_cast<BigBoardCore*>(z->userdata)->ioRead(uint16_t(z->port_hi << 8 | p)); }
void pout(z80* z, uint8_t p, uint8_t d) { static_cast<BigBoardCore*>(z->userdata)->ioWrite(uint16_t(z->port_hi << 8 | p), d); }
bool readFile(const std::string& path, std::vector<uint8_t>& out)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}
}

BigBoardCore::BigBoardCore(const std::string& id) : m_id(id)
{
    m_x820 = id == "x820";
    m_eight = !(id == "x820" || id == "bigboard5");
    m_out.assign(560 * 240, 0xff000000u);
    m_chain[0] = &m_sio; m_chain[1] = &m_kbpio; m_chain[2] = &m_gppio; m_chain[3] = &m_ctc;
    m_kbpio.readPort = [this](int port) -> uint8_t { return port == 0 ? paRead() : uint8_t(m_kbLatch ^ 0xff); };
    m_kbpio.writePort = [this](int port, uint8_t v) { if (port == 0) paWrite(v); };
    m_ctc.onZero = [this](int ch) { if (ch == 0) m_ctc.trigger(1); else if (ch == 2) m_ctc.trigger(3); };
    m_fdc.sideFromCommand = false;
    m_fdc.onIntrq = [this](bool v) { m_fdcIrq = v; };
    m_fdc.onDrq = [this](bool v) { m_fdcDrq = v; };
}

bool BigBoardCore::load(const std::vector<RomFile>& roms, std::string& error)
{
    m_rom.assign(0x1000, 0xff);
    m_chr.assign(0x800, 0xff);
    auto put = [&](const char* name, std::vector<uint8_t>& dst, size_t at) -> bool {
        const RomFile* r = find(roms, name);
        if (!r) { error = std::string("Missing ") + name; return false; }
        std::memcpy(dst.data() + at, r->data.data(), std::min(r->data.size(), dst.size() - at));
        return true;
    };
    if (m_id == "mojmikro") {
        if (!put("mikro-s.u67", m_rom, 0) || !put("mmschar-yu.u73", m_chr, 0)) return false;
    } else if (m_id == "x820") {
        // the monitor: MICROCode SmartROM / Plus2 if present, else Xerox v2.0
        const char* lo = "x820v20.u64"; const char* hi = "x820v20.u63";
        if (find(roms, "mxkx25a.u64") && find(roms, "mxkx25b.u63") && !find(roms, "x820v20.u64")) { lo = "mxkx25a.u64"; hi = "mxkx25b.u63"; }
        if (!put(lo, m_rom, 0) || !put(hi, m_rom, 0x800) || !put("x820.u92", m_chr, 0)) return false;
    } else {
        if (!put("bigboard.u67", m_rom, 0) || !put("bigboard.u73", m_chr, 0)) return false;
    }
    reset();
    return true;
}

void BigBoardCore::reset()
{
    std::memset(m_ram, 0, sizeof(m_ram));
    std::memset(m_vram, 0, sizeof(m_vram));
    z80_init(&m_z80);
    m_z80.read_byte = rd; m_z80.write_byte = wr; m_z80.port_in = pin; m_z80.port_out = pout; m_z80.userdata = this;
    m_kbpio.reset(); m_gppio.reset(); m_ctc.reset(); m_sio.reset();
    m_fdc.reset();
    m_romView = true; m_scroll = 0; m_ncset2 = 0; m_pa = 0; m_bit5 = false; m_beepClocks = 0;
    m_nmiLine = false; m_fdcIrq = m_fdcDrq = false; m_drive = -1;
    m_keys.clear(); m_kbLatch = 0; m_keyGap = 0;
    selectDrive();
}

uint8_t BigBoardCore::memRead(uint16_t a)
{
    if (a < 0x4000 && m_romView) {
        if (a < 0x1000) return m_rom[a];
        if (a >= 0x3000) return m_vram[a & 0xfff];
        return 0xff;
    }
    return m_ram[a];
}

void BigBoardCore::memWrite(uint16_t a, uint8_t d)
{
    if (a < 0x4000 && m_romView) {
        if (a >= 0x3000) m_vram[a & 0xfff] = d;
        return;
    }
    m_ram[a] = d;
}

uint8_t BigBoardCore::paRead()
{
    // PA3 keyboard data waiting (PBRDY), PA4 8" drives, PA5 single-sided
    uint8_t v = 0;
    if (m_kbpio.rdy(1)) v |= 0x08;
    if (m_eight) v |= 0x10;
    v |= 0x20;
    return v;
}

void BigBoardCore::paWrite(uint8_t d)
{
    // PA0-1 drive select, PA2 side, PA5 beeper pulse (Big Board), PA6 character set, PA7 ROM / RAM bank
    m_pa = d;
    selectDrive();
    m_ncset2 = (d & 0x40) ? 0 : 1;
    m_romView = d & 0x80;
    if (!m_x820) {
        bool b5 = d & 0x20;
        if (b5 && !m_bit5) m_beepClocks = CPU_HZ / 25;   // 40 ms
        m_bit5 = b5;
    }
}

void BigBoardCore::selectDrive()
{
    int drv = -1;
    if (m_x820) { if (m_pa & 1) drv = 0; else if (m_pa & 2) drv = 1; }
    else drv = (m_pa & 3) < 2 ? (m_pa & 3) : -1;
    m_drive = drv;
    m_fdc.setDisk(drv >= 0 ? m_disk[drv].get() : nullptr);
    // the drive size follows the disk in it (77 tracks: 8", else 5.25"); PA4 tells the monitor
    if (drv >= 0 && m_disk[drv]) m_eight = m_disk[drv]->cylinders > 50;
    m_fdc.setSide((m_pa >> 2) & 1);
}

uint8_t BigBoardCore::ioRead(uint16_t port)
{
    uint8_t p = port & 0xff;
    switch (p & 0xfc) {
    case 0x04: return (p & 2) ? m_sio.readCtl(p & 1) : m_sio.readData(p & 1);     // cd_ba: 4 A data, 5 B data, 6 A ctl, 7 B ctl
    case 0x08: return m_gppio.read((p >> 1) & 1, p & 1);           // alt order: 8 A data, 9 A ctl, A B data, B B ctl
    case 0x10: m_busyFrames = 6; return m_fdc.read(p & 3);   // (MAME inverts twice: the FD1771 bus and the board)
    case 0x18: return m_ctc.read(p & 3);
    case 0x1c: return m_kbpio.read((p >> 1) & 1, p & 1);
    }
    return 0xff;
}

void BigBoardCore::ioWrite(uint16_t port, uint8_t d)
{
    uint8_t p = port & 0xff;
    switch (p & 0xfc) {
    case 0x04: if (p & 2) m_sio.writeCtl(p & 1, d); else m_sio.writeData(p & 1, d); break;
    case 0x08: m_gppio.write((p >> 1) & 1, p & 1, d); break;
    case 0x10: m_busyFrames = 6; m_fdc.write(p & 3, d); break;
    case 0x14: m_scroll = (port >> 8) & 0x1f; break;
    case 0x18: m_ctc.write(p & 3, d); break;
    case 0x1c: m_kbpio.write((p >> 1) & 1, p & 1, d); break;
    }
}

void BigBoardCore::keyEvent(int code, bool down)
{
    if (down && code > 0 && code < 0x80 && m_keys.size() < 64) m_keys.push_back(uint8_t(code));
}

void BigBoardCore::feedKey()
{
    // one character at a time once the monitor has read the last (PIO B ready again)
    if (m_keyGap > 0) { m_keyGap--; return; }
    if (m_keys.empty() || !m_kbpio.rdy(1)) return;
    m_kbLatch = m_keys.front(); m_keys.pop_front();
    m_kbpio.strobe(1, uint8_t(m_kbLatch ^ 0xff));
    m_keyGap = 2;
}

void BigBoardCore::step()
{
    // RETI ends the in-service interrupt of the chip nearest the top of the chain
    if (memRead(m_z80.pc) == 0xed && memRead(uint16_t(m_z80.pc + 1)) == 0x4d)
        for (auto* d : m_chain) if (d->reti()) break;
    // the daisy chain: the first chip with an interrupt, unless one above it is in service
    m_ackFrom = -1;
    m_z80.int_pending = 0;
    for (int i = 0; i < 4; i++) {
        if (m_chain[i]->inService()) break;
        int v = m_chain[i]->pending();
        if (v >= 0) { m_z80.int_pending = 1; m_z80.int_data = uint8_t(v); m_ackFrom = i; break; }
    }
    bool wanted = m_z80.int_pending;
    unsigned long before = m_z80.cyc;
    z80_step(&m_z80);
    int cyc = int(m_z80.cyc - before); if (cyc <= 0) cyc = 4;
    if (wanted && !m_z80.int_pending && m_ackFrom >= 0) m_chain[m_ackFrom]->ack();
    m_ctc.clock(cyc);
    // the FDC core counts 32 us a byte; single density is 32 us a byte on 8" and 64 on 5.25"
    // (2.5 clocks a microsecond; 5.25" runs its clock at half speed)
    m_fdcCarry += cyc * 2;
    int per = m_eight ? 5 : 10;
    if (m_fdcCarry >= per) { int us = m_fdcCarry / per; m_fdcCarry -= us * per; m_fdc.advance(us); }
    // /NMI = /HALT and (DRQ or INTRQ)
    bool nmi = m_z80.halted && (m_fdcIrq || m_fdcDrq);
    if (nmi && !m_nmiLine) z80_gen_nmi(&m_z80);
    m_nmiLine = nmi;
    // the beeper (950 Hz square) into 44.1 kHz
    m_sampleAcc += cyc * 44100.0;
    while (m_sampleAcc >= CPU_HZ) {
        m_sampleAcc -= CPU_HZ;
        int16_t s = 0;
        if (m_beepClocks > 0) { m_beepPhase += 950.0 / 44100.0; if (m_beepPhase >= 1) m_beepPhase -= 1; s = m_beepPhase < 0.5 ? 6000 : -6000; }
        m_audio.push_back(s);
    }
    if (m_beepClocks > 0) m_beepClocks -= cyc;
}

void BigBoardCore::runFrame()
{
    int budget = CPU_HZ / 60;
    feedKey();
    while (budget > 0) {
        unsigned long c0 = m_z80.cyc;
        step();
        budget -= int(m_z80.cyc - c0) > 0 ? int(m_z80.cyc - c0) : 4;
    }
    if (m_busyFrames > 0) m_busyFrames--;
    m_frame++;
    render();
}

void BigBoardCore::render()
{
    int ma = (m_scroll + 1) * 0x80;
    for (int row = 0; row < 24; row++) {
        if (ma > 0xb80) ma = 0;
        for (int ra = 0; ra < 10; ra++) {
            uint32_t* line = &m_out[size_t(row * 10 + ra) * 560];
            for (int x = 0; x < 80; x++) {
                uint8_t gfx = 0xff;
                if (ra < 8) {
                    uint8_t chr = uint8_t(m_vram[(ma + x) & 0xfff] ^ 0x80);
                    if (chr < 0x80 && (m_frame & 0x08)) chr |= 0x80;     // flashing characters
                    gfx = m_chr[((m_ncset2 << 10) | (chr << 3) | ra) & 0x7ff];
                }
                uint32_t* px = line + x * 7;
                px[0] = 0xff000000u;
                for (int b = 0; b < 5; b++) px[1 + b] = ((gfx >> (4 - b)) & 1) ? 0xff000000u : 0xffe8f0e8u;
                px[6] = 0xff000000u;
            }
        }
        ma += 128;
    }
}

size_t BigBoardCore::readAudio(int16_t* out, size_t maxSamples)
{
    size_t n = std::min(maxSamples, m_audio.size());
    std::memcpy(out, m_audio.data(), n * sizeof(int16_t));
    m_audio.erase(m_audio.begin(), m_audio.begin() + long(n));
    return n;
}

bool BigBoardCore::mediaInsert(int drive, const std::string& path, std::string& error)
{
    if (drive < 0 || drive > 1) { error = "No such drive"; return false; }
    std::vector<uint8_t> data;
    if (!readFile(path, data)) { error = "Can't read " + path; return false; }
    std::unique_ptr<DiskImage> d(new DiskImage());
    bool ok = d->loadImd(data);
    if (!ok) {
        // raw single-density images: 8" 77 x 26 x 128 (256,256 bytes) or 5.25" 40 x 18 x 128
        if (data.size() == 77 * 26 * 128) ok = d->loadRaw(data, 77, 1, 26, 128);
        else if (data.size() == 40 * 18 * 128) ok = d->loadRaw(data, 40, 1, 18, 128);
        else if (data.size() == 77 * 26 * 128 * 2) ok = d->loadRaw(data, 77, 2, 26, 128);
    }
    if (!ok) { error = "Unrecognised disk image (use .imd or a raw single-density image)"; return false; }
    d->path = path;
    if (drive == 0) m_eight = d->cylinders > 50;
    m_disk[drive] = std::move(d);
    selectDrive();
    return true;
}

bool BigBoardCore::mediaSave(int drive, const std::string& path)
{
    if (drive < 0 || drive > 1 || !m_disk[drive] || !m_disk[drive]->dirty) return false;
    std::ofstream f(path, std::ios::binary);
    if (!f) return false;
    auto img = m_disk[drive]->saveImd();
    f.write(reinterpret_cast<const char*>(img.data()), std::streamsize(img.size()));
    m_disk[drive]->dirty = false;
    return true;
}

void BigBoardCore::mediaEject(int drive)
{
    if (drive < 0 || drive > 1) return;
    m_disk[drive].reset();
    selectDrive();
}
