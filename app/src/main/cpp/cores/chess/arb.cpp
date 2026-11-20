// PocketPlug - ARB V2 (port of MAME's chess/ave_arb.cpp). See arb.h.
// SPDX-License-Identifier: GPL-2.0-or-later

#include "arb.h"
#include <algorithm>
#include <cstring>

namespace {
constexpr int CPU_HZ = 16000000;
constexpr int AUDIO_HZ = 44100;
}

ArbV2Core::ArbV2Core() : m_cpu(this)
{
    std::memset(m_out, 0, sizeof(m_out));
    std::memset(&m_s, 0, sizeof(m_s));
    std::memset(m_acc, 0, sizeof(m_acc));
    m_via.outA = [this](uint8_t d) { m_s.ledLatch = uint8_t(~d); updateDisplay(); };
    m_via.outB = [this](uint8_t d) {
        m_s.inpMux = d & 0xf;
        m_s.ledSelect = uint16_t((1u << (d & 0xf)) & 0x1ff);
        m_s.ledGroup = d >> 4 & 3;
        updateDisplay();
        m_s.spk = d >> 7 & 1;
    };
    m_via.inA = [this]() -> uint8_t {
        uint8_t data = 0;
        if (m_s.inpMux < 8) {
            // read_file(x): the squares of file x, bit n = rank n + 1
            for (int rk = 0; rk < 8; rk++) if (m_s.board.sensor(m_s.inpMux, rk)) data |= uint8_t(1 << rk);
        }
        else if (m_s.inpMux == 8) data = uint8_t(m_keys);
        return uint8_t(~data);
    };
    m_via.inB = []() -> uint8_t { return 0xff; };
    m_via.irqOut = [this](bool on) { m_cpu.setIrq(on); };
}

bool ArbV2Core::load(const std::vector<RomFile>& roms, std::string& error)
{
    m_rom.clear();
    for (auto& r : roms) if (r.name == "sargon_4.0" && r.data.size() == 0x10000) m_rom = r.data;
    if (m_rom.empty()) { error = "Missing sargon_4.0"; return false; }
    std::memset(m_s.ram, 0, sizeof(m_s.ram));
    m_s.board.init(true, CPU_HZ / 10);    // 100 ms sensor delay
    reset();
    return true;
}

void ArbV2Core::reset()
{
    m_via.reset();
    m_cpu.reset();
}

void ArbV2Core::setInputs(uint32_t pressed)
{
    // HALT is tied to NMI; RESET only reaches the CPU while HALT is held
    if ((pressed & 0x200) && !(m_keys & 0x200)) m_cpu.nmi();
    bool rst = (pressed & 0x300) == 0x300;
    if (rst) { m_via.reset(); m_cpu.reset(); }
    m_inReset = rst;
    m_keys = pressed;
}

void ArbV2Core::updateDisplay()
{
    uint16_t mask = 0;
    if (m_s.ledGroup & 1) mask |= 0xf00;
    if (m_s.ledGroup & 2) mask |= 0x0ff;
    m_s.ledData = uint16_t((m_s.ledData & ~mask) | ((m_s.ledLatch << 8 | m_s.ledLatch) & mask));
}

uint8_t ArbV2Core::read(uint16_t a)
{
    if (a < 0x8000) return m_s.ram[a];
    if (a < 0xa000) return m_via.read(a & 15);
    return m_rom[a];
}

void ArbV2Core::write(uint16_t a, uint8_t d)
{
    if (a < 0x8000) { m_s.ram[a] = d; return; }
    if (a < 0xa000) m_via.write(a & 15, d);
}

