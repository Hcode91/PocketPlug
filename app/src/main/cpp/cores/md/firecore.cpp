// PocketPlug - Firecore Mega Drive clone (AtGames 40 Bonus Games in 1). See firecore.h.
// SPDX-License-Identifier: GPL-2.0-or-later

#include "firecore.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
#include "../unixpc/freebee/musashi/m68k.h"
#include "../unixpc/freebee/m68k_bus.h"
}

namespace {
constexpr double MASTER = 53203424.0;
constexpr double CPU68K = MASTER / 7.0;
constexpr double CPUZ80 = MASTER / 15.0;
constexpr int LINES = 313;
constexpr double LINE68K = 3420.0 / 7.0;
constexpr int LINEZ80 = 228;
constexpr int AUDIO_HZ = 44100;

FirecoreCore* g_core = nullptr;
unsigned int b_r8(unsigned int a) { return g_core->read8(a & 0xffffff); }
unsigned int b_r16(unsigned int a) { return g_core->read16(a & 0xffffff); }
unsigned int b_r32(unsigned int a) { return uint32_t(g_core->read16(a & 0xffffff)) << 16 | g_core->read16((a + 2) & 0xffffff); }
void b_w8(unsigned int a, unsigned int v) { g_core->write8(a & 0xffffff, uint8_t(v)); }
void b_w16(unsigned int a, unsigned int v) { g_core->write16(a & 0xffffff, uint16_t(v)); }
void b_w32(unsigned int a, unsigned int v) { g_core->write16(a & 0xffffff, uint16_t(v >> 16)); g_core->write16((a + 2) & 0xffffff, uint16_t(v)); }
int b_ack(int level) { return g_core->intAck(level); }
const M68kBus kBus = { b_r8, b_r16, b_r32, b_w8, b_w16, b_w32, b_ack };
const m68k_register_t kRegs[] = {
    M68K_REG_SR, M68K_REG_USP, M68K_REG_ISP,
    M68K_REG_D0, M68K_REG_D1, M68K_REG_D2, M68K_REG_D3, M68K_REG_D4, M68K_REG_D5, M68K_REG_D6, M68K_REG_D7,
    M68K_REG_A0, M68K_REG_A1, M68K_REG_A2, M68K_REG_A3, M68K_REG_A4, M68K_REG_A5, M68K_REG_A6, M68K_REG_A7,
    M68K_REG_PPC, M68K_REG_IR, M68K_REG_PC };
constexpr int kNumRegs = int(sizeof(kRegs) / sizeof(kRegs[0]));

uint8_t zr(void* u, uint16_t a) { return static_cast<FirecoreCore*>(u)->zRead(a); }
void zw(void* u, uint16_t a, uint8_t d) { static_cast<FirecoreCore*>(u)->zWrite(a, d); }
uint8_t zin(z80*, uint8_t) { return 0xff; }
void zout(z80*, uint8_t, uint8_t) {}
bool dbg() { static int v = getenv("MDDBG") ? 1 : 0; return v; }
}

FirecoreCore::FirecoreCore() : m_ym(*this)
{
    std::memset(m_fb, 0, sizeof(m_fb));
    std::memset(&m_s, 0, sizeof(m_s));
}

FirecoreCore::~FirecoreCore()
{
    if (g_core == this) { m68k_bus_set(nullptr); g_core = nullptr; }
}

bool FirecoreCore::load(const std::vector<RomFile>& roms, std::string& error)
{
    const RomFile* f = nullptr;
    for (auto& r : roms) if (r.name == "40bonusgamesin1.bin" && r.data.size() == 0x1000000) f = &r;
    if (!f) { error = "Missing 40bonusgamesin1.bin"; return false; }
    // byte-swapped words; the file's halves are swapped (MAME: load at 0x800000, continue at 0)
    m_rom.resize(0x1000000);
    for (size_t i = 0; i < 0x800000; i++) {
        m_rom[0x800000 + i] = f->data[i ^ 1];
        m_rom[i] = f->data[0x800000 + (i ^ 1)];
    }
    m_romMask = 0xffffff;
    m_vdp.pal = true;
    m_vdp.busRead16 = [this](uint32_t a) { return read16(a & 0xffffff); };
    m_psg.setSega();
    if (getenv("MDVTRACE")) m_vdp.traceWrite = [](uint8_t c, uint16_t a, uint16_t d) { fprintf(stderr, "vdpw %02x %04x %04x\n", c, a, d); };
    g_core = this;
    m68k_bus_set(&kBus);
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    reset();
    return true;
}

