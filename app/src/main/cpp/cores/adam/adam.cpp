// PocketPlug - Coleco ADAM
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Port of MAME's adam.cpp and bus/adamnet. See adam.h.

#include "adam.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>

#ifdef M6801_TRACE
extern double g_m6801Now;
#endif
namespace {
constexpr double Z80_HZ = 7159090.0 / 2;
constexpr double US_PER_Z80 = 1e6 / Z80_HZ;
constexpr double US_PER_E = 1.0;              // 6801s: 4 MHz crystal, E = 1 MHz
constexpr int LINES = 262;
constexpr int Z80_PER_LINE = 228;
constexpr double LINE_US = Z80_PER_LINE * US_PER_Z80;
constexpr int AUDIO_HZ = 44100;
enum { LO_SMARTWRITER = 0, LO_INTERNAL_RAM, LO_RAM_EXPANSION, LO_OS7_ROM_INTERNAL_RAM };
enum { HI_INTERNAL_RAM = 0, HI_ROM_EXPANSION, HI_RAM_EXPANSION, HI_CARTRIDGE_ROM };

uint8_t z80Read(void* u, uint16_t a) { return static_cast<AdamCore*>(u)->memRead(a); }
void z80Write(void* u, uint16_t a, uint8_t d) { static_cast<AdamCore*>(u)->memWrite(a, d); }
uint8_t z80In(z80* z, uint8_t p) { return static_cast<AdamCore*>(z->userdata)->ioRead(p); }
void z80Out(z80* z, uint8_t p, uint8_t d) { static_cast<AdamCore*>(z->userdata)->ioWrite(p, d); }
}

AdamCore::AdamCore()
    : m_master(&m_masterPorts), m_kb(&m_kbPorts), m_prn(&m_prnPorts)
{
    m_masterPorts.c = this; m_kbPorts.c = this; m_prnPorts.c = this;
    std::memset(m_os7, 0xff, sizeof(m_os7));
    std::memset(m_boot, 0xff, sizeof(m_boot));
    std::memset(&m_s, 0, sizeof(m_s));
    m_frame.assign(W * H, 0xff000000);
    m_master.setRom(m_romMaster); m_kb.setRom(m_romKb); m_prn.setRom(m_romPrn);
}

bool AdamCore::load(const std::vector<RomFile>& roms, std::string& error)
{
    auto find = [&](const char* name, size_t size) -> const RomFile* {
        for (auto& r : roms) if (r.name == name && r.data.size() == size) return &r;
        return nullptr;
    };
    struct { const char* name; size_t size; } need[] = {
        { "os7.u2", 0x2000 }, { "eos 6 rev 57 08dd.u22", 0x4000 }, { "wp_r80.rom", 0x8000 },
        { "master rev a 174b.u6", 0x800 }, { "keyboard.u2", 0x800 }, { "printer.u2", 0x800 } };
    std::string missing;
    for (auto& n : need) if (!find(n.name, n.size)) missing += std::string(missing.empty() ? "" : ", ") + n.name;
    if (!missing.empty()) { error = "Missing " + missing; return false; }
    std::memcpy(m_os7, find("os7.u2", 0x2000)->data.data(), 0x2000);
    // as MAME's ROM_LOAD order: the alf ROMs, then EOS (first half at 8000,
    // second at 6000), then the SmartWriter dump over 0000-7fff
    for (int i = 0; i < 3; i++) {
        static const char* alf[3] = { "alf @1 rev 57 e3d5.u8", "alf @2 rev 57 ae6a.u20", "alf @3 rev 57 8534.u21" };
        if (auto r = find(alf[i], 0x2000)) std::memcpy(m_boot + i * 0x2000, r->data.data(), 0x2000);
    }
    const RomFile* eos = find("eos 6 rev 57 08dd.u22", 0x4000);
    std::memcpy(m_boot + 0x8000, eos->data.data(), 0x2000);
    std::memcpy(m_boot + 0x6000, eos->data.data() + 0x2000, 0x2000);
    std::memcpy(m_boot, find("wp_r80.rom", 0x8000)->data.data(), 0x8000);
    std::memcpy(m_romMaster, find("master rev a 174b.u6", 0x800)->data.data(), 0x800);
    std::memcpy(m_romKb, find("keyboard.u2", 0x800)->data.data(), 0x800);
    std::memcpy(m_romPrn, find("printer.u2", 0x800)->data.data(), 0x800);
    reset();
    return true;
}

