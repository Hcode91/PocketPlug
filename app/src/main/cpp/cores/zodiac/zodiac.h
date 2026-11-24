// PocketPlug - Tapwave Zodiac (2003)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Full hardware emulation: Motorola i.MX1 (MC9328MX1, ARM920T @ 200 MHz, run on
// DidjDroid's ARM926 interpreter) booting Tapwave's own Palm OS 5.2T ROM. The ROM
// comes from Tapwave's official 1.1 update (zodiac_1.1_update.prc), whose ROMZ
// resource is a gzip of the full 8 MB flash image (small ROM + big ROM).
//
// Memory map (i.MX1 chip selects; registers after the i.MX1 reference manual as
// used by Linux 2.6's mach-imx):
//   0x00000000 flash alias (boot)       0x00200000-0x00224fff i.MX1 peripherals
//   0x00300000 eSRAM (128 KB)           0x08000000 SDRAM CSD0  0x0c000000 SDRAM CSD1
//   0x10000000 CS0 flash (8 MB)         0x12000000-0x16ffffff CS1-CS5 (ATI W4200 and others)

#pragma once
#include "../../core_api.h"
#include "../gpl32/arm926.h"
#include <map>
#include <string>
#include <vector>

class ZodiacCore : public Core, public didj::Bus {
public:
    ZodiacCore();
    ~ZodiacCore() override;
    bool load(const std::vector<RomFile>& roms, std::string& error) override;
    void reset() override;
    void runFrame() override;
    const uint32_t* frameBuffer() const override { return m_fb.data(); }
    int frameWidth() const override { return 480; }
    int frameHeight() const override { return 320; }
    double refreshRate() const override { return 60.0; }
    int sampleRate() const override { return 44100; }
    size_t readAudio(int16_t* out, size_t maxSamples) override;
    void setInputs(uint32_t pressed) override { m_keys = pressed; }
    // stick 0: analog stick (-1..1); stick 2: stylus position on the screen (0..1, x < 0 = lifted)
    void setAnalog(int stick, float x, float y) override {
        if (stick == 0) { m_stickX = 512 + int(x * 511); m_stickY = 512 + int(y * 511); }
        else if (stick == 2) { m_penDown = x >= 0; if (m_penDown) { m_penX = x; m_penY = y; } }
    }
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

    // Unpacks the flash image from a Tapwave update .prc (ROMZ resource); empty if none.
    static std::vector<uint8_t> romFromUpdate(const std::vector<uint8_t>& prc);

    // headless debugging
    std::string serialLog;
    didj::ARM926* cpu() { return m_cpu; }
    const std::vector<uint8_t>& debugRam() const { return m_ram0; }
    const std::vector<uint8_t>& debugVram() const { return m_vram; }

private:
    static constexpr uint32_t FLASH_BASE = 0x10000000, FLASH_SIZE = 0x1000000;
    static constexpr uint32_t SDRAM0 = 0x08000000, SDRAM1 = 0x0c000000, SDRAM_SIZE = 0x4000000;
    static constexpr uint32_t ESRAM = 0x00300000, ESRAM_SIZE = 0x20000;
    // ATI Imageon W4200 on CS5: CPU interface (CIF) registers, register file, 8 MB local memory
    // (internal addresses: on-chip memory 0x100000-0x15ffff, external SDRAM 0x800000-0xffffff,
    // seen by the CPU at CS5 + the same offset; command packets are streamed through a window
    // at CS5 + 0x80000)
    static constexpr uint32_t W42_CIF = 0x16000000, W42_REG = 0x16010000, W42_WRAP = 0x16080000;
    static constexpr uint32_t W42_MEM = 0x16100000, W42_MEM_SIZE = 0xf00000;