void FirecoreCore::reset()
{
    std::memset(&m_s, 0, sizeof(m_s));
    m_s.ymTimer[0] = m_s.ymTimer[1] = -1;
    m_s.padData[0] = m_s.padData[1] = m_s.padData[2] = 0x7f;
    m_vdp.reset();
    m_psg.reset();
    m_ym.reset();
    z80_init(&m_z80);
    m_z80.read_byte = zr; m_z80.write_byte = zw;
    m_z80.port_in = zin; m_z80.port_out = zout;
    m_z80.userdata = this;
    g_core = this;
    m68k_bus_set(&kBus);
    m68k_set_irq(0);
    m68k_pulse_reset();
}

void FirecoreCore::updateIrq() { m68k_set_irq(unsigned(m_vdp.irqLevel())); }

int FirecoreCore::intAck(int level)
{
    m_vdp.ackIrq(level);
    m68k_set_irq(unsigned(m_vdp.irqLevel()));
    return M68K_INT_ACK_AUTOVECTOR;
}

uint8_t FirecoreCore::padRead(int port)
{
    uint8_t data = m_s.padData[port];
    if (port != 0) return uint8_t((data & 0x80) | 0x7f);
    uint8_t th = data & 0x40;
    uint8_t v;
    uint32_t k = m_keys;
    if (th) {
        v = uint8_t(0x40 | (~((k & 0x0f) | ((k & 0x20) ? 0x10 : 0) | ((k & 0x80) ? 0x20 : 0)) & 0x3f));
    } else {
        v = uint8_t(~((k & 0x03) | ((k & 0x10) ? 0x10 : 0) | ((k & 0x40) ? 0x20 : 0)) & 0x33);
    }
    // output bits come from the data register
    uint8_t ctrl = m_s.padCtrl[port];
    return uint8_t((v & ~ctrl & 0x7f) | (data & ctrl & 0x7f) | (data & 0x80));
}

uint8_t FirecoreCore::read8(uint32_t a)
{
    uint16_t w = read16(a & ~1u);
    return uint8_t((a & 1) ? w : w >> 8);
}

uint16_t FirecoreCore::read16(uint32_t a)
{
    a &= 0xfffffe;
    if (a < 0x400000) {
        uint32_t p = (m_s.bank + a) & m_romMask;
        return uint16_t(m_rom[p] << 8 | m_rom[(p + 1) & m_romMask]);
    }
    if (a >= 0xe00000) return uint16_t(m_s.ram[a & 0xffff] << 8 | m_s.ram[(a + 1) & 0xffff]);
    if (a >= 0xc00000 && a < 0xe00000) {
        switch (a & 0x1f) {
        case 0x00: case 0x02: return m_vdp.readData();
        case 0x04: case 0x06: m_vdp.hpos = int(m68k_cycles_run()); return m_vdp.readControl();
        case 0x08: case 0x0a: case 0x0c: case 0x0e: return m_vdp.hvCounter(int(m68k_cycles_run()) * 342 / 488);
        default: return 0xffff;
        }
    }
    if (a >= 0xa00000 && a < 0xa10000) {
        // the YM2612 answers the 68000 without the Z80 bus (games here poll it directly)
        if ((a & 0xe000) == 0x4000) { uint8_t v = m_ym.read(a & 3); return uint16_t(v << 8 | v); }
        if (!m_s.busReq || m_s.zReset == 0) return 0xffff;
        uint8_t v = zRead(uint16_t(a & 0xffff));
        return uint16_t(v << 8 | v);
    }
    if (a >= 0xa10000 && a < 0xa10020) {
        int r = (a & 0x1f) >> 1;
        uint8_t v = 0;
        switch (r) {
        case 0: v = 0xe0; break;   // overseas, PAL, no expansion
        case 1: case 2: case 3: v = padRead(r - 1); break;
        case 4: case 5: case 6: v = m_s.padCtrl[r - 4]; break;
        default: v = 0; break;
        }
        return uint16_t(v << 8 | v);
    }
    if (a == 0xa11100) {
        bool granted = m_s.busReq && m_s.zReset;
        return uint16_t(granted ? 0 : 0x0100);
    }
    if (dbg()) fprintf(stderr, "r16 %06x\n", a);
    return 0;
}

