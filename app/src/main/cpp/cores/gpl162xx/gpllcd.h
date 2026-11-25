// PocketPlug - GeneralPlus GPL162xx LCD handhelds (BornKid BC-19 218-in-1, PCP 8718 / 8728)
// SPDX-License-Identifier: BSD-3-Clause
//
// Port of MAME's handheld/generalplus_gpl162xx_lcdtype.cpp (David Haywood, BSD-3)
// with the BL handhelds menu controller (bl_handhelds_menucontrol, unSP variant):
// a u'nSP 2.0 at 96 MHz running from a small internal ROM (F000h) that streams
// the game from SPI flash into RAM, its system DMA, the 7300h / 7400h banked
// RAM, and an external 320 x 240 RGB565 LCD controller (command 22h writes
// pixels). IRQ 4 at 60 Hz. MAME has no sound for these yet, nor do we.
//
// Inputs come from the vertical handheld skin (Up 0x01, Down 0x02, Left 0x04,
// Right 0x08, A 0x10, B 0x20, Start 0x40) and are moved to the unit's bits.

#pragma once
#include "../../core_api.h"
#include "../spg2xx/unsp.h"
#include <vector>

class GplLcdCore : public Core, public unsp_device::Bus {
public:
    GplLcdCore(const char* flashName, uint32_t flashSize);
    bool load(const std::vector<RomFile>& roms, std::string& error) override;
    void reset() override;
    void runFrame() override;
    const uint32_t* frameBuffer() const override { return m_out.data(); }
    int frameWidth() const override { return 320; }
    int frameHeight() const override { return 240; }
    double refreshRate() const override { return 60.0; }
    int sampleRate() const override { return 44100; }
    size_t readAudio(int16_t* out, size_t maxSamples) override;
    // app bits (HandheldView): Up 01, Down 02, Left 04, Right 08, A 10, B 20, Start 40
    void setInputs(uint32_t p) override {
        m_inputs = (p & 0x40 ? 0x01 : 0) | (p & 0x01 ? 0x02 : 0) | (p & 0x02 ? 0x04 : 0) | (p & 0x04 ? 0x08 : 0)
                 | (p & 0x08 ? 0x10 : 0) | (p & 0x10 ? 0x20 : 0) | (p & 0x20 ? 0x40 : 0)
                 | (p & 0x80 ? 0x80 : 0) | (p & 0x100 ? 0x100 : 0);
    }
    bool saveState(std::vector<uint8_t>&) override { return false; }
    bool loadState(const std::vector<uint8_t>&) override { return false; }

    uint16_t read_word(uint32_t a) override;
    void write_word(uint32_t a, uint16_t d) override;

private:
    void spiTx(uint8_t d);
    uint8_t spiNext();
    void menuClock(int state);
    void menuCommand();

    std::string m_flashName;
    uint32_t m_flashSize;
    std::vector<uint16_t> m_rom;     // internal ROM (F000h)
    std::vector<uint8_t> m_spi;
    uint16_t m_ram[0x7000];
    uint16_t m_bank7300[0x400], m_bank7400[0x800], m_7100[0x100];
    uint16_t m_703a = 0, m_707e = 0, m_7860 = 0, m_7868 = 0, m_78a1 = 0;
    uint16_t m_dma[8] = {};
    int m_spiState = 0;
    uint32_t m_spiAddr = 0;
    uint8_t m_rx[5] = {};
    int m_lcdState = 0, m_lcdCmd = 0;
    uint32_t m_lcdAddr = 0;
    std::vector<uint8_t> m_display;
    // menu controller
    uint8_t mCommand = 0, mCommandBit = 0, mResponse = 0, mResponseBit = 0, mShift = 0, mClockState = 0;
    int mState = 0;
    uint16_t mPos = 0;
    uint32_t m_inputs = 0;
    int m_silence = 0;
    unsp_device m_cpu;
    std::vector<uint32_t> m_out;
};
