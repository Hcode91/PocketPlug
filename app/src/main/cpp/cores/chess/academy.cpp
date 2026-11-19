// PocketPlug - Mephisto Academy chess computer
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Port of MAME's academy.cpp (Sandro Ronco, hap). See academy.h.

#include "academy.h"
#include <algorithm>
#include <cstring>

namespace {
constexpr int CPU_HZ = 4915200;
constexpr int NMI_PERIOD = 0x2000;             // 4.9152 MHz / 0x2000 = 600 Hz
constexpr int AUDIO_HZ = 44100;
}

AcademyCore::AcademyCore(std::string rom16, std::string rom32)
    : m_rom16(std::move(rom16)), m_rom32(std::move(rom32)), m_cpu(this)
{
    std::memset(m_cgrom, 0, sizeof(m_cgrom));
    std::memset(m_out, 0, sizeof(m_out));
    std::memset(&m_s, 0, sizeof(m_s));
    std::memset(m_ledAcc, 0, sizeof(m_ledAcc));
    m_lcd.setCgrom(m_cgrom);
    m_lcd.setClockRatio(double(CPU_HZ) / 270000.0);   // OSC = 91K resistor
}

bool AcademyCore::load(const std::vector<RomFile>& roms, std::string& error)
{
    m_rom.assign(0x10000, 0xff);
    bool have16 = false, have32 = false, haveCg = false;
    for (auto& r : roms) {
        if (r.name == m_rom16 && r.data.size() == 0x8000) { std::memcpy(&m_rom[0], r.data.data(), 0x8000); have16 = true; }
        else if (r.name == m_rom32 && r.data.size() == 0x8000) { std::memcpy(&m_rom[0x8000], r.data.data(), 0x8000); have32 = true; }
        else if (r.name == "hd44780_a00.bin" && r.data.size() == 0x1000) { std::memcpy(m_cgrom, r.data.data(), 0x1000); haveCg = true; }
    }
    if (!have16 || !have32) { error = "Missing the Academy ROMs (" + m_rom16 + ", " + m_rom32 + ")"; return false; }
    if (!haveCg) { error = "Missing hd44780_a00.bin (the LCD's character ROM, from MAME's hd44780 zip)"; return false; }
    std::memset(m_s.ram, 0, sizeof(m_s.ram));   // NVRAM starts cleared
    m_s.board.init(true, CPU_HZ * 15 / 100);    // 150 ms sensor delay
    reset();
    return true;
}

void AcademyCore::reset()
{
    m_s.mux = 0xff;
    m_s.ledData = 0;
    m_s.latch = 0;
    m_s.keyLed = 0;
    m_s.nmiCount = NMI_PERIOD;
    m_lcd.reset();
    m_cpu.p = 0x34;
    m_cpu.reset();
}

uint8_t AcademyCore::read(uint16_t a)
{
    if (a < 0x2000) return m_s.ram[a];
    if (a >= 0x4000) return m_rom[a];
    if (a == 0x2400) {
        // board sensors (selected ranks), and the keys when 74259 Q1 is low
        uint8_t data = (m_s.latch & 0x02) ? 0 : m_keys;
        uint8_t b = 0xff;
        for (int i = 0; i < 8; i++) if (!(m_s.mux >> i & 1)) b &= uint8_t(~m_s.board.readRank(i));
        return uint8_t(~b | data);
    }
    if (a == 0x3800 || a == 0x3801) return m_lcd.read(a & 1);
    return 0xff;
}

void AcademyCore::write(uint16_t a, uint8_t d)
{
    if (a < 0x2000) { m_s.ram[a] = d; return; }
    switch (a) {
    case 0x2800: m_s.mux = d; return;
    case 0x2c00: m_s.ledData = d; return;
    case 0x3400: m_s.keyLed = d; return;
    case 0x3800: case 0x3801: m_lcd.write(a & 1, d); return;
    default: break;
    }
    if (a >= 0x3000 && a <= 0x3007) {
        // 74259 addressable latch, data on d7
        int bit = a & 7;
        m_s.latch = uint8_t((m_s.latch & ~(1 << bit)) | ((d >> 7 & 1) << bit));
    }
}

void AcademyCore::sampleOutputs()
{
    // board LEDs: ~mux selects the rank (as for the sensors), data the file
    for (int r = 0; r < 8; r++) {
        if (m_s.mux >> r & 1) continue;
        for (int f = 0; f < 8; f++) if (m_s.ledData >> f & 1) m_ledAcc[r * 8 + f]++;
    }
    // key LEDs: 4x4 matrix, rows = d0-d3, columns = ~d4-d7
    for (int y = 0; y < 4; y++) {
        if (!(m_s.keyLed >> y & 1)) continue;
        for (int x = 0; x < 4; x++) if (!(m_s.keyLed >> (4 + x) & 1)) m_ledAcc[64 + y * 4 + x]++;
    }
    m_slices++;
    // 2-bit ones' complement DAC on 74259 Q2-Q3
    static const int16_t level[4] = { 0, 6000, -6000, 0 };
    m_audio.push_back(level[(m_s.latch >> 2) & 3]);
}

