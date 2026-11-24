// PocketPlug - Mattel HyperScan (2006)
// SPDX-License-Identifier: BSD-3-Clause
//
// Sunplus SPG290 (S+core 7 @ 108 MHz) running the console's BIOS, which boots the game
// from the CD. The hardware model follows MAME's spg29x driver and its SPG290 devices
// (license BSD-3-Clause, copyright-holders: Sandro Ronco): timers, I2C (the two
// controllers), PPU (text layers and sprites), TV encoder frame buffers, CD servo, and
// the RFID card reader on GPIO.
//
// Memory map (addresses masked to 29 bits):
//   0x00000000 SDRAM 16 MB (mirrored up to 0x07ffffff)   0x08000000 peripherals
//   0x0a000000 internal SRAM 16 KB   0x0b000000 internal ROM 32 KB
//   0x10000000 BIOS 1 MB (mirrored up to 0x1fffffff; the CPU starts at 0x9f000000)
#pragma once
#include "../../core_api.h"
#include "score7.h"
#include "cdimage.h"
#include <cstdint>
#include <string>
#include <vector>

class HyperScanCore : public Core, public Score7::Bus {
public:
    HyperScanCore();
    ~HyperScanCore() override;
    bool load(const std::vector<RomFile>& roms, std::string& error) override;
    bool wantsRomPaths() const override { return true; }
    void reset() override;
    void runFrame() override;
    const uint32_t* frameBuffer() const override { return m_out.data(); }
    int frameWidth() const override { return 640; }
    int frameHeight() const override { return 480; }
    double refreshRate() const override { return 59.94; }
    int sampleRate() const override { return 48000; }
    size_t readAudio(int16_t* out, size_t maxSamples) override;
    void setInputs(uint32_t pressed) override { m_keys = pressed; }
    void setOption(int id, int value) override { if (id == 0) m_fastCd = value != 0; }
    void setAnalog(int stick, float x, float y) override { if (stick == 0) { m_anaX = x; m_anaY = y; } }
    bool saveState(std::vector<uint8_t>&) override { return false; }
    bool loadState(const std::vector<uint8_t>&) override { return false; }

    // Controller buttons (bit = 1 pressed); directions move the analog stick fully.
    enum Button : uint32_t {
        UP = 1u << 0, DOWN = 1u << 1, LEFT = 1u << 2, RIGHT = 1u << 3,
        GREEN = 1u << 4, RED = 1u << 5, YELLOW = 1u << 6, BLUE = 1u << 7,
        START = 1u << 8, SELECT = 1u << 9, L_SHOULDER = 1u << 10, R_SHOULDER = 1u << 11,
        L_TRIGGER = 1u << 12, R_TRIGGER = 1u << 13,
    };

    // Score7::Bus
    uint8_t read8(uint32_t a) override;
    uint16_t read16(uint32_t a) override;
    uint32_t read32(uint32_t a) override;
    void write8(uint32_t a, uint8_t v) override;
    void write16(uint32_t a, uint16_t v) override;
    void write32(uint32_t a, uint32_t v) override;

    Score7* cpu() { return m_cpu; }
    std::string debugLog;
    int debugLevel = 0;

private:
    static constexpr double CPU_HZ = 108000000.0;
    static constexpr uint64_t FRAME_CYCLES = 1801800;   // 858 x 525 dots at 27 MHz, in CPU cycles
    static constexpr uint64_t LINE_CYCLES = FRAME_CYCLES / 525;

    uint64_t now() const;
    void schedule();                         // after a write that moves a device's next event
    uint32_t ioRead(uint32_t a, uint32_t mask);
    uint32_t read32ForSub(uint32_t a, uint32_t mask);
    void writeMasked(uint32_t a, uint32_t v, uint32_t mask);
    void ioWrite(uint32_t a, uint32_t v, uint32_t mask);
    void unknown(const char* what, uint32_t a, uint32_t v);

    Score7* m_cpu = nullptr;
    std::vector<uint8_t> m_ram, m_sram, m_bios, m_irom;
    uint64_t m_cycle = 0;                    // time at the start of the current CPU slice
    int m_sliceLen = 0;

    // TV encoder
    uint32_t m_tveControl = 0, m_tveFade = 0, m_tveBufCtrl = 3, m_tvStart[3] = {};
    uint32_t m_timersClkSel = 0, m_gpioOut = 0;

    // timers (6): count up to 0xffff at their clock, reload, interrupt 56
    struct Timer {
        bool enabled = false;
        uint32_t control = 0, control2 = 0, preload = 0, counter = 0, ccp = 0, upcount = 0;
        double hz = 27000000.0;
        uint64_t last = 0;                   // CPU cycle the counter was last brought up to date
        double frac = 0;
    } m_timer[6];
    void timerUpdate(int i, uint64_t t);
    uint64_t timerNext(int i) const;

