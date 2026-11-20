// PocketPlug - Tamagotchi-style LCD pets on the Epson E0C6S46 (BSD-3-Clause)
// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include "../../core_api.h"
#include "../e0c6s46/e0c6s46.h"

struct TamaConfig {
    std::string romName;
    uint32_t romCrc = 0;
    int variant = 0;     // 0 = E0C6S46, 1 = E0C6S48
    uint32_t osc3 = 0;   // fast oscillator on the board (Hz), 0 = none
    const int8_t* segCol = nullptr;   // segment line -> dot column (-1 = none), size lcdWidth
    int iconSeg[8] = {8, 8, 8, 8, 28, 28, 28, 28};
    int iconCom[8] = {0, 1, 2, 3, 12, 13, 14, 15};
    // > 0: the LCD is a plain dot matrix this wide (x = segment, y = common),
    // as MAME's e0c6s46 screen_update (alienfev); the frame is rawWidth x 16
    int rawWidth = 0;
    // K0x inputs: input bit n drives K0n directly (active low) instead of the pets' A/B/C order
    bool directKeys = false;
};

// Frame buffer layout (32 x 17): rows 0-15 are the 32x16 dot matrix,
// row 16 pixels 0-7 are the eight icons (4 top, 4 bottom). The app draws
// the actual LCD; 0xFF000000 = segment on, 0 = off.
class TamaCore : public Core {
public:
    explicit TamaCore(const TamaConfig& c) : m_cfg(c), m_cpu(c.variant) { m_cpu.setOsc3(c.osc3); }
    bool load(const std::vector<RomFile>& roms, std::string& error) override;
    void reset() override;
    void runFrame() override;
    const uint32_t* frameBuffer() const override { return m_fb; }
    int frameWidth() const override { return m_cfg.rawWidth > 0 ? m_cfg.rawWidth : 32; }
    int frameHeight() const override { return m_cfg.rawWidth > 0 ? 16 : 17; }
    double refreshRate() const override { return 64.0; }
    int sampleRate() const override { return 32768; }
    size_t readAudio(int16_t* out, size_t maxSamples) override;
    void setInputs(uint32_t pressed) override;
    bool saveState(std::vector<uint8_t>& out) override;
    bool loadState(const std::vector<uint8_t>& in) override;
    bool advanceTime(double seconds) override;

private:
    static void buzzerCb(void* ctx, uint64_t when, int level);
    void audioUpTo(uint64_t when);
    void render();

    TamaConfig m_cfg;
    E0C6S46 m_cpu;
    uint32_t m_fb[64 * 17] = {};
    uint32_t m_inputs = 0;
    int m_level = 0;
    uint64_t m_audioPos = 0;          // in units (1/65536 s)
    std::vector<int16_t> m_audio;
    bool m_muteAudio = false;
};