void FirecoreCore::write8(uint32_t a, uint8_t d)
{
    if (a >= 0xe00000) { m_s.ram[a & 0xffff] = d; return; }
    if (a >= 0xc00000 && a < 0xe00000) {
        if ((a & 0x1f) == 0x11 || (a & 0x1f) == 0x13 || (a & 0x1f) == 0x15 || (a & 0x1f) == 0x17) { m_psg.write(d); return; }
        write16(a & ~1u, uint16_t(d << 8 | d));
        return;
    }
    if (a >= 0xa00000 && a < 0xa10000) {
        if ((a & 0xe000) == 0x4000) { m_ym.write(a & 3, d); return; }
        if (m_s.busReq && m_s.zReset) zWrite(uint16_t(a & 0xffff), d);
        return;
    }
    if (a >= 0xa10000 && a < 0xa10020) {
        int r = (a & 0x1f) >> 1;
        if (r >= 1 && r <= 3) m_s.padData[r - 1] = d;
        else if (r >= 4 && r <= 6) m_s.padCtrl[r - 4] = d;
        return;
    }
    if ((a & ~1u) == 0xa11100) { m_s.busReq = (a & 1) ? m_s.busReq : (d & 1); return; }
    if ((a & ~1u) == 0xa11200) {
        if (a & 1) return;
        uint8_t r = d & 1;
        if (r && !m_s.zReset) { auto keep = m_z80; z80_init(&m_z80); m_z80.read_byte = keep.read_byte; m_z80.write_byte = keep.write_byte; m_z80.port_in = keep.port_in; m_z80.port_out = keep.port_out; m_z80.userdata = keep.userdata; m_ym.reset(); }
        m_s.zReset = r;
        return;
    }
    write16(a & ~1u, (a & 1) ? uint16_t(d) : uint16_t(d << 8));
}

void FirecoreCore::write16(uint32_t a, uint16_t d)
{
    a &= 0xfffffe;
    if (a >= 0xe00000) { m_s.ram[a & 0xffff] = uint8_t(d >> 8); m_s.ram[(a + 1) & 0xffff] = uint8_t(d); return; }
    if (a >= 0xc00000 && a < 0xe00000) {
        switch (a & 0x1f) {
        case 0x00: case 0x02: m_vdp.writeData(d); return;
        case 0x04: case 0x06: m_vdp.writeControl(d); updateIrq(); return;
        case 0x10: case 0x12: case 0x14: case 0x16: m_psg.write(uint8_t(d)); return;
        default: return;
        }
    }
    if (a >= 0xa00000 && a < 0xa10000) { if (m_s.busReq && m_s.zReset) zWrite(uint16_t(a & 0xffff), uint8_t(d >> 8)); return; }
    if (a >= 0xa10000 && a < 0xa10020) { write8(a | 1, uint8_t(d)); return; }
    if (a == 0xa11100) { m_s.busReq = (d >> 8) & 1; return; }
    if (a == 0xa11200) { write8(a, uint8_t(d >> 8)); return; }
    // Firecore banking (MAME's megadriv_firecore.cpp)
    if (a == 0xa10104) { m_s.bank |= 0x800000; if (dbg()) fprintf(stderr, "bank_upper %04x -> %06x\n", d, m_s.bank); return; }
    if (a == 0xb01028) { m_s.bank = (m_s.bank & 0xffff0000) | d; if (dbg()) fprintf(stderr, "bank_low %04x -> %06x\n", d, m_s.bank); return; }
    if (a == 0xb0102a) { m_s.bank = (m_s.bank & 0xff80ffff) | uint32_t(d & 0x7f) << 16; if (dbg()) fprintf(stderr, "bank_high %04x -> %06x\n", d, m_s.bank); return; }
    if (a == 0xb01036) { m_s.altMode = uint8_t(d); if (dbg()) fprintf(stderr, "b01036 %04x\n", d); return; }
    if (dbg()) fprintf(stderr, "w16 %06x=%04x\n", a, d);
}

