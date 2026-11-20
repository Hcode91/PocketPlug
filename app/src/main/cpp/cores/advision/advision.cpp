// PocketPlug - Entex Adventure Vision
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Port of MAME's advision.cpp. See advision.h.

#include "advision.h"
#include <algorithm>
#include <cstring>

namespace {
constexpr double CPU_HZ = 11000000.0 / 15.0;        // 8048 machine cycles
constexpr double COP_HZ = 200000.0 / 4.0;           // COP411 instruction cycles
constexpr double SWEEP = CPU_HZ / 14.0;             // mirror: two sides at ~7 rev/s
constexpr double COL_STEP = CPU_HZ * 20e-6;         // one LED sample every 20 us
constexpr int64_t SYNC_LEN = int64_t(CPU_HZ * 100e-6 + 0.5);   // mirror sync pulse, 100 us
constexpr int64_t LED_OFF_DELAY = int64_t(CPU_HZ * 39e-6 + 0.5);
constexpr int AUDIO_HZ = 44100;
}

AdvisionCore::AdvisionCore()
    : m_cpu(this, m_bios, 0x400, 64), m_cop(this, m_soundRom, 0x7, 0x3)
{
    std::memset(m_bios, 0, sizeof(m_bios));
    std::memset(m_soundRom, 0, sizeof(m_soundRom));
    std::memset(&m_s, 0, sizeof(m_s));
    m_frame.assign(W * H, 0xff000000);
}

bool AdvisionCore::load(const std::vector<RomFile>& roms, std::string& error)
{
    bool bios = false, snd = false;
    for (auto& r : roms) {
        if (r.name == "ins8048-11kdp_n.u5" && r.data.size() == 0x400) { std::memcpy(m_bios, r.data.data(), 0x400); bios = true; }
        else if (r.name == "cop411l-kcn_n.u8" && r.data.size() == 0x200) { std::memcpy(m_soundRom, r.data.data(), 0x200); snd = true; }
        else if (!r.data.empty()) m_cart = r.data;
    }
    if (!bios || !snd) { error = "Missing the BIOS (ins8048-11kdp_n.u5 and cop411l-kcn_n.u8 from the advision zip)"; return false; }
    if (m_cart.empty()) { error = "No cartridge"; return false; }
    if (m_cart.size() > 0x1000) { error = "Cartridge is larger than 4K - not an Adventure Vision cartridge"; return false; }
    reset();
    return true;
}

void AdvisionCore::reset()
{
    std::memset(m_s.extRam, 0, sizeof(m_s.extRam));
    m_s.ramBank = 0;
    m_s.videoStrobe = m_s.videoEnable = 0;
    m_s.videoBank = 0;
    std::memset(m_s.ledOutput, 0, 5);
    std::memset(m_s.ledLatch, 0, 5);
    m_s.soundCmd = 0;
    m_s.soundReset = 1;
    m_s.time = 0;
    m_s.sweepStart = 0;
    m_s.ledOffAt = -1;
    m_s.hpos = 0;
    std::memset(m_s.display, 0, sizeof(m_s.display));
    m_cpu.setEa(false);
    m_cpu.reset();
    m_cop.reset();
}

uint8_t AdvisionCore::mcsExtRead(uint8_t a)
{
    uint8_t d = m_s.extRam[(m_s.ramBank << 8) | a];
    if (m_s.videoBank > 0 && m_s.videoBank < 6)
        m_s.ledLatch[5 - m_s.videoBank] = d ^ 0xff;     // RAM data goes to an LED latch too
    else if (m_s.videoBank == 6) {
        bool held = !(d & 1);                           // sound CPU reset
        m_s.soundReset = held;
        if (held) m_cop.reset();
    }
    return d;
}

uint8_t AdvisionCore::mcsPortRead(int port)
{
    if (port != 1) return 0xff;
    // P1: joystick and buttons, active low (some buttons share direction bits)
    uint8_t d = 0xff;
    if (m_inputs & 0x10) d &= ~0x01;    // button 1
    if (m_inputs & 0x20) d &= ~0x02;    // button 2
    if (m_inputs & 0x80) d &= ~0x04;    // button 4
    if (m_inputs & 0x40) d &= ~0x08;    // button 3
    if (m_inputs & 0x02) d &= ~0x10;    // down
    if (m_inputs & 0x01) d &= ~0x20;    // up
    if (m_inputs & 0x08) d &= ~0x40;    // right
    if (m_inputs & 0x04) d &= ~0x80;    // left
    if (~d & 0x01) d &= 0xcf;
    if (~d & 0x02) d &= 0xaf;
    if (~d & 0x04) d &= 0x6f;
    return d | 0x07;
}

void AdvisionCore::mcsPortWrite(int port, uint8_t d)
{
    if (port == 1) {
        m_s.ramBank = d & 3;                            // P10-P11: RAM bank
        m_cpu.setEa(d & 4);                             // P12: EA (cartridge)
        return;
    }
    // P2
    m_s.videoBank = d >> 5 & 7;                         // P25-P27: LED latch select
    if (m_s.videoBank == 0 && m_s.ledOffAt < 0) m_s.ledOffAt = m_s.time + LED_OFF_DELAY;
    if (!m_s.videoStrobe && (d & 0x10)) {               // P24 rising: latches to LEDs
        std::memcpy(m_s.ledOutput, m_s.ledLatch, 5);
        m_s.ledOffAt = -1;
        m_s.videoEnable = 1;
    }
    m_s.videoStrobe = (d & 0x10) ? 1 : 0;
    m_s.soundCmd = d >> 4;                              // L0-L3 of the sound CPU
}

