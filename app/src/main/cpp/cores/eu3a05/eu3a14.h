// PocketPlug - Elan EU3A14 TV game SoC
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Port of MAME's elan_eu3a14 driver and devices (David Haywood; BSD-3): a
// 6502 with 16 KB of RAM, 32 KB banks into an 8 MB external bus, DMA, the
// EU3A05-style custom interrupts (NMI with vectors at FFB0 + 4 * level: a
// 2048 Hz timer = 0, sound ends = 2-7, VBL = 9), paged 4/8 bpp tilemaps and
// bitmap-style sprites read from the bus, 512 HSL colours, six ADPCM voices.
//
// Frame: 256 x 224 RGB at 60 Hz.
// Inputs (1 = pressed): 0 PAUSE, 4 punch, 5 kick, 6 guard, 7 jab, 9 the fourth move,
// 8 RESET. The moves go out over the motion gear's radio link (serialTick).

#pragma once
#include "../../core_api.h"
#include "../../common/m6502.h"
#include <string>
#include <vector>

class Eu3a14Core : public Core {
public:
    explicit Eu3a14Core(const std::string& game);
    bool load(const std::vector<RomFile>& roms, std::string& error) override;
    void reset() override;
    void runFrame() override;
    const uint32_t* frameBuffer() const override { return m_frame.data(); }
    int frameWidth() const override { return 256; }
    int frameHeight() const override { return 224; }
    double refreshRate() const override { return 60.0; }
    int sampleRate() const override { return 44100; }
    size_t readAudio(int16_t* out, size_t maxSamples) override;
    void setInputs(uint32_t pressed) override;
    bool saveState(std::vector<uint8_t>& out) override;
    bool loadState(const std::vector<uint8_t>& in) override;

private:
    static constexpr int CPU_HZ = 21477272 / 2;
    static uint8_t rdThunk(void* c, uint16_t a) { return static_cast<Eu3a14Core*>(c)->read(a); }
    static void wrThunk(void* c, uint16_t a, uint8_t d) { static_cast<Eu3a14Core*>(c)->write(a, d); }
    uint8_t read(uint16_t a);
    void write(uint16_t a, uint8_t d);
    uint8_t ext(uint32_t a) const { return m_rom[a & (m_rom.size() - 1)]; }
    uint8_t sysRead(int o);
    void sysWrite(int o, uint8_t d);
    uint8_t portRead(int port);
    void customInterrupt(int level);
    void updatePen(int pen);
    uint8_t vram(int offset) const { int a = offset + 0x200; return a < 0x4000 ? m_s.ram[a] : 0; }
    void render();
    int xscrollFor(int y) const;
    void drawTile(int bpp, int tile, int palette, int priority, int xpos, int ypos, int size, int base, bool fromRam);
    void drawPage(int ramstart, int ramend, int xbase, int ybase, int size, int bpp, int base, int pagewidth, int bytespertile, uint8_t palettepri, bool fromRam);
    void drawBackground();
    void drawRamLayer();
    void drawSprites();
    void spritePix(int realx, int y, int priority, uint8_t pix, uint8_t mask, uint8_t shift, int palette);
    void spriteLine(int offset, int line, int palette, int flipx, int priority, int xpos, int ypos, int bpp);
    void soundSample();
    void serialTick();
    int gearCode() const;
    int adpcmClock(int ch, uint8_t nib);

    std::string m_game;
    std::vector<uint8_t> m_rom;
    M6502 m_cpu;
    std::vector<uint32_t> m_frame;
    uint32_t m_pen[512];
    uint8_t m_pri[256 * 224];
    std::vector<int16_t> m_audio;
    uint32_t m_inputs = 0;
    int m_defaultSpriteAddr = 0x0c;
    int m_tileRamBase = 0;
    int m_wrapW = 256, m_wrapH = 224;
    int wrapX(int x, bool fromRam) const { return fromRam ? x : ((x % m_wrapW) + m_wrapW) % m_wrapW; }

    struct State {
        uint8_t ram[0x4000];
        uint8_t palram[0x400];
        uint8_t sysRam[0x20];
        uint8_t intmask[2];
        uint16_t nmiVector;
        uint8_t customNmi, rombankHi, rombankLo, portDir[3], portOut[3];
        uint16_t bank;
        uint8_t dma[9];
        // video (5100-517f)
        uint8_t vid[0x80];
        // sound
        uint32_t sndAddr[6], sndLen[6], sndPos[6];
        int32_t adSignal[6], adStep[6];
        uint8_t stopped, trigger, volumes[2], r50a4, r50a9;
        int32_t timerCycles, sndCycles;
        uint32_t rng, resampleAcc;
        // the motion gear's link (port B bit 2)
        uint8_t serLine, serGap[40];
        int32_t serWait, serPos, serLen;
    } m_s;
};
