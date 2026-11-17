// PocketPlug - Tamagotchi-style LCD pets on the Epson E0C6S46 (BSD-3-Clause)
// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include "../../core_api.h"
#include "../e0c6s46/e0c6s46.h"

struct TamaConfig {
    std::string romName;
    uint32_t romCrc = 0;
    int variant = 0;     // 0 = E0C6S46, 1 = E0C6S48
};

// Frame buffer layout (32 x 17): rows 0-15 are the 32x16 dot matrix,
// row 16 pixels 0-7 are the eight icons (4 top, 4 bottom). The app draws
// the actual LCD; 0xFF000000 = segment on, 0 = off.
class TamaCore : public Core {
public:
    explicit TamaCore(const TamaConfig& c) : m_cfg(c), m_cpu(c.variant) {}
    bool load(const std::vector<RomFile>& roms, std::string& error) override;
    void reset() override;
    void runFrame() override;
    const uint32_t* frameBuffer() const override { return m_fb; }
    int frameWidth() const override { return 32; }
    int frameHeight() const override { return 17; }
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
    uint32_t m_fb[32 * 17] = {};
    uint32_t m_inputs = 0;
    int m_level = 0;
    uint64_t m_audioPos = 0;          // in units (1/65536 s)
    std::vector<int16_t> m_audio;
    bool m_muteAudio = false;
};
