// PocketPlug - Entex Adventure Vision (1982 tabletop console)
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Port of MAME's advision.cpp (Dan Boris, hap; BSD-3-Clause): INS8048 at
// 11 MHz with 1 KB of internal BIOS ROM, 1 KB of external RAM in four banks,
// 4 KB cartridges, a COP411 sound CPU driving a 1-bit speaker, and a column
// of 40 red LEDs swept across the screen by a spinning mirror (~14 Hz).
//
// The picture is 480 x 160: 480 LED samples (20 us apart) per sweep, each of
// the 40 LEDs four lines tall (three lit).

#pragma once
#include "../../core_api.h"
#include "../../common/mcs48.h"
#include "../../common/cop410.h"
#include <vector>

class AdvisionCore : public Core, private Mcs48::Bus, private Cop410::Bus {
public:
    AdvisionCore();
    bool load(const std::vector<RomFile>& roms, std::string& error) override;
    void reset() override;
    void runFrame() override;
    const uint32_t* frameBuffer() const override { return m_frame.data(); }
    int frameWidth() const override { return W; }
    int frameHeight() const override { return H; }
    double refreshRate() const override { return 60.0; }
    int sampleRate() const override { return 44100; }
    size_t readAudio(int16_t* out, size_t maxSamples) override;
    // bits: 0 up, 1 down, 2 left, 3 right, 4-7 buttons 1-4
    void setInputs(uint32_t pressed) override { m_inputs = pressed; }
    bool saveState(std::vector<uint8_t>& out) override;
    bool loadState(const std::vector<uint8_t>& in) override;

    static constexpr int W = 480, H = 160, COLS = 0x400;

private:
    // Mcs48::Bus
    uint8_t mcsProgram(uint16_t a) override { return a < m_cart.size() ? m_cart[a] : 0xff; }
    uint8_t mcsExtRead(uint8_t a) override;
    void mcsExtWrite(uint8_t a, uint8_t d) override { m_s.extRam[(m_s.ramBank << 8) | a] = d; }
    uint8_t mcsPortRead(int port) override;
    void mcsPortWrite(int port, uint8_t d) override;
    int mcsTest(int t) override;
    // Cop410::Bus
    uint8_t copReadL() override { return m_s.soundCmd; }
    void copWriteG(uint8_t v) override { m_s.speaker = v & 1; }
    void copWriteD(uint8_t v) override { m_s.volumeHalf = v & 1; }

    void advance(int cycles);
    void sampleColumn();
    void present();

    uint8_t m_bios[0x400];
    uint8_t m_soundRom[0x200];
    std::vector<uint8_t> m_cart;
    Mcs48 m_cpu;
    Cop410 m_cop;
    uint32_t m_inputs = 0;
    std::vector<uint32_t> m_frame;
    std::vector<int16_t> m_audio;
    double m_audioAcc = 0, m_audioPhase = 0;
    double m_copPhase = 0;
    double m_frameCarry = 0;
    double m_dcX = 0, m_dcY = 0;

    struct State {
        uint8_t extRam[0x400];
        uint8_t ramBank, videoStrobe, videoEnable, videoBank;
        uint8_t ledOutput[5], ledLatch[5];
        uint8_t soundCmd, soundReset, speaker, volumeHalf;
        int64_t time;               // 8048 cycles since power-on
        int64_t sweepStart;         // start of the current mirror sweep
        int64_t ledOffAt;           // pending "LEDs off" (-1 = none)
        int32_t hpos;
        uint8_t display[40 * COLS];
    } m_s;
};
