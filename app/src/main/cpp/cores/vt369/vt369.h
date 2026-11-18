// PocketPlug - VT369 "NES-on-a-chip" (phone-case and handheld multi-carts)
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Port of MAME's VT02/VT03/VT3xx SoC (nes_vt_soc.cpp, nes_vt369_vtunknown_soc.cpp,
// David Haywood, BSD-3-Clause), its PPU (ppu2c0x.cpp - Ernesto Corvi, Brad Oliver,
// Fabio Priuli; ppu2c0x_vt.cpp - David Haywood; BSD-3-Clause) and the NES APU
// (nes_apu.cpp - Matthew Conte, GPL-2.0+). MAME's device/timer framework is
// replaced by a plain scanline loop with the same event order and timings.

#pragma once
#include "../../core_api.h"
#include "../../common/m6502.h"
#include <vector>

struct Vt369Config {
    std::string romName;
    uint32_t romSize = 0x400000;
    int crypt = -1;            // opcode bitswap (MAME which_crypt), -1 = none
    bool bank41e6 = false;     // 41e6 bits 0-1 select a 32 MB ROM bank (red5mam style)
    std::string internalRomName;   // VT3XX_INTERNAL_* (4 KB, 0x1000 main / 0x4000 sound)
    std::string protRomName;       // vt_menu_protection serial ROM (4152/4153)
    int protStartByte = 0;
    bool case36 = false;           // enable_36pcase_gpio + 36pcase late protection protocol
};

class Vt369Core : public Core {
public:
    explicit Vt369Core(const Vt369Config& c);
    bool load(const std::vector<RomFile>& roms, std::string& error) override;
    void reset() override;
    void runFrame() override;
    const uint32_t* frameBuffer() const override { return m_out; }
    int frameWidth() const override { return 256; }
    int frameHeight() const override { return 240; }
    double refreshRate() const override { return 60.0988; }
    int sampleRate() const override { return 44100; }
    size_t readAudio(int16_t* out, size_t maxSamples) override;
    // bits: A, B, Select, Start, Up, Down, Left, Right (as MAME's vt369 IO0)
    void setInputs(uint32_t pressed) override { m_inputs = pressed; }
    bool saveState(std::vector<uint8_t>& out) override;
    bool loadState(const std::vector<uint8_t>& in) override;

    // ---------- bus (public for the CPU callbacks) ----------
    uint8_t cpuRead(uint16_t a);
    void cpuWrite(uint16_t a, uint8_t d);

private:
    Vt369Config m_cfg;
    std::vector<uint8_t> m_rom;     // 4 MB SPI flash image
    M6502 m_cpu;
    M6502 m_spu;                    // sound CPU (runs at the 21.47 MHz master clock)
    std::vector<uint8_t> m_internal;    // internal ROM (empty = not present)
    std::vector<uint8_t> m_protRom;     // serial protection ROM
    uint8_t gpioBusLatch() const { return (m_s.gpio414x[0x0f] & 0x1f) | ((m_s.gpio41ex[0x07] & 0x1c) << 3); }
    uint8_t gpioBusR() const { return m_s.gpioResponsePos < m_s.gpioResponseLen ? m_s.gpioResponse[m_s.gpioResponsePos] : gpioBusLatch(); }
    void gpioBusW();
    void protWriteClock(int state);
    void protWriteEnable(int state);
    void flushOamDma();
    uint8_t m_cryptTable[256];
    uint32_t m_inputs = 0;
    uint32_t m_out[256 * 240];

