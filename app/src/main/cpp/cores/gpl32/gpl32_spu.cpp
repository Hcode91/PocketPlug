// PocketPlug - GeneralPlus GPL32600 sound unit (see gpl32_spu.h)
// SPDX-License-Identifier: GPL-2.0-or-later
#include "gpl32_spu.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cstdlib>

namespace {
const int8_t kImaIndexShift[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };
int g_imaDiff[89 * 16];
bool g_imaTables = false;

void imaTables()
{
    if (g_imaTables) return;
    g_imaTables = true;
    static const int8_t bits[16][4] = {
        { 1, 0, 0, 0 }, { 1, 0, 0, 1 }, { 1, 0, 1, 0 }, { 1, 0, 1, 1 }, { 1, 1, 0, 0 }, { 1, 1, 0, 1 }, { 1, 1, 1, 0 }, { 1, 1, 1, 1 },
        { -1, 0, 0, 0 }, { -1, 0, 0, 1 }, { -1, 0, 1, 0 }, { -1, 0, 1, 1 }, { -1, 1, 0, 0 }, { -1, 1, 0, 1 }, { -1, 1, 1, 0 }, { -1, 1, 1, 1 } };
    for (int step = -8; step <= 80; step++) {
        int v = int(std::min(std::floor(16.0 * std::pow(11.0 / 10.0, double(step))), 32767.0));
        if (step == -5 || step == -4) v++;
        for (int n = 0; n < 16; n++)
            g_imaDiff[(step + 8) * 16 + n] = bits[n][0] * (v * bits[n][1] + v / 2 * bits[n][2] + v / 4 * bits[n][3] + v / 8);
    }
}

const uint32_t kRampdownFrames[8] = { 13 * 4, 13 * 16, 13 * 64, 13 * 256, 13 * 1024, 13 * 4096, 13 * 8192, 13 * 8192 };
const uint32_t kEnvclkFrames[16] = { 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 8192, 8192, 8192, 8192 };
constexpr double kSpuRate = 281250.0 / 4;   // the SPU's own sample clock
}

int16_t GplSpu::Ima::clock(uint8_t nibble)
{
    signal += g_imaDiff[step * 16 + (nibble & 15)];
    signal = std::max(-32768, std::min(32767, signal));
    step = std::max(0, std::min(88, step + kImaIndexShift[nibble & 7]));
    return int16_t(signal);
}

void GplSpu::reset()
{
    imaTables();
    std::memset(m_attr, 0, sizeof m_attr);
    std::memset(m_phase, 0, sizeof m_phase);
    std::memset(m_ctrl, 0, sizeof m_ctrl);
    for (int ch = 0; ch < 32; ch++) {
        m_wordAddr[ch] = m_loopAddr[ch] = m_envAddr[ch] = 0;
        m_rate[ch] = m_accum[ch] = 0;
        m_shift[ch] = m_envFrame[ch] = m_rampFrame[ch] = 0;
        m_ima[ch] = Ima();
    }
    m_ctrl[0][MAIN_VOLUME] = m_ctrl[1][MAIN_VOLUME] = 0x7f;
}

uint32_t GplSpu::read(uint32_t off)
{
    if (off >= 0x1000 && off < 0x1800) {
        int ch = int(((off >> 6) & 15) | ((off >> 6) & 16)), reg = int((off >> 2) & 15);
        if (reg == WAVE_ADDR) return m_wordAddr[ch] & 0xffff;
        if (reg == MODE) return (m_attr[ch][MODE] & ~0x3fu) | ((m_wordAddr[ch] >> 16) & 0x3f);
        return m_attr[ch][reg];
    }
    if (off >= 0x1800 && off < 0x2000) {
        int ch = int(((off >> 6) & 15) | ((off >> 6) & 16)), reg = int((off >> 2) & 15);
        return reg < 8 ? m_phase[ch][reg] : 0;
    }
    if (off >= 0xe00 && off < 0xf00) {
        int bank = (off >> 7) & 1, reg = int((off >> 2) & 31);
        return m_ctrl[bank][reg];
    }
    return 0;
}