void AdamCore::reset() { powerOn(!m_cart.empty()); }

bool AdamCore::mediaInsert(int drive, const std::string& path, std::string& error)
{
    if (drive != 2) { error = "No such drive"; return false; }
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) { error = "Can't open the cartridge"; return false; }
    std::vector<uint8_t> d(0x8000, 0xff);
    size_t n = fread(d.data(), 1, d.size(), f);
    bool more = fgetc(f) != EOF;
    fclose(f);
    if (n == 0) { error = "The cartridge file is empty"; return false; }
    if (more) { error = "Cartridges larger than 32 KB (megacarts) aren't supported yet"; return false; }
    // smaller dumps repeat through the 32 KB space
    for (size_t i = n; i < d.size(); i++) d[i] = d[i % n];
    m_cart = std::move(d);
    powerOn(true);
    return true;
}

void AdamCore::mediaEject(int drive)
{
    if (drive != 2 || m_cart.empty()) return;
    m_cart.clear();
    powerOn(false);
}

void AdamCore::powerOn(bool game)
{
    uint8_t keys[13];
    std::memcpy(keys, m_s.keys, sizeof(keys));
    std::memset(&m_s, 0, sizeof(m_s));
    std::memcpy(m_s.keys, keys, sizeof(keys));
    m_s.txMaster = m_s.txKb = m_s.txPrn = 1;
    m_s.keyY = 0x1fff;
    // computer mode, or game mode (OS7 + cartridge) as the cartridge reset switch
    m_s.game = game ? 1 : 0;
    m_s.mioc = game ? uint8_t((HI_CARTRIDGE_ROM << 2) | LO_OS7_ROM_INTERNAL_RAM) : 0;
    m_s.an = 0;
    m_s.dma = 1; m_s.bwr = 1;
    std::memset(&m_z80, 0, sizeof(m_z80));
    z80_init(&m_z80);
    m_z80.read_byte = z80Read; m_z80.write_byte = z80Write;
    m_z80.port_in = z80In; m_z80.port_out = z80Out;
    m_z80.userdata = this;
    std::memset(m_vdp.vram, 0, sizeof(m_vdp.vram));
    m_vdp.reset();
    m_psg.reset();
    m_master.reset(); m_kb.reset(); m_prn.reset();
}

void AdamCore::setTx(uint8_t& tx, int v)
{
    int before = netLine();
    tx = uint8_t(v);
    int after = netLine();
    if (after != before) {
        m_master.lineChanged(m_now, after);
        m_kb.lineChanged(m_now, after);
        m_prn.lineChanged(m_now, after);
    }
}

void AdamCore::netReset()
{
    // the devices restart; their clocks keep counting
    int64_t tk = m_kb.cycles(), tp = m_prn.cycles();
    m_kb.reset(); m_prn.reset();
    m_kb.state()->cycles = tk; m_prn.state()->cycles = tp;
    m_s.txKb = m_s.txPrn = 1;
}

void AdamCore::keyEvent(int code, bool down)
{
    if (code < 0 || code >= 13 * 8) return;
    if (down) m_s.keys[code >> 3] |= uint8_t(1 << (code & 7));
    else m_s.keys[code >> 3] &= uint8_t(~(1 << (code & 7)));
}

