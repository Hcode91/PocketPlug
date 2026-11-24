// PocketPlug - Novag Beluga chess computer (model 903, 1990)
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Port of MAME's novag/primo.cpp (hap; BSD-3-Clause): HD6301Y0 in mode 2 at
// 16 MHz (4 MHz E clock) with 2 KB of RAM at 4000, an 8x8 button board with
// 16 LEDs (ranks on the left, files underneath), 16 keys, a piezo and the
// VIP-style LCD (4 digits + symbols, nvip.svg).
//
// The frame is 384 x 124:
//   [0..63] pieces, [64] piece in hand, [65] its square + 1,
//   [130..145] LED brightness: 130 + n = rank LED n (0 = rank 8), 138 + n = file LED n (a..h),
//   from index 1152 (row 3) on: the LCD picture, 384 x 121.
// Input bits: 0-7 the top row (Trace Back, Trace Fwd, Set Level, Info, Easy, Solve Mate,
// Sound, Referee; also the piece keys Pawn..King), 8-15 the second row (Go, Restore, Hint,
// Clear, Color, Verify, Random, New Game).

#pragma once
#include "../../core_api.h"
#include "../../common/hd6301.h"
#include "../../common/chessboard.h"
#include "../../common/svgscreen.h"
#include "../../common/pwmdisplay.h"
#include <vector>

class BelugaCore : public Core, private Hd6301::Ports {
public:
    BelugaCore() : m_cpu(this) {}
    bool load(const std::vector<RomFile>& roms, std::string& error) override;
    void reset() override;
    void runFrame() override;
    const uint32_t* frameBuffer() const override { return m_out.data(); }
    int frameWidth() const override { return W; }
    int frameHeight() const override { return H; }
    double refreshRate() const override { return 60.0; }
    int sampleRate() const override { return 44100; }
    size_t readAudio(int16_t* out, size_t maxSamples) override;
    void setInputs(uint32_t pressed) override { m_keys = uint16_t(pressed); }
    void keyEvent(int code, bool down) override { if (down) chessBoardKey(m_s.board, code); }
    bool saveState(std::vector<uint8_t>& out) override;
    bool loadState(const std::vector<uint8_t>& in) override;

    static constexpr int W = 384, H = 124, LCD_TOP = 3;

private:
    uint8_t portIn(int port) override;
    void portOut(int port, uint8_t data, uint8_t ddr) override;
    uint8_t extRead(uint16_t a) override;
    void extWrite(uint16_t a, uint8_t d) override;
    uint64_t now() const { return m_cycles; }

    std::vector<uint8_t> m_rom;
    Hd6301 m_cpu;
    SvgScreen m_svg;
    std::vector<int16_t> m_keyOf;          // lcd row * 10 + col -> svg key
    std::vector<uint8_t> m_levels;
    PwmDisplay m_lcd, m_led;
    uint16_t m_keys = 0;
    uint64_t m_cycles = 0;
    std::vector<uint32_t> m_out = std::vector<uint32_t>(W * H, 0);
    std::vector<int16_t> m_audio;
    double m_carry = 0;

    struct State {
        uint8_t ram[0x800];
        uint8_t inpMux, select, ledData, lcdStrobe, speaker;
        ChessBoard board;
    } m_s = {};
};