void GplSpu::write(uint32_t off, uint32_t value)
{
    uint16_t v = uint16_t(value);
    if (off >= 0x1000 && off < 0x1800) {
        int ch = int(((off >> 6) & 15) | ((off >> 6) & 16)), reg = int((off >> 2) & 15);
        switch (reg) {
        case WAVE_ADDR:   // the 32-bit register may hold the whole address
            m_wordAddr[ch] = value > 0xffff ? value : ((m_wordAddr[ch] & ~0xffffu) | v);
            break;
        case MODE:
            m_wordAddr[ch] = (m_wordAddr[ch] & ~0x3f0000u) | (uint32_t(v & 0x3f) << 16);
            m_loopAddr[ch] = (m_loopAddr[ch] & ~0x3f0000u) | (uint32_t((v >> 6) & 0x3f) << 16);
            break;
        case LOOP_ADDR:
            m_loopAddr[ch] = value > 0xffff ? value : ((m_loopAddr[ch] & ~0xffffu) | v);
            break;
        case PAN_VOL: v &= 0x7f7f; break;
        case ENVELOPE_DATA: v &= 0xff7f; break;
        case ADPCM_SEL: v &= 0xfe00; break;
        default: break;
        }
        m_attr[ch][reg] = v;
        return;
    }
    if (off >= 0x1800 && off < 0x2000) {
        int ch = int(((off >> 6) & 15) | ((off >> 6) & 16)), reg = int((off >> 2) & 15);
        if (reg >= 8) return;
        m_phase[ch][reg] = (reg <= RAMP_DOWN_CLOCK) ? (v & 7) : v;
        if (reg == PHASE_HIGH || reg == PHASE) {
            uint32_t phase = (uint32_t(m_phase[ch][PHASE_HIGH] & 7) << 16) | m_phase[ch][PHASE];
            m_rate[ch] = double(phase) * 140625.0 * 2.0 / double(1 << 19);
            m_accum[ch] = 0;
        }
        return;
    }
    if (off >= 0xe00 && off < 0xf00) ctrlWrite((off >> 7) & 1, int((off >> 2) & 31), v);
}

void GplSpu::ctrlWrite(int bank, int reg, uint16_t v)
{
    static const bool log = getenv("GPLSPU") != nullptr;
    if (log && m_ctrl[bank][reg] != v) fprintf(stderr, "SPU ctrl%d[%02x] = %04x\n", bank, reg, v);
    uint16_t* c = m_ctrl[bank];
    switch (reg) {
    case CHANNEL_ENABLE: {
        uint16_t changed = c[reg] ^ v;
        c[reg] = v;
        for (int b = 0; b < 16; b++) {
            uint16_t m = uint16_t(1u << b);
            if (!(changed & m)) continue;
            int ch = bank * 16 + b;
            if (v & m) { if (!(c[CHANNEL_STOP] & m) && !(c[CHANNEL_STATUS] & m)) startChannel(ch); }
            else if (c[CHANNEL_STATUS] & m) stopChannel(ch);
        }
        break;
    }
    case MAIN_VOLUME: c[reg] = v & 0x7f; break;
    case FIQ_STATUS: c[reg] &= uint16_t(~v); break;
    case BEAT_COUNT: c[reg] = uint16_t((c[reg] & ~v & 0x4000) | (v & ~0x4000)); break;
    case ENV_RAMP_DOWN: {
        uint16_t old = c[reg];
        c[reg] = v & c[CHANNEL_STATUS];
        uint16_t changed = old ^ c[reg];
        for (int b = 0; b < 16; b++)
            if ((changed & v) & (1u << b)) m_rampFrame[bank * 16 + b] = kRampdownFrames[m_phase[bank * 16 + b][RAMP_DOWN_CLOCK] & 7];
        break;
    }
    case CHANNEL_STOP: {
        uint16_t old = c[reg];
        c[reg] &= uint16_t(~v);
        uint16_t changed = old ^ c[reg];
        for (int b = 0; b < 16; b++) {
            uint16_t m = uint16_t(1u << b);
            if ((changed & m) && (c[CHANNEL_ENABLE] & m) && !(c[CHANNEL_STATUS] & m)) startChannel(bank * 16 + b);
        }
        break;
    }
    case CHANNEL_STATUS: break;                    // read-only
    case ENV_IRQ: c[reg] &= uint16_t(~v); break;
    default: c[reg] = v; break;
    }
}

void GplSpu::startChannel(int ch)
{
    static const bool log = getenv("GPLSPU") != nullptr;
    if (log) fprintf(stderr, "SPU start ch%d mode %04x addr %06x loop %06x rate %.0f vol %04x env %04x/%04x/%04x envmode %d\n", ch, m_attr[ch][MODE],
        m_wordAddr[ch], m_loopAddr[ch], m_rate[ch], m_attr[ch][PAN_VOL], m_attr[ch][ENVELOPE0], m_attr[ch][ENVELOPE_DATA], m_attr[ch][ENVELOPE1],
        (ctrl(ch, ENV_MODE) >> (ch & 15)) & 1);
    ctrl(ch, CHANNEL_STATUS) |= uint16_t(1u << (ch & 15));
    m_envAddr[ch] = (uint32_t(m_attr[ch][ENVELOPE_ADDR_HIGH] & 0x3f) << 16) | m_attr[ch][ENVELOPE_ADDR];
    m_attr[ch][ENVELOPE_DATA] = uint16_t(edd(ch) | ((m_attr[ch][ENVELOPE1] & 0xff) << 8));
    m_ima[ch] = Ima();
    m_shift[ch] = 0;
    m_envFrame[ch] = 0;
}

