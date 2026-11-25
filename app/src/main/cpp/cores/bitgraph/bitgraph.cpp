// PocketPlug - BBN BitGraph (port of MAME's skeleton/bitgraph.cpp)
// SPDX-License-Identifier: BSD-3-Clause

#include "bitgraph.h"
#include <algorithm>
#include <cstring>
#include "../unixpc/freebee/musashi/m68k.h"
#include "../unixpc/freebee/m68k_bus.h"

namespace {
const double CPU_HZ = 6900000.0;
const double PSG_HZ = 1294400.0;
BitGraphCore* g_core = nullptr;
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

BitGraphCore::BitGraphCore(bool revB) : m_revB(revB), m_pia(this)
{
    m_ram.assign(0x80000, 0);
    m_out.assign(768 * 1024, 0xff000000u);
    std::memset(m_earom, 0xff, sizeof(m_earom));
}

BitGraphCore::~BitGraphCore()
{
    if (g_core == this) { m68k_bus_set(nullptr); g_core = nullptr; }
}

void BitGraphCore::bindBus()
{
    g_core = this;
    m68k_bus_set(&kBus);
}

bool BitGraphCore::load(const std::vector<RomFile>& roms, std::string& error)
{
    m_rom.assign(0x8000, 0xff);
    if (!m_revB) {
        const RomFile* r = find(roms, "bg125.rom");
        if (!r) { error = "Missing bg125.rom"; return false; }
        std::memcpy(m_rom.data(), r->data.data(), std::min<size_t>(r->data.size(), 0x8000));
        m_rom[0x38e4] = 0x4e; m_rom[0x38e5] = 0x75;     // MAME: disable clksync()
    } else {
        // the four 8 KB EPROMs interleave: hi = even bytes, lo = odd bytes
        const char* sets[2][4] = { { "bg2.32hi_u13.bin", "bg2.32lo_u11.bin", "bg2.32hi_u12.bin", "bg2.32lo_u10.bin" },
                                   { "bg5176_u13.bin", "bg5174_u11.bin", "bg5175_u12.bin", "bg5173_u10.bin" } };
        int b = m_bios;
        for (int k = 0; k < 2; k++) {
            int bb = k == 0 ? b : 1 - b;
            bool ok = true;
            for (auto n : sets[bb]) if (!find(roms, n)) ok = false;
            if (!ok) continue;
            m_bios = bb;
            for (int half = 0; half < 2; half++) {
                const RomFile* hi = find(roms, sets[bb][half * 2]);
                const RomFile* lo = find(roms, sets[bb][half * 2 + 1]);
                for (size_t i = 0; i < 0x2000; i++) {
                    m_rom[half * 0x4000 + i * 2] = i < hi->data.size() ? hi->data[i] : 0xff;
                    m_rom[half * 0x4000 + i * 2 + 1] = i < lo->data.size() ? lo->data[i] : 0xff;
                }
            }
            goto loaded;
        }
        error = "Missing the BitGraph rev B ROMs (bg2.32*.bin or bg517*.bin)";
        return false;
    }
loaded:
    bindBus();
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    reset();
    return true;
}

void BitGraphCore::setOption(int id, int value)
{
    if (id == 0) m_loop = value != 0;
    else if (id == 1 && m_revB && (value ? 1 : 0) != m_bios) {
        // reloading needs the ROM files again: the app restarts the machine for this
        m_bios = value ? 1 : 0;
    }
}

void BitGraphCore::reset()
{
    bindBus();
    std::fill(m_ram.begin(), m_ram.end(), 0);
    for (auto& c : m_acia) c = Acia();
    m_piaA = m_piaB = 0;
    m_pia.reset();
    std::memset(m_psgReg, 0, sizeof(m_psgReg));
    m_psgReg[7] = 0xff;
    m_clkPhase = 0; m_clkLine = false; m_irq6 = false;
    m68k_set_irq(0);
    m68k_pulse_reset();
}

// ---- memory -----------------------------------------------------------------

uint8_t BitGraphCore::read8(uint32_t a)
{
    a &= 0xffffff;
    if (a < 0x8000) return m_rom[a];
    if (a >= 0x380000 && a < 0x400000) {
        if (!m_revB && a < 0x3e0000) return 0xff;
        return m_ram[a - 0x380000];
    }
    switch (a) {
    case 0x010000: return aciaRead(m_acia[0], true);
    case 0x010002: return aciaRead(m_acia[0], false);
    case 0x010009: return aciaRead(m_acia[1], true);
    case 0x01000b: return aciaRead(m_acia[1], false);
    case 0x010011: return aciaRead(m_acia[2], true);
    case 0x010013: return aciaRead(m_acia[2], false);
    case 0x010019: return m_revB ? 0xff : aciaRead(m_acia[3], true);
    case 0x01001b: return m_revB ? 0xff : aciaRead(m_acia[3], false);
    case 0x010028: case 0x01002a: case 0x01002c: case 0x01002e:
        return m_pia.read(3 - int((a - 0x010028) >> 1));
    }
    return 0xff;
}

uint16_t BitGraphCore::read16(uint32_t a)
{
    a &= 0xfffffe;
    if (a < 0x8000) return uint16_t(m_rom[a] << 8 | m_rom[a + 1]);
    if (a >= 0x380000 && a < 0x400000 && (m_revB || a >= 0x3e0000))
        return uint16_t(m_ram[a - 0x380000] << 8 | m_ram[a - 0x380000 + 1]);
    return uint16_t(read8(a) << 8 | read8(a + 1));
}

void BitGraphCore::write8(uint32_t a, uint8_t d)
{
    a &= 0xffffff;
    if (a >= 0x380000 && a < 0x400000) {
        if (m_revB || a >= 0x3e0000) m_ram[a - 0x380000] = d;
        return;
    }
    switch (a) {
    case 0x010000: aciaWrite(0, m_acia[0], true, d); break;
    case 0x010002: aciaWrite(0, m_acia[0], false, d); break;
    case 0x010009: aciaWrite(1, m_acia[1], true, d); break;
    case 0x01000b: aciaWrite(1, m_acia[1], false, d); break;
    case 0x010011: aciaWrite(2, m_acia[2], true, d); break;
    case 0x010013: aciaWrite(2, m_acia[2], false, d); break;
    case 0x010019: if (!m_revB) aciaWrite(3, m_acia[3], true, d); break;
    case 0x01001b: if (!m_revB) aciaWrite(3, m_acia[3], false, d); break;   // rev B: MISCCR (written once)
    case 0x010028: case 0x01002a: case 0x01002c: case 0x01002e:
        m_pia.write(3 - int((a - 0x010028) >> 1), d);
        break;
    }
    updateIrq();
}

void BitGraphCore::write16(uint32_t a, uint16_t d)
{
    a &= 0xfffffe;
    if (a >= 0x380000 && a < 0x400000) {
        if (m_revB || a >= 0x3e0000) { m_ram[a - 0x380000] = uint8_t(d >> 8); m_ram[a - 0x380000 + 1] = uint8_t(d); }
        return;
    }
    if (a == 0x010030) return;      // baud rates: the serial timing here is fixed
    write8(a, uint8_t(d >> 8));
    write8(a + 1, uint8_t(d));
}

int BitGraphCore::intAck(int) { return M68K_INT_ACK_AUTOVECTOR; }

// ---- 6850 ACIAs -----------------------------------------------------------------

uint8_t BitGraphCore::aciaRead(Acia& c, bool data)
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

void BitGraphCore::aciaWrite(int n, Acia& c, bool data, uint8_t d)
{
    if (!data) {
        if ((d & 3) == 3) { c.reset = true; c.status = 0x02; c.in.clear(); }
        else c.reset = false;
        c.ctl = d;
        return;
    }
    // the transmitter is always ready; only the host port has something on the far end
    if (n == 0 && m_loop) m_acia[0].in.push_back(d);
    c.status |= 0x02;
}

void BitGraphCore::serviceSerial(int cycles)
{
    // host 9600 baud (~1 ms a character), keyboard 300 baud (~33 ms)
    static const int gapCycles[4] = { 7200, 230000, 7200, 7200 };
    for (int n = 0; n < 4; n++) {
        Acia& c = m_acia[n];
        if (c.gap > 0) c.gap -= cycles;
        if (c.gap <= 0 && !c.in.empty() && !c.reset) {
            if (!(c.status & 0x01)) {
                c.rx = c.in.front(); c.in.pop_front();
                c.status |= 0x01;
                c.gap = gapCycles[n];
            }
        }
    }
    updateIrq();
}

void BitGraphCore::keyEvent(int code, bool down)
{
    if (down && code > 0 && code < 0x100 && m_acia[1].in.size() < 64) m_acia[1].in.push_back(uint8_t(code));
}

void BitGraphCore::updateIrq()
{
    bool i1 = false;
    for (int n = 0; n < (m_revB ? 3 : 4); n++) if (m_acia[n].irq()) i1 = true;
    int level = m_irq6 ? 6 : i1 ? 1 : 0;
    m68k_set_irq(level);
}

// ---- PIA: the PSG and the EAROM ---------------------------------------------------

uint8_t BitGraphCore::inA()
{
    return (m_piaB & 0x08) ? m_earomData : m_piaA;
}

void BitGraphCore::outB(uint8_t d, uint8_t)
{
    uint8_t old = m_piaB;
    m_piaB = d;
    switch (d & 3) {
    case 2: psgWrite(m_psgAddr, m_piaA); break;
    case 3: m_psgAddr = m_piaA & 0x0f; break;
    }
    // ER2055: CS1 = CS2 = PB3, C1 = PA6, C2 = PA7, CLK = PB2
    bool cs = d & 0x08;
    if (cs) m_earomData = (d & 0x08) ? m_piaA : m_earomData;
    uint8_t ctl = uint8_t((m_piaA & 0x40 ? 1 : 0) | (m_piaA & 0x80 ? 2 : 0));
    if (cs) {
        if ((ctl & 1) == 0 && ctl != m_earomCtl) {
            if (ctl == 0) m_earom[m_earomAddr] &= m_earomData;      // write (after an erase)
            else if (ctl == 2) m_earom[m_earomAddr] = 0xff;         // erase
        }
        if ((old & 0x04) && !(d & 0x04) && (ctl & 1)) m_earomData = m_earom[m_earomAddr];   // read on CLK falling
    }
    m_earomCtl = ctl;
    // PB6 low clears the clock interrupt (and holds it off)
    if (!(d & 0x40)) m_irq6 = false;
    updateIrq();
}

void BitGraphCore::psgWrite(uint8_t reg, uint8_t d)
{
    m_psgReg[reg & 15] = d;
    if ((reg & 15) == 14) m_earomAddr = d & 0x3f;       // port A: the EAROM address
}

// ---- running ------------------------------------------------------------------

void BitGraphCore::runFrame()
{
    bindBus();
    const double clockHz = m_revB ? 1040.0 : 40.0;
    const int total = int(CPU_HZ / 40.0);
    const int slice = 400;
    for (int done = 0; done < total; ) {
        int n = m68k_execute(slice);
        if (n <= 0) n = slice;
        done += n;
        serviceSerial(n);
        // the system clock: high for the first half of each period, IRQ 6 while high and PB6 set
        m_clkPhase += n * clockHz / CPU_HZ;
        while (m_clkPhase >= 1.0) m_clkPhase -= 1.0;
        bool line = m_clkPhase < 0.5;
        if (line != m_clkLine) {
            m_clkLine = line;
            m_irq6 = line && (m_piaB & 0x40);
            updateIrq();
        }
        // sound: three tones and noise (no envelope) at 44.1 kHz
        m_sampleAcc += n * 44100.0 / CPU_HZ;
        while (m_sampleAcc >= 1.0) {
            m_sampleAcc -= 1.0;
            m_psgAcc += PSG_HZ / 16.0 / 44100.0;
            while (m_psgAcc >= 1.0) {
                m_psgAcc -= 1.0;
                for (int ch = 0; ch < 3; ch++) {
                    int per = (m_psgReg[ch * 2] | (m_psgReg[ch * 2 + 1] & 15) << 8); if (per < 1) per = 1;
                    if (++m_toneCnt[ch] >= per) { m_toneCnt[ch] = 0; m_toneOut[ch] ^= 1; }
                }
                int np = (m_psgReg[6] & 31) * 2; if (np < 2) np = 2;
                if (++m_noiseCnt >= np) { m_noiseCnt = 0; m_rng = (m_rng >> 1) | (((m_rng ^ (m_rng >> 3)) & 1) << 16); m_noiseOut = m_rng & 1; }
            }
            int s = 0;
            for (int ch = 0; ch < 3; ch++) {
                bool tone = (m_psgReg[7] >> ch & 1) || m_toneOut[ch];
                bool noise = (m_psgReg[7] >> (ch + 3) & 1) || m_noiseOut;
                int amp = (m_psgReg[8 + ch] & 0x10) ? 12 : (m_psgReg[8 + ch] & 15);
                if (tone && noise && amp) s += amp * 500;
            }
            m_audio.push_back(int16_t(s - 7500 > 32000 ? 32000 : s));
        }
    }
    m_frame++;
    m_pia.ca1W(m_frame & 1);
    render();
    if (m_audio.size() > 44100 / 4) m_audio.erase(m_audio.begin(), m_audio.end() - 44100 / 10);
}

void BitGraphCore::render()
{
    // 64 words a line, 768 lines; rev A's monitor turns it 90 degrees clockwise, rev B's 270
    const uint8_t* vram = &m_ram[0x60000];
    for (int y = 0; y < 768; y++) {
        for (int xw = 0; xw < 64; xw++) {
            uint16_t g = uint16_t(vram[(y * 64 + xw) * 2] << 8 | vram[(y * 64 + xw) * 2 + 1]);
            for (int b = 0; b < 16; b++) {
                int x = xw * 16 + b;
                uint32_t px = (g >> (15 - b)) & 1 ? 0xfff0f0f0u : 0xff000000u;
                int dx, dy;
                if (!m_revB) { dx = 767 - y; dy = x; } else { dx = y; dy = 1023 - x; }
                m_out[size_t(dy) * 768 + dx] = px;
            }
        }
    }
}

size_t BitGraphCore::readAudio(int16_t* out, size_t maxSamples)
{
    size_t n = std::min(maxSamples, m_audio.size());
    std::memcpy(out, m_audio.data(), n * sizeof(int16_t));
    m_audio.erase(m_audio.begin(), m_audio.begin() + long(n));
    return n;
}