void AcademyCore::runFrame()
{
    const int perFrame = CPU_HZ / 60;
    int done = 0;
    double perSample = double(CPU_HZ) / AUDIO_HZ;
    while (done < perFrame) {
        m_carry += perSample;
        int slice = int(m_carry);
        m_carry -= slice;
        int ran = 0;
        while (ran < slice) {
            int step = std::min(slice - ran, m_s.nmiCount);
            int n = m_cpu.run(step);
            ran += n;
            m_s.nmiCount -= n;
            m_lcd.tick(n);
            m_lcd.blinkTick(n);
            m_s.board.tick(n);
            if (m_s.nmiCount <= 0) { m_s.nmiCount += NMI_PERIOD; m_cpu.nmi(); }
        }
        done += ran;
        sampleOutputs();
    }

    // status block for the skin
    std::memset(m_out, 0, sizeof(m_out));
    for (int i = 0; i < 64; i++) m_out[i] = m_s.board.piece[i];
    m_out[64] = m_s.board.hand;
    m_out[65] = uint32_t(m_s.board.handPos + 1);
    for (int i = 0; i < 64; i++) m_out[66 + i] = std::min<uint32_t>(255, m_slices ? m_ledAcc[i] * 255 * 8 / m_slices : 0);
    for (int k = 0; k < 8; k++) {
        int y = k & 3, xr = (k < 4) ? 0 : 2;
        auto b = [&](int x) { return std::min<uint32_t>(255, m_slices ? m_ledAcc[64 + y * 4 + x] * 255 * 4 / m_slices : 0); };
        m_out[130 + k] = b(xr);
        m_out[138 + k] = b(xr + 1);
    }
    uint8_t lcd[2 * 16 * 8];
    m_lcd.render(lcd, 2, 16);
    for (int i = 0; i < 256; i++) m_out[160 + i] = lcd[i];
    std::memset(m_ledAcc, 0, sizeof(m_ledAcc));
    m_slices = 0;
}

size_t AcademyCore::readAudio(int16_t* out, size_t maxSamples)
{
    size_t n = std::min(maxSamples, m_audio.size());
    std::memcpy(out, m_audio.data(), n * sizeof(int16_t));
    m_audio.erase(m_audio.begin(), m_audio.begin() + long(n));
    return n;
}

bool AcademyCore::saveState(std::vector<uint8_t>& out)
{
    out.clear();
    uint32_t magic = 0x41434144;   // 'DACA'
    auto put = [&](const void* p, size_t n) { const uint8_t* b = (const uint8_t*)p; out.insert(out.end(), b, b + n); };
    put(&magic, 4);
    put(&m_s, sizeof(m_s));
    put(&m_lcd.s, sizeof(m_lcd.s));
    struct { uint16_t pc; uint8_t a, x, y, s, p, wait, stop, irq, nmi; } c =
        { m_cpu.pc, m_cpu.a, m_cpu.x, m_cpu.y, m_cpu.s, m_cpu.p, m_cpu.waiting, m_cpu.stopped, m_cpu.m_irqLine, m_cpu.m_nmiPending };
    put(&c, sizeof(c));
    return true;
}

bool AcademyCore::loadState(const std::vector<uint8_t>& in)
{
    struct { uint16_t pc; uint8_t a, x, y, s, p, wait, stop, irq, nmi; } c;
    if (in.size() != 4 + sizeof(m_s) + sizeof(m_lcd.s) + sizeof(c)) return false;
    uint32_t magic; std::memcpy(&magic, in.data(), 4);
    if (magic != 0x41434144) return false;
    const uint8_t* p = in.data() + 4;
    std::memcpy(&m_s, p, sizeof(m_s)); p += sizeof(m_s);
    std::memcpy(&m_lcd.s, p, sizeof(m_lcd.s)); p += sizeof(m_lcd.s);
    std::memcpy(&c, p, sizeof(c));
    m_cpu.pc = c.pc; m_cpu.a = c.a; m_cpu.x = c.x; m_cpu.y = c.y; m_cpu.s = c.s; m_cpu.p = c.p;
    m_cpu.waiting = c.wait; m_cpu.stopped = c.stop; m_cpu.m_irqLine = c.irq; m_cpu.m_nmiPending = c.nmi;
    return true;
}
