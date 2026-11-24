// PocketPlug - Mephisto Almeria 16 Bit / 32 Bit chess modules
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Port of MAME's modular.cpp (alm16 / alm32 memory maps). See almeria.h.

#include "almeria.h"
#include <algorithm>
#include <cstring>

extern "C" {
#include "../unixpc/freebee/musashi/m68k.h"
#include "../unixpc/freebee/m68k_bus.h"
}

namespace {
constexpr int CPU_HZ = 12288000;
constexpr int AUDIO_HZ = 44100;

AlmeriaCore* g_core = nullptr;

unsigned int b_r8(unsigned int a) { return g_core->read8(a); }
unsigned int b_r16(unsigned int a) { return g_core->read16(a); }
unsigned int b_r32(unsigned int a) { return g_core->read32(a); }
void b_w8(unsigned int a, unsigned int v) { g_core->write8(a, uint8_t(v)); }
void b_w16(unsigned int a, unsigned int v) { g_core->write16(a, uint16_t(v)); }
void b_w32(unsigned int a, unsigned int v) { g_core->write32(a, v); }
int b_ack(int level) { return g_core->intAck(level); }

const M68kBus kBus = { b_r8, b_r16, b_r32, b_w8, b_w16, b_w32, b_ack };

// saved CPU registers (set back in this order, see loadState)
const m68k_register_t kRegs[] = {
    M68K_REG_SR, M68K_REG_USP, M68K_REG_ISP, M68K_REG_MSP,
    M68K_REG_D0, M68K_REG_D1, M68K_REG_D2, M68K_REG_D3, M68K_REG_D4, M68K_REG_D5, M68K_REG_D6, M68K_REG_D7,
    M68K_REG_A0, M68K_REG_A1, M68K_REG_A2, M68K_REG_A3, M68K_REG_A4, M68K_REG_A5, M68K_REG_A6, M68K_REG_A7,
    M68K_REG_VBR, M68K_REG_SFC, M68K_REG_DFC, M68K_REG_CACR, M68K_REG_CAAR, M68K_REG_PPC, M68K_REG_IR, M68K_REG_PC };
constexpr int kNumRegs = int(sizeof(kRegs) / sizeof(kRegs[0]));
}

AlmeriaCore::AlmeriaCore(bool is32, std::string romEven, std::string romOdd, bool berlin)
    : m_is32(is32), m_berlin(berlin), m_romEven(std::move(romEven)), m_romOdd(std::move(romOdd))
{
    std::memset(m_cgrom, 0, sizeof(m_cgrom));
    std::memset(m_out, 0, sizeof(m_out));
    std::memset(&m_s, 0, sizeof(m_s));
    std::memset(m_ledAcc, 0, sizeof(m_ledAcc));
    m_ram.assign(is32 ? 0x100000 : 0x80000, 0);
    m_ramMask = uint32_t(m_ram.size() - 1);
    // 16 bit: 12.288 MHz / 10 / 0x800 = 600 Hz; 32 bit: 12.288 MHz / 0x4000 = 750 Hz
    m_irqPeriod = is32 ? 0x4000 : 10 * 0x800;
    m_hz = CPU_HZ;
    m_irqLevel = 3;
    if (berlin) {
        // Berlin 68000: 12.288 MHz / 0x4000; Professional: 68EC020 at 24.576 MHz / 0x8000 (both 750 Hz)
        m_hz = is32 ? 24576000 : CPU_HZ;
        m_irqPeriod = is32 ? 0x8000 : 0x4000;
        m_irqLevel = 2;
    }
    m_lcd.setCgrom(m_cgrom);
    m_lcd.setClockRatio(double(m_hz) / 270000.0);   // OSC = 91K resistor
}

AlmeriaCore::~AlmeriaCore()
{
    if (g_core == this) { m68k_bus_set(nullptr); g_core = nullptr; }
}

