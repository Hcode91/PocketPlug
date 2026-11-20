// PocketPlug - Sitronix ST2302U handhelds (MAME's bbl380 family): arcade10
// SPDX-License-Identifier: BSD-3-Clause
//
// Port of MAME's st2302u_bbl_spi.cpp (David Haywood), the ST2XXX / ST2302U
// SoC (devices/cpu/m6502/st2xxx.cpp, st2205u.cpp; AJR), the ST7735 LCD
// controller and the BaoBaoLong menu controller (David Haywood) and the
// generic SPI flash (David Haywood). The CPU is PocketPlug's W65C02.
//
// The SoC banks 16K at 4000-7FFF (PRR) and 32K at 8000-FFFF (DRR) over a
// 26-bit data space holding the 8K internal BIOS (2000-3FFF) and the LCD
// controller (1800000 command / 1804000 data). The games live in the SPI
// flash and are copied into the 2K of RAM by the BIOS as they run.

#pragma once
#include "../../core_api.h"
#include "../../common/w65c02.h"
#include <cstdint>
#include <vector>

// Input bits (1 = pressed): 0 up, 1 down, 2 left, 3 right, 4 A, 5 B, 6 START, 7 SOUND
class Bbl380Core : public Core, private W65C02::Bus {
public:
    Bbl380Core();
    bool load(const std::vector<RomFile>& roms, std::string& error) override;
    void reset() override;
    void runFrame() override;
    const uint32_t* frameBuffer() const override { return m_frame; }
    int frameWidth() const override { return 160; }
    int frameHeight() const override { return 128; }
    double refreshRate() const override { return 60.0; }
    int sampleRate() const override { return 44100; }
    size_t readAudio(int16_t* out, size_t maxSamples) override;
    void setInputs(uint32_t pressed) override { m_inputs = pressed; }
    bool saveState(std::vector<uint8_t>& out) override;
    bool loadState(const std::vector<uint8_t>& in) override;

private:
    // W65C02::Bus
    uint8_t read(uint16_t a) override;
    void write(uint16_t a, uint8_t d) override;
    uint8_t readVector(uint16_t a, bool irq) override;
    void rti() override { m_s.irqService = 0; }

    uint8_t pread(uint16_t a);
    void pwrite(uint16_t a, uint8_t d);
    uint8_t dataRead(uint32_t a);
    void dataWrite(uint32_t a, uint8_t d);
    uint8_t regRead(uint8_t r);
    void regWrite(uint8_t r, uint8_t d);

    // SoC blocks
    uint8_t pdataR(int port);
    void portOut(int port, uint8_t data, uint8_t mask);
    void updateIrq();
    uint8_t activeIrqLevel() const;
    uint16_t presCount() const;
    void prsW(uint8_t d);
    void btenW(uint8_t d);
    void btFire(int n);
    unsigned btDivider(int n) const;
    void t12Process(int t);
    uint16_t t12Count(int t) const;
    void t12StartTclk(int t);
    void tienW(uint8_t d);
    void tclkStart();
    void psgcW(uint8_t d);
    void sctrW(uint8_t d);
    void spiStart();
    void spiComplete();
    void dmaStart(uint8_t d);

    // board devices
    uint16_t spiExchange(uint16_t data, uint8_t bits);
    void flashReset();
    void flashWrite(uint8_t d);
    void flashCommand(uint8_t d);
    void menuClock(int state);
    void menuCommand();
    void lcdCommand(uint8_t d);
    void lcdData(uint8_t d);

    void audioTo(double cycle);
    double now() const;
    void schedule();

    W65C02 m_cpu;
    uint8_t m_bios[0x2000];
    std::vector<uint8_t> m_flash;      // 8 MB region, 0xff past the dump
    uint32_t m_inputs = 0;
    uint32_t m_frame[160 * 128];

    static constexpr double CLOCK = 32000000.0;
    static constexpr double NEVER = 1e300;

    struct State {
        int64_t cycles;
        uint8_t ram[0x800];
        // banks / interrupts
        uint16_t prr, drr, irr;
        uint8_t irrEnable, irqService, irqLevel;
        uint16_t ireq, iena;
        // ports
        uint8_t pdata[7], pctrl[7], psel[7], pfun[2], pmcr;
        // base timer
        uint8_t bten, btsr, btc;
        double btAt[8];
        // prescaler
        uint16_t presBase; double presStarted; uint8_t prs;
        uint8_t sys, misc;
        // 12-bit timers
        uint16_t tc[4], count[4]; double tAt[4];
        uint8_t t4c, tien;
        // PSG
        uint16_t fifo[4][16]; uint8_t fifoFilled[4], fifoPos[4];
        uint8_t psgc, psgm, psgOn, vol[4], volm[2];
        int16_t adpcm[4]; uint8_t amp[4]; int16_t out[4];
        uint16_t mul;
        uint8_t usbcon, usbien;
        uint16_t dptr[4], dbkr[4], dcnt[2]; uint8_t dctr, dmod[2];
        uint8_t rctr, lvctr;
        // SPI
        uint8_t sctr, sckr, ssr, smod;
        uint16_t sdataTx, sdataRx, spiPendingRx;
        uint8_t spiBusy, spiTxPending; double spiAt;
        // board
        uint8_t output2val;
        // SPI flash
        uint32_t fAddr; uint8_t fState, fLatch, fStep, fStatus, fConfig;
        // menu controller
        uint8_t mClock, mDataBit, mShift, mResponseBit, mResponse, mCommand, mState; uint16_t mPos;
        // ST7735
        uint16_t lx, ly, lminx, lmaxx, lminy, lmaxy, latch;
        uint8_t lcmd, lstep, lon, lsleep, madctl;
    } m_s;
    std::vector<uint16_t> m_lcd;       // 256 x 256

    // audio
    double m_audioLast = 0, m_audioPhase = 0, m_audioAcc = 0, m_dc = 0;
    std::vector<int16_t> m_audio;
};
