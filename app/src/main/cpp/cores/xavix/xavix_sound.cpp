// PocketPlug - XaviX sound
// SPDX-License-Identifier: BSD-3-Clause
//
// Port of MAME's tvgames/xavix_sound.cpp (ramacat, David Haywood), BSD-3-Clause.

#include "xavix_sound.h"

namespace {
const uint8_t MIXER_ORDER_MULTIPLEX[16] = { 0x0, 0xa, 0x7, 0xd, 0xc, 0x6, 0xb, 0x1, 0x4, 0xe, 0x3, 0x9, 0x8, 0x2, 0xf, 0x5 };
const uint8_t MIXER_ORDER_BROADCAST[16] = { 0x0, 0x8, 0x4, 0xc, 0x2, 0xa, 0x6, 0xe, 0x1, 0x9, 0x5, 0xd, 0x3, 0xb, 0x7, 0xf };
const int AMP_TABLE[8] = { 2, 4, 8, 12, 16, 20, 20, 20 };
}

void XavixSound::reset(uint8_t tempoReset)
{
    s.cyclerate_div = 0x0f;
    for (int i = 0; i < 4; i++) setTempo(i, tempoReset);
    for (int v = 0; v < 16; v++) {
        Voice& vv = s.voice[v];
        vv.enabled = 0;
        vv.position = 0;
        vv.loop_position = 0;
        vv.bank = 0;
        vv.env_vol_left = 0;
        vv.env_vol_right = 0;
        vv.env_period_samples = envelopePeriodTicks(s.tempo_div[v & 3]);
        vv.env_countdown = vv.env_period_samples;
        vv.env_active_left = 1;
        vv.env_active_right = 1;
        vv.noise_state = 0;
    }
    s.monaural = 0; s.capacity = 0; s.amp = 2; s.dac = 0; s.gap = 0; s.lead = 0; s.lag = 0;
    s.mastervol = 0xff;
    s.gain = 2;
}

void XavixSound::tick(int32_t& outL, int32_t& outR)
{
    const uint8_t* order = s.dac ? MIXER_ORDER_BROADCAST : MIXER_ORDER_MULTIPLEX;
    const uint8_t cap = s.capacity & 3;
    const int allowed = (cap & 2) ? 4 : (cap & 1) ? 8 : 16;
    int64_t totalL = 0, totalR = 0;
    for (int idx = 0; idx < allowed; idx++) {
        const int v = order[idx];
        Voice& vo = s.voice[v];
        if (!vo.enabled) continue;
        int32_t smp = 0;
        if (vo.type == 1) {
            uint16_t st = vo.noise_state ? vo.noise_state : 0xace1u;
            const uint16_t fb = ((st >> 0) ^ (st >> 2) ^ (st >> 3) ^ (st >> 5)) & 1;
            st = uint16_t((st >> 1) | (fb << 15));
            if (st == 0) st = 0xace1u;
            vo.noise_state = st;
            smp = int8_t(((st >> 8) & 0xff) ^ 0x80);
        } else {
            const uint32_t phase = vo.position >> 14;
            uint8_t raw = (vo.type == 0) ? 0x80 : sample((uint32_t(vo.bank) << 16) | phase);
            if (raw == 0x80) {
                smp = 0;
                if (vo.type == 3) { vo.enabled = 0; continue; }
                if ((vo.position >> 14) != (vo.loop_position >> 14)) vo.position = vo.loop_position;
            } else {
                uint8_t wv = ((raw & 0x7f) == 0) ? 0x80 : uint8_t((~raw & 0x80) | (raw & 0x7f));
                smp = int32_t(wv) - 128;
            }
        }
        const int64_t base = int64_t(smp) * int64_t(vo.vol & 0x0f);
        const int64_t lr = (base * int64_t(s.mastervol)) / 255;
        totalL += (lr * int64_t(vo.env_vol_left)) / 255;
        totalR += (lr * int64_t(vo.env_vol_right)) / 255;
        vo.position += vo.rate;
        stepEnvelope(v);
        stepPitch(v);
        if (vo.env_vol_left == 0 && vo.env_vol_right == 0) vo.enabled = 0;
    }
    auto gainf = [&](int64_t x) -> int64_t {
        int64_t y = x * int64_t(s.gain) * int64_t(allowed) * 3;
        return (y >= 0) ? ((y + 64) >> 7) : -(((-y) + 64) >> 7);
    };
    totalL = gainf(totalL); totalR = gainf(totalR);
    int32_t l = totalL > 32767 ? 32767 : totalL < -32768 ? -32768 : int32_t(totalL);
    int32_t r = totalR > 32767 ? 32767 : totalR < -32768 ? -32768 : int32_t(totalR);
    if (s.monaural) l = r = (l + r) / 2;
    outL = l; outR = r;
}