bool AlmeriaCore::load(const std::vector<RomFile>& roms, std::string& error)
{
    size_t romSize = (m_berlin && m_is32) ? 0x40000 : 0x20000;
    m_rom.assign(romSize, 0xff);
    bool haveEven = false, haveOdd = m_romOdd.empty(), haveCg = false;
    for (auto& r : roms) {
        if (m_romOdd.empty() && r.name == m_romEven && r.data.size() == romSize) {
            std::memcpy(m_rom.data(), r.data.data(), romSize); haveEven = true;
        } else if (!m_romOdd.empty() && r.name == m_romEven && r.data.size() == 0x10000) {
            for (int i = 0; i < 0x10000; i++) m_rom[2 * i] = r.data[i];
            haveEven = true;
        } else if (!m_romOdd.empty() && r.name == m_romOdd && r.data.size() == 0x10000) {
            for (int i = 0; i < 0x10000; i++) m_rom[2 * i + 1] = r.data[i];
            haveOdd = true;
        } else if (r.name == "hd44780_a00.bin" && r.data.size() == 0x1000) {
            std::memcpy(m_cgrom, r.data.data(), 0x1000); haveCg = true;
        }
    }
    if (!haveEven || !haveOdd) {
 error = std::string(m_berlin ? "Missing the Berlin ROM" : "Missing the Almeria ROM") + std::string(m_romOdd.empty() ? " (" + m_romEven + ")" : "s (" + m_romEven + ", " + m_romOdd + ")");
        return false;
    }
    if (!haveCg) { error = "Missing hd44780_a00.bin (the LCD's character ROM, from MAME's hd44780 zip)"; return false; }
    std::memset(m_s.nvram, 0, sizeof(m_s.nvram));   // battery RAM starts cleared
    m_s.board.init(!m_berlin, m_hz * 15 / 100);      // 150 ms sensor delay (Berlin: buttons)
    g_core = this;
    m68k_bus_set(&kBus);
    m68k_init();
    m68k_set_cpu_type(m_is32 ? M68K_CPU_TYPE_68020 /* the EC020 is a 68020 on a 24-bit bus: addresses are masked here */ : M68K_CPU_TYPE_68000);
    reset();
    return true;
}

void AlmeriaCore::reset()
{
    m_s.mux = 0xff;
    m_s.ledData = 0;
    m_s.latch = 0;
    m_s.ctrl = 0;
    m_s.irqLine = 0;
    m_s.irqCount = m_irqPeriod;
    std::fill(m_ram.begin(), m_ram.end(), 0);
    m_lcd.reset();
    g_core = this;
    m68k_bus_set(&kBus);
    m68k_set_irq(0);
    m68k_pulse_reset();
}

// ---- bus ----

uint8_t AlmeriaCore::keyByte(int port) const
{
    // which of my input bits (0 ENT, 1 UP, 2 CL, 3 LEFT, 4 RIGHT, 5 DOWN) a port's two keys are
    static const int k16[3][2] = { { 3, 0 }, { 4, 1 }, { 5, 2 } };   // KEY1-3: d0, d1 (active high)
    static const int k32[3][2] = { { 4, 2 }, { 5, 1 }, { 3, 0 } };   // KEY1-3: d6, d7 (active low)
    if (!m_is32) {
        return uint8_t((m_keys >> k16[port][0] & 1) | (m_keys >> k16[port][1] & 1) << 1);
    }
    uint8_t v = uint8_t((m_keys >> k32[port][0] & 1) << 6 | (m_keys >> k32[port][1] & 1) << 7);
    return uint8_t(~v & 0xc0);
}

uint8_t AlmeriaCore::boardInput() const
{
    uint8_t b = 0xff;
    for (int i = 0; i < 8; i++) if (!(m_s.mux >> i & 1)) b &= uint8_t(~m_s.board.readRank(i));
    return b;
}

