// PocketPlug - AT&T Blit (port of MAME's att/blit.cpp)
// SPDX-License-Identifier: BSD-3-Clause

#include "blit.h"
#include <algorithm>
#include <cstring>
#include "../unixpc/freebee/musashi/m68k.h"
#include "../unixpc/freebee/m68k_bus.h"

namespace {
const double CPU_HZ = 33000000.0 / 4;
BlitCore* g_core = nullptr;
unsigned int b_r8(unsigned int a) { return g_core->read8(a); }
unsigned int b_r16(unsigned int a) { return g_core->read16(a); }
unsigned int b_r32(unsigned int a) { return uint32_t(g_core->read16(a)) << 16 | g_core->read16(a + 2); }
void b_w8(unsigned int a, unsigned int v) { g_core->write8(a, uint8_t(v)); }
void b_w16(unsigned int a, unsigned int v) { g_core->write16(a, uint16_t(v)); }
void b_w32(unsigned int a, unsigned int v) { g_core->write16(a, uint16_t(v >> 16)); g_core->write16(a + 2, uint16_t(v)); }
int b_ack(int level) { return g_core->intAck(level); }
const M68kBus kBus = { b_r8, b_r16, b_r32, b_w8, b_w16, b_w32, b_ack };
const RomFile* find(const std::vector<RomFile>& roms, const char* name)
{
    for (auto& r : roms) if (r.name == name) return &r;
    return nullptr;
}
}

BlitCore::BlitCore()
{
    m_ram.assign(0x40000, 0);
    m_out.assign(800 * 1024, 0xff000000u);
}

BlitCore::~BlitCore()
{
    if (g_core == this) { m68k_bus_set(nullptr); g_core = nullptr; }
}

void BlitCore::bindBus()
{
    g_core = this;
    m68k_bus_set(&kBus);
}

bool BlitCore::load(const std::vector<RomFile>& roms, std::string& error)
{
    // rom0/2/4 on the odd bytes, rom1/3/5 on the even bytes (MAME ROM_SKIP(1))
    m_rom.assign(0x6000, 0xff);
    static const struct { const char* name; uint32_t at; } parts[6] = {
        { "rom0.bin", 0x0001 }, { "rom1.bin", 0x0000 }, { "rom2.bin", 0x2001 }, { "rom3.bin", 0x2000 }, { "rom4.bin", 0x4001 }, { "rom5.bin", 0x4000 } };
    for (auto& p : parts) {
        const RomFile* r = find(roms, p.name);
        if (!r) { error = std::string("Missing ") + p.name; return false; }
        for (size_t i = 0; i < r->data.size() && p.at + i * 2 < m_rom.size(); i++) m_rom[p.at + i * 2] = r->data[i];
    }
    bindBus();
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    reset();
    return true;
}

void BlitCore::reset()
{
    bindBus();
    std::fill(m_ram.begin(), m_ram.end(), 0);
    for (auto& c : m_acia) c = Acia();
    m_shadow = true;
    m_misc = 0; m_start = 0;
    m_vblank = m_mouseIrq = false;
    m68k_set_irq(0);
    m68k_pulse_reset();
}

// ---- memory ------------------------------------------------------------------------

uint8_t BlitCore::read8(uint32_t a)
{
    a &= 0xffffff;
    if (a < 8 && m_shadow) return m_rom[a];
    if (a < 0x40000) return m_ram[a];
    if (a < 0x46000) { m_shadow = false; return m_rom[a - 0x40000]; }
    switch (a) {
    case 0x060000: return uint8_t(m_mouseY >> 8);
    case 0x060001: return uint8_t(m_mouseY);
    case 0x060002: return uint8_t(m_mouseX >> 8);
    case 0x060003: return uint8_t(m_mouseX);
    case 0x060008: case 0x060009: return aciaRead(m_acia[0], false);
    case 0x06000a: case 0x06000b: return aciaRead(m_acia[0], true);
    case 0x060011: case 0x060015: m_mouseIrq = false; updateIrq(); return m_buttons;
    case 0x060030: case 0x060031: return aciaRead(m_acia[1], false);
    case 0x060032: case 0x060033: return aciaRead(m_acia[1], true);
    case 0x060020: return uint8_t(m_misc >> 8);
    case 0x060021: return uint8_t(m_misc);
    }
    return 0xff;
}

uint16_t BlitCore::read16(uint32_t a)
{
    a &= 0xfffffe;
    if (a < 8 && m_shadow) return uint16_t(m_rom[a] << 8 | m_rom[a + 1]);
    if (a < 0x40000) return uint16_t(m_ram[a] << 8 | m_ram[a + 1]);
    if (a < 0x46000) { m_shadow = false; return uint16_t(m_rom[a - 0x40000] << 8 | m_rom[a - 0x40000 + 1]); }
    return uint16_t(read8(a) << 8 | read8(a + 1));
}

void BlitCore::write8(uint32_t a, uint8_t d)
{
    a &= 0xffffff;
    if (a < 0x40000) { m_ram[a] = d; return; }
    switch (a) {
    case 0x060008: case 0x060009: aciaWrite(0, m_acia[0], false, d); break;
    case 0x06000a: case 0x06000b: aciaWrite(0, m_acia[0], true, d); break;
    case 0x060030: case 0x060031: aciaWrite(1, m_acia[1], false, d); break;
    case 0x060032: case 0x060033: aciaWrite(1, m_acia[1], true, d); break;
    case 0x060018: m_start = uint16_t((m_start & 0x00ff) | d << 8); break;
    case 0x060019: m_start = uint16_t((m_start & 0xff00) | d); break;
    case 0x060020: m_misc = uint16_t((m_misc & 0x00ff) | d << 8); break;
    case 0x060021: m_misc = uint16_t((m_misc & 0xff00) | d); break;
    case 0x060038: case 0x060039: m_vblank = false; break;
    }
    updateIrq();
}

