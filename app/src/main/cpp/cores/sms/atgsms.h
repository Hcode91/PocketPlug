// PocketPlug - AtGames "20 SEGA Master System & Game Gear Classics" plug & play
// SPDX-License-Identifier: GPL-2.0-or-later
//
// An SMS-on-a-chip TV game with a 4 MB flash: the menu at 0x200000 and 20 Master
// System / Game Gear games packed round it. MAME has the dump but no driver for the
// chip (atgsms, "not working"); this is PocketPlug's own: Z80, the SMS / GG VDP, the
// Sega PSG, the Sega mapper on top of the chip's flash base, and the menu's game launch.
//
// Input bits (the AtGames pad): 0 up, 1 down, 2 left, 3 right, 4 A (button 1), 5 B (button 2),
// 6 START (Game Gear START / Master System PAUSE), 7 C (button 2).

#pragma once
#include "../../core_api.h"
#include "../../common/z80/z80.h"
#include "../../common/smsvdp.h"
#include "../../common/sn76489.h"
#include <vector>

class AtgSmsCore : public Core {
public:
    bool load(const std::vector<RomFile>& roms, std::string& error) override;
    void reset() override;
    void runFrame() override;
    const uint32_t* frameBuffer() const override { return m_fb; }
    int frameWidth() const override { return SmsVdp::W; }
    int frameHeight() const override { return SmsVdp::H; }
    double refreshRate() const override { return 59.92; }
    int sampleRate() const override { return 44100; }
    size_t readAudio(int16_t* out, size_t maxSamples) override;
    void setInputs(uint32_t pressed) override;
    bool saveState(std::vector<uint8_t>& out) override;
    bool loadState(const std::vector<uint8_t>& in) override;

    uint8_t memRead(uint16_t a);
    void memWrite(uint16_t a, uint8_t d);
    uint8_t ioRead(uint8_t port);
    void ioWrite(uint8_t port, uint8_t d);

private:
    void launch(int game);
    std::vector<uint8_t> m_flash;
    uint32_t m_fb[SmsVdp::W * SmsVdp::H];
    z80 m_z80;
    SmsVdp m_vdp;
    Sn76489 m_psg;
    uint32_t m_keys = 0;
    int m_lineCarry = 0;
    std::vector<int16_t> m_audio;

    struct State {
        uint8_t ram[0x2000];
        uint32_t base;          // flash offset of the running program
        uint32_t size;          // its size (bank numbers wrap)
        uint8_t bank[3], ctrl, gg, ioCtrl;
        uint8_t custom[256];
        int32_t psgCarry;
        double audioAcc, audioN, audioPos;
        uint8_t pauseHeld;
    } m_s;
};