// ---------------- Z80 bus ----------------
uint8_t AdamCore::memRead(uint16_t a)
{
    const uint8_t lo = m_s.mioc & 3, hi = (m_s.mioc >> 2) & 3;
    if (a < 0x8000) {
        switch (lo) {
        case LO_SMARTWRITER: {
            int eos = (m_s.an >> 1) & 1;
            return a < 0x6000 ? m_boot[a] : m_boot[(eos << 13) + a];
        }
        case LO_INTERNAL_RAM: return m_s.ram[a];
        case LO_RAM_EXPANSION: return m_s.xram[a];
        default: return a < 0x2000 ? m_os7[a] : m_s.ram[a];
        }
    }
    switch (hi) {
    case HI_INTERNAL_RAM: return m_s.ram[a];
    case HI_RAM_EXPANSION:
        if (!m_s.game) return m_s.xram[a];
        [[fallthrough]];
    case HI_CARTRIDGE_ROM: return m_cart.empty() ? 0xff : m_cart[a & 0x7fff];
    default: return 0xff;           // expansion ROM: none
    }
}

void AdamCore::memWrite(uint16_t a, uint8_t d)
{
    const uint8_t lo = m_s.mioc & 3, hi = (m_s.mioc >> 2) & 3;
    if (a < 0x8000) {
        if (lo == LO_INTERNAL_RAM) m_s.ram[a] = d;
        else if (lo == LO_RAM_EXPANSION) m_s.xram[a] = d;
        else if (lo == LO_OS7_ROM_INTERNAL_RAM && a >= 0x2000) m_s.ram[a] = d;
        return;
    }
    if (hi == HI_INTERNAL_RAM) m_s.ram[a] = d;
    else if (hi == HI_RAM_EXPANSION && !m_s.game) m_s.xram[a] = d;
}

uint8_t AdamCore::padRead(int n)
{
    if (n != 0) return 0x7f;
    uint8_t d = 0x7f;
    if (m_s.joyMode) {
        if (m_pad & 0x01) d &= ~0x01;   // up
        if (m_pad & 0x08) d &= ~0x02;   // right
        if (m_pad & 0x02) d &= ~0x04;   // down
        if (m_pad & 0x04) d &= ~0x08;   // left
        if (m_pad & 0x10) d &= ~0x40;   // left fire
    } else {
        // keypad: 0-9, #, * in input bits 8-19 (codes as MAME's hand controller)
        static const uint8_t code[12] = { 0x0a, 0x0d, 0x07, 0x0c, 0x02, 0x03, 0x0e, 0x05, 0x01, 0x0b, 0x06, 0x09 };
        for (int k = 0; k < 12; k++) if (m_pad >> (8 + k) & 1) { d = uint8_t((d & 0xf0) | code[k]); break; }
        if (m_pad & 0x20) d &= ~0x40;   // right fire
    }
    return d;
}

uint8_t AdamCore::ioRead(uint8_t a)
{
    switch ((a >> 5) & 7) {
    case 1: return m_s.an & 0x0f;
    case 3: return m_s.mioc & 0x0f;
    case 5: return (a & 1) ? m_vdp.registerRead() : m_vdp.vramRead();
    case 7: return padRead((a >> 1) & 1);
    default: return 0;
    }
}

void AdamCore::ioWrite(uint8_t a, uint8_t d)
{
    switch ((a >> 5) & 7) {
    case 1:
        if ((m_s.an & 1) && !(d & 1)) netReset();    // network reset
        m_s.an = d;
        break;
    case 3: m_s.mioc = d; break;
    case 4: m_s.joyMode = 0; break;
    case 5: if (a & 1) m_vdp.registerWrite(d); else m_vdp.vramWrite(d); break;
    case 6: m_s.joyMode = 1; break;
    case 7: m_psg.write(d); break;
    default: break;
    }
}

// ---------------- ADAMnet ----------------
M6801::u8 AdamCore::Master::portIn(int port)
{
    if (port == 2) return uint8_t(0x07 | (c->netLine() << 3));
    if (port == 3) return c->m_s.dataOut;
    return 0xff;
}