void AlmeriaCore::displayIo(uint8_t d)
{
    if ((d & 2) && !(m_s.ctrl & 2)) m_lcd.write(d & 1, m_s.latch);
    m_s.ctrl = d;
}

uint8_t AlmeriaCore::read8(uint32_t a)
{
    if (m_berlin) {
        a &= 0xffffff;
        if (a < m_rom.size()) return m_rom[a];
        // keypad (Enter, Clear, Up, Down, Left, Right, New Game 1/2) when display i/o d7 is low
        uint8_t keys = 0;
        if (!(m_s.ctrl & 0x80))
            keys = uint8_t((m_keys & 1) | (m_keys >> 2 & 1) << 1 | (m_keys >> 1 & 1) << 2 | (m_keys >> 5 & 1) << 3
                | (m_keys >> 3 & 1) << 4 | (m_keys >> 4 & 1) << 5 | ((m_keys >> 6 & 1) ? 0xc0 : 0));
        uint8_t input = uint8_t(~boardInput() | keys);
        if (!m_is32) {
            if (a >= 0x800000 && a < 0x880000) return m_ram[a & m_ramMask];
            if (a >= 0x900000 && a < 0x904000) return (a & 1) ? 0 : m_s.nvram[(a - 0x900000) >> 1];
            if (a == 0xa00000) return input;
            return 0;
        }
        if (a >= 0x400000 && a < 0x500000) return m_ram[a & m_ramMask];
        if (a >= 0xd00000 && a < 0xd08000) return (a & 3) ? 0 : m_s.nvram[(a - 0xd00000) >> 2];
        if (a == 0x800000) return input;
        return 0;
    }
    if (a < 0x20000) return m_rom[a];
    if (!m_is32) {
        a &= 0xffffff;
        if (a >= 0x400000 && a < 0x480000) return m_ram[a & m_ramMask];
        if (a >= 0x800000 && a < 0x804000) return (a & 1) ? 0 : m_s.nvram[(a - 0x800000) >> 1];
        if (a == 0xc00000) return boardInput();
        if (a >= 0xf00000 && a < 0xf0000c) return (a & 1) ? 0 : keyByte(int((a - 0xf00000) >> 2));
        return 0;
    }
    if (a >= 0x40000000 && a < 0x40100000) return m_ram[a & m_ramMask];
    if (a >= 0xa8000000 && a < 0xa8008000) return (a & 3) ? 0 : m_s.nvram[(a - 0xa8000000) >> 2];
    if (a == 0x800000fc) return boardInput();
    if (a == 0x800000ee) return keyByte(0);
    if (a == 0x800000f6) return keyByte(1);
    if (a == 0x800000fa) return keyByte(2);
    return 0;
}

void AlmeriaCore::write8(uint32_t a, uint8_t d)
{
    if (m_berlin) {
        a &= 0xffffff;
        if (!m_is32) {
            if (a >= 0x800000 && a < 0x880000) { m_ram[a & m_ramMask] = d; return; }
            if (a >= 0x900000 && a < 0x904000) { if (!(a & 1)) m_s.nvram[(a - 0x900000) >> 1] = d; return; }
            switch (a) {
            case 0xb00000: m_s.mux = d; return;
            case 0xc00000: m_s.latch = d; return;
            case 0xd00008: displayIo(d); return;
            case 0xe00000: m_s.ledData = d; return;
            default: return;
            }
        }
        if (a >= 0x400000 && a < 0x500000) { m_ram[a & m_ramMask] = d; return; }
        if (a >= 0xd00000 && a < 0xd08000) { if (!(a & 3)) m_s.nvram[(a - 0xd00000) >> 2] = d; return; }
        switch (a) {
        case 0x900000: m_s.mux = d; return;
        case 0xa00000: m_s.ledData = d; return;
        case 0xb00000: displayIo(d); return;
        case 0xc00000: m_s.latch = d; return;
        default: return;
        }
    }
    if (!m_is32) {
        a &= 0xffffff;
        if (a >= 0x400000 && a < 0x480000) { m_ram[a & m_ramMask] = d; return; }
        if (a >= 0x800000 && a < 0x804000) { if (!(a & 1)) m_s.nvram[(a - 0x800000) >> 1] = d; return; }
        switch (a) {
        case 0xc80000: m_s.mux = d; return;
        case 0xd00000: m_s.ledData = d; return;
        case 0xd80000: m_s.latch = d; return;
        case 0xd80008: displayIo(d); return;
        default: return;
        }
    }
    if (a >= 0x40000000 && a < 0x40100000) { m_ram[a & m_ramMask] = d; return; }
    if (a >= 0xa8000000 && a < 0xa8008000) { if (!(a & 3)) m_s.nvram[(a - 0xa8000000) >> 2] = d; return; }
    switch (a) {
    case 0x88000000: case 0x88000004: m_s.mux = d; return;
    case 0x90000000: case 0x90000004: m_s.ledData = d; return;
    case 0xa0000000: m_s.latch = d; return;
    case 0xa0000010: displayIo(d); return;
    default: return;
    }
}

