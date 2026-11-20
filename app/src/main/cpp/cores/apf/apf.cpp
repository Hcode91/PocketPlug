// PocketPlug - APF M-1000 (MP1000) TV game
// SPDX-License-Identifier: BSD-3-Clause
//
// Port of MAME's apf.cpp (0.264, Robbbert) and bus/apf (Fabio Priuli). See apf.h.

#include "apf.h"
#include <cstring>
#include <cmath>
#include <algorithm>

static const double CPU_CLOCK = 3579545.0 / 4.0;

ApfCore::ApfCore() : m_cpu(this), m_vdg(this), m_pia(this)
{
    m_cpu.setPlain6800(true);
    m_vdg.setFixedMode(MC6847::MODE_GM2 | MC6847::MODE_GM1);
    std::memset(m_bios, 0xff, sizeof(m_bios));
    std::memset(&m_s, 0, sizeof(m_s));
    std::memset(m_frame, 0, sizeof(m_frame));
}

bool ApfCore::load(const std::vector<RomFile>& roms, std::string& error)
{
    bool bios = false;
    m_cart.clear();
    for (auto& r : roms) {
        if (r.name == "apf_4000.rom" && r.data.size() == 0x800) {
            // ROMX_LOAD at 0, ROM_RELOAD at 0x800
            std::memcpy(m_bios, r.data.data(), 0x800);
            std::memcpy(m_bios + 0x800, r.data.data(), 0x800);
            bios = true;
        } else if (!r.data.empty()) m_cart = r.data;
    }
    if (!bios) { error = "Missing the BIOS (apf_4000.rom from the apfm1000 zip)"; return false; }
    if (m_cart.size() > 0x2000) { error = "Cartridge is larger than 8K - BASIC needs the Imagination Machine keyboard"; return false; }
    // bus/apf: a loose 6K image is Space Destroyers, with 1K of RAM in the cartridge
    m_cartRamFitted = m_cart.size() == 0x1800;
    reset();
    return true;
}

void ApfCore::reset()
{
    std::memset(&m_s, 0, sizeof(m_s));
    m_vdg.reset();
    m_pia.reset();
    // apf_state::machine_reset (after the devices)
    m_s.ca2 = 0;
    m_s.latch = 0;
    m_s.nextHsync = 228;
    m_cpu.reset();
    m_audioLast = m_cpu.cycles();
}

// ---- bus ----
uint8_t ApfCore::extRead(uint16_t a)
{
    if (a < 0x2000) return m_s.videoram[a & 0x3ff];
    if (a < 0x4000) return m_pia.read(a & 3);
    if (a < 0x6000) return m_bios[a & 0xfff];
    if (a >= 0xe000) return m_bios[a & 0xfff];
    if (a >= 0x8000 && a < 0xa000) {
        if (m_cartRamFitted && a >= 0x9800) return a < 0x9c00 ? m_s.cartRam[a & 0x3ff] : 0;
        uint32_t off = a - 0x8000u;
        return off < m_cart.size() ? m_cart[off] : 0xff;
    }
    return 0;
}

void ApfCore::extWrite(uint16_t a, uint8_t d)
{
    if (a < 0x2000) { m_s.videoram[a & 0x3ff] = d; return; }
    if (a < 0x4000) { m_pia.write(a & 3, d); return; }
    if (m_cartRamFitted && a >= 0x9800 && a < 0x9c00) m_s.cartRam[a & 0x3ff] = d;
}

// apf_state::videoram_r
uint8_t ApfCore::vdgInput(uint16_t offset)
{
    const uint8_t* vram = m_s.videoram;
    if (m_s.padData & 0x80) {          // AG line
        // MAME's hacks to line the CPU and VDG up (Rocket Patrol and Blackjack; Space Destroyers)
        if ((m_s.padData & 0x40) && !m_cartRamFitted) offset = uint16_t(offset - 0x400);
        if ((m_s.padData & 0x40) && m_cartRamFitted) offset = uint16_t(offset - 0x120);
        uint16_t part1 = offset & 0x1f;
        uint16_t part2 = (offset & 0x1e0) >> 5;
        uint16_t part3 = (offset & 0x1e00) >> 4;
        if (m_s.ca2) m_s.latch = vram[part3 | part1];
        m_vdg.changeMode(MC6847::MODE_CSS, (m_s.latch >> 6) & 1);
        uint16_t latch = uint16_t((m_s.latch & 0x1f) << 4);
        return vram[(latch | part2 | 0x200) & 0x3ff];
    }
    uint8_t data = vram[(offset & 0x1ff) | 0x200];
    if (m_s.ca2) m_vdg.changeMode(MC6847::MODE_CSS, (data >> 6) & 1);
    m_vdg.changeMode(MC6847::MODE_INV, (data >> 6) & 1);
    m_vdg.changeMode(MC6847::MODE_AS, (data >> 7) & 1);
    return data;
}