uint8_t FirecoreCore::zRead(uint16_t a)
{
    if (a < 0x4000) return m_s.zram[a & 0x1fff];
    if (a < 0x6000) return m_ym.read(a & 3);
    if (a >= 0x8000) {
        uint32_t ad = m_s.zBank | (a & 0x7fff);
        return read8(ad);
    }
    return 0xff;
}

void FirecoreCore::zWrite(uint16_t a, uint8_t d)
{
    if (a < 0x4000) { m_s.zram[a & 0x1fff] = d; return; }
    if (a < 0x6000) { m_ym.write(a & 3, d); return; }
    if (a < 0x6100) { m_s.zBank = ((m_s.zBank >> 1) | (uint32_t(d & 1) << 23)) & 0xff8000; return; }
    if (a >= 0x7f00 && a < 0x8000) { if ((a & 0xff) >= 0x10 && (a & 0xff) < 0x18) m_psg.write(d); return; }
    if (a >= 0x8000) write8(m_s.zBank | (a & 0x7fff), d);
}

void FirecoreCore::audioLine(int)
{
    // one line's worth of output samples: YM2612 at clock / 144, PSG at the Z80 clock
    const double linesPerSec = MASTER / 3420.0;
    const double ymRate = CPU68K / 144.0;
    m_s.outPos += AUDIO_HZ / linesPerSec;
    while (m_s.outPos >= 1.0) {
        m_s.outPos -= 1.0;
        // YM2612
        m_s.ymPos += ymRate / AUDIO_HZ;
        int32_t ymSum = 0; int n = 0;
        while (m_s.ymPos >= 1.0) {
            m_s.ymPos -= 1.0;
            ymfm::ym2612::output_data o;
            m_ym.generate(&o);
            ymSum += (o.data[0] + o.data[1]) / 2; n++;
            for (int t = 0; t < 2; t++) {
                if (m_s.ymTimer[t] >= 0) {
                    m_s.ymTimer[t] -= 144;
                    if (m_s.ymTimer[t] < 0) { m_s.ymTimer[t] = -1; m_engine->engine_timer_expired(uint32_t(t)); }
                }
            }
        }
        int32_t ym = n ? ymSum / n : m_s.ymLast;
        m_s.ymLast = ym;
        // PSG
        m_s.psgPos += CPUZ80 / AUDIO_HZ;
        int32_t ps = 0; int pn = 0;
        while (m_s.psgPos >= 1.0) { m_s.psgPos -= 1.0; ps += m_psg.clock(); pn++; }
        int32_t psg = pn ? ps / pn : 0;
        int32_t v = ym * 2 + psg / 3;
        m_audio.push_back(int16_t(std::clamp(v, -32767, 32767)));
    }
}