uint16_t AlmeriaCore::read16(uint32_t a)
{
    if (m_berlin) {
        uint32_t r = a & 0xffffff;
        if (r < m_rom.size() - 1) return uint16_t(m_rom[r] << 8 | m_rom[r + 1]);
        if (m_is32 ? (r >= 0x400000 && r < 0x4fffff) : (r >= 0x800000 && r < 0x87ffff))
            return uint16_t(m_ram[r & m_ramMask] << 8 | m_ram[(r + 1) & m_ramMask]);
        return uint16_t(read8(a) << 8 | read8(a + 1));
    }
    if (a < 0x20000 - 1) return uint16_t(m_rom[a] << 8 | m_rom[a + 1]);
    uint32_t r = m_is32 ? a : (a & 0xffffff);
    if (m_is32 ? (r >= 0x40000000 && r < 0x400fffff) : (r >= 0x400000 && r < 0x47ffff))
        return uint16_t(m_ram[r & m_ramMask] << 8 | m_ram[(r + 1) & m_ramMask]);
    return uint16_t(read8(a) << 8 | read8(a + 1));
}

void AlmeriaCore::write16(uint32_t a, uint16_t d)
{
    write8(a, uint8_t(d >> 8));
    write8(a + 1, uint8_t(d));
}

uint32_t AlmeriaCore::read32(uint32_t a)
{
    return uint32_t(read16(a)) << 16 | read16(a + 2);
}

void AlmeriaCore::write32(uint32_t a, uint32_t d)
{
    write16(a, uint16_t(d >> 16));
    write16(a + 2, uint16_t(d));
}

int AlmeriaCore::intAck(int level)
{
    // irq3_line_hold: the line drops when the CPU takes the interrupt
    (void)level;
    m_s.irqLine = 0;
    m68k_set_irq(0);
    return M68K_INT_ACK_AUTOVECTOR;
}

// ---- running ----

void AlmeriaCore::sampleOutputs()
{
    for (int r = 0; r < 8; r++) {
        if (m_s.mux >> r & 1) continue;
        for (int f = 0; f < 8; f++) if (m_s.ledData >> f & 1) m_ledAcc[r * 8 + f]++;
    }
    m_slices++;
    // 2-bit ones' complement DAC on the display module's d2-d3
    static const int16_t level[4] = { 0, 6000, -6000, 0 };
    m_audio.push_back(level[(m_s.ctrl >> 2) & 3]);
}

