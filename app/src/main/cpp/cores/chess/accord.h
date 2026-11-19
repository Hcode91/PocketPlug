// PocketPlug - Novag Accord chess computer (model 875, 1987)
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Port of MAME's novag/accord.cpp (hap; BSD-3-Clause): HD6301X0 at ~8 MHz
// (2 MHz E clock), 8x8 pressure-sensitive board, 8 buttons, 24 LEDs
// (rank and file LEDs plus 4 status LEDs), piezo.
//
// The frame buffer is a status block for the skin, not a picture:
//   [0..63] pieces, [64] piece in hand, [65] its square + 1 (0 = none),
//   [130..153] LED brightness (0-255), row y * 8 + x as MAME's "y.x":
//   row 0 = status LEDs (0 White, 1 Black, 2 Verify, 3 Setup),
//   row 1 = rank LEDs 1-8, row 2 = file LEDs a-h.

#pragma once
#include "../../core_api.h"
#include "../../common/hd6301.h"
#include "../../common/chessboard.h"
#include <vector>

class AccordCore : public Core, private Hd6301::Ports {
public:
    explicit AccordCore(std::string romName) : m_romName(std::move(romName)), m_cpu(this) {}
    bool load(const std::vector<RomFile>& roms, std::string& error) override;
    void reset() override;
    void runFrame() override;
    const uint32_t* frameBuffer() const override { return m_out; }
    int frameWidth() const override { return 32; }
    int frameHeight() const override { return 16; }
    double refreshRate() const override { return 60.0; }
    int sampleRate() const override { return 44100; }
    size_t readAudio(int16_t* out, size_t maxSamples) override;
    // bits 0-7: Go, Take Back, Sound, Set Level, New Game, Set Up, Verify, Black/White
    void setInputs(uint32_t pressed) override { m_keys = uint8_t(pressed); }
    void keyEvent(int code, bool down) override { if (down) chessBoardKey(m_s.board, code); }
    bool saveState(std::vector<uint8_t>& out) override;
    bool loadState(const std::vector<uint8_t>& in) override;

private:
    uint8_t portIn(int port) override;
    void portOut(int port, uint8_t data, uint8_t ddr) override;
    void sampleOutputs();

    std::string m_romName;
    std::vector<uint8_t> m_rom;
    Hd6301 m_cpu;
    uint8_t m_keys = 0;
    uint32_t m_out[32 * 16] = {};
    std::vector<int16_t> m_audio;
    uint32_t m_ledAcc[24] = {};
    uint32_t m_slices = 0;
    double m_carry = 0;

    struct State {
        uint16_t inpMux;
        uint8_t mx, my, speaker;
        ChessBoard board;
    } m_s = {};
};