void FirecoreCore::runFrame()
{
    if (g_core != this) { g_core = this; m68k_bus_set(&kBus); }
    const int vis = m_vdp.visibleLines();
    for (int line = 0; line < LINES; line++) {
        bool vb = m_vdp.beginLine(line);
        if (vb) m_s.zIrqLines = 1;
        updateIrq();
        m_s.lineCarry68k += LINE68K;
        int c68 = int(m_s.lineCarry68k);
        m_s.lineCarry68k -= c68;
        m68k_execute(c68);
        if (m_s.zReset && !m_s.busReq) {
            int done = 0;
            while (done < LINEZ80) {
                m_z80.int_pending = m_s.zIrqLines != 0;
                m_z80.int_data = 0xff;
                unsigned long before = m_z80.cyc;
                z80_step(&m_z80);
                int c = int(m_z80.cyc - before);
                done += c > 0 ? c : 4;
                if (m_s.zIrqLines && !m_z80.int_pending) m_s.zIrqLines = 0;   // taken
            }
            m_z80.int_pending = 0;
        }
        if (m_s.zIrqLines && line != vis) m_s.zIrqLines = 0;   // the Z80 INT lasts about a line
        if (line < 224) m_vdp.renderLine(line + (vis == 240 ? 8 : 0), m_fb + line * MdVdp::W);
        audioLine(c68);
    }
    if (m_audio.size() > 44100) m_audio.erase(m_audio.begin(), m_audio.end() - 4410);
}

size_t FirecoreCore::readAudio(int16_t* out, size_t maxSamples)
{
    size_t n = std::min(maxSamples, m_audio.size());
    std::memcpy(out, m_audio.data(), n * sizeof(int16_t));
    m_audio.erase(m_audio.begin(), m_audio.begin() + long(n));
    return n;
}

namespace { constexpr uint32_t MAGIC = 0x3144464d; }   // 'MFD1'

bool FirecoreCore::saveState(std::vector<uint8_t>& out)
{
    out.clear();
    auto put = [&](const void* p, size_t n) { const uint8_t* b = (const uint8_t*)p; out.insert(out.end(), b, b + n); };
    uint32_t magic = MAGIC;
    put(&magic, 4); put(&m_s, sizeof(m_s)); put(&m_vdp.s, sizeof(m_vdp.s)); put(&m_psg.s, sizeof(m_psg.s)); put(&m_z80, sizeof(m_z80));
    if (g_core != this) { g_core = this; m68k_bus_set(&kBus); }
    for (int i = 0; i < kNumRegs; i++) { uint32_t v = m68k_get_reg(nullptr, kRegs[i]); put(&v, 4); }
    std::vector<uint8_t> ym;
    ymfm::ymfm_saved_state st(ym, true);
    m_ym.save_restore(st);
    uint32_t n = uint32_t(ym.size()); put(&n, 4); put(ym.data(), ym.size());
    return true;
}

bool FirecoreCore::loadState(const std::vector<uint8_t>& in)
{
    size_t fixed = 4 + sizeof(m_s) + sizeof(m_vdp.s) + sizeof(m_psg.s) + sizeof(m_z80) + kNumRegs * 4 + 4;
    if (in.size() < fixed) return false;
    uint32_t magic; std::memcpy(&magic, in.data(), 4);
    if (magic != MAGIC) return false;
    const uint8_t* p = in.data() + 4;
    std::memcpy(&m_s, p, sizeof(m_s)); p += sizeof(m_s);
    std::memcpy(&m_vdp.s, p, sizeof(m_vdp.s)); p += sizeof(m_vdp.s);
    std::memcpy(&m_psg.s, p, sizeof(m_psg.s)); p += sizeof(m_psg.s);
    auto keep = m_z80;
    std::memcpy(&m_z80, p, sizeof(m_z80)); p += sizeof(m_z80);
    m_z80.read_byte = keep.read_byte; m_z80.write_byte = keep.write_byte;
    m_z80.port_in = keep.port_in; m_z80.port_out = keep.port_out; m_z80.userdata = keep.userdata;
    if (g_core != this) { g_core = this; m68k_bus_set(&kBus); }
    for (int i = 0; i < kNumRegs; i++) { uint32_t v; std::memcpy(&v, p, 4); p += 4; m68k_set_reg(kRegs[i], v); }
    uint32_t n; std::memcpy(&n, p, 4); p += 4;
    if (in.size() != fixed + n) return false;
    std::vector<uint8_t> ym(p, p + n);
    ymfm::ymfm_saved_state st(ym, false);
    m_ym.save_restore(st);
    return true;
}