void AlmeriaCore::runFrame()
{
    if (g_core != this) { g_core = this; m68k_bus_set(&kBus); }
    const int perFrame = m_hz / 60;
    int done = 0;
    const double perSample = double(m_hz) / AUDIO_HZ;
    while (done < perFrame) {
        m_carry += perSample;
        int slice = int(m_carry);
        m_carry -= slice;
        int ran = 0;
        while (ran < slice) {
            int step = std::min(slice - ran, int(m_s.irqCount));
            int n = m68k_execute(step);
            if (n <= 0) n = step;
            ran += n;
            m_s.irqCount -= n;
            m_lcd.tick(n);
            m_lcd.blinkTick(n);
            m_s.board.tick(n);
            if (m_s.irqCount <= 0) {
                m_s.irqCount += m_irqPeriod;
                m_s.irqLine = 1;
                m68k_set_irq(m_irqLevel);
            }
        }
        done += ran;
        sampleOutputs();
    }

    std::memset(m_out, 0, sizeof(m_out));
    for (int i = 0; i < 64; i++) m_out[i] = m_s.board.piece[i];
    m_out[64] = m_s.board.hand;
    m_out[65] = uint32_t(m_s.board.handPos + 1);
    for (int i = 0; i < 64; i++) m_out[66 + i] = std::min<uint32_t>(255, m_slices ? m_ledAcc[i] * 255 * 8 / m_slices : 0);
    uint8_t lcd[2 * 16 * 8];
    m_lcd.render(lcd, 2, 16);
    for (int i = 0; i < 256; i++) m_out[160 + i] = lcd[i];
    std::memset(m_ledAcc, 0, sizeof(m_ledAcc));
    m_slices = 0;
    if (m_audio.size() > 44100) m_audio.erase(m_audio.begin(), m_audio.end() - 4410);
}

size_t AlmeriaCore::readAudio(int16_t* out, size_t maxSamples)
{
    size_t n = std::min(maxSamples, m_audio.size());
    std::memcpy(out, m_audio.data(), n * sizeof(int16_t));
    m_audio.erase(m_audio.begin(), m_audio.begin() + long(n));
    return n;
}

// ---- save states ----

bool AlmeriaCore::saveState(std::vector<uint8_t>& out)
{
    out.clear();
    uint32_t magic = (m_is32 ? 0x32334c41 : 0x36314c41) ^ (m_berlin ? 0x00004200 : 0);   // 'AL32' / 'AL16'
    auto put = [&](const void* p, size_t n) { const uint8_t* b = (const uint8_t*)p; out.insert(out.end(), b, b + n); };
    put(&magic, 4);
    put(&m_s, sizeof(m_s));
    put(&m_lcd.s, sizeof(m_lcd.s));
    put(m_ram.data(), m_ram.size());
    uint32_t regs[kNumRegs];
    for (int i = 0; i < kNumRegs; i++) regs[i] = m68k_get_reg(nullptr, kRegs[i]);
    put(regs, sizeof(regs));
    return true;
}

bool AlmeriaCore::loadState(const std::vector<uint8_t>& in)
{
    uint32_t regs[kNumRegs];
    if (in.size() != 4 + sizeof(m_s) + sizeof(m_lcd.s) + m_ram.size() + sizeof(regs)) return false;
    uint32_t magic; std::memcpy(&magic, in.data(), 4);
    if (magic != ((m_is32 ? 0x32334c41u : 0x36314c41u) ^ (m_berlin ? 0x00004200u : 0u))) return false;
    const uint8_t* p = in.data() + 4;
    std::memcpy(&m_s, p, sizeof(m_s)); p += sizeof(m_s);
    std::memcpy(&m_lcd.s, p, sizeof(m_lcd.s)); p += sizeof(m_lcd.s);
    std::memcpy(m_ram.data(), p, m_ram.size()); p += m_ram.size();
    std::memcpy(regs, p, sizeof(regs));
    g_core = this;
    m68k_bus_set(&kBus);
    for (int i = 0; i < kNumRegs; i++) m68k_set_reg(kRegs[i], regs[i]);
    m68k_set_irq(m_s.irqLine ? m_irqLevel : 0);
    return true;
}