    uint32_t ioRead(uint32_t pa, int size);
    void ioWrite(uint32_t pa, uint32_t v, int size);
    uint32_t& reg(uint32_t pa) { return m_regs[pa & ~3u]; }
    // command processor: PM4 packets
    void renderScreen();
    // W4200 ADC: 0xd08 bits 2-0 pick the input (0: analog stick, two samples on channels 1
    // and 2; 1: channel 9; 2: channel 10, the battery), a 0-to-1 edge of 0xd0c bit 10 starts
    // a conversion, 0xd10 bits 24-20 count the results waiting and 0xd14 pops one
    // (channel << 26 | 10-bit value)
    std::vector<uint32_t> m_adcFifo;
    int m_stickX = 512, m_stickY = 512;
    bool m_penDown = false;            // (touch panel not hooked up yet)
    float m_penX = 0, m_penY = 0;
    void cpWord(uint32_t w);
    uint32_t m_cpHeader = 0, m_cpLeft = 0, m_cpReg = 0, m_cpReg2 = 0, m_cpIndex = 0;
    std::vector<uint32_t> m_cpBody;
    void cpPacket3(uint32_t op, const std::vector<uint32_t>& body);
    uint16_t* w42Pixel(uint32_t offset, uint32_t pitch, int x, int y);
    uint32_t w42Read(uint32_t off, int size);
    void w42Write(uint32_t off, uint32_t v, int size);
    void unknown(const char* what, uint32_t pa, uint32_t v, int size);

    // AITC interrupt controller (64 sources)
    uint64_t m_intSrc = 0, m_intEnable = 0, m_intType = 0, m_intForce = 0;
    uint32_t m_intCntl = 0, m_niMask = 0x1f;
    uint32_t m_niPrio[8] = {};
    void setIrq(int n, bool on) { if (on) m_intSrc |= 1ull << n; else m_intSrc &= ~(1ull << n); }
    void updateIrq();
    int prio(int n) const { return (m_niPrio[7 - (n >> 3)] >> ((n & 7) * 4)) & 15; }
    uint32_t niVector() const;

    // general-purpose timers 1 and 2
    struct Timer { uint32_t tctl = 0, tprer = 0, tcmp = 0xffffffff, tcr = 0, tcn = 0, tstat = 0; double acc = 0; };
    Timer m_tim[2];
    void tickTimers(uint64_t cycles);

    // GPIO ports A-D
    struct Port { uint32_t ddir = 0, ocr1 = 0, ocr2 = 0, iconf[4] = {}, dr = 0, gius = 0, icr1 = 0, icr2 = 0, imr = 0, isr = 0, gpr = 0, swr = 0, puen = 0xffffffff, last = 0; };
    Port m_gpio[4];
    uint32_t gpioIn(int port);
    void gpioUpdate();

    // time: emulated CPU cycles (CPI per instruction plus idle time)
    static constexpr uint64_t CPU_HZ = 200000000, CPI = 2;
    uint64_t now() const { return (m_cpu->insn_count + m_idle) * CPI; }
    void sync() { uint64_t n = now(); if (n > m_lastSync) { tickTimers(n - m_lastSync); m_lastSync = n; } }
    uint64_t m_idle = 0, m_lastSync = 0;
    uint32_t perclk1() const;

    // I2C master and the board's I2C devices
    uint32_t m_i2cr = 0, m_i2sr = 0x81, m_i2dr = 0, m_i2cAddr = 0, m_i2cPtr = 0;
    int m_i2cPhase = 0, m_i2cWa = 2;              // 0 idle, 1 address byte next, 2 data
    bool m_i2cRead = false;
    std::vector<uint8_t> m_eeprom = std::vector<uint8_t>(0x2000, 0xff);
    void i2cWrite(uint32_t off, uint32_t v);
    uint32_t i2cRead(uint32_t off);
    uint8_t i2cDevRead();
    void i2cDevWrite(uint8_t v);

    // RTC
    int64_t m_wallClock = 0;
    uint32_t m_rtcSeconds = 0, m_rtcRtcctl = 0, m_rtcIsr = 0, m_rtcIenr = 0;
    double m_rtcAcc = 0;

    std::vector<uint8_t> m_flash = std::vector<uint8_t>(FLASH_SIZE, 0xff);
    std::vector<uint8_t> m_ram0 = std::vector<uint8_t>(SDRAM_SIZE);
    std::vector<uint8_t> m_ram1 = std::vector<uint8_t>(SDRAM_SIZE);
    std::vector<uint8_t> m_esram = std::vector<uint8_t>(ESRAM_SIZE);
    std::vector<uint8_t> m_vram = std::vector<uint8_t>(W42_MEM_SIZE);
    std::vector<uint32_t> m_fb = std::vector<uint32_t>(480 * 320, 0xff000000u);
    std::map<uint32_t, uint32_t> m_regs;
    std::map<uint32_t, int> m_seen;           // unknown accesses already logged
    didj::ARM926* m_cpu = nullptr;
    uint32_t m_keys = 0;
    uint64_t m_frame = 0;
    size_t m_audioPending = 0;
};
