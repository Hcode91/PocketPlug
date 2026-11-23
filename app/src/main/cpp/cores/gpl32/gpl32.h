// PocketPlug - GeneralPlus GPL32600 (ARM926) TV / handheld games: Millennium Arcade Genius
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The NAND's own boot loader copies the application ("APP" area after the second GPNandTag2
// header) to SDRAM 0 and jumps there; this core does that copy itself and runs the
// application on DidjDroid's ARM926 interpreter. The SoC is undocumented; everything here was
// worked out from what the application does with it:
//   0xc0000000 GPIO ports (n*0x20: input, output, direction)   0xc0020000 timers (375 kHz)
//   0xc0030000 time bases                                       0xc0040000 RTC (periodic tick)
//   0xc00a0000 SD controller (no card)                          0xc00c0000 SAR ADC (battery)
//   0xd0100000 interrupt controller (vector n = mask bit 32-n)   0xd0300000 4-channel DMA
//   0xd0500000 PPU (gpl32_ppu.cpp)                              0xd0900000 NAND controller
//   0xd0400000 SPU (gpl32_spu.cpp)
// Not done yet: the camera, the motion sensor and saving to NAND. (This model has no touch
// screen: the touch driver in the firmware is never called.)

#pragma once
#include "../../core_api.h"
#include "arm926.h"
#include "gpl32_spu.h"
#include <map>
#include <unordered_map>
#include <vector>

class GplCore : public Core, public didj::Bus {
public:
    GplCore();
    ~GplCore() override;
    bool load(const std::vector<RomFile>& roms, std::string& error) override;
    void reset() override;
    void runFrame() override;
    const uint32_t* frameBuffer() const override { return m_fb.data(); }
    int frameWidth() const override { return W; }
    int frameHeight() const override { return H; }
    double refreshRate() const override { return 60.0; }
    int sampleRate() const override { return 44100; }
    size_t readAudio(int16_t* out, size_t maxSamples) override;
    void setInputs(uint32_t pressed) override { m_keys = pressed; }
    void setAnalog(int stick, float x, float y) override {
        if (stick == 0) { m_mouseVx = x; m_mouseVy = y; }
        else if (stick == 9) { m_mouseAccX -= x * 3.0f; m_mouseAccY += y * 4.5f; }   // relative moves in screen pixels (the sensor's X runs right to left)
    }
    // the SPI laptops' keyboard matrix: code = row * 32 + column (row 0-7, column = port 1 bit)
    void keyEvent(int code, bool down) override {
        if (code < 0 || code >= 256) return;
        uint32_t b = 1u << (code & 31);
        m_kbd[code >> 5] = down ? (m_kbd[code >> 5] | b) : (m_kbd[code >> 5] & ~b);
    }
    bool saveState(std::vector<uint8_t>&) override { return false; }
    bool loadState(const std::vector<uint8_t>&) override { return false; }

    // didj::Bus
    uint8_t* page_ptr(uint32_t pa) override;
    uint32_t read32(uint32_t pa) override;
    uint16_t read16(uint32_t pa) override { return uint16_t(read32(pa & ~3u) >> ((pa & 2) * 8)); }
    uint8_t read8(uint32_t pa) override { return uint8_t(read32(pa & ~3u) >> ((pa & 3) * 8)); }
    void write32(uint32_t pa, uint32_t v) override;
    void write16(uint32_t pa, uint16_t v) override;
    void write8(uint32_t pa, uint8_t v) override;

    int W = 320, H = 240;                // the LCD: 320 x 240 (Arcade Genius), 480 x 272 (the SPI laptops)
    static constexpr uint32_t RAM_SIZE = 0x4000000;

    // debugging (headless)
    std::map<uint32_t, uint32_t> ioReads, ioWrites;

private:
    void debugDump();
    uint32_t ioRead(uint32_t pa);
    void ioWrite(uint32_t pa, uint32_t v, uint32_t mask);

