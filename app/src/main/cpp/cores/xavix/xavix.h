// PocketPlug - SSD XaviX (SSD 98 PA7351) plug-and-play TV games
// SPDX-License-Identifier: BSD-3-Clause
//
// Port of MAME's tvgames/xavix*.cpp (David Haywood, ramacat) and the XaviX CPU
// (cpu/m6502/xavix.cpp, oxavix.lst, David Haywood), BSD-3-Clause.
//
// The XaviX is a 6502 with a 24-bit bus: a code bank (zero page $FE) picks the
// 64 KB bank opcodes come from, a data bank ($FF) the one data at $8000-$FFFF
// is read from; $0000-$7FFF is always the "low bus" (16 KB RAM and the
// on-chip registers). CALLF / RETF / interrupts save the code bank on a
// private stack. Video is two tilemaps and 256 sprites ("fragments") drawn
// from ROM in 1-8 bpp with a hue / saturation / lightness palette; sound is
// 16 wavetable voices with envelopes.
//
// Inputs: bits 0-7 = IN0 (Keroro's four pads are bits 0-3), bits 8-15 = IN1.
#pragma once
#include "../../core_api.h"
#include <vector>
#include <cstring>
#include "xavix_sound.h"

class XavixCore : public Core {
public:
    struct Config {
        std::string romName;
        uint32_t romSize = 0x400000;
        int cpu = 0;              // 0 = XaviX (SSD 97/98), 1 = SSD 2000 / 2002 (extra opcodes, bank on the main stack)
        int i2cSize = 0;          // 24Cxx EEPROM on IO1 (SDA bit 3, SCL bit 4): 0x100 / 0x200 / 0x400, 0 = none
        int i2cPage = 16;
        int sdaBit = 3, sclBit = 4;   // IO1 pins of the EEPROM
        bool lotrIo = false;      // IN1 bits 1-2 read random (MAME xavix_i2c_lotr_state)
        bool camera = false;      // Bandai "Taikan" camera: 32x31 infrared image on ADC channel 0, clocked on IN1 bits 1-2;
                                  // IO1 bit 5 lights the LEDs, so reflective stickers show up in the difference

        bool camOnEx = false;     // camera clock on EX1 bit 2 and LED on EX out 1 bit 5 (PreCure) instead of IN1 / IO1
        bool superXavix = false;  // SSD 2002: twice the clock, extended I/O ports, extra video registers
        uint8_t in1Default = 0;   // IN1 bits held high (MAME DIP defaults)
        uint8_t exDefault[3] = { 0, 0, 0 };   // SSD 2002 extended inputs EX0-EX2
    };
    explicit XavixCore(const Config& c) : m_cfg(c) {}

    bool load(const std::vector<RomFile>& roms, std::string& error) override;
    void reset() override;
    void runFrame() override;
    const uint32_t* frameBuffer() const override { return m_out; }
    int frameWidth() const override { return m_cfg.superXavix ? 512 : 256; }
    int frameHeight() const override { return 224; }
    double refreshRate() const override { return 60.0; }
    int sampleRate() const override { return 48000; }
    size_t readAudio(int16_t* out, size_t maxSamples) override;
    void setInputs(uint32_t pressed) override { m_inputs = pressed; }
    void setAnalog(int stick, float x, float y) override { if (stick >= 0 && stick < 2) { m_ax[stick] = x; m_ay[stick] = y; m_analogSet = true; } }
    bool saveState(std::vector<uint8_t>& out) override;
    bool loadState(const std::vector<uint8_t>& in) override;

    // ---- CPU ----
    struct Cpu {
        uint16_t pc;
        uint8_t a, x, y, s, p;
        uint8_t codebank, databank;
        uint8_t sstack[256];
        uint8_t sstackpos;
        uint8_t irqLine, nmiLine, nmiPending;
        uint8_t j, k, l, m;      // SSD 2000 extra registers
        uint32_t pa, pb;         // SSD 2000 24-bit pointers
    };
    // 24Cxx serial EEPROM (MAME i2cmem)
    struct I2c {
        uint8_t data[0x800];
        uint8_t page[16];
        uint8_t scl, sdaw, sdar, state, bits, shift, devsel, addresshigh;
        int byteaddr, pageOffset, pageWritten;
    };

private:
    int cpuStep();
    void cpuInterrupt(bool nmi, bool brk);
    uint8_t fetch(uint16_t pc);
    uint8_t rdZ(uint8_t a);
    void wrZ(uint8_t a, uint8_t d);
    uint8_t rdD(uint16_t a);
    void wrD(uint16_t a, uint8_t d);
    uint8_t readFull(uint8_t bank, uint16_t a);
    void writeFull(uint8_t bank, uint16_t a, uint8_t d);
    void adc(uint8_t v);
    void sbc(uint8_t v);
    void cmp(uint8_t r, uint8_t v);
    void setNZ(uint8_t v) { m_cpu.p = (m_cpu.p & ~0x82) | (v & 0x80) | (v ? 0 : 0x02); }
    void push(uint8_t v) { lowWrite(0x100 | m_cpu.s, v); m_cpu.s--; }
    uint8_t pop() { m_cpu.s++; return lowRead(0x100 | m_cpu.s); }
    void pushBank(uint8_t b) { if (m_cfg.cpu) push(b); else m_cpu.sstack[m_cpu.sstackpos--] = b; }
    uint8_t popBank() { return m_cfg.cpu ? pop() : m_cpu.sstack[++m_cpu.sstackpos]; }
    bool step2000(uint8_t op, int& cyc);
    void i2cSda(int st);
    void i2cScl(int st);
    void io1Lines();