void XavixSound::enableVoice(int voice, bool updateOnly)
{
    const int base = voice * 0x10;
    Voice& v = s.voice[voice];
    const uint16_t waveControl = (regs(base + 1) << 8) | regs(base + 0);
    const uint16_t waveAddr = (regs(base + 3) << 8) | regs(base + 2);
    const uint16_t waveLoop = (regs(base + 5) << 8) | regs(base + 4);
    const uint8_t waveBank = regs(base + 6);
    const uint8_t newType = waveControl & 3;
    const uint32_t newRate = waveControl >> 2;
    const uint8_t envConfig = regs(base + 8);
    const uint16_t envL = (regs(base + 0xb) << 8) | regs(base + 0xa);
    const uint16_t envR = (regs(base + 0xd) << 8) | regs(base + 0xc);
    const uint8_t envBank = regs(base + 0xe);

    v.vol = envConfig & 0x0f;
    v.env_mode = (envConfig >> 4) & 3;
    v.env_bank = envBank;
    v.env_rom_base_left = envL;
    v.env_rom_base_right = envR;
    v.type = newType;
    v.rate = newRate;
    if (newType == 1 && v.noise_state == 0) v.noise_state = waveAddr ? waveAddr : 0xace1u;
    if (updateOnly) return;

    v.enabled = 1;
    v.bank = waveBank;
    v.position = uint32_t(waveAddr) << 14;
    v.loop_position = waveLoop ? (uint32_t(waveLoop - 1) << 14) : 0;
    v.start_position = v.position;
    v.noise_state = waveAddr ? waveAddr : 0xace1u;
    const uint8_t tp = s.tempo_div[voice & 3];
    v.env_period_samples = tp ? envelopePeriodTicks(tp) : 0;
    v.env_countdown = v.env_period_samples;
    v.env_active_left = 1;
    v.env_active_right = 1;

    if (v.env_mode == 0) {
        v.env_vol_left = regs(base + 0xa);
        v.env_vol_right = regs(base + 0xc);
    } else if (v.env_mode == 1) {
        auto incLow = [](uint16_t x) { return uint16_t((x & 0xfff0) | ((x + 1) & 0x000f)); };
        v.env_vol_left = envByte(voice, envL);
        v.env_vol_right = envByte(voice, envR);
        v.env_pos_left = incLow(envL);
        v.env_pos_right = incLow(envR);
        regsW(base + 0x0a, uint8_t(envL));
        regsW(base + 0x0c, uint8_t(envR));
    } else if (v.env_mode == 2) {
        v.env_pos_left = envL;
        v.env_pos_right = envR;
        for (int ch = 0; ch < 2; ch++) {
            uint32_t& pos = ch ? v.env_pos_right : v.env_pos_left;
            uint8_t& lvl = ch ? v.env_vol_right : v.env_vol_left;
            const uint16_t addr = uint16_t(pos);
            lvl = envByte(voice, addr);
            regsW(base + (ch ? 0x0c : 0x0a), uint8_t(addr));
            pos = uint16_t(addr + 1);
        }
    } else {
        v.env_vol_left = regs(base + 0xa);
        v.env_vol_right = regs(base + 0xc);
    }
}

void XavixSound::mixerW(uint8_t d)
{
    s.monaural = d >> 7 & 1;
    s.capacity = d >> 4 & 3;
    s.amp = d & 7;
    s.gain = AMP_TABLE[s.amp];
}

uint32_t XavixSound::tempoToPeriodTicks(uint8_t tp) const
{
    if (!tp) return 0;
    const uint32_t sc = uint32_t(s.cyclerate_div) + 1;
    uint64_t samples = (uint64_t(sc) * 1024u + (tp / 2)) / tp;
    if (samples < 1) samples = 1;
    if (samples > 2000000u) samples = 2000000u;
    return uint32_t(samples);
}

uint32_t XavixSound::envelopePeriodTicks(uint8_t tp) const
{
    const uint32_t base = tempoToPeriodTicks(tp);
    if (!base) return 0;
    const uint64_t scaled = uint64_t(base) * 16u;
    return uint32_t(scaled > 64000000u ? 64000000u : scaled);
}

