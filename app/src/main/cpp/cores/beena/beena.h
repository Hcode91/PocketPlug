// PocketPlug - Sega Toys Advanced Pico BEENA (2005)
// SPDX-License-Identifier: BSD-3-Clause
//
// Port of MAME's sega_beena.cpp (license BSD-3-Clause, copyright-holders R. Belmont, QUFB) and
// its AP2010 PCM device. The SoC ("9H0-0008", an Applause AP2010) is a big-endian ARM7TDMI at
// 81 MHz; it runs here on DidjDroid's ARM926 interpreter in big-endian mode. Memory is kept as
// host-order 32-bit words, so bytes and halfwords inside a word sit at offset ^ 3 / ^ 2.
//
// Inputs: bits 0-4 left pad up/down/left/right/red, bits 5-9 right pad up/down/left/right/red,
// bit 10 pen pressed. setAnalog(2, x, y): pen on the tablet (screen, 0..1); setAnalog(3, x, y):
// pen on the storyware page (0..1). Option 0: open page (0 = closed book, 1-6).
// Not emulated (as in MAME): the MIDI synthesizer (music), card readers.

#pragma once
#include "../../core_api.h"
#include "../gpl32/arm926.h"
#include <vector>

class BeenaCore : public Core, public didj::Bus {
public:
    BeenaCore();
    ~BeenaCore() override;
    bool load(const std::vector<RomFile>& roms, std::string& error) override;
    void reset() override;
    void runFrame() override;
    const uint32_t* frameBuffer() const override { return m_fb.data(); }
    int frameWidth() const override { return W; }
    int frameHeight() const override { return H; }
    double refreshRate() const override { return 60.0; }
    int sampleRate() const override { return 16000; }
    size_t readAudio(int16_t* out, size_t maxSamples) override;
    void setInputs(uint32_t pressed) override { m_keys = pressed; }
    void setAnalog(int stick, float x, float y) override;
    void setOption(int id, int value) override;
    void setWallClock(int64_t localSeconds) override { m_wallClock = localSeconds; }
    bool saveState(std::vector<uint8_t>&) override { return false; }
    bool loadState(const std::vector<uint8_t>&) override { return false; }

    // didj::Bus
    uint8_t* page_ptr(uint32_t pa) override;
    bool page_readonly(uint32_t pa) override;
    uint32_t read32(uint32_t pa) override;
    uint16_t read16(uint32_t pa) override;
    uint8_t read8(uint32_t pa) override;
    void write32(uint32_t pa, uint32_t v) override;
    void write16(uint32_t pa, uint16_t v) override;
    void write8(uint32_t pa, uint8_t v) override;

    static constexpr int W = 704, H = 480;

private:
    uint32_t ioRead(uint32_t pa);
    void ioWrite(uint32_t pa, uint32_t data, uint32_t mask);
    void palWrite(int idx, uint16_t data, uint16_t mask);
    void scanline(int param);
    void pulseIrq(bool fiq);
    void updateRtc();
    void updateSensor(int offset);
    void findIdleLoop();

    // memcache (the non-mapped cache at 0x500300bc / 0x500300c0)
    uint32_t memcacheRead();
    void memcacheAdvance(uint32_t& status);
    void memcacheParseBit(uint32_t& status);
    uint8_t stPop();
    void stPush(uint8_t s);
    uint8_t evPop();
    void evPush(uint8_t e);
    void stClear() { m_stSize = m_stHead = m_stTail = 0; }

    // PCM
    void pcmWrite(int off, uint32_t data, uint32_t mask);
    uint32_t pcmRead(int off);
    int16_t pcmPop();

    // video
    void render();
    void drawLayer(uint32_t mapAddr, int scrollIdx, bool active, bool overlay);
    void drawLayerTiles(uint32_t mapAddr, int scrollIdx, bool overlay);
    void drawLayerScanlines(uint32_t mapAddr, int scrollIdx, bool overlay);
    void drawSprites(bool overlay);
    void drawBitmap();
    void screenBlend();
    int scrollX(int x, int i);
    int scrollY(int y, int i);
    // draws one 16x16 8bpp tile scaled by 'sc'; linePal: use the per-scanline palette cache
    void drawTile(uint32_t* dst, int dw, int dh, const uint8_t* tiles, int code, bool fx, bool fy,
                  int x, int y, int sc, bool linePal);

    inline uint8_t vb8(uint32_t a) const { return m_vram[(a & 0x1fffff) ^ 3]; }
    inline uint16_t vb16(uint32_t a) const { uint16_t v; memcpy(&v, &m_vram[(a & 0x1ffffe) ^ 2], 2); return v; }
    inline uint16_t vid16(uint32_t a) const { uint16_t v; memcpy(&v, &m_vid[((a - 0x40010000u) & ~1u) ^ 2], 2); return v; }

    didj::ARM926* m_cpu = nullptr;
    std::vector<uint8_t> m_bios;           // 128 KB, host-order words
    std::vector<uint8_t> m_cart;           // host-order words, power-of-two size
    uint32_t m_cartBase = 0x80000000u;
    std::vector<uint8_t> m_iram, m_vid, m_ram60, m_ram70, m_vram;
    uint32_t m_io50[4] = {};
    uint32_t m_videoRegs[64] = {};
    uint32_t m_sensor[32] = {};
    uint32_t m_aux[64] = {};
    uint16_t m_rtc[0x80] = {};
    uint32_t m_midiRegs[8] = {};
    uint32_t m_midiBusy = 0;
    int m_scale = 1;
    int m_scanline = 0;
    bool m_reqFiq = false;
    uint32_t m_idleAddr = 0xffffffffu;
    int64_t m_wallClock = 0;

    uint32_t m_palette[256] = {};
    std::vector<uint32_t> m_palCache;      // 256 per scanline (0 = not written on that line)
    std::vector<uint32_t> m_cacheLayer;    // 1024 x 512 for x-scrolled scanlines

    uint32_t m_keys = 0;
    float m_penX = 0.5f, m_penY = 0.5f;
    bool m_penBook = false;
    int m_page = 0, m_effPage = 0;

    // memcache
    uint8_t m_mc[0x800] = {};
    uint32_t m_mcSeq = 0, m_mcAddr = 0;
    uint8_t m_mcData = 0, m_mcI = 0, m_mcState = 0;
    uint8_t m_st[0x100] = {}; uint16_t m_stSize = 0, m_stHead = 0, m_stTail = 0;
    uint8_t m_ev[0x100] = {}; uint16_t m_evSize = 0, m_evHead = 0, m_evTail = 0;

    // PCM
    uint32_t m_pcmRegs[16] = {};
    float m_pcmVol = 1.0f;
    int m_pcmRate = 8000;
    static constexpr int FIFO = 0x800;
    uint16_t m_fifo[FIFO] = {}; int m_fifoSize = 0, m_fifoHead = 0, m_fifoTail = 0;
    uint16_t m_fast[FIFO] = {}; int m_fastSize = 0, m_fastHead = 0, m_fastTail = 0;
    int m_pcmAcc = 0, m_outAcc = 0;
    int16_t m_pcmCur = 0;
    std::vector<int16_t> m_audio;

    std::vector<uint32_t> m_fb;
    uint64_t m_frame = 0;
};
