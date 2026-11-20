// PocketPlug - Mephisto Amsterdam / Dallas / Roma (port of MAME's amsterdam.cpp). See mephmod1.h.
// SPDX-License-Identifier: GPL-2.0-or-later

#include "mephmod1.h"
#include <algorithm>
#include <cstring>

extern "C" {
#include "../unixpc/freebee/musashi/m68k.h"
#include "../unixpc/freebee/m68k_bus.h"
}

namespace {
constexpr int AUDIO_HZ = 44100;
MephMod1Core* g_core = nullptr;
unsigned int b_r8(unsigned int a) { return g_core->read8(a); }
unsigned int b_r16(unsigned int a) { return g_core->read16(a); }
unsigned int b_r32(unsigned int a) { return uint32_t(g_core->read16(a)) << 16 | g_core->read16(a + 2); }
void b_w8(unsigned int a, unsigned int v) { g_core->write8(a, uint8_t(v)); }
void b_w16(unsigned int a, unsigned int v) { g_core->write16(a, uint16_t(v)); }
void b_w32(unsigned int a, unsigned int v) { g_core->write16(a, uint16_t(v >> 16)); g_core->write16(a + 2, uint16_t(v)); }
int b_ack(int level) { return g_core->intAck(level); }
const M68kBus kBus = { b_r8, b_r16, b_r32, b_w8, b_w16, b_w32, b_ack };
const m68k_register_t kRegs[] = {
    M68K_REG_SR, M68K_REG_USP, M68K_REG_ISP, M68K_REG_MSP,
    M68K_REG_D0, M68K_REG_D1, M68K_REG_D2, M68K_REG_D3, M68K_REG_D4, M68K_REG_D5, M68K_REG_D6, M68K_REG_D7,
    M68K_REG_A0, M68K_REG_A1, M68K_REG_A2, M68K_REG_A3, M68K_REG_A4, M68K_REG_A5, M68K_REG_A6, M68K_REG_A7,
    M68K_REG_VBR, M68K_REG_SFC, M68K_REG_DFC, M68K_REG_CACR, M68K_REG_CAAR, M68K_REG_PPC, M68K_REG_IR, M68K_REG_PC };
constexpr int kNumRegs = int(sizeof(kRegs) / sizeof(kRegs[0]));
}

MephMod1Core::MephMod1Core(bool is32, std::string romHi, std::string romLo)
    : m_is32(is32), m_romHi(std::move(romHi)), m_romLo(std::move(romLo))
{
    std::memset(m_out, 0, sizeof(m_out));
    std::memset(&m_s, 0, sizeof(m_s));
    std::memset(m_ledAcc, 0, sizeof(m_ledAcc));
    m_hz = is32 ? 14000000 : 12000000;
    m_irqLevel = is32 ? 4 : 5;
    m_irqPeriod = m_hz / 50;
}

MephMod1Core::~MephMod1Core()
{
    if (g_core == this) { m68k_bus_set(nullptr); g_core = nullptr; }
}

bool MephMod1Core::load(const std::vector<RomFile>& roms, std::string& error)
{
    m_rom.assign(0x10000, 0xff);
    bool hi = false, lo = m_romLo.empty();
    for (auto& r : roms) {
        if (m_romLo.empty() && r.name == m_romHi && r.data.size() == 0x10000) { std::memcpy(m_rom.data(), r.data.data(), 0x10000); hi = true; }
        else if (!m_romLo.empty() && r.name == m_romHi && r.data.size() == 0x8000) { for (int i = 0; i < 0x8000; i++) m_rom[2 * i] = r.data[i]; hi = true; }
        else if (!m_romLo.empty() && r.name == m_romLo && r.data.size() == 0x8000) { for (int i = 0; i < 0x8000; i++) m_rom[2 * i + 1] = r.data[i]; lo = true; }
    }
    if (!hi || !lo) { error = "Missing the ROM" + std::string(m_romLo.empty() ? " (" + m_romHi + ")" : "s (" + m_romHi + ", " + m_romLo + ")"); return false; }
    m_s.board.init(true, int32_t(int64_t(m_hz) * 200 / 1000));   // 200 ms sensor delay
    m_s.lcd.init(4, 8);
    m_s.lcd.setLevel(0.75f);
    g_core = this;
    m68k_bus_set(&kBus);
    m68k_init();
    m68k_set_cpu_type(m_is32 ? M68K_CPU_TYPE_68020 : M68K_CPU_TYPE_68000);
    reset();
    return true;
}