double XavixSound::tempoTickHz(uint8_t tempo) const
{
    const uint32_t samples = tempoToPeriodTicks(tempo);
    return samples ? double(RATE_HZ) / double(samples) : 0.0;
}

void XavixSound::setTempo(int index, uint8_t value)
{
    if (index < 0 || index > 3) return;
    s.tempo_div[index] = value;
    for (int v = 0; v < 16; v++) {
        if ((v & 3) != index) continue;
        Voice& vv = s.voice[v];
        const uint32_t newp = value ? envelopePeriodTicks(value) : 0;
        vv.env_period_samples = newp;
        if (newp) vv.env_countdown = newp;
    }
}

void XavixSound::setCyclerate(uint8_t value)
{
    s.cyclerate_div = value;
    for (int v = 0; v < 16; v++) {
        Voice& vv = s.voice[v];
        const uint8_t tp = s.tempo_div[v & 3];
        const uint32_t oldp = vv.env_period_samples;
        const uint32_t newp = tp ? envelopePeriodTicks(tp) : 0;
        if (oldp && newp) vv.env_countdown = uint32_t(uint64_t(vv.env_countdown) * newp / oldp);
        else if (!oldp && newp) vv.env_countdown = newp;
        vv.env_period_samples = newp;
    }
}

void XavixSound::stepEnvelope(int voice)
{
    Voice& v = s.voice[voice];
    if (!v.enabled) return;
    const uint32_t target = envelopePeriodTicks(s.tempo_div[voice & 3]);
    if (v.env_period_samples != target) {
        const uint32_t oldp = v.env_period_samples ? v.env_period_samples : 1;
        v.env_countdown = uint32_t(uint64_t(v.env_countdown) * target / oldp);
        v.env_period_samples = target;
    }
    if (v.env_period_samples == 0) return;
    if (v.env_countdown) { v.env_countdown--; return; }
    v.env_countdown = v.env_period_samples;
    const int base = voice * 0x10;

    if (v.env_mode == 0) {
        v.env_vol_left = regs(base + 0xa);
        v.env_vol_right = regs(base + 0xc);
        return;
    }
    if (v.env_mode == 1) {
        for (int ch = 0; ch < 2; ch++) {
            if (ch ? !v.env_active_right : !v.env_active_left) continue;
            uint32_t& p32 = ch ? v.env_pos_right : v.env_pos_left;
            const uint16_t ptr = uint16_t(p32);
            const uint8_t level = envByte(voice, ptr);
            if (ch) v.env_vol_right = level; else v.env_vol_left = level;
            const uint16_t next = uint16_t((ptr & 0xfff0) | ((ptr + 1) & 0x000f));
            p32 = next;
            regsW(base + (ch ? 0x0c : 0x0a), uint8_t(next));
        }
        if ((v.env_vol_left | v.env_vol_right) == 0) { v.env_active_left = 0; v.env_active_right = 0; }
        return;
    }
    if (v.env_mode == 2) {
        for (int ch = 0; ch < 2; ch++) {
            if (ch ? !v.env_active_right : !v.env_active_left) continue;
            uint32_t& p32 = ch ? v.env_pos_right : v.env_pos_left;
            const uint16_t ptr = uint16_t(p32);
            const uint8_t level = envByte(voice, ptr);
            if (ch) v.env_vol_right = level; else v.env_vol_left = level;
            regsW(base + (ch ? 0x0c : 0x0a), uint8_t(ptr));
            p32 = uint16_t(ptr + 1);
        }
        if ((v.env_vol_left | v.env_vol_right) == 0) { v.env_active_left = 0; v.env_active_right = 0; }
        return;
    }
    auto decay = [](uint8_t x) -> uint8_t {
        const uint8_t sub = (x >> 4) + ((x & 0x0f) ? 1 : 0);
        return (sub >= x) ? 0 : uint8_t(x - sub);
    };
    v.env_vol_left = decay(v.env_vol_left);
    v.env_vol_right = decay(v.env_vol_right);
    if ((v.env_vol_left | v.env_vol_right) == 0) { v.env_active_left = 0; v.env_active_right = 0; }
}

void XavixSound::stepPitch(int voice)
{
    Voice& v = s.voice[voice];
    if (!v.enabled) return;
    const int base = voice * 0x10;
    const uint32_t target = uint32_t(((regs(base + 1) << 8) | regs(base + 0)) >> 2);
    if (v.rate < target) v.rate++;
    else if (v.rate > target) v.rate--;
}
