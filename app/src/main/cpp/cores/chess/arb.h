// PocketPlug - ARB V2 (AVE Auto Response Board, 2012 rebuild with Sargon 4.0)
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Port of MAME's chess/ave_arb.cpp (hap, thanks to Berger; BSD-3-Clause):
// W65C02S + W65C22S at 16 MHz, 32 KB battery RAM, magnetic sensor board with
// a LED in each square, 8 keys with LEDs, 4 status LEDs, 1-bit speaker.
//
// The frame is a status block for the skin:
//   [0..63] pieces, [64] piece in hand, [65] its square + 1,
//   [66..129] square LED brightness (rank * 8 + file),
//   [130..137] key LEDs (8.7 ... 8.0: keys 0-6, W, B), [138..141] Your Move,
//   Sargon's Move, Check, Change Board.
// Input bits: 0-7 IN.0 (Hint/B, Variable/W, Monitor, Self Play, Change Board,
// Change Color, Change Level, New Game), 8 RESET, 9 HALT (RESET only works
// while HALT is held, as on the real one).

#pragma once
#include "../../core_api.h"
#include "../../common/w65c02.h"
#include "../../common/via6522.h"
#include "../../common/chessboard.h"
#include <vector>

class ArbV2Core : public Core, private W65C02::Bus {
public:
    ArbV2Core();
    bool load(const std::vector<RomFile>& roms, std::string& error) override;
    void reset() override;
    void runFrame() override;
    const uint32_t* frameBuffer() const override { return m_out; }
    int frameWidth() const override { return 32; }
    int frameHeight() const override { return 16; }
    double refreshRate() const override { return 60.0; }
    int sampleRate() const override { return 44100; }
    size_t readAudio(int16_t* out, size_t maxSamples) override;
    void setInputs(uint32_t pressed) override;
    void keyEvent(int code, bool down) override { if (down) chessBoardKey(m_s.board, code); }
    bool saveState(std::vector<uint8_t>& out) override;
    bool loadState(const std::vector<uint8_t>& in) override;

private:
    uint8_t read(uint16_t a) override;
    void write(uint16_t a, uint8_t d) override;
    void updateDisplay();

    std::vector<uint8_t> m_rom;
    W65C02 m_cpu;
    Via6522 m_via;
    uint32_t m_keys = 0;
    bool m_inReset = false;
    uint32_t m_out[32 * 16];
    std::vector<int16_t> m_audio;
    uint32_t m_acc[10 * 12];
    uint32_t m_slices = 0;
    double m_carry = 0;

    struct State {
        uint8_t ram[0x8000];
        uint8_t inpMux, ledGroup, ledLatch, spk;
        uint16_t ledSelect, ledData;
        ChessBoard board;
    } m_s;
};