    // ---------------- emulation state (POD, saved as one block) ----------------
    struct State {
        // memory
        uint8_t ram[0x1000];
        uint8_t ram6000[0x2000];
        uint8_t soundram[0x800];
        uint8_t ntram[0x2000];
        uint8_t chrram[0x2000];
        // SoC (nes_vt02_vt03)
        uint8_t r410x[0x10];
        uint8_t r411c, r411d, r4242;
        uint8_t relative[2];
        uint8_t addrLatch8000;
        int32_t timerIrqEnabled, timerRunning, timerVal;
        uint8_t vdmaCtrl, newdma4024;
        int32_t bankaddr[4];
        uint8_t initialE000;
        // SoC (vt3xx)
        uint8_t bank6000, bank6000Enable, r411f;
        uint8_t alu[8];
        uint8_t dir415x, data4152, data4153, dir414x, data414a, data414b;
        uint8_t gpio414x[0x10], gpio415x[0x10], gpio41ex[0x10];
        // controller
        uint8_t latch0, latch1, prevPort0;
        // CPU timing
        int32_t clockMul;          // 1, or 3 when 411c bit 7 is set
        // ---- PPU ----
        int32_t scanline;
        uint8_t spriteram[0x200];
        uint8_t paletteRam[0x100];
        uint8_t regs[8];
        uint32_t tilePage, spritePage;
        uint8_t backColor;
        int32_t refreshData, refreshLatch;
        uint8_t xFine;
        uint8_t toggle;
        int32_t tilecount;
        uint8_t renderAtHblank;
        int32_t add;
        uint32_t videomemAddr;
        uint8_t dataLatch, bufferedData;
        uint8_t planebuf[16];
        uint8_t videobank0[6], videobank0Extra, videobank1;
        uint8_t extModes, extModes2;
        int32_t readBg4Bg3, whichpixel;
        uint8_t newvid1b, newvid1c, newvid1d, newvid1e;
        uint8_t tilebases[4];
        uint8_t vt3xxPalette[0x400];
        uint16_t dmaVramTarget;
        uint8_t extraSpriteBits;
        uint8_t screenregs204x[0xa], spritehigh2008, ext201f, lightgun2024, lcdc2050;
        int32_t visMinX, visMaxX, visMinY, visMaxY;
        // ---- APU (nes_defs.h apu_t) ----
        struct Square { uint8_t regs[4]; int32_t vbl_length, freq; float phaseacc, env_phase, sweep_phase; uint8_t adder, env_vol, enabled, output; } squ[2];
        struct Triangle { uint8_t regs[4]; int32_t linear_length; uint8_t linear_reload; int32_t vbl_length, write_latency; float phaseacc; uint8_t adder, counter_started, enabled, output; } tri;
        struct Noise { uint8_t regs[4]; uint16_t lfsr; int32_t vbl_length; float phaseacc, env_phase; uint8_t env_vol, enabled, output; } noi;
        struct Dpcm { uint8_t regs[4]; uint32_t address, length; int32_t bits_left; float phaseacc; uint8_t cur_byte, enabled, irq_occurred; int16_t vol; uint8_t output; } dpcm;
        uint8_t stepMode, frameIrqEnabled, frameIrqOccurred;
        int64_t frameTimerAt;      // next APU frame-timer tick (dots), -1 = stopped
        int64_t lineStart;         // dot at which the current scanline began
        int32_t evPhase;           // 0 = hblank pending, 1 = end of line pending
        int64_t nmiAt;             // pending NMI (dots), -1 = none
        // timing
        int64_t dot;               // master time in PPU dots since power-on
        int64_t apuDot;            // APU synced up to here (12 dots per APU sample)
        double resampleAcc;
        double resampleSum;
        int32_t resampleN;
        double dcPrevIn, dcPrevOut;
        // external banking / opcode scrambling
        uint32_t ahigh;
        uint8_t encrypted;
        // ---- sound CPU (VT3xx SPU) ----
        uint8_t spuRam[0x1800];
        uint8_t spuRunning;
        uint16_t spuTimerPeriod;
        uint8_t spuTimerCtrl;
        double spuTimerNext;       // next timer IRQ (sound-CPU cycles), <0 = off
        int64_t spuCycle;          // sound CPU time (4 cycles per dot)
        uint8_t adderAddr[2], adderResult[2], adpcmAddr[2], adpcmResult[2], dac[4];
        int16_t dacOut[2];
        struct SpuRegs { uint16_t pc; uint8_t a, x, y, s, p, databank, irq; } spuRegs;
        // 36pcase GPIO handshake, LCDC NMI and deferred OAM DMA (vt3xx_soc_base_device)
        uint8_t gpioWriteHistory[2], gpioResponse[2], gpioResponsePos, gpioResponseLen;
        uint16_t oamDmaSrc[4], oamDmaDst[4];
        uint8_t oamDmaLen[4], oamDmaCount;
        uint16_t oamDmaNext;
        int64_t lcdcNmiAt, oamDmaAt;
        // vt_menu_protection_device
        uint8_t protCommand, protCommandBits, protState, protLatch, protLateResponse, protLateResponsePos;
        uint16_t protReadPos;
        uint8_t protInData, protInEnable, protInClock;
    } m_s;

    // ---------------- helpers ----------------
    uint32_t m_bitmapBuf[240 * (256 + 64)];
    uint32_t* pix(int y, int x) { return &m_bitmapBuf[y * (256 + 64) + 32 + x]; }
    uint32_t m_pens[0x200 + 0x1000 * 8];       // NES palette + VT03 HSV palette
    uint32_t m_rgb555[0x8000 * 8];
    uint32_t m_rgb444[0x1000 * 8];
    float m_squareLut[31];
    float m_tndLut[16][16][128];
    uint32_t m_sampsPerSync, m_vblTimes[0x20], m_syncTimes1[0x20], m_syncTimes2[0x80];
    std::vector<int16_t> m_audio;
    int64_t m_cpuDot = 0;          // CPU's position (dots) within the frame loop

    uint8_t progRead(uint32_t addr) const { addr = (addr & 0x1ffffff) | m_s.ahigh; return m_rom[addr & (m_rom.size() - 1)]; }
    int relative() const { return (m_s.relative[0] + ((m_s.relative[1] & 0x0f) << 8)) * 0x2000; }