void AdamCore::Master::portOut(int port, M6801::u8 data, M6801::u8)
{
    auto& s = c->m_s;
    switch (port) {
    case 1: s.ba = uint16_t((data << 8) | (s.ba & 0xff)); break;
    case 2: s.dma = data & 1; s.bwr = (data >> 2) & 1; c->setTx(s.txMaster, (data >> 4) & 1); break;
    case 3: s.dataIn = data; break;
    case 4: s.ba = uint16_t((s.ba & 0xff00) | data); break;
    }
}

void AdamCore::Master::os3(int state)
{
    auto& s = c->m_s;
    if (state && !s.dma) {
        if (!s.bwr) s.ram[s.ba] = s.dataIn;
        else {
            s.dataOut = s.ram[s.ba];
            c->m_master.is3PulseLater();
        }
    }
}

M6801::u8 AdamCore::Keyboard::portIn(int port)
{
    auto& s = c->m_s;
    if (port == 1) {
        uint8_t d = 0xff;
        for (int i = 0; i < 13; i++) if (!(s.keyY >> i & 1)) d &= uint8_t(~s.keys[i]);
        return d;
    }
    if (port == 2) return uint8_t(0x07 | (c->netLine() << 3));
    return 0xff;
}

void AdamCore::Keyboard::portOut(int port, M6801::u8 data, M6801::u8)
{
    auto& s = c->m_s;
    if (port == 2) c->setTx(s.txKb, (data >> 4) & 1);
    else if (port == 3) s.keyY = uint16_t((s.keyY & 0x1f00) | data);
    else if (port == 4) s.keyY = uint16_t(((data & 0x1f) << 8) | (s.keyY & 0xff));
}

M6801::u8 AdamCore::Printer::portIn(int port)
{
    if (port == 2) return uint8_t(0x07 | (c->netLine() << 3));
    if (port == 4) return uint8_t(0x80 | c->m_s.prnSensors);
    return 0xff;
}

void AdamCore::Printer::portOut(int port, M6801::u8 data, M6801::u8)
{
    auto& s = c->m_s;
    if (port == 1) {
        // the motors' steps move the left margin / wheel home sensors
        if ((data ^ s.prnP1) & 0x0f) s.prnSensors ^= 0x10;
        if ((data ^ s.prnP1) & 0xf0) s.prnSensors ^= 0x40;
        s.prnP1 = data;
    } else if (port == 2) c->setTx(s.txPrn, (data >> 4) & 1);
}

// ---------------- frame ----------------
void AdamCore::runFrame()
{
    const double perSample = Z80_HZ / AUDIO_HZ;
    for (int line = 0; line < LINES; line++) {
        const double end = (line + 1) * LINE_US;
        // step whichever processor is furthest behind, so the 6801s'
        // serial line and the master's RAM accesses stay in order
        for (;;) {
            // the next thing to happen: a CPU step or one of the 6801s' serial events
            M6801* cpus[3] = { &m_master, &m_kb, &m_prn };
            double t = m_s.tZ80; int who = -1;
            for (int k = 0; k < 3; k++) {
                double tc = double(cpus[k]->cycles()) * US_PER_E - m_s.base;
                if (tc < t) { t = tc; who = k; }
                double te = double(cpus[k]->nextSciEvent()) * US_PER_E - m_s.base;
                if (te < t) { t = te; who = 3 + k; }
            }
            if (t >= end) break;
#ifdef M6801_TRACE
            g_m6801Now = t + m_traceBase;
#endif
            if (who < 0) {
                unsigned long before = m_z80.cyc;
                z80_step(&m_z80);
                int n = int(m_z80.cyc - before);
                m_s.tZ80 += n * US_PER_Z80;
                for (int i = 0; i < n; i++) {
                    m_audioAcc += m_psg.clock();
                    m_audioN++;
                    m_audioPhase += 1.0;
                    if (m_audioPhase >= perSample) {
                        m_audioPhase -= perSample;
                        double x = m_audioAcc / m_audioN;
                        m_audioAcc = 0; m_audioN = 0;
                        m_dcY = x - m_dcX + 0.995 * m_dcY;
                        m_dcX = x;
                        m_audio.push_back(int16_t(std::max(-32000.0, std::min(32000.0, m_dcY))));
                    }
                }
            } else if (who < 3) {
                m_now = cpus[who]->cycles();
                cpus[who]->run(1);
            } else {
                m_now = cpus[who - 3]->nextSciEvent();
                cpus[who - 3]->sciEvent(m_now);
            }
        }
        // video line; the frame interrupt goes to the Z80's NMI
        int y = line - 8;           // active picture starts after the top border lines we show
        (void)y;
        m_vdp.line(line);
        bool irq = m_vdp.irq();
        if (irq && !m_s.vdpIrq) z80_gen_nmi(&m_z80);
        m_s.vdpIrq = irq;
    }
    // rebase the clocks
    const double frameUs = LINES * LINE_US;
#ifdef M6801_TRACE
    m_traceBase += frameUs;
#endif
    m_s.tZ80 -= frameUs;
    m_s.base += frameUs;        // the 6801s count absolute E cycles

    // picture in its border
    uint32_t back = Tms9928a::palette(m_vdp.regs[7] & 15);
    std::fill(m_frame.begin(), m_frame.end(), back);
    for (int y = 0; y < 192; y++) std::memcpy(&m_frame[(y + 8) * W + 8], &m_vdp.out[y * 256], 256 * 4);
    if (m_audio.size() > AUDIO_HZ / 4) m_audio.erase(m_audio.begin(), m_audio.end() - AUDIO_HZ / 10);
}

