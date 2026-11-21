// PocketPlug - GeneralPlus GPL32600 sound unit
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The SPU at 0xd0400000 is the 32-channel SPG2xx / GPL162xx SPU with each 16-bit register in
// a 32-bit slot: channel attributes at 0x1000 + (ch & 15) * 0x40 (+ 0x400 for channels 16-31),
// phase registers at 0x1800 + the same, and the control registers at 0xe00 (channels 0-15)
// and 0xe80 (channels 16-31). Sample addresses count 16-bit words in SDRAM.
// Channel playback, envelopes and ramp-down follow MAME's spg2xx_audio.cpp (Ryan Holtz,
// Jonathan Gevaryahu, BSD-3-Clause) as ported for PocketPlug's SPG2xx core.
#pragma once
#include <cstdint>
#include <functional>

class GplSpu {
public:
    std::function<uint16_t(uint32_t wordAddr)> readWord;

    void reset();
    // offset = address - 0xd0400000
    uint32_t read(uint32_t offset);
    void write(uint32_t offset, uint32_t value);
    void render(int16_t* out, int samples, int rate);

private:
    enum {   // channel attribute registers
        WAVE_ADDR = 0, MODE = 1, LOOP_ADDR = 2, PAN_VOL = 3, ENVELOPE0 = 4, ENVELOPE_DATA = 5,
        ENVELOPE1 = 6, ENVELOPE_ADDR_HIGH = 7, ENVELOPE_ADDR = 8, WAVE_DATA_PREV = 9,
        ENVELOPE_LOOP_CTRL = 10, WAVE_DATA = 11, ADPCM_SEL = 13,
    };
    enum {   // phase registers
        PHASE_HIGH = 0, PHASE_ACCUM_HIGH = 1, TARGET_PHASE_HIGH = 2, RAMP_DOWN_CLOCK = 3,
        PHASE = 4, PHASE_ACCUM = 5, TARGET_PHASE = 6, PHASE_CTRL = 7,
    };
    enum {   // control registers (one bank per 16 channels)
        CHANNEL_ENABLE = 0, MAIN_VOLUME = 1, FIQ_ENABLE = 2, FIQ_STATUS = 3, BEAT_BASE_COUNT = 4,
        BEAT_COUNT = 5, ENVCLK0 = 6, ENVCLK0_HIGH = 7, ENVCLK1 = 8, ENVCLK1_HIGH = 9,
        ENV_RAMP_DOWN = 10, CHANNEL_STOP = 11, ZERO_CROSS = 12, CONTROL = 13, COMPRESS = 14,
        CHANNEL_STATUS = 15, WAVE_IN_L = 16, WAVE_IN_R = 17, CHANNEL_REPEAT = 20, ENV_MODE = 21,
        TONE_RELEASE = 22, ENV_IRQ = 23,
    };
    struct Ima { int32_t signal = 0, step = 0; int16_t clock(uint8_t nibble); };

    uint16_t m_attr[32][16] = {};
    uint16_t m_phase[32][8] = {};
    uint16_t m_ctrl[2][32] = {};
    uint32_t m_wordAddr[32] = {};     // current sample address (full width)
    uint32_t m_loopAddr[32] = {};
    double m_rate[32] = {}, m_accum[32] = {};
    uint32_t m_shift[32] = {};
    uint32_t m_envAddr[32] = {};
    uint32_t m_envFrame[32] = {}, m_rampFrame[32] = {};
    Ima m_ima[32];

    uint16_t& ctrl(int ch, int reg) { return m_ctrl[ch >> 4][reg]; }
    bool status(int ch) { return ctrl(ch, CHANNEL_STATUS) & (1u << (ch & 15)); }
    void startChannel(int ch);
    void stopChannel(int ch);
    void ctrlWrite(int bank, int reg, uint16_t v);
    bool fetchSample(int ch);
    bool advance(int ch, double outRate);
    void envelopeTick(int ch);
    void rampdownTick(int ch);
    uint32_t envelopeClock(int ch);
    uint16_t edd(int ch) { return m_attr[ch][ENVELOPE_DATA] & 0x7f; }
};
