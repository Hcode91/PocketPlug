// PocketPlug - Acorn System 1
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Port of MAME's acrnsys1.cpp and ins8154.cpp. See acrnsys1.h.

#include "acrnsys1.h"
#include <algorithm>
#include <cstring>

namespace {
constexpr int CPU_HZ = 1008000;
}

AcornSystem1Core::AcornSystem1Core()
{
    std::memset(m_rom, 0xff, sizeof(m_rom));
    std::memset(m_out, 0, sizeof(m_out));
    std::memset(m_acc, 0, sizeof(m_acc));
    std::memset(&m_s, 0, sizeof(m_s));
    m_cpu.setBus(this, rdThunk, wrThunk);
}

bool AcornSystem1Core::load(const std::vector<RomFile>& roms, std::string& error)
{
    for (auto& r : roms) {
        if (r.name == "acrnsys1.bin" && r.data.size() == 0x200) {
            std::memcpy(m_rom, r.data.data(), 0x200);
            reset();
            return true;
        }
    }
    error = "Missing acrnsys1.bin (the monitor PROMs)";
    return false;
}

void AcornSystem1Core::reset()
{
    // INS8154 reset: all pins inputs
    m_s.outA = m_s.outB = m_s.odra = m_s.odrb = m_s.mdr = 0;
    m_s.digit = 0;
    m_cpu.reset();
}

void AcornSystem1Core::setInputs(uint32_t pressed)
{
    if ((pressed & ~m_keys) >> 24 & 1) reset();     // RST key
    m_keys = pressed;
}

uint8_t AcornSystem1Core::portAIn()
{
    // the 7445 selects one keypad column; its keys answer on bits 3-5 (active low)
    int line = m_s.digit & 7;
    uint8_t k = 0x38;
    if (m_keys >> (8 + line) & 1) k &= ~0x08;     // 8-F
    if (m_keys >> (16 + line) & 1) k &= ~0x10;    // command keys
    if (m_keys >> line & 1) k &= ~0x20;           // 0-7
    return uint8_t(k | m_s.digit);                // bit 7: cassette in (silent)
}

uint8_t AcornSystem1Core::read(uint16_t a)
{
    if (a < 0x400) return m_s.ram[a];
    if (a >= 0xf800) return m_rom[a & 0x1ff];
    if ((a & 0xfe00) == 0x0e00) {
        uint8_t off = a & 0xff;
        if (off >= 0x80) return m_s.ioRam[off & 0x7f];
        if (off > 0x24) return 0xff;
        if (off == 0x20) return portAIn();
        if (off == 0x21) return 0xff;                  // port B has no inputs
        if (off < 0x08) return (portAIn() & ~m_s.odra & (1 << off)) ? 0x80 : 0;
        return 0;
    }
    return 0xff;
}

void AcornSystem1Core::write(uint16_t a, uint8_t d)
{
    if (a < 0x400) { m_s.ram[a] = d; return; }
    if ((a & 0xfe00) != 0x0e00) return;
    uint8_t off = a & 0xff;
    if (off >= 0x80) { m_s.ioRam[off & 0x7f] = d; return; }
    auto setA = [&](uint8_t v) {
        m_s.outA = v;
        if (m_s.odra) {
            uint8_t o = uint8_t((v & m_s.odra) | (m_s.odra ^ 0xff));
            m_s.digit = o & 0x47;
        }
    };
    switch (off) {
    case 0x20: setA(d); return;
    case 0x21: m_s.outB = d; return;
    case 0x22: m_s.odra = d; return;
    case 0x23: m_s.odrb = d; return;
    case 0x24: m_s.mdr = d; return;
    default: break;
    }
    if (off > 0x24) return;
    // single-bit set (0x10-0x1f) / clear (0x00-0x0f); like MAME, the set
    // range only reaches port B
    int bit = off & 7;
    if (off & 0x10) m_s.outB |= uint8_t(1 << bit);
    else if (off < 0x08) setA(uint8_t(m_s.outA & ~(1 << bit)));
    else m_s.outB &= uint8_t(~(1 << bit));
}

void AcornSystem1Core::runFrame()
{
    const int perFrame = CPU_HZ / 60;
    int done = 0;
    while (done < perFrame) {
        int n = m_cpu.step();
        done += n;
        // the selected digit shows port B's segments (weighted by time)
        uint8_t seg = portBOut();
        int dgt = m_s.digit & 7;
        for (int s = 0; s < 8; s++) if (seg >> s & 1) m_acc[dgt * 8 + s] += uint32_t(n);
        m_slices += uint32_t(n);
    }
    // one scan of the eight digits takes ~19 ms, longer than a frame: show
    // the average of the last three frames (the LEDs' 1/8 duty is full on)
    std::memmove(m_hist[1], m_hist[0], sizeof(m_hist[0]) * 2);
    std::memcpy(m_hist[0], m_acc, sizeof(m_acc));
    m_histCycles[2] = m_histCycles[1]; m_histCycles[1] = m_histCycles[0]; m_histCycles[0] = m_slices;
    uint32_t total = m_histCycles[0] + m_histCycles[1] + m_histCycles[2];
    for (int i = 0; i < 64; i++) {
        uint64_t on = uint64_t(m_hist[0][i]) + m_hist[1][i] + m_hist[2][i];
        m_out[i] = uint32_t(std::min<uint64_t>(255, total ? on * 255 * 8 / total : 0));
    }
    std::memset(m_acc, 0, sizeof(m_acc));
    m_slices = 0;
    m_audioDue += 735;
}

size_t AcornSystem1Core::readAudio(int16_t* out, size_t maxSamples)
{
    size_t n = std::min<size_t>(maxSamples, m_audioDue);
    std::memset(out, 0, n * sizeof(int16_t));
    m_audioDue -= uint32_t(n);
    return n;
}

namespace { struct CpuRegs { uint16_t pc; uint8_t a, x, y, s, p, irq, nmi; }; }

bool AcornSystem1Core::saveState(std::vector<uint8_t>& out)
{
    out.clear();
    uint32_t magic = 0x31534341;   // 'ACS1'
    CpuRegs c = { m_cpu.pc, m_cpu.a, m_cpu.x, m_cpu.y, m_cpu.s, m_cpu.p, m_cpu.m_irqLine, m_cpu.m_nmiPending };
    auto put = [&](const void* p, size_t n) { const uint8_t* b = (const uint8_t*)p; out.insert(out.end(), b, b + n); };
    put(&magic, 4); put(&m_s, sizeof(m_s)); put(&c, sizeof(c));
    return true;
}

bool AcornSystem1Core::loadState(const std::vector<uint8_t>& in)
{
    CpuRegs c;
    if (in.size() != 4 + sizeof(m_s) + sizeof(c)) return false;
    uint32_t magic; std::memcpy(&magic, in.data(), 4);
    if (magic != 0x31534341) return false;
    std::memcpy(&m_s, in.data() + 4, sizeof(m_s));
    std::memcpy(&c, in.data() + 4 + sizeof(m_s), sizeof(c));
    m_cpu.pc = c.pc; m_cpu.a = c.a; m_cpu.x = c.x; m_cpu.y = c.y; m_cpu.s = c.s; m_cpu.p = c.p;
    m_cpu.m_irqLine = c.irq; m_cpu.m_nmiPending = c.nmi;
    return true;
}
