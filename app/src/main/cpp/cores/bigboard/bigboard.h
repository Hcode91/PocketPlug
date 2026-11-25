// PocketPlug - Big Board family CP/M computers
// SPDX-License-Identifier: BSD-3-Clause
//
// Port of MAME's xerox/xerox820.cpp (Curt Coder and contributors, BSD-3) for the
// Digital Research Computers Big Board (8" and 5.25" drives), the Scomar MK-82,
// "Moj mikro Slovenija" and the Xerox 820: Z80 at 2.5 MHz, keyboard / system
// PIO, general PIO, CTC, SIO (no serial devices), an FD1771 (the NGEN WD core,
// data bus inverted) and the 80 x 24 video from 4 KB of RAM at 3000h.
//
// Keyboard: keyEvent(code, true) with an ASCII code (the app does SHIFT / CTRL).
// Drives: media 0 = A:, 1 = B: (.imd, or raw single-density images).
// Frame: 560 x 240 (7 x 10 cells), white on black.

#pragma once
#include "../../core_api.h"
#include "../../common/z80/z80.h"
#include "../ngen/ngen_dev.h"
#include "z80dev.h"
#include <memory>
#include <string>
#include <deque>

class BigBoardCore : public Core {
public:
    explicit BigBoardCore(const std::string& id);
    bool load(const std::vector<RomFile>& roms, std::string& error) override;
    void reset() override;
    void runFrame() override;
    const uint32_t* frameBuffer() const override { return m_out.data(); }
    int frameWidth() const override { return 560; }
    int frameHeight() const override { return 240; }
    double refreshRate() const override { return 60.0; }
    int sampleRate() const override { return 44100; }
    size_t readAudio(int16_t* out, size_t maxSamples) override;
    void setInputs(uint32_t) override {}
    void keyEvent(int code, bool down) override;
    bool mediaInsert(int drive, const std::string& path, std::string& error) override;
    bool mediaSave(int drive, const std::string& path) override;
    void mediaEject(int drive) override;
    int mediaActivity() override { return m_busyFrames > 0 ? 1 : 0; }
    bool saveState(std::vector<uint8_t>&) override { return false; }
    bool loadState(const std::vector<uint8_t>&) override { return false; }

    uint8_t memRead(uint16_t a);
    void memWrite(uint16_t a, uint8_t d);
    uint8_t ioRead(uint16_t port);
    void ioWrite(uint16_t port, uint8_t d);

private:
    void paWrite(uint8_t d);
    uint8_t paRead();
    void selectDrive();
    void feedKey();
    void render();
    void step();

    std::string m_id;
    bool m_x820 = false;      // bit-per-drive select, no beeper
    bool m_eight = true;      // 8" drives (FD1771 at 2 MHz)
    std::vector<uint8_t> m_rom, m_chr;
    uint8_t m_ram[0x10000];
    uint8_t m_vram[0x1000];
    bool m_romView = true;
    int m_scroll = 0, m_ncset2 = 0, m_frame = 0;
    uint8_t m_pa = 0;
    bool m_bit5 = false;
    int m_beepClocks = 0;
    double m_beepPhase = 0;

    z80 m_z80;
    Z80Pio m_kbpio, m_gppio;
    Z80Ctc m_ctc;
    Z80Sio m_sio;
    Z80DaisyDev* m_chain[4];
    int m_ackFrom = -1;
    bool m_nmiLine = false;

    Wd2797 m_fdc;
    std::unique_ptr<DiskImage> m_disk[2];
    int m_drive = -1;
    bool m_fdcIrq = false, m_fdcDrq = false;
    int m_fdcCarry = 0;
    int m_busyFrames = 0;

    std::deque<uint8_t> m_keys;
    uint8_t m_kbLatch = 0;
    int m_keyGap = 0;

    std::vector<uint32_t> m_out;
    std::vector<int16_t> m_audio;
    double m_sampleAcc = 0;
};