    // SoC
    void socReset();
    uint32_t getBanks(uint8_t bnk) const;
    void updateBanks();
    uint16_t decodeNtAddr(uint16_t addr, bool singlePage);
    void w410x(int offset, uint8_t data);
    void scrambled8000w(uint16_t offset, uint8_t data);
    int va17va10(int addr);
    int realVideoAddress(int addr, int readtype);
    uint8_t chrR(int offset);
    uint8_t sprR(int offset);
    void chrW(int offset, uint8_t data);
    uint8_t ntR(int offset) { return m_s.ntram[decodeNtAddr(offset, true)]; }
    void ntW(int offset, uint8_t data) { m_s.ntram[decodeNtAddr(offset, false)] = data; }
    void videoIrq(bool hblank, int scanline, bool vblank, bool blanked);
    void vtDmaW(uint8_t data);
    void vtDmaWInner(uint8_t data);
    void doDma(uint8_t data, bool ntscBug);
    void w4034(uint8_t data);
    void aluW(int offset, uint8_t data);

    // PPU
    void ppuReset();
    void initPalettes();
    uint8_t ppuReadbyte(uint32_t address);
    void ppuWritebyte(uint32_t address, uint8_t data);
    uint8_t vt3xxPaletteR(int offset);
    void vt3xxPaletteW(int offset, uint8_t data);
    uint8_t paletteRead(int offset);
    void paletteWrite(int offset, uint8_t data);
    uint8_t ppuRead(int offset);
    void ppuWrite(int offset, uint8_t data);
    void ppuWriteBase(int offset, uint8_t data);
    void writeSpriteramWithIncrement(uint8_t data);
    void hblankTick();
    void scanlineTick();
    void updateScanline();
    void renderScanline();
    void scanlineIncrementFineY();
    void drawBackground(uint8_t* lp);
    void drawBackgroundBase(uint8_t* lp);
    void drawBackgroundPen();
    void drawBackPen(uint32_t* dst, int backPen);
    void readTilePlaneData(int address, int color);
    void readTilePlaneDataVt03(int address, int color);
    void shiftTilePlaneData(uint8_t& pix);
    void shiftTilePlaneDataVt03(uint8_t& pix);
    void drawTilePixel(uint8_t pix, int color, uint32_t backPen, uint32_t* dest);
    void drawTilePixelInner(uint8_t pen, uint32_t* dest);
    void drawTile(uint8_t* lp, int colorByte, int colorBits, int address, int startX, uint32_t backPen, uint32_t*& dest);
    void drawSprites(uint8_t* lp);
    void drawSpritesBase(uint8_t* lp);
    void drawSpritesStandardRes(uint8_t* lp);
    void drawSpritesHighRes(uint8_t* lp);
    void readSpritePlaneData(int address);
    void makeSpritePixelData(uint8_t& pixelData, bool flipx);
    void drawSpritePixel(int xpos, int color, int pixel, uint8_t pixelData);
    void drawSpritePixelLow(int pixelData, int pixel, int xpos, int color, int spriteIndex, uint8_t* lp);
    void drawSpritePixelHigh(int pixelData, int pixel, int xpos, int color, int spriteIndex, uint8_t* lp);
    uint32_t getPenValue(int pixelData, int bpp, int pal);
    uint32_t apply_grayscale_and_emphasis(uint8_t color);
    uint32_t recalc8x8x4(int address, int va34);
    uint32_t recalc8x8x8(int address, int va34);
    uint32_t recalc16x16x8(int address, int va34);
    void lcdcRegsW(int offset, uint8_t data);
    uint8_t onespaceRel(uint32_t offset) { return progRead(relative() + offset); }

    // sound CPU
public:
    uint8_t spuRead(uint16_t a);
    void spuWrite(uint16_t a, uint8_t d);
    uint8_t spuExtRead(uint32_t a) { return progRead(relative() + a); }
private:
    void spuControl(uint8_t data);
    void spuRun();
    void spuTimerUpdate();
    void setEncryption(bool on) { m_s.encrypted = on; m_cpu.setOpcodeTable(on ? m_cryptTable : nullptr); }

    // APU
    void apuInit();
    void apuReset();
    void apuWrite(int offset, uint8_t value);
    uint8_t apuStatus();
    void apuSync();
    void apuSyncTo(int64_t now);
    void apuSample();
    void apuSquare(State::Square* chan);
    void apuTriangle(State::Triangle* chan);
    void apuNoise(State::Noise* chan);
    void apuDpcm(State::Dpcm* chan);
    void dpcmReset(State::Dpcm* chan);

    // timing: the CPU runs instruction by instruction; scanline events fire at
    // their exact dot, also in the middle of an instruction when a bus access
    // happens later than the event (as MAME's cycle-resumable 6502 does)
    int64_t m_instStart = 0;
    int m_access = 0;
    bool m_inInst = false;
    bool m_frameDone = false;
    int64_t nextEventDot() const;
    void fireEvent();
    void catchUp() {
        if (!m_inInst) return;
        int64_t t = m_instStart + (int64_t)m_access * (3 / m_s.clockMul);
        m_access++;
        while (nextEventDot() <= t) fireEvent();
    }
};