    // ---- low bus ($0000-$7FFF) ----
    uint8_t lowRead(uint16_t a);
    void lowWrite(uint16_t a, uint8_t d);
    uint8_t rom(uint32_t a) const { return m_rom[a & (m_romLen - 1)]; }
    uint8_t lowBypass(uint16_t a);
    uint8_t readBypass(uint32_t a);
    uint8_t txarray(uint16_t o) const;

    void updateIrqs();
    void romDma(uint8_t data);
    void spriteDma(uint8_t data);
    void spriteramWrite(uint16_t o, uint8_t d);
    void spriteramHighX(uint16_t o, uint8_t d);
    void videoCtrlWrite(uint8_t data);
    void timerControlWrite(uint8_t data);
    void timerStart();
    void timerDone();
    void soundIrqStatusWrite(uint8_t data);
    void advance(int cycles);
    void refreshSoundIrq();
    void reprogramSoundTimer(int i);
    void soundTimerDone(int i);
    void posIrq();
    uint8_t ioRead(int port);
    void mathDo(bool mulShf);

    // ---- video ----
    void updatePen(int pen);
    void renderUpTo(int line);
    void renderLine(int line);
    void drawTilemapLine(int which, int line);
    void drawSpritesLine(int line);
    void drawTileLine(int tile, int bpp, int xpos, int ypos, int drawheight, int drawwidth, int flipx, int flipy, int pal, int zval, int line);
    void drawBitmapLine(int line);
    void sxPalWrite(int reg, uint8_t d);
    uint8_t sxPalRead(int reg);
    uint32_t extSegment(int i) const { const uint8_t* e = &m_s.sxRam[0x40 + i * 4]; return e[0] | (e[1] << 8) | (e[2] << 16) | (uint32_t(e[3]) << 24); }
    void decodeInlineHeader(int& flipx, int& flipy, int& test, int& pal);
    uint8_t nextBit();
    uint8_t nextByte();

    // ---- sound ----
    void soundSync();
    static uint8_t sndRegs(void* c, uint32_t a);
    static void sndRegsW(void* c, uint32_t a, uint8_t d);
    static uint8_t sndSamples(void* c, uint32_t a);

    Config m_cfg;
    std::vector<uint8_t> m_romData;
    const uint8_t* m_rom = nullptr;
    uint32_t m_romLen = 1;
    uint32_t m_inputs = 0;
    uint32_t m_out[512 * 224];
    uint32_t m_pens[512];
    uint16_t m_z[512];
    int m_rendered = -1;     // last screen line drawn this frame
    int m_vpos = 0;
    uint32_t m_tmpAddr = 0;
    int m_tmpBit = 0;
    uint8_t m_bit = 0;
    std::vector<int16_t> m_audio;

    Cpu m_cpu;
    XavixSound m_snd;
    bool m_eepromInit = false;
    float m_ax[2] = { -0.45f, 0.45f }, m_ay[2] = { 0.1f, 0.1f };
    float m_reach[2] = { 0, 0 };   // how far each glove has travelled into a punch, 0..1
    bool m_analogSet = false;
    uint8_t cameraPixel(int row, int col, bool led);
    uint8_t cameraClock();
    int64_t m_cycle = 0;       // CPU cycles run (for sound sync)

    struct State {
        uint8_t ram[0x4000];
        uint8_t sprite[0x800];
        uint8_t palSh[0x100], palL[0x100];
        uint8_t segment[0x20];
        uint8_t soundRam[0x180];
        uint8_t tmap1[8], tmap2[8];
        uint8_t spritereg;
        uint8_t sfDma1[2], sfDma2[2];
        uint8_t arenaStart, arenaEnd, arenaControl;
        uint8_t colmixSh, colmixL, colmixCtrl;
        uint8_t videoCtrl, posirqX, posirqY;
        uint8_t txarray[3];
        uint8_t dmaSrc[3], dmaDst[2], dmaLen[2];
        uint8_t extbus[3];
        uint8_t io0Data, io1Data, io0Dir, io1Dir;
        uint8_t ioeventEnable, ioeventActive;
        uint8_t timerControl, timerBase, timerFreq, timerCur;
        int64_t timerLeft;          // cycles until the next count (-1 = stopped)
        uint8_t mpr, mpd, mad, sgnMpd, sgnMpr, mult[2];
        uint8_t adcControl, adcLatch;
        uint8_t vectorEnable, nmiLo, nmiHi, irqLo, irqHi;
        uint8_t irqSource;
        // sound
        uint8_t soundStartStop[2];
        uint8_t soundRegbase, cyclerate, tp[4], soundIrqStatus;
        int64_t soundTimerLeft[4], soundTimerPeriod[4];   // in 1/2^16 CPU cycles (period 0 = off)
        int64_t sndCycles;          // CPU cycles not yet turned into sound samples
        int32_t outAccL, outAccN;   // box filter down to the output rate
        int64_t outPhase;
        uint8_t posArmed;
        I2c i2c;
        uint32_t camClock, camCount;
        uint8_t camLed;
        uint8_t sxDir[3], sxOut[3];
        uint8_t sxRam[0x600];         // SSD 2002 registers at $6A00-$6FFF that are plain storage for now
        uint8_t punch[2];
        uint32_t rng;
    } m_s;
};