    // I2C master (the controllers): one transfer completes after a few bit times
    uint32_t m_i2cConfig = 0, m_i2cIrq = 0, m_i2cClock = 0, m_i2cId = 0, m_i2cPort = 0, m_i2cW = 0, m_i2cR = 0;
    uint64_t m_i2cAt = UINT64_MAX, m_i2cPeriod = 0;
    void i2cEvent();
    uint16_t ctrlRead(int pad, uint32_t addr);
    uint8_t m_ctrlData[2][4] = {};

    // PPU
    uint32_t m_ppuRegs[0x40] = {};
    uint32_t m_charPal[0x200] = {}, m_spritePal[0x200] = {}, m_hoff[0x200] = {}, m_voff[0x200] = {}, m_spriteRam[0x400] = {};
    uint32_t m_ppuIrqStatus = 0;
    uint32_t ppuRead(uint32_t off);
    void ppuWrite(uint32_t off, uint32_t v, uint32_t mask);
    void ppuVblank(bool start);
    void render();
    uint32_t spaceRead32(uint32_t a);
    uint32_t blend(uint32_t c0, uint32_t c1, uint8_t level) const;
    void putArgb1555(int y, int x, uint16_t argb, uint8_t bl);
    void putRgb565(int y, int x, uint16_t rgb, uint8_t bl);
    void blitSprite(uint32_t control, uint32_t attribute, uint32_t bufStart);
    void blitBitmap(uint32_t control, uint32_t attribute, int posy, int posx, uint32_t nptr, uint32_t bufStart, uint8_t bl);
    void blitCharacter(uint32_t control, uint32_t attribute, int posy, int posx, uint32_t nptr, uint32_t bufStart, uint8_t bl);
    int m_visW = 640, m_visH = 480;
    std::vector<uint32_t> m_bmp;             // m_visW x m_visH
    std::vector<uint32_t> m_out;             // 640 x 480

    // CD servo
    CdImage m_cd;
    uint32_t m_cdAddr = 0, m_cdData = 0, m_cdBufStart = 0, m_cdBufEnd = 0, m_cdBufPtr = 0;
    uint8_t m_cdSpeed = 1, m_seekMin = 0, m_seekSec = 0, m_seekFrm = 0;
    uint32_t m_seekLba = 0, m_sectorSize = 0, m_dspData = 0;
    bool m_frameFound = false;
    uint32_t m_cdControl0 = 0, m_cdControl1 = 0;
    uint16_t m_cdSkip = 0;
    uint32_t m_dspRegs[0x10] = {};
    std::vector<uint32_t> m_dspMem;
    std::vector<uint8_t> m_qsub;
    uint32_t m_totSectors = 0;
    int m_curSector = 0;
    uint64_t m_cdAt = UINT64_MAX;
    void cdReset();
    void cdEvent();
    void cdTiming();
    double cdBoost() const { return m_fastCd ? 2.0 : 1.0; }   // option 0: the drive spins twice as fast (4x and up overrun the BIOS's buffer)
    bool m_fastCd = false;
    void servoCmdR();
    void servoCmdW();
    void addQsub(int sector, uint8_t addrctrl, uint8_t track, uint8_t index, uint32_t relMsf, uint32_t absMsf);
    void generateQsub();

    // SPU DAC: a ring of 16-bit unsigned PCM in SDRAM read at 54 MHz / (divider + 1), with an
    // interrupt (63) at each half of the ring. Register meanings from HyperScanEmu (BSD-3-Clause,
    // Aloys); the SPU's synthesizer isn't emulated.
    uint32_t m_dacBufStart = 0, m_dacClockCfg = 0, m_dacMode1 = 0, m_dacMode2 = 0, m_dacOutL = 0, m_dacOutR = 0;
    uint32_t m_dacDivider = 0, m_dacBaseLo = 0, m_dacBaseHi = 0, m_dacIrq = 0, m_dacPos = 0;
    double m_dacPhase = 0;
    uint64_t m_dacLast = 0;
    std::vector<int16_t> m_dacOut;          // mono, at the DAC rate
    uint32_t dacSize() const { return 1024u << (m_dacIrq & 3); }
    bool dacOn() const { return (m_dacClockCfg & 3) == 3 && !(m_dacMode1 & 3) && (m_dacMode2 & 0x1000) && (m_dacMode2 & 3); }
    bool dacReg(uint32_t a, uint32_t* r, uint32_t v, bool write);
    void dacRun(uint64_t t);

    // RFID card (on GPIO): absent for now
    int cardRead() { return 0; }

    uint32_t m_keys = 0;
    float m_anaX = 0, m_anaY = 0;
    uint64_t m_frame = 0;
    bool m_failed = false;
    std::vector<std::pair<uint32_t, int>> m_seen;
};