size_t AdamCore::readAudio(int16_t* out, size_t maxSamples)
{
    size_t n = std::min(maxSamples, m_audio.size());
    std::memcpy(out, m_audio.data(), n * sizeof(int16_t));
    m_audio.erase(m_audio.begin(), m_audio.begin() + long(n));
    return n;
}

bool AdamCore::saveState(std::vector<uint8_t>& out)
{
    out.clear();
    uint32_t magic = 0x4d414441;   // 'ADAM'
    auto put = [&](const void* p, size_t n) { const uint8_t* b = (const uint8_t*)p; out.insert(out.end(), b, b + n); };
    put(&magic, 4); put(&m_s, sizeof(m_s));
    put(m_master.state(), sizeof(M6801::State)); put(m_kb.state(), sizeof(M6801::State)); put(m_prn.state(), sizeof(M6801::State));
    put(&m_z80, sizeof(m_z80));
    put(&m_vdp, sizeof(m_vdp));
    put(&m_psg.s, sizeof(m_psg.s));
    return true;
}

bool AdamCore::loadState(const std::vector<uint8_t>& in)
{
    size_t need = 4 + sizeof(m_s) + 3 * sizeof(M6801::State) + sizeof(m_z80) + sizeof(m_vdp) + sizeof(m_psg.s);
    if (in.size() != need) return false;
    uint32_t magic; std::memcpy(&magic, in.data(), 4);
    if (magic != 0x4d414441) return false;
    const uint8_t* p = in.data() + 4;
    std::memcpy(&m_s, p, sizeof(m_s)); p += sizeof(m_s);
    std::memcpy(m_master.state(), p, sizeof(M6801::State)); p += sizeof(M6801::State);
    std::memcpy(m_kb.state(), p, sizeof(M6801::State)); p += sizeof(M6801::State);
    std::memcpy(m_prn.state(), p, sizeof(M6801::State)); p += sizeof(M6801::State);
    z80 keep = m_z80;
    std::memcpy(&m_z80, p, sizeof(m_z80)); p += sizeof(m_z80);
    m_z80.read_byte = keep.read_byte; m_z80.write_byte = keep.write_byte;
    m_z80.port_in = keep.port_in; m_z80.port_out = keep.port_out; m_z80.userdata = this;
    std::memcpy(&m_vdp, p, sizeof(m_vdp)); p += sizeof(m_vdp);
    std::memcpy(&m_psg.s, p, sizeof(m_psg.s));
    return true;
}
