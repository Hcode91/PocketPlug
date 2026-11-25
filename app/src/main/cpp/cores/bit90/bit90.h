// PocketPlug - Bit Corporation Bit90 (ColecoVision-compatible home computer)
// SPDX-License-Identifier: BSD-3-Clause
//
// Port of the bit90 parts of MAME's coleco/coleco.cpp (Mike Balfour, Ben Bruscella,
// Sean Young, Dirk Best, Curt Coder and contributors, BSD-3): a Z80 at 3.58 MHz, a
// TMS9929A, an SN76489A, the 8 KB BASIC ROM at 2000h over two 8 KB banks at
// 0000h (reading I/O 80h / 88h picks one: the ColecoVision BIOS-compatible
// monitor or the BASIC's second half), 2 KB of RAM at 6000h plus 32 KB at
// 8000h, the 8 x 8 keyboard matrix (row on port E0h bits 4-6, read on C0h)
// and the ColecoVision controller ports. A ColecoVision cartridge in the slot
// (media 2) replaces the RAM at 8000h.
//
// keyEvent(code, down): code = row * 8 + bit of MAME's ROW0-ROW7.
// Inputs (controller 1): up 0x01, down 0x02, left 0x04, right 0x08, left fire
// 0x10, right fire 0x20; keypad 0-9, #, * in bits 8-19.
// Option 0: BASIC 3.0 (0) or 3.1 (1, default, as MAME).

#pragma once
#include "../../core_api.h"
#include "../../common/z80/z80.h"
#include "../../common/tms9928a.h"
#include "../../common/sn76489.h"
#include <vector>

class Bit90Core : public Core {
public:
    Bit90Core();
    bool load(const std::vector<RomFile>& roms, std::string& error) override;
    void reset() override;
    void runFrame() override;
    const uint32_t* frameBuffer() const override { return m_frame.data(); }
    int frameWidth() const override { return W; }
    int frameHeight() const override { return H; }
    double refreshRate() const override { return 50.0; }
    int sampleRate() const override { return 44100; }
    size_t readAudio(int16_t* out, size_t maxSamples) override;
    void setInputs(uint32_t pressed) override { m_pad = pressed; }
    void keyEvent(int code, bool down) override;
    void setOption(int id, int value) override;
    bool mediaInsert(int drive, const std::string& path, std::string& error) override;
    void mediaEject(int drive) override;
    bool saveState(std::vector<uint8_t>& out) override;
    bool loadState(const std::vector<uint8_t>& in) override;

    uint8_t memRead(uint16_t a);
    void memWrite(uint16_t a, uint8_t d);
    uint8_t ioRead(uint8_t a);
    void ioWrite(uint8_t a, uint8_t d);

    static constexpr int W = 272, H = 208;

private:
    uint8_t padRead(int n);
    void applyBios();

    std::vector<uint8_t> m_basic[2], m_banked[2];   // [bios]: 2000h ROM, 0000h banks (2 x 8 KB)
    int m_bios = 1;
    std::vector<uint8_t> m_cart;
    struct State {
        uint8_t ram[0x800];
        uint8_t xram[0x8000];
        int bank, joyMode, keySel;
        bool vdpIrq;
        double phase;
    } m_s;
    uint8_t m_keys[8];
    uint32_t m_pad = 0;
    z80 m_z80;
    Tms9928a m_vdp;
    Sn76489 m_psg;
    std::vector<uint32_t> m_frame;
    std::vector<int16_t> m_audio;
    double m_audioAcc = 0, m_dcX = 0, m_dcY = 0;
    int m_audioN = 0;
};