void MephMod1Core::reset()
{
    std::memset(m_s.ram, 0, sizeof(m_s.ram));
    m_s.mux = 0xff; m_s.ledData = 0; m_s.common = 0; m_s.dac = 0; m_s.irqLine = 0;
    m_s.irqCount = m_irqPeriod;
    g_core = this;
    m68k_bus_set(&kBus);
    m68k_set_irq(0);
    m68k_pulse_reset();
}

void MephMod1Core::setInputs(uint32_t pressed)
{
    // the two RES keys in series pulse the CPU's reset
    if ((pressed & 0x30000) == 0x30000 && (m_keys & 0x30000) != 0x30000) { g_core = this; m68k_bus_set(&kBus); m68k_pulse_reset(); }
    m_keys = pressed;
}

uint8_t MephMod1Core::boardInput() const
{
    uint8_t b = 0xff;
    for (int i = 0; i < 8; i++) if (!(m_s.mux >> i & 1)) b &= uint8_t(~m_s.board.readRank(i));
    return b;
}

void MephMod1Core::updateLcd()
{
    uint32_t data = m_s.common ? ~m_s.digitData : m_s.digitData;
    for (int i = 0; i < 4; i++) m_s.lcd.writeRow(m_time + uint64_t(m68k_cycles_run()), i, data >> (i * 8) & 0xff);
}

uint8_t MephMod1Core::read8(uint32_t a)
{
    if (!m_is32) a &= 0xffffff;
    if (a < 0x10000) return m_rom[a];
    if (!m_is32 && a >= 0xffc000 && a <= 0xffffff) return m_s.ram[a & 0x3fff];
    if (m_is32 && a >= 0x10000 && a < 0x20000) return m_s.ram[a & 0xffff];
    switch (a) {
    case 0x800020: return boardInput();
    case 0x800040: return uint8_t(m_keys >> (m_s.common ? 8 : 0));   // the LCD common also selects the key row
    default: return 0;
    }
}

void MephMod1Core::write8(uint32_t a, uint8_t d)
{
    if (!m_is32) a &= 0xffffff;
    if (!m_is32 && a >= 0xffc000 && a <= 0xffffff) { m_s.ram[a & 0x3fff] = d; return; }
    if (m_is32 && a >= 0x10000 && a < 0x20000) { m_s.ram[a & 0xffff] = d; return; }
    switch (a) {
    case 0x800002: m_s.digitData = (m_s.digitData >> 8) | (uint32_t(d) << 24); updateLcd(); return;   // shifts the digits
    case 0x800004: m_s.dac = d & 1; return;
    case 0x800008: case 0x800088:
        m_s.ledData = d;                         // board LEDs
        m_s.common = (a & 0x80) ? 1 : 0;         // a7: LCD common
        updateLcd();
        return;
    case 0x800010: m_s.mux = d; return;
    default: return;
    }
}

uint16_t MephMod1Core::read16(uint32_t a)
{
    uint32_t r = m_is32 ? a : (a & 0xffffff);
    if (r < 0xffff) return uint16_t(m_rom[r] << 8 | m_rom[r + 1]);
    return uint16_t(read8(a) << 8 | read8(a + 1));
}

void MephMod1Core::write16(uint32_t a, uint16_t d)
{
    write8(a, uint8_t(d >> 8));
    write8(a + 1, uint8_t(d));
}

