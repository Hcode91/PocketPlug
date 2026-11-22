// PocketPlug - XaviX sound: 16 wavetable voices with envelopes
// SPDX-License-Identifier: BSD-3-Clause
//
// Port of MAME's tvgames/xavix_sound.cpp (ramacat, David Haywood), BSD-3-Clause.
// Runs at the hardware sequencer rate (master clock / 128 = 167,791 Hz).
#pragma once
#include <cstdint>

class XavixSound {
public:
    typedef uint8_t (*ReadFn)(void* ctx, uint32_t a);
    typedef void (*WriteFn)(void* ctx, uint32_t a, uint8_t d);
    void setCallbacks(void* ctx, ReadFn regs, WriteFn regsW, ReadFn samples) { m_ctx = ctx; m_regs = regs; m_regsW = regsW; m_samples = samples; }

    static constexpr uint32_t RATE_HZ = 167791;

    void reset(uint8_t tempoReset);
    void enableVoice(int voice, bool updateOnly);
    void disableVoice(int voice) { s.voice[voice].enabled = 0; }
    bool voiceEnabled(int voice) const { return s.voice[voice].enabled != 0; }
    void setTempo(int index, uint8_t value);
    void setCyclerate(uint8_t value);
    double tempoTickHz(uint8_t tempo) const;

    uint8_t volumeR() const { return s.mastervol; }
    void volumeW(uint8_t d) { s.mastervol = d; }
    uint8_t mixerR() const { return (s.monaural ? 0x80 : 0) | ((s.capacity & 3) << 4) | (s.amp & 7); }
    void mixerW(uint8_t d);
    uint8_t dacR() const { return ((s.dac & 1) << 7) | ((s.gap & 3) << 5) | ((s.lead & 7) << 2) | (s.lag & 3); }
    void dacW(uint8_t d) { s.dac = d >> 7 & 1; s.gap = d >> 5 & 3; s.lead = d >> 2 & 7; s.lag = d & 3; }

    // one output sample (left + right, each roughly +-32767)
    void tick(int32_t& l, int32_t& r);

    struct Voice {
        uint8_t enabled;
        uint32_t position, loop_position, start_position;
        uint32_t env_pos_left, env_pos_right;
        uint8_t env_bank, env_mode, bank;
        uint32_t rate;
        uint8_t type, vol;
        uint16_t env_rom_base_left, env_rom_base_right;
        uint8_t env_vol_left, env_vol_right;
        uint32_t env_period_samples, env_countdown;
        uint8_t env_active_left, env_active_right;
        uint16_t noise_state;
    };
    struct State {
        Voice voice[16];
        uint8_t tempo_div[4];
        uint8_t cyclerate_div;
        uint8_t monaural, capacity, amp, dac, gap, lead, lag, mastervol;
        int32_t gain;
    } s;

private:
    uint8_t regs(uint32_t a) { return m_regs(m_ctx, a); }
    void regsW(uint32_t a, uint8_t d) { m_regsW(m_ctx, a, d); }
    uint8_t sample(uint32_t a) { return m_samples(m_ctx, a); }
    uint32_t tempoToPeriodTicks(uint8_t tp) const;
    uint32_t envelopePeriodTicks(uint8_t tp) const;
    void stepEnvelope(int voice);
    void stepPitch(int voice);
    uint8_t envByte(int voice, uint16_t addr) { return sample((uint32_t(s.voice[voice].env_bank) << 16) | addr); }

    void* m_ctx = nullptr;
    ReadFn m_regs = nullptr, m_samples = nullptr;
    WriteFn m_regsW = nullptr;
};