    void nandCommand(uint8_t cmd);
    // interrupts: m_irqRaw uses the controller's mask-bit numbering (vector n = bit 32-n)
    void setIrq(int vector, bool on);
    void updateIrq();
    void updateTimerIrqs();
    void syncPeripherals();
    bool inWaitLoop(uint32_t pc);
    std::unordered_map<uint32_t, std::pair<uint32_t, bool>> m_waitLoops;
    uint64_t m_lastSync = 0;
    uint32_t pendingVector();
    void tickPeripherals(uint64_t cycles);
    uint32_t m_irqRaw = 0;
    bool m_irqOut = false;
    uint64_t m_tbAcc = 0, m_tmAcc = 0, m_rtcAcc = 0, m_tmAccFast = 0;
    void nandDma();
    void dmaRun(int ch);
    void ppuIrq();
    uint32_t gpioRead(int port);
    GplSpu m_spu;
    std::vector<uint32_t> m_ppuRam = std::vector<uint32_t>(0x4000);   // 0xd0500000-0xd050ffff, one 32-bit slot per entry
    uint32_t m_lastWatch = 0xdeadbeef;
    uint32_t m_adc[8] = { 0x200, 0x200, 0x200, 0x3ff, 0x200, 0x200, 0x200, 0x200 };   // channel 3: battery
    // Pins pulled low from outside: 0x52 (no USB power). The keypad lines are added from the
    // pressed buttons in gpioRead.
    uint32_t m_gpioLow[8] = { 0, 0, 0, 0, 0, 1u << 2, 0, 0 };
    // External pull-ups: MENU (0x75), VOL+ (0x42) and VOL- (0x43) buttons pull their pin low.
    uint32_t m_gpioPull[8] = { 0, 0, 0, 0, 0xcu, 0, 0, 1u << 5 };
    uint32_t keypadLines(int port) const;
    void ppuRender();
    void ppuDrawLayer(int layer);
    void ppuDrawSprites(int depth);
    void ppuIndexed(uint32_t& dst, uint32_t index);
    const uint8_t* ramPtr(uint32_t addr, uint32_t len) const;
    std::vector<uint8_t> m_nand;
    std::vector<uint8_t> m_spi;          // GPL32612 SPI-boot units: the serial flash ("GPspispi" header)
    // SPI master at 0xc0080000 (+8 TX, +0x10 RX, +0xc status: RX count in bits 0-2) talking to the
    // flash, whose chip select is GPIO port 5 bit 1 (low = selected)
    std::vector<uint8_t> m_spiCmd;
    std::vector<uint8_t> m_spiRx;
    uint32_t m_spiAddr = 0;
    bool m_spiCs = false;
    int m_spiRxDma = -1;
    // the SPI laptops' audio: two DAC channels at 0xc00d0000 / 0xc00d0020 fed sample by sample from a FIFO
    // (+0 control: bit 15 half-empty flag, write 1 to clear; bit 14 its interrupt (vectors 31 / 30);
    //  +4 sample in; +8 bit 15 FIFO full)
    std::vector<int16_t> m_dacFifo[2];
    int16_t m_dacCur[2] = {};
    uint64_t m_dacAcc = 0, m_dacOutAcc = 0;
    std::vector<int16_t> m_dacOut;
    void dacTick();
    void dacIrq();
    // the laptops' optical mouse sensor (PixArt PAN3101-style): SCLK on pin 0x59, SDIO on pin 0x58
    // (port 5 bits 9 / 8); register 0 is the product id 0x30, 2 motion, 3 / 4 the X / Y deltas
    struct Mouse { uint8_t sclk = 0, shift = 0, nbits = 0, addr = 0, reply = 0, phase = 0, sdo = 1; } m_mouse;
    int8_t m_dx = 0, m_dy = 0;
    float m_mouseVx = 0, m_mouseVy = 0, m_mouseAccX = 0, m_mouseAccY = 0;
    void mouseGpio(uint32_t out, uint32_t dir);
    uint8_t mouseReg(uint8_t r);
    bool adcAuto() const;
    void adcFifoIrq();                 // a DMA channel waiting to receive from the SPI FIFO
    uint8_t spiXfer(uint8_t tx);
    // NAND controller (0xd0900000)
    std::vector<uint8_t> m_nbuf = std::vector<uint8_t>(2112, 0xff);
    std::vector<uint8_t> m_nid;
    uint32_t m_nptr = 0, m_ncol = 0, m_nrow = 0;
    uint8_t m_ncmd = 0;
    bool m_nstatus = false;
    std::vector<uint8_t> m_app;
    std::vector<uint8_t> m_ram;
    std::vector<uint8_t> m_sram = std::vector<uint8_t>(0x100000);   // internal SRAM at 0xf8000000
    std::vector<uint32_t> m_fb;
    std::map<uint32_t, uint32_t> m_regs;
    didj::ARM926* m_cpu = nullptr;
    uint32_t m_keys = 0;
    uint32_t m_kbd[8] = {};
    uint64_t m_frame = 0;
    size_t m_audioPending = 0;
};