void ArbV2Core::runFrame()
{
    const int perFrame = CPU_HZ / 60;
    const double perSample = double(CPU_HZ) / AUDIO_HZ;
    int done = 0;
    while (done < perFrame) {
        m_carry += perSample;
        int slice = int(m_carry);
        m_carry -= slice;
        int ran = 0;
        while (ran < slice) {
            int n = m_inReset ? slice - ran : m_cpu.run(std::min(slice - ran, 64));
            m_via.tick(n);
            m_s.board.tick(n);
            ran += n;
        }
        done += ran;
        uint16_t rows = m_s.ledSelect | 0x200;
        for (int r = 0; r < 10; r++) {
            if (!(rows >> r & 1)) continue;
            for (int c = 0; c < 12; c++) if (m_s.ledData >> c & 1) m_acc[r * 12 + c]++;
        }
        m_slices++;
        m_audio.push_back(m_s.spk ? 6000 : 0);
    }
    std::memset(m_out, 0, sizeof(m_out));
    for (int i = 0; i < 64; i++) m_out[i] = m_s.board.piece[i];
    m_out[64] = m_s.board.hand;
    m_out[65] = uint32_t(m_s.board.handPos + 1);
    auto lvl = [&](int r, int c, int mul) { return std::min<uint32_t>(255, m_slices ? m_acc[r * 12 + c] * 255 * uint32_t(mul) / m_slices : 0); };
    // square LEDs: row = file, column = rank (MAME "file.rank")
    for (int f = 0; f < 8; f++) for (int rk = 0; rk < 8; rk++) m_out[66 + rk * 8 + f] = lvl(f, rk, 9);
    for (int i = 0; i < 8; i++) m_out[130 + i] = lvl(8, 7 - i, 9);   // keys 0-6, W, B
    for (int i = 0; i < 4; i++) m_out[138 + i] = lvl(9, 11 - i, 2);  // Your Move, Sargon's Move, Check, Change Board
    std::memset(m_acc, 0, sizeof(m_acc));
    m_slices = 0;
    if (m_audio.size() > 44100) m_audio.erase(m_audio.begin(), m_audio.end() - 4410);
}

size_t ArbV2Core::readAudio(int16_t* out, size_t maxSamples)
{
    size_t n = std::min(maxSamples, m_audio.size());
    std::memcpy(out, m_audio.data(), n * sizeof(int16_t));
    m_audio.erase(m_audio.begin(), m_audio.begin() + long(n));
    return n;
}

bool ArbV2Core::saveState(std::vector<uint8_t>& out)
{
    out.clear();
    uint32_t magic = 0x32425241;   // 'ARB2'
    auto put = [&](const void* p, size_t n) { const uint8_t* b = (const uint8_t*)p; out.insert(out.end(), b, b + n); };
    put(&magic, 4);
    put(&m_s, sizeof(m_s));
    put(&m_via.s, sizeof(m_via.s));
    struct { uint16_t pc; uint8_t a, x, y, s, p, wait, stop, irq, nmi; } c =
        { m_cpu.pc, m_cpu.a, m_cpu.x, m_cpu.y, m_cpu.s, m_cpu.p, m_cpu.waiting, m_cpu.stopped, m_cpu.m_irqLine, m_cpu.m_nmiPending };
    put(&c, sizeof(c));
    return true;
}

bool ArbV2Core::loadState(const std::vector<uint8_t>& in)
{
    struct { uint16_t pc; uint8_t a, x, y, s, p, wait, stop, irq, nmi; } c;
    if (in.size() != 4 + sizeof(m_s) + sizeof(m_via.s) + sizeof(c)) return false;
    uint32_t magic; std::memcpy(&magic, in.data(), 4);
    if (magic != 0x32425241) return false;
    const uint8_t* p = in.data() + 4;
    std::memcpy(&m_s, p, sizeof(m_s)); p += sizeof(m_s);
    std::memcpy(&m_via.s, p, sizeof(m_via.s)); p += sizeof(m_via.s);
    std::memcpy(&c, p, sizeof(c));
    m_cpu.pc = c.pc; m_cpu.a = c.a; m_cpu.x = c.x; m_cpu.y = c.y; m_cpu.s = c.s; m_cpu.p = c.p;
    m_cpu.waiting = c.wait; m_cpu.stopped = c.stop; m_cpu.m_irqLine = c.irq; m_cpu.m_nmiPending = c.nmi;
    return true;
}