int AdvisionCore::mcsTest(int t)
{
    if (t != 1) return 1;
    // T1: mirror sync pulse at the start of each sweep
    return (m_s.time - m_s.sweepStart) < SYNC_LEN ? 0 : 1;
}

void AdvisionCore::sampleColumn()
{
    for (int y = 0; y < 8; y++)
        for (int b = 0; b < 5; b++)
            m_s.display[(b * 8 + y) * COLS + m_s.hpos] = m_s.videoEnable ? (m_s.ledOutput[b] >> (y ^ 7) & 1) : 0;
    m_s.hpos++;
    if (m_s.hpos == W) present();
}

void AdvisionCore::present()
{
    const uint32_t lit = 0xff000000u | 0xff0000u | (0xff / 16) << 8 | (0xff / 12);
    for (int y = 0; y < 40; y++) {
        const uint8_t* src = &m_s.display[y * COLS];
        for (int line = 0; line < 4; line++) {
            uint32_t* dst = &m_frame[(y * 4 + line) * W];
            if (line == 0) { std::fill(dst, dst + W, 0xff000000u); continue; }   // LEDs 75% tall
            for (int x = 0; x < W; x++) dst[x] = src[x] ? lit : 0xff000000u;
        }
    }
}

void AdvisionCore::advance(int cycles)
{
    const int64_t end = m_s.time + cycles;
    while (m_s.time < end) {
        // next event: LED-off timer, next column, next sweep
        int64_t t = end;
        if (m_s.ledOffAt >= 0) t = std::min(t, m_s.ledOffAt);
        int64_t nextSweep = m_s.sweepStart + int64_t(SWEEP);
        t = std::min(t, nextSweep);
        int64_t nextCol = m_s.hpos < COLS ? m_s.sweepStart + int64_t(m_s.hpos * COL_STEP) : INT64_MAX;
        t = std::min(t, nextCol);
        // sound CPU and speaker up to t
        int64_t span = t - m_s.time;
        if (span > 0) {
            double level = m_s.speaker ? (m_s.volumeHalf ? 0.5 : 1.0) * 9000.0 : 0.0;
            double perCpu = COP_HZ / CPU_HZ;
            for (int64_t i = 0; i < span; i++) {
                if (!m_s.soundReset) {
                    m_copPhase += perCpu;
                    while (m_copPhase >= 1.0) { m_copPhase -= 1.0; m_cop.step(); level = m_s.speaker ? (m_s.volumeHalf ? 0.5 : 1.0) * 9000.0 : 0.0; }
                }
                m_audioAcc += level;
                m_audioPhase += AUDIO_HZ / CPU_HZ;
                if (m_audioPhase >= 1.0) {
                    m_audioPhase -= 1.0;
                    double x = m_audioAcc * AUDIO_HZ / CPU_HZ;
                    m_dcY = x - m_dcX + 0.995 * m_dcY;     // remove the speaker's DC level
                    m_dcX = x;
                    m_audio.push_back(int16_t(std::max(-32000.0, std::min(32000.0, m_dcY))));
                    m_audioAcc = 0;
                }
            }
        }
        m_s.time = t;
        if (m_s.ledOffAt >= 0 && m_s.time >= m_s.ledOffAt) { m_s.videoEnable = 0; m_s.ledOffAt = -1; }
        if (m_s.time >= nextSweep) {
            std::memset(m_s.display, 0, sizeof(m_s.display));
            m_s.sweepStart = nextSweep;
            m_s.hpos = 0;
        }
        while (m_s.hpos < COLS && m_s.time >= m_s.sweepStart + int64_t(m_s.hpos * COL_STEP)) sampleColumn();
    }
}

void AdvisionCore::runFrame()
{
    m_frameCarry += CPU_HZ / 60.0;
    int budget = int(m_frameCarry);
    m_frameCarry -= budget;
    int done = 0;
    while (done < budget) {
        int n = m_cpu.step();
        advance(n);
        done += n;
    }
    // keep the audio queue from growing without bound
    if (m_audio.size() > AUDIO_HZ / 4) m_audio.erase(m_audio.begin(), m_audio.end() - AUDIO_HZ / 10);
}

size_t AdvisionCore::readAudio(int16_t* out, size_t maxSamples)
{
    size_t n = std::min(maxSamples, m_audio.size());
    std::memcpy(out, m_audio.data(), n * sizeof(int16_t));
    m_audio.erase(m_audio.begin(), m_audio.begin() + long(n));
    return n;
}

bool AdvisionCore::saveState(std::vector<uint8_t>& out)
{
    out.clear();
    uint32_t magic = 0x56444141;   // 'AADV'
    auto put = [&](const void* p, size_t n) { const uint8_t* b = (const uint8_t*)p; out.insert(out.end(), b, b + n); };
    put(&magic, 4); put(&m_s, sizeof(m_s)); put(&m_cpu.s, sizeof(m_cpu.s)); put(&m_cop.s, sizeof(m_cop.s));
    return true;
}

bool AdvisionCore::loadState(const std::vector<uint8_t>& in)
{
    if (in.size() != 4 + sizeof(m_s) + sizeof(m_cpu.s) + sizeof(m_cop.s)) return false;
    uint32_t magic; std::memcpy(&magic, in.data(), 4);
    if (magic != 0x56444141) return false;
    const uint8_t* p = in.data() + 4;
    std::memcpy(&m_s, p, sizeof(m_s)); p += sizeof(m_s);
    std::memcpy(&m_cpu.s, p, sizeof(m_cpu.s)); p += sizeof(m_cpu.s);
    std::memcpy(&m_cop.s, p, sizeof(m_cop.s));
    return true;
}
