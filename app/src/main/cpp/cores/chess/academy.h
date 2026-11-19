// PocketPlug - Mephisto Academy chess computer (Hegener + Glaser, 1989)
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Port of MAME's academy.cpp with the Mephisto sensors board (mboard.cpp) and
// display module 2 (mdisplay2.cpp) (Sandro Ronco, hap; BSD-3-Clause):
// W65C02 at 4.9152 MHz, 600 Hz NMI, magnet sensor board with 64 LEDs,
// HD44780 2x16 LCD, 8 keys with LEDs, 2-bit sound DAC.
//
// The frame buffer is a status block for the skin, not a picture:
//   [0..63] pieces, [64] piece in hand, [65] its square + 1 (0 = none),
//   [66..129] board LED brightness (0-255, rank * 8 + file),
//   [130..137] key LEDs red, [138..145] green,
//   [160..415] LCD: 2 lines x 16 characters x 8 rows of 5 pixels.

#pragma once
#include "../../core_api.h"
#include "../../common/w65c02.h"
#include "../../common/hd44780.h"
#include "../../common/chessboard.h"
#include <vector>

class AcademyCore : public Core, private W65C02::Bus {
public:
    AcademyCore(std::string rom16, std::string rom32);
    bool load(const std::vector<RomFile>& roms, std::string& error) override;
    void reset() override;
    void runFrame() override;
    const uint32_t* frameBuffer() const override { return m_out; }
    int frameWidth() const override { return 32; }
    int frameHeight() const override { return 16; }
    double refreshRate() const override { return 60.0; }
    int sampleRate() const override { return 44100; }
    size_t readAudio(int16_t* out, size_t maxSamples) override;
    // bits 0-7: TRAIN, INFO, MEM, POS, LEV, FCT, ENT, CL
    void setInputs(uint32_t pressed) override { m_keys = uint8_t(pressed); }
    void keyEvent(int code, bool down) override { if (down) chessBoardKey(m_s.board, code); }
    bool saveState(std::vector<uint8_t>& out) override;
    bool loadState(const std::vector<uint8_t>& in) override;

private:
    uint8_t read(uint16_t a) override;
    void write(uint16_t a, uint8_t d) override;
    void sampleOutputs();

    std::string m_rom16, m_rom32;
    std::vector<uint8_t> m_rom;      // 64 KB region
    uint8_t m_cgrom[0x1000];
    W65C02 m_cpu;
    HD44780 m_lcd;
    uint8_t m_keys = 0;
    uint32_t m_out[32 * 16];
    std::vector<int16_t> m_audio;
    uint32_t m_ledAcc[64 + 16];
    uint32_t m_slices = 0;
    double m_carry = 0;

    struct State {
        uint8_t ram[0x2000];
        uint8_t mux, ledData, latch, keyLed;
        int32_t nmiCount;
        ChessBoard board;
    } m_s;
};