// apf_state::pia0_porta_r: the controllers, one row per keypad line
uint8_t ApfCore::inA()
{
    uint8_t joy[4] = { 0xff, 0xff, 0xff, 0xff };
    for (int p = 0; p < 2; p++) {
        uint32_t in = m_inputs >> (p * 16);
        int sh = p == 0 ? 4 : 0;         // player 1 (left) on the high nibble
        auto press = [&](int row, int bit, bool on) { if (on) joy[row] &= uint8_t(~(1 << (bit + sh))); };
        auto key = [&](int k) { return (in >> (5 + k)) & 1; };
        press(0, 0, key(1)); press(0, 1, key(0)); press(0, 2, key(4)); press(0, 3, key(7));
        press(1, 0, in & 2); press(1, 1, in & 8); press(1, 2, in & 1); press(1, 3, in & 4);
        press(2, 0, key(3)); press(2, 1, (in >> 15) & 1); press(2, 2, key(6)); press(2, 3, key(9));
        press(3, 0, key(2)); press(3, 1, (in >> 4) & 1); press(3, 2, key(5)); press(3, 3, key(8));
    }
    uint8_t data = 0xff;
    for (int i = 3; i >= 0; i--)
        if (!((m_s.padData >> i) & 1)) data &= joy[i];
    return data;
}

void ApfCore::outB(uint8_t data, uint8_t)
{
    m_vdg.changeMode(MC6847::MODE_AG, (data >> 7) & 1);
    m_vdg.changeMode(MC6847::MODE_GM0, (data >> 6) & 1);
    m_s.padData = data;
}

void ApfCore::cb2(int state)
{
    audioTo(m_cpu.cycles());
    m_s.speaker = uint8_t(state ? 1 : 0);
}

// ---- timing ----
void ApfCore::runFrame()
{
    for (int guard = 0; guard < 400; guard++) {
        int64_t next = m_s.nextHsync;
        int64_t fs = m_vdg.fsyncDue();
        if (fs >= 0 && fs < next) next = fs;
        int64_t target = (next + 3) / 4;
        int64_t now = m_cpu.cycles();
        if (now < target) m_cpu.run(int(target - now));
        int64_t t = m_cpu.cycles() * 4;
        // events that are due, in time order
        for (;;) {
            int64_t h = m_s.nextHsync, f = m_vdg.fsyncDue();
            if (f >= 0 && f <= t && f < h) { m_vdg.fireFsync(); continue; }
            if (h <= t) { m_vdg.hsyncOn(h); m_s.nextHsync = h + 228; continue; }
            break;
        }
        if (m_vdg.frameReady()) {
            m_vdg.render(m_frame);
            break;
        }
    }
    audioTo(m_cpu.cycles());
}

void ApfCore::audioTo(int64_t cycle)
{
    const double step = 44100.0 / CPU_CLOCK;
    double lvl = m_s.speaker ? 1.0 : 0.0;
    while (m_audioLast < cycle) {
        int64_t n = cycle - m_audioLast;
        double room = (1.0 - m_audioPhase) / step;      // cycles until the next sample
        if (n < room) {
            m_audioAcc += lvl * n * step; m_audioPhase += n * step; m_audioLast = cycle;
        } else {
            int64_t take = (int64_t)std::ceil(room);
            if (take < 1) take = 1;
            if (take > n) take = n;
            m_audioAcc += lvl * (1.0 - m_audioPhase);
            double v = m_audioAcc;
            m_dc += (v - m_dc) * 0.002;
            double o = (v - m_dc) * 12000.0;
            o = std::max(-32768.0, std::min(32767.0, o));
            if (m_audio.size() < 44100) m_audio.push_back(int16_t(o));
            m_audioPhase = take * step - (1.0 - m_audioPhase);
            m_audioAcc = lvl * m_audioPhase;
            m_audioLast += take;
        }
    }
}

size_t ApfCore::readAudio(int16_t* out, size_t maxSamples)
{
    size_t n = std::min(maxSamples, m_audio.size());
    std::memcpy(out, m_audio.data(), n * 2);
    m_audio.erase(m_audio.begin(), m_audio.begin() + long(n));
    return n;
}

// ---- save states ----
bool ApfCore::saveState(std::vector<uint8_t>& out)
{
    out.clear();
    auto put = [&](const void* p, size_t n) { out.insert(out.end(), (const uint8_t*)p, (const uint8_t*)p + n); };
    uint32_t magic = 0x31465041;   // 'APF1'
    put(&magic, 4);
    put(&m_s, sizeof(m_s));
    put(m_cpu.state(), sizeof(M6801::State));
    put(&m_vdg.s, sizeof(m_vdg.s));
    put(&m_pia.s, sizeof(m_pia.s));
    return true;
}

bool ApfCore::loadState(const std::vector<uint8_t>& in)
{
    size_t need = 4 + sizeof(m_s) + sizeof(M6801::State) + sizeof(m_vdg.s) + sizeof(m_pia.s);
    if (in.size() != need) return false;
    uint32_t magic; std::memcpy(&magic, in.data(), 4);
    if (magic != 0x31465041) return false;
    const uint8_t* p = in.data() + 4;
    std::memcpy(&m_s, p, sizeof(m_s)); p += sizeof(m_s);
    std::memcpy(m_cpu.state(), p, sizeof(M6801::State)); p += sizeof(M6801::State);
    std::memcpy(&m_vdg.s, p, sizeof(m_vdg.s)); p += sizeof(m_vdg.s);
    std::memcpy(&m_pia.s, p, sizeof(m_pia.s));
    m_audioLast = m_cpu.cycles();
    return true;
}