void GplSpu::stopChannel(int ch)
{
    uint16_t m = uint16_t(1u << (ch & 15));
    ctrl(ch, CHANNEL_STATUS) &= uint16_t(~m);
    m_attr[ch][MODE] &= ~0x8000u;
    ctrl(ch, TONE_RELEASE) &= uint16_t(~m);
    ctrl(ch, ENV_RAMP_DOWN) &= uint16_t(~m);
}

bool GplSpu::fetchSample(int ch)
{
    uint16_t* a = m_attr[ch];
    a[WAVE_DATA_PREV] = a[WAVE_DATA];
    uint16_t mode = a[MODE];
    int tone = (mode >> 12) & 3;
    bool adpcm = mode & 0x8000, pcm16 = mode & 0x4000;
    uint16_t raw = tone ? (readWord ? readWord(m_wordAddr[ch]) : 0xffff) : a[WAVE_DATA];
    auto endOfSample = [&]() -> bool {
        if (tone == 1) {                             // one-shot: stop
            ctrl(ch, CHANNEL_STOP) |= uint16_t(1u << (ch & 15));
            stopChannel(ch);
            return false;
        }
        m_wordAddr[ch] = m_loopAddr[ch];             // loop
        m_shift[ch] = 0;
        return true;
    };
    if (adpcm) {
        if (tone && raw == 0xffff) {
            if (!endOfSample()) return false;
            a[MODE] &= ~0x8000u;
        } else {
            uint8_t nibble = uint8_t((raw >> m_shift[ch]) & 15);
            a[WAVE_DATA] = uint16_t(m_ima[ch].clock(nibble)) ^ 0x8000;
        }
    } else if (pcm16) {
        if (tone && raw == 0xffff) { if (!endOfSample()) return false; }
        else a[WAVE_DATA] = raw;
    } else if (tone) {
        raw = m_shift[ch] ? (raw & 0xff00) : uint16_t(raw << 8);
        raw |= raw >> 8;
        if (raw == 0xffff) { if (!endOfSample()) return false; }
        else a[WAVE_DATA] = raw;
    }
    return true;
}

bool GplSpu::advance(int ch, double outRate)
{
    m_accum[ch] += m_rate[ch];
    uint32_t steps = 0;
    while (m_accum[ch] >= outRate) { m_accum[ch] -= outRate; steps++; }
    uint16_t mode = m_attr[ch][MODE];
    for (uint32_t s = 0; s < steps; s++) {
        if (!fetchSample(ch)) return false;
        mode = m_attr[ch][MODE];
        if (mode & 0x8000) {
            m_shift[ch] += 4;
            if (m_shift[ch] >= 16) { m_shift[ch] = 0; m_wordAddr[ch]++; }
        } else if (mode & 0x4000) {
            m_wordAddr[ch]++;
        } else {
            m_shift[ch] += 8;
            if (m_shift[ch] >= 16) { m_shift[ch] = 0; m_wordAddr[ch]++; }
        }
    }
    return true;
}

uint32_t GplSpu::envelopeClock(int ch)
{
    const uint16_t* c = m_ctrl[ch >> 4];
    int b = ch & 15;
    if (b < 4) return (c[ENVCLK0] >> (b * 4)) & 15;
    if (b < 8) return (c[ENVCLK0_HIGH] >> ((b - 4) * 4)) & 15;
    if (b < 12) return (c[ENVCLK1] >> ((b - 8) * 4)) & 15;
    return (c[ENVCLK1_HIGH] >> ((b - 12) * 4)) & 15;
}

void GplSpu::rampdownTick(int ch)
{
    uint8_t old = uint8_t(edd(ch));
    uint8_t next = uint8_t(old - (m_attr[ch][ENVELOPE_LOOP_CTRL] >> 9));
    if (next > old) next = 0;
    if (next) {
        m_attr[ch][ENVELOPE_DATA] = uint16_t((m_attr[ch][ENVELOPE_DATA] & ~0x7fu) | (next & 0x7f));
        m_rampFrame[ch] = kRampdownFrames[m_phase[ch][RAMP_DOWN_CLOCK] & 7];
    } else {
        ctrl(ch, CHANNEL_STOP) |= uint16_t(1u << (ch & 15));
        stopChannel(ch);
    }
}

