// PocketPlug - Elan EU3A05 TV game SoC (Air-Blaster Joystick)
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Port of MAME's elan_eu3a05 driver and devices (David Haywood, R. Belmont;
// BSD-3): a 6502 with 16 KB of RAM, 32 KB ROM banking into a 4 MB external
// bus, a DMA engine, custom interrupts delivered through NMI with vectors
// at FFB0 + 4 * level (VBL = level 9, a 2048 Hz timer = level 0), the tile /
// sprite video with an HSL palette, six ADPCM sample channels.
//
// Frame: 256 x 224 RGB. PAL timing (50 Hz).
// Inputs (active high here): 0 up, 1 down, 2 left, 3 right, 4 pause,
// 5 start, 6 trigger, 7 missile; bit 8 RESET.

#pragma once
#include "../../core_api.h"
#include "../../common/m6502.h"
#include <vector>

class Eu3a05Core : public Core {
public:
    Eu3a05Core();
    bool load(const std::vector<RomFile>& roms, std::string& error) override;
    void reset() override;
    void runFrame() override;
    const uint32_t* frameBuffer() const override { return m_frame.data(); }
    int frameWidth() const override { return 256; }
    int frameHeight() const override { return 224; }
    double refreshRate() const override { return 50.0; }
    int sampleRate() const override { return 44100; }
    size_t readAudio(int16_t* out, size_t maxSamples) override;
    void setInputs(uint32_t pressed) override;
    bool saveState(std::vector<uint8_t>& out) override;
    bool loadState(const std::vector<uint8_t>& in) override;

private:
    static constexpr int CPU_HZ = 21281370 / 8;
    static uint8_t rdThunk(void* c, uint16_t a) { return static_cast<Eu3a05Core*>(c)->read(a); }
    static void wrThunk(void* c, uint16_t a, uint8_t d) { static_cast<Eu3a05Core*>(c)->write(a, d); }
    uint8_t read(uint16_t a);
    void write(uint16_t a, uint8_t d);
    uint8_t ext(uint32_t a) const { return m_rom[a & (m_rom.size() - 1)]; }
    uint8_t sysRead(int o);
    void sysWrite(int o, uint8_t d);
    uint8_t vidRead(int o);
    void vidWrite(int o, uint8_t d);
    uint8_t sndRead(int o);
    void sndWrite(int o, uint8_t d);
    void customInterrupt(int level);
    void updatePen(int pen);
    void render();
    void drawTilemaps(int drawpri);
    void drawSprites();
    bool tileData(int base, int drawpri, int& tile, int& attr, int& unk2);
    void tileLine(int tile, int attr, int tilexsize, int i, int xpos, uint32_t* row);
    uint16_t tilemapIndex(uint16_t x, uint16_t y);
    uint16_t scroll(int which) const { return uint16_t(m_s.tileScroll[which * 2] | m_s.tileScroll[which * 2 + 1] << 8); }
    void soundSample();
    int adpcmClock(int ch, uint8_t nib);

    std::vector<uint8_t> m_rom;
    M6502 m_cpu;
    std::vector<uint32_t> m_frame;
    uint32_t m_pen[256];
    uint8_t m_pri[256 * 256];
    std::vector<int16_t> m_audio;
    uint32_t m_inputs = 0;

    struct State {
        uint8_t ram[0x4000];
        uint8_t palram[0x200];
        uint8_t sysRam[0x20];
        uint8_t intmask[2];
        uint16_t nmiVector;
        uint8_t customNmi, rombankHi, rombankLo, pad0;
        uint16_t bank;
        uint8_t dma[7];
        uint8_t gpioDdr[3];
        uint8_t ram5060[0x0e];
        // video
        uint8_t vidRam[0x20];
        uint8_t vidctrl, tileBaseLo, tileBaseHi, sprBaseLo, sprBaseHi, transpen;
        uint8_t tileScroll[8], splitpos[2];
        // sound
        uint32_t sndAddr[6], sndLen[6], sndPos[6];
        int32_t adSignal[6], adStep[6];
        uint8_t stopped, trigger, volumes[2], r50a4, r50a9, pad1[2];
        // timing
        int32_t timerCycles, sndCycles;
        uint32_t rng;
        uint32_t resampleAcc;
    } m_s;
};
