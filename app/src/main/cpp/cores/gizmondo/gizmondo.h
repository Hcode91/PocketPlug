// PocketPlug - Tiger Telematics Gizmondo (2005)
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Full hardware emulation: Samsung S3C2440 (ARM920T @ 400 MHz, run on DidjDroid's
// ARM926 interpreter), 64 MB SDRAM at 0x30000000, M-Systems DiskOnChip G3 in the
// boot window, NVIDIA GoForce 4500 at 0x34000000 (320 x 240 LCD).
//
// Boot: fboot.bin (MAME set "gizmondo") runs from address 0, finds the "BOOT"
// partition on the DiskOnChip and copies the Windows CE bootloader (EBOOT) to
// 0x30200000. The DiskOnChip image is built here from an official firmware update
// package (install.bin): its bootloader is used as-is. The Windows CE system in the
// package (NK.BIN) is encrypted, so for now the bootloader finds no system to start.
//
// Memory map (S3C2440 nGCS banks):
//   0x00000000 fboot (2 KB)   0x00000800 DiskOnChip data port   0x00001000 DiskOnChip registers
//   0x30000000 SDRAM (64 MB)  0x34000000 GoForce 4500            0x40000000 S3C2440 internal SRAM (4 KB)
//   0x48000000-0x5bffffff S3C2440 peripherals

#pragma once
#include "../../core_api.h"
#include "../gpl32/arm926.h"
#include "docg3.h"
#include <map>
#include <string>
#include <vector>

class GizmondoCore : public Core, public didj::Bus {
public:
    GizmondoCore();
    ~GizmondoCore() override;
    bool load(const std::vector<RomFile>& roms, std::string& error) override;
    void reset() override;
    void runFrame() override;
    const uint32_t* frameBuffer() const override { return m_fb.data(); }
    int frameWidth() const override { return 320; }
    int frameHeight() const override { return 240; }
    double refreshRate() const override { return 60.0; }
    int sampleRate() const override { return 44100; }
    size_t readAudio(int16_t* out, size_t maxSamples) override;
    void setInputs(uint32_t pressed) override;
    void setWallClock(int64_t localSeconds) override { m_wallClock = localSeconds; }
    bool saveState(std::vector<uint8_t>&) override { return false; }
    bool loadState(const std::vector<uint8_t>&) override { return false; }

    // Buttons (bit = 1 pressed)
    enum Button : uint32_t {
        UP = 1u << 0, DOWN = 1u << 1, LEFT = 1u << 2, RIGHT = 1u << 3,
        PLAY = 1u << 4, FORWARD = 1u << 5, REWIND = 1u << 6, STOP = 1u << 7,
        L = 1u << 8, R = 1u << 9, F1 = 1u << 10, F2 = 1u << 11, F3 = 1u << 12, F4 = 1u << 13, F5 = 1u << 14,
    };

    // didj::Bus
    uint8_t* page_ptr(uint32_t pa) override;
    uint32_t read32(uint32_t pa) override;
    uint16_t read16(uint32_t pa) override;
    uint8_t read8(uint32_t pa) override;
    void write32(uint32_t pa, uint32_t v) override;
    void write16(uint32_t pa, uint16_t v) override;
    void write8(uint32_t pa, uint8_t v) override;

    // headless debugging: what the bootloader printed on its serial port
    std::string serialLog;
    const std::vector<uint8_t>& debugRam() const { return m_ram; }

private:
    static constexpr uint32_t RAM_BASE = 0x30000000, RAM_SIZE = 0x4000000;

    bool buildDoc(const std::vector<uint8_t>& eboot, std::string& error);
    uint32_t ioRead(uint32_t pa, int size);
    void ioWrite(uint32_t pa, uint32_t v, int size);
    uint32_t& reg(uint32_t pa) { return m_regs[pa & ~3u]; }

    // interrupts
    void setIrq(int n, bool on);
    void setSubIrq(int n);
    void updateIrq();
    uint32_t m_srcpnd = 0, m_intmod = 0, m_intmsk = 0xffffffff, m_intpnd = 0, m_intoffset = 0;
    uint32_t m_subsrcpnd = 0, m_intsubmsk = 0x7fff;
    uint32_t m_eintpend = 0, m_eintmask = 0xfffff0;
    void eint(int n);

    // timers
    struct Timer { uint32_t cnt = 0, cntb = 0, cmpb = 0; bool on = false, reload = false; uint64_t acc = 0; };
    Timer m_t[5];
    uint32_t m_tcfg0 = 0, m_tcfg1 = 0, m_tcon = 0;
    void tickTimers(uint64_t cpuCycles);
    // Emulated time in CPU clock cycles: each instruction counts as CPI cycles (an
    // interpreter can't run 400 million instructions a second; most of the boot is
    // timer-based waiting, which this shortens without changing what is waited for).
    static constexpr uint64_t CPI = 4;
    uint64_t now() const { return (m_cpu->insn_count + m_idle) * CPI; }
    void syncTimers() { uint64_t n = now(); if (n > m_lastSync) { tickTimers(n - m_lastSync); m_lastSync = n; } }
    uint64_t m_idle = 0, m_lastSync = 0;
    uint32_t pclk() const;
    uint32_t fclk() const;

    // GPIO
    uint32_t m_gpdat[9] = {};           // A..H, J as written
    uint32_t gpioRead(int port);

    // GoForce 4500 (2D framebuffer path, after MAME's gf4500.cpp)
    std::vector<uint32_t> m_gf = std::vector<uint32_t>(0x140000 / 4);
    uint32_t m_gfx = 0, m_gfy = 0, m_gfxMax = 0, m_gfyMax = 0, m_gfxMin = 0, m_gfyMin = 0;
    void gfVram16(uint16_t v);
    void gfWrite(uint32_t off, uint32_t v);
    void renderScreen();

    // RTC / ADC / watchdog
    int64_t m_wallClock = 0;
    uint32_t m_adccon = 0x3fc4, m_adcdat0 = 0;

    std::vector<uint8_t> m_fboot;
    std::vector<uint8_t> m_ram;
    std::vector<uint8_t> m_sram = std::vector<uint8_t>(0x1000);
    std::vector<uint32_t> m_fb;
    std::map<uint32_t, uint32_t> m_regs;
    DocG3 m_doc;
    didj::ARM926* m_cpu = nullptr;
    uint32_t m_keys = 0;
    uint64_t m_frame = 0;
    size_t m_audioPending = 0;
    bool m_irqOut = false;
};