void GplSpu::envelopeTick(int ch)
{
    uint16_t* a = m_attr[ch];
    uint16_t count = a[ENVELOPE_DATA] >> 8;
    uint16_t cur = edd(ch);
    if (count > 0) { count--; a[ENVELOPE_DATA] = uint16_t(cur | (count << 8)); }
    if (count != 0) return;
    uint16_t target = (a[ENVELOPE0] >> 8) & 0x7f, inc = a[ENVELOPE0] & 0x7f, next = cur;
    if (next != target) {
        if (a[ENVELOPE0] & 0x80) {
            next = uint16_t(next - inc);
            if (next > cur) next = 0;
            else if (next < target) next = target;
            if (next == 0) {
                ctrl(ch, CHANNEL_STOP) |= uint16_t(1u << (ch & 15));
                stopChannel(ch);
                return;
            }
        } else {
            next = uint16_t(next + inc);
            if (next >= target) next = target;
        }
    }
    if (next == target) {
        if (a[ENVELOPE1] & 0x100) {
            uint16_t repeat = uint16_t(((a[ENVELOPE1] >> 9) & 0x7f) - 1);
            if (repeat == 0) {
                if (readWord) {
                    a[ENVELOPE0] = readWord(m_envAddr[ch]);
                    a[ENVELOPE1] = readWord(m_envAddr[ch] + 1);
                    a[ENVELOPE_LOOP_CTRL] = readWord(m_envAddr[ch] + 2);
                }
                m_envAddr[ch] = ((uint32_t(a[ENVELOPE_ADDR_HIGH] & 0x3f) << 16) | a[ENVELOPE_ADDR]) + (a[ENVELOPE_LOOP_CTRL] & 0x1ff);
            } else {
                a[ENVELOPE1] = uint16_t((a[ENVELOPE1] & ~0xfe00u) | ((repeat << 9) & 0xfe00));
            }
        } else if (readWord) {
            a[ENVELOPE0] = readWord(m_envAddr[ch]);
            a[ENVELOPE1] = readWord(m_envAddr[ch] + 1);
            m_envAddr[ch] += 2;
        }
    }
    a[ENVELOPE_DATA] = uint16_t((next & 0x7f) | ((a[ENVELOPE1] & 0xff) << 8));
}

void GplSpu::render(int16_t* out, int samples, int rate)
{
    // envelope and ramp-down counters run on the SPU's own clock
    const double tickScale = kSpuRate / double(rate);
    for (int i = 0; i < samples; i++) {
        int32_t left = 0, right = 0;
        for (int ch = 0; ch < 32; ch++) {
            if (!status(ch)) continue;
            if (!advance(ch, double(rate))) continue;
            int32_t s = int16_t(m_attr[ch][WAVE_DATA] ^ 0x8000);
            s = (s * int32_t(edd(ch))) >> 7;
            int32_t vol = m_attr[ch][PAN_VOL] & 0x7f, pan = (m_attr[ch][PAN_VOL] >> 8) & 0x7f;
            int32_t pl, pr;
            if (pan < 0x40) { pl = 0x7f * vol; pr = pan * 2 * vol; }
            else { pl = (0x7f - pan) * 2 * vol; pr = 0x7f * vol; }
            left += (s * pl) >> 14;
            right += (s * pr) >> 14;
            uint16_t m = uint16_t(1u << (ch & 15));
            uint32_t ticks = uint32_t(tickScale * (i + 1)) - uint32_t(tickScale * i);
            for (uint32_t t = 0; t < ticks && status(ch); t++) {
                if (ctrl(ch, ENV_RAMP_DOWN) & m) {
                    if (m_rampFrame[ch] > 0) m_rampFrame[ch]--;
                    if (m_rampFrame[ch] == 0) rampdownTick(ch);
                } else if (!(ctrl(ch, ENV_MODE) & m)) {
                    if (m_envFrame[ch] > 0) m_envFrame[ch]--;
                    if (m_envFrame[ch] == 0) { envelopeTick(ch); m_envFrame[ch] = kEnvclkFrames[envelopeClock(ch)]; }
                }
            }
        }
        int volSel = (m_ctrl[0][CONTROL] >> 6) & 3;
        int shift = volSel == 0 ? 4 : 2;
        int32_t mix = ((left + right) / 2) >> shift;
        mix = (mix * int32_t(m_ctrl[0][MAIN_VOLUME])) >> 7;
        out[i] = int16_t(std::max(-32768, std::min(32767, mix * 2)));
    }
}