void BlitCore::write16(uint32_t a, uint16_t d)
{
    a &= 0xfffffe;
    if (a < 0x40000) { m_ram[a] = uint8_t(d >> 8); m_ram[a + 1] = uint8_t(d); return; }
    if (a == 0x060018) { m_start = d; return; }
    if (a == 0x060020) { m_misc = d; return; }
    if (a == 0x060038) { m_vblank = false; updateIrq(); return; }
    write8(a, uint8_t(d >> 8));
    write8(a + 1, uint8_t(d));
}

int BlitCore::intAck(int) { return M68K_INT_ACK_AUTOVECTOR; }

// ---- 6850 ACIAs -------------------------------------------------------------------

uint8_t BlitCore::aciaRead(Acia& c, bool data)
{
    if (!data) {
        uint8_t s = c.status;
        if (c.irq()) s |= 0x80;
        return s;
    }
    c.status &= uint8_t(~0x01);
    uint8_t v = c.rx;
    updateIrq();
    return v;
}

void BlitCore::aciaWrite(int n, Acia& c, bool data, uint8_t d)
{
    if (!data) {
        if ((d & 3) == 3) { c.reset = true; c.status = 0x02; c.in.clear(); }
        else c.reset = false;
        c.ctl = d;
        return;
    }
    if (n == 0 && m_loop) m_acia[0].in.push_back(d);
    if (n == 1 && (d & 2)) m_bell = 4410;     // the keyboard's bell
    c.status |= 0x02;
}

void BlitCore::serviceSerial(int cycles)
{
    // 19200 baud: about 0.57 ms a character
    for (int n = 0; n < 2; n++) {
        Acia& c = m_acia[n];
        if (c.gap > 0) c.gap -= cycles;
        if (c.gap <= 0 && !c.in.empty() && !c.reset && !(c.status & 0x01)) {
            c.rx = c.in.front(); c.in.pop_front();
            c.status |= 0x01;
            c.gap = 4700;
        }
    }
    updateIrq();
}

void BlitCore::keyEvent(int code, bool down)
{
    if (!down || code <= 0 || code >= 0x100 || m_acia[1].in.size() >= 64) return;
    if (code == 10) code = 13;
    m_acia[1].in.push_back(uint8_t(code));
}

void BlitCore::setAnalog(int stick, float x, float y)
{
    if (stick != 0) return;
    m_mouseX = uint16_t(std::clamp(x, 0.0f, 1.0f) * 799);
    m_mouseY = uint16_t(std::clamp(y, 0.0f, 1.0f) * 1023);
}

void BlitCore::setInputs(uint32_t pressed)
{
    uint8_t b = uint8_t(pressed & 7);
    if (b != m_buttons) { m_buttons = b; m_mouseIrq = true; updateIrq(); }
}

void BlitCore::updateIrq()
{
    int level = 0;
    if (m_vblank) level = 1;
    if (m_acia[1].irq()) level = 2;
    if (m_mouseIrq) level = 4;
    if (m_acia[0].irq()) level = 5;
    m68k_set_irq(level);
}

// ---- running ----------------------------------------------------------------------

void BlitCore::runFrame()
{
    bindBus();
    const int total = int(CPU_HZ / 60.0);
    const int slice = 400;
    for (int done = 0; done < total; ) {
        int n = m68k_execute(slice);
        if (n <= 0) n = slice;
        done += n;
        serviceSerial(n);
    }
    m_vblank = true;
    updateIrq();
    render();
    // the bell: a short beep
    int samples = 735;
    for (int i = 0; i < samples; i++) {
        int16_t s = 0;
        if (m_bell > 0) { m_bell--; s = (m_bell / 25) & 1 ? 5000 : -5000; }
        m_audio.push_back(s);
    }
    if (m_audio.size() > 44100 / 4) m_audio.erase(m_audio.begin(), m_audio.end() - 44100 / 10);
}

void BlitCore::render()
{
    const bool inv = m_misc & 1;
    const uint32_t start = uint32_t(m_start) << 2;     // a byte address (register = address / 4)
    for (int y = 0; y < 1024; y++) {
        uint32_t* row = &m_out[size_t(y) * 800];
        for (int xw = 0; xw < 50; xw++) {
            uint32_t a = (start + uint32_t(y * 50 + xw) * 2) & 0x3fffe;
            uint16_t g = uint16_t(m_ram[a] << 8 | m_ram[a + 1]);
            for (int b = 0; b < 16; b++) {
                bool on = (g >> (15 - b)) & 1;
                row[xw * 16 + b] = (on != inv) ? 0xfff0f0e8u : 0xff101010u;
            }
        }
    }
}

size_t BlitCore::readAudio(int16_t* out, size_t maxSamples)
{
    size_t n = std::min(maxSamples, m_audio.size());
    std::memcpy(out, m_audio.data(), n * sizeof(int16_t));
    m_audio.erase(m_audio.begin(), m_audio.begin() + long(n));
    return n;
}
