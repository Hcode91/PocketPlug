// PocketPlug - "trkfldch" SoC (65C816-based TV game chip: 4 Player System 10-in-1)
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Port of MAME's trkfldch.cpp driver (David Haywood, BSD-3-Clause) on MAME's
// G65816 CPU core (Karl Stenerud, BSD-3-Clause). The tile layers, sprites, YUV
// palette, DMA and interrupt timing follow the MAME driver. The chip's sound
// isn't known (MAME has none either), so the machine is silent.

#pragma once
#include "../../core_api.h"
#include "g65816/g65816.h"
#include <vector>

class TrkfldCore : public Core, private G65816Bus {
public:
    explicit TrkfldCore(const std::string& romName, uint32_t romSize);
    bool load(const std::vector<RomFile>& roms, std::string& error) override;
    void reset() override;
    void runFrame() override;
    const uint32_t* frameBuffer() const override { return m_out; }
    int frameWidth() const override { return 320; }
    int frameHeight() const override { return 240; }
    double refreshRate() const override { return 60.0; }
    int sampleRate() const override { return 44100; }
    size_t readAudio(int16_t* out, size_t maxSamples) override;
    // bits 0-7: player 1 (START, SELECT, up, down, left, right, button 1, button 2);
    // bits 8-13, 16-21, 24-29: players 2-4 (up, down, left, right, button 1, button 2)
    void setInputs(uint32_t pressed) override { m_inputs = pressed; }
    bool saveState(std::vector<uint8_t>& out) override;
    bool loadState(const std::vector<uint8_t>& in) override;

private:
    class Cpu : public g65816_device {
    public:
        void setLine(int line, int state) { execute_set_input(line, state); }
    };

    // G65816Bus
    uint8_t read(uint32_t a) override;
    void write(uint32_t a, uint8_t d) override;
    uint8_t vector(uint32_t a) override;

    void scanline(int line);
    void render();
    void renderTileLayer(int which);
    void drawSprites(int pri);
    // MAME gfx decode of one pixel (planes / x / y bit offsets as in gfx_trkfldch)
    int pixel(int layout, uint32_t code, int x, int y) const;
    void drawTile(int layout, uint32_t code, int color, bool flipx, bool flipy, int sx, int sy, int scale);
    void doDma();
    uint8_t romByte(uint32_t off) const { return off < m_rom.size() ? m_rom[off] : 0; }

    std::string m_romName;
    uint32_t m_romSize;
    std::vector<uint8_t> m_rom;     // 16 MB region (ROMREGION_ERASE00)
    Cpu m_cpu;
    uint32_t m_inputs = 0;
    uint32_t m_out[320 * 240];
    uint8_t m_bitmap[320 * 240];    // pens
    uint32_t m_pal[256];

    struct State {
        uint8_t ram[0x4000];
        uint8_t spriteram[0x500];
        uint8_t palram[0x300];
        uint8_t dmaregs[0xe], modebank[0xb], tilemapbase[3], sysregs[0x10], unkregs[0x90];
        uint8_t tmapscroll[2][0x12];
        uint8_t whichVector;
        uint32_t rng;
    } m_s;
    int m_cycleDebt = 0;
    double m_frac = 0;
    size_t m_pendingSamples = 0;
};
