// PocketPlug - Firecore Mega Drive clone: AtGames "40 Bonus Games in 1"
// SPDX-License-Identifier: GPL-2.0-or-later
//
// A Mega Drive on a chip (Firecore) with its game in a 16 MB flash and a banking register
// pair at 0xb01028 / 0xb0102a (+ 0xa10104 for the upper half), after MAME's
// sega/megadriv_firecore.cpp (David Haywood; BSD-3-Clause). The Mega Drive itself is
// PocketPlug's own: Musashi 68000, the superzazu Z80, the 315-5313 VDP (common/mdvdp.h),
// ymfm's YM2612 (Aaron Giles, BSD-3) and the Sega PSG. PAL timing, 3-button pad.
//
// Input bits (A3dBits, the blue pad): 0 up, 1 down, 2 left, 3 right, 4 A, 5 B, 6 START, 7 C.

#pragma once
#include "../../core_api.h"
#include "../../common/z80/z80.h"
#include "../../common/mdvdp.h"
#include "../../common/sn76489.h"
#include "../../common/ymfm/ymfm_opn.h"
#include <vector>

class FirecoreCore : public Core, public ymfm::ymfm_interface {
public:
    FirecoreCore();
    ~FirecoreCore() override;
    bool load(const std::vector<RomFile>& roms, std::string& error) override;
    void reset() override;
    void runFrame() override;
    const uint32_t* frameBuffer() const override { return m_fb; }
    int frameWidth() const override { return MdVdp::W; }
    int frameHeight() const override { return 224; }
    double refreshRate() const override { return 53203424.0 / (3420.0 * 313.0); }
    int sampleRate() const override { return 44100; }
    size_t readAudio(int16_t* out, size_t maxSamples) override;
    void setInputs(uint32_t pressed) override { m_keys = pressed; }
    bool saveState(std::vector<uint8_t>& out) override;
    bool loadState(const std::vector<uint8_t>& in) override;

    uint8_t read8(uint32_t a);
    uint16_t read16(uint32_t a);
    void write8(uint32_t a, uint8_t d);
    void write16(uint32_t a, uint16_t d);
    int intAck(int level);
    uint8_t zRead(uint16_t a);
    void zWrite(uint16_t a, uint8_t d);

    // ymfm_interface
    void ymfm_set_timer(uint32_t tnum, int32_t duration) override { m_s.ymTimer[tnum & 1] = duration; }

private:
    uint8_t padRead(int port);
    void audioLine(int cycles68k);
    void updateIrq();

    std::vector<uint8_t> m_rom;      // big-endian bytes, 16 MB
    uint32_t m_romMask = 0;
    uint32_t m_fb[MdVdp::W * MdVdp::H];
    z80 m_z80;
    MdVdp m_vdp;
    Sn76489 m_psg;
    ymfm::ym2612 m_ym;
    uint32_t m_keys = 0;
    std::vector<int16_t> m_audio;
    uint32_t m_firstTop = 0;

    struct State {
        uint8_t ram[0x10000];
        uint8_t zram[0x2000];
        uint32_t bank;               // flash bank (byte offset) added to 0x000000-0x3fffff
        uint32_t zBank;              // Z80's window into 68000 space
        uint8_t busReq, zReset, zIrqLines;
        uint8_t padData[3], padCtrl[3];
        int32_t ymTimer[2];
        double lineCarry68k, ymPos, psgPos, outPos;
        int32_t ymLast;
        uint8_t altMode;             // 0xb01036 written (the extended display mode)
    } m_s;
};
