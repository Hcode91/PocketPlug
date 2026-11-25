// PocketPlug - Ferguson / Digital Research Computers Big Board II
// SPDX-License-Identifier: BSD-3-Clause
//
// Port of MAME's xerox/bigbord2.cpp (Robbbert and contributors, BSD-3): Z80 at
// 4 MHz, two CTCs, an SIO (nothing on its ports), an MB8877 (the NGEN WD core)
// through the HALT-gated NMI, an MC6845 with the SMC CRT8002 attribute
// generator over 2 KB of characters and 2 KB of attributes, the bank latch that
// swaps the 24 KB of ROM / video for RAM, and an ASCII keyboard on port D0.
// The Z80 DMA and the SASI hard disk port are not emulated.
//
// Keyboard: keyEvent(code, true) with an ASCII code. Drives: media 0 / 1.
// Frame: 640 x 288 (80 x 24 cells of 8 x 12), white on black.

#pragma once
#include "../../core_api.h"
#include "../../common/z80/z80.h"
#include "../ngen/ngen_dev.h"
#include "z80dev.h"
#include <memory>
#include <deque>

class BigBoard2Core : public Core {
public:
    BigBoard2Core();
    bool load(const std::vector<RomFile>& roms, std::string& error) override;
    void reset() override;
    void runFrame() override;
    const uint32_t* frameBuffer() const override { return m_out.data(); }
    int frameWidth() const override { return 640; }
    int frameHeight() const override { return 288; }
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
    void selectDrive();
    void render();
    void step();
    uint8_t crt8002(int ra, uint8_t chr, uint8_t attr, bool cursor);

    std::vector<uint8_t> m_rom, m_chr;
    uint8_t m_ram[0x10000];
    uint8_t m_vram[0x800], m_aram[0x800];
    uint8_t m_latch1 = 0, m_latch2 = 0;
    uint8_t m_crtcIdx = 0, m_crtc[32] = {};
    uint8_t m_termData = 0, m_termStatus = 0;
    unsigned m_cnt = 0;

    z80 m_z80;
    Z80Ctc m_ctc1, m_ctc2;
    Z80Sio m_sio;
    Z80DaisyDev* m_chain[3];
    int m_ackFrom = -1;
    bool m_nmiLine = false;

    Wd2797 m_fdc;
    std::unique_ptr<DiskImage> m_disk[2];
    bool m_fdcIrq = false, m_fdcDrq = false;
    int m_fdcCarry = 0, m_busyFrames = 0;

    std::deque<uint8_t> m_keys;
    int m_keyGap = 0;
    double m_beepPhase = 0, m_sampleAcc = 0;
    std::vector<uint32_t> m_out;
    std::vector<int16_t> m_audio;
};