int MephMod1Core::intAck(int level)
{
    (void)level;
    m_s.irqLine = 0;
    m68k_set_irq(0);
    return M68K_INT_ACK_AUTOVECTOR;
}

void MephMod1Core::runFrame()
{
    if (g_core != this) { g_core = this; m68k_bus_set(&kBus); }
    const int perFrame = m_hz / 60;
    int done = 0;
    const double perSample = double(m_hz) / AUDIO_HZ;
    uint64_t start = m_time;
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
            m_time += uint64_t(n);
            m_s.irqCount -= n;
            m_s.board.tick(n);
            if (m_s.irqCount <= 0) { m_s.irqCount += m_irqPeriod; m_s.irqLine = 1; m68k_set_irq(m_irqLevel); }
        }
        done += ran;
        for (int r = 0; r < 8; r++) {
            if (m_s.mux >> r & 1) continue;
            for (int f = 0; f < 8; f++) if (m_s.ledData >> f & 1) m_ledAcc[r * 8 + f]++;
        }
        m_slices++;
        m_audio.push_back(m_s.dac ? 6000 : 0);
    }
    m_s.lcd.frame(m_time, uint32_t(m_time - start));

    std::memset(m_out, 0, sizeof(m_out));
    for (int i = 0; i < 64; i++) m_out[i] = m_s.board.piece[i];
    m_out[64] = m_s.board.hand;
    m_out[65] = uint32_t(m_s.board.handPos + 1);
    for (int i = 0; i < 64; i++) m_out[66 + i] = std::min<uint32_t>(255, m_slices ? m_ledAcc[i] * 255 * 8 / m_slices : 0);
    for (int d = 0; d < 4; d++) for (int sgm = 0; sgm < 8; sgm++) m_out[130 + d * 8 + sgm] = m_s.lcd.on(d, sgm) ? 1 : 0;
    std::memset(m_ledAcc, 0, sizeof(m_ledAcc));
    m_slices = 0;
    if (m_audio.size() > 44100) m_audio.erase(m_audio.begin(), m_audio.end() - 4410);
}

size_t MephMod1Core::readAudio(int16_t* out, size_t maxSamples)
{
    size_t n = std::min(maxSamples, m_audio.size());
    std::memcpy(out, m_audio.data(), n * sizeof(int16_t));
    m_audio.erase(m_audio.begin(), m_audio.begin() + long(n));
    return n;
}

bool MephMod1Core::saveState(std::vector<uint8_t>& out)
{
    out.clear();
    uint32_t magic = 0x314d4d48;   // 'HMM1'
    auto put = [&](const void* p, size_t n) { const uint8_t* b = (const uint8_t*)p; out.insert(out.end(), b, b + n); };
    put(&magic, 4);
    put(&m_s, sizeof(m_s));
    put(&m_time, sizeof(m_time));
    uint32_t regs[kNumRegs];
    for (int i = 0; i < kNumRegs; i++) regs[i] = m68k_get_reg(nullptr, kRegs[i]);
    put(regs, sizeof(regs));
    return true;
}

bool MephMod1Core::loadState(const std::vector<uint8_t>& in)
{
    uint32_t regs[kNumRegs];
    if (in.size() != 4 + sizeof(m_s) + sizeof(m_time) + sizeof(regs)) return false;
    uint32_t magic; std::memcpy(&magic, in.data(), 4);
    if (magic != 0x314d4d48) return false;
    const uint8_t* p = in.data() + 4;
    std::memcpy(&m_s, p, sizeof(m_s)); p += sizeof(m_s);
    std::memcpy(&m_time, p, sizeof(m_time)); p += sizeof(m_time);
    std::memcpy(regs, p, sizeof(regs));
    g_core = this;
    m68k_bus_set(&kBus);
    for (int i = 0; i < kNumRegs; i++) m68k_set_reg(kRegs[i], regs[i]);
    m68k_set_irq(m_s.irqLine ? m_irqLevel : 0);
    return true;
}
