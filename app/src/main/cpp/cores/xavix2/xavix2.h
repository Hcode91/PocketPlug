// PocketPlug - SSD XaviX 2 (Bandai "Let's! TV Play" second generation)
// SPDX-License-Identifier: BSD-3-Clause
//
// Port of MAME's tvgames/xavix2.cpp and cpu/xavix2 (Olivier Galibert, David
// Haywood), BSD-3-Clause. The XaviX 2 is a 32-bit RISC-like CPU at 98 MHz with
// variable-length (1-4 byte) instructions, 8 registers and 64 "hidden"
// registers (flags, multiply / divide results). The GPU draws lists of 64-bit
// sprite commands into a 2048x1024 surface; a 640x400 window of it is shown.
// No sound yet (MAME has none either).
//
// Inputs: the 32-bit PIO port (Naruto: bits 16 B / Execute, 17 D / Cancel,
// 18 up, 19 down, 0-13 and 22-31 sensor buttons).
#pragma once
#include "../../core_api.h"
#include <vector>

class Xavix2Core : public Core {
public:
    struct Config {
        std::string romName;
        int sdaBit = 21, sclBit = 20;     // 24C08 on the PIO
        int i2cSize = 0x400, i2cPage = 16;
    };
    explicit Xavix2Core(const Config& c);

    bool load(const std::vector<RomFile>& roms, std::string& error) override;
    void reset() override;
    void runFrame() override;
    const uint32_t* frameBuffer() const override { return m_out.data(); }
    int frameWidth() const override { return 640; }
    int frameHeight() const override { return 400; }
    double refreshRate() const override { return 60.0; }
    int sampleRate() const override { return 48000; }
    size_t readAudio(int16_t* out, size_t maxSamples) override;
    void setInputs(uint32_t pressed) override { m_inputs = pressed; }
    void setAnalog(int stick, float x, float y) override { if (stick >= 0 && stick < 2) { m_ax[stick] = x; m_ay[stick] = y; } }
    bool saveState(std::vector<uint8_t>& out) override;
    bool loadState(const std::vector<uint8_t>& in) override;

private:
    enum { F_Z = 1, F_N = 2, F_C = 4, F_V = 8, F_MASK = 15, F_I = 16 };
    enum { IRQ_TIMER = 7, IRQ_LINKDONE = 8, IRQ_LINKPOLL = 10, IRQ_DMA = 12 };

    uint8_t rd8(uint32_t a);
    uint16_t rd16(uint32_t a) { return a >= 0xffffe000u ? uint16_t(ioRead(a, 2)) : uint16_t(rd8(a) | (rd8(a + 1) << 8)); }
    uint32_t rd32(uint32_t a) { return a >= 0xffffe000u ? ioRead(a, 4) : (rd8(a) | (rd8(a + 1) << 8) | (rd8(a + 2) << 16) | (uint32_t(rd8(a + 3)) << 24)); }
    uint64_t rd64(uint32_t a) { return rd32(a) | (uint64_t(rd32(a + 4)) << 32); }
    void wr8(uint32_t a, uint8_t d);
    void wr16(uint32_t a, uint16_t d);
    void wr32(uint32_t a, uint32_t d);
    uint32_t ioRead(uint32_t a, int size);
    void ioWrite(uint32_t a, uint32_t d, int size);

    void execute(int64_t cycles);
    uint32_t checkInterrupt(uint32_t pc);
    uint32_t doAdd(uint32_t a, uint32_t b);
    uint32_t doSub(uint32_t a, uint32_t b);
    uint32_t snz(uint32_t r);
    uint32_t doLsl(uint32_t v, uint32_t s);
    uint32_t doLsr(uint32_t v, uint32_t s);
    uint32_t doAsr(uint32_t v, uint32_t s);

    void irqRaise(int level);
    void irqClear(uint32_t mask);
    void updateIrqLine() { m_s.intLine = m_s.intActive != 0; }
    void gpuUpdate(uint16_t count, uint16_t adr);
    void pioUpdate();
    void i2cSda(int st);
    void i2cScl(int st);
    static uint32_t rgb555(uint16_t c);

    Config m_cfg;
    std::vector<uint8_t> m_rom;          // 16 MB region (the ROM, zero-filled above it)
    std::vector<uint32_t> m_sd;          // 2048 x 1024 drawing surface
    std::vector<uint32_t> m_out;         // 640 x 400
    uint32_t m_inputs = 0;
    int64_t m_icount = 0;

    struct State {
        uint8_t ram[0x10000];
        uint8_t vram[0x20000];           // $C0000800-$C001FFFF (first 0x800 bytes unused: palette)
        uint32_t palette[512];
        uint32_t pc, r[8], hr[64], ilr1;
        uint8_t if1, intLine, wait, eiCount;
        uint32_t intActive;
        uint16_t intNmi, intEnabled, bgColor;
        uint32_t dmaSrc; uint16_t dmaDst, dmaCount;
        int64_t dmaLeft;
        uint32_t pioMode[2], pioData, pioMaskOut;
        uint16_t gpu0Adr, gpu0Count, gpu1Adr, gpu1Count, gpu0bAdr, gpu0bCount, gpu1bAdr, gpu1bCount;
        uint16_t descSizeAdr, descDataAdr;
        // 24Cxx
        uint8_t eep[0x2000], page[32];
        uint8_t scl, sdaw, sdar, est, bits, shift, devsel;
        int byteaddr, pageOffset, pageWritten;
        // the link to the controller unit: a timer polls it, 16 bytes come back by DMA
        uint16_t e270, linkStart, linkEnd, linkLen; int64_t linkLeft;
        // sound: 64 voices of signed 8-bit samples, set up in video RAM at $C0010000 + voice * 64
        struct Voice { uint32_t pos, frac, loop, step; uint8_t on, looping, vol, panL, panR; } voice[64];
        uint16_t sndVol; uint8_t sndPanL, sndPanR;
    } m_s;
    uint8_t m_pkt[16] = {};
    float m_ax[2] = { -0.45f, 0.45f }, m_ay[2] = { 0.1f, 0.1f };
    float m_reach[2] = { 0, 0 };   // a punch toward the camera: the sticker grows over a few frames   // the two glove stickers, -1..1
    void buildPacket();
    bool m_eepInit = false;
    std::vector<int16_t> m_audio;
    void soundCommand(uint16_t cmd);
    void mixAudio(int n);
};
