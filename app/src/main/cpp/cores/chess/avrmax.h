// PocketPlug - Elektor AVR-Max / ATM18 Mini Chess Computer
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Port of MAME's elektor/avrmax.cpp (hap; BSD-3-Clause): H.G. Muller's micro-Max on
// an ATmega88. The LED version (avrmax, avrmaxg) has a 4-digit display; the ATM18
// version (atm18mcc) drives a 4 x 20 HD44780U LCD through a CD4094 shift register.
// There is no board: pieces are moved by hand on the skin's board, for show.
//
// Frame (32 x 32 status block): [0..63] pieces, [64] piece in hand, [65] its square + 1,
//   [130 + d * 8 + s] digit d (0 = left) segment s (a..g, dp) on,
//   [200 + (line * 20 + col) * 8 + row] LCD character rows (5 bits).
// Input bits: A1 B2 C3 D4 E5 F6 G7 H8 = 0-7, FN 8, CL 9, GO 10 (MAME's IN.n bit b = n + 4 * b).

#pragma once
#include "../../core_api.h"
#include "../../common/avr8.h"
#include "../../common/hd44780.h"
#include "../../common/chessboard.h"
#include <vector>

class AvrMaxCore : public Core {
public:
    AvrMaxCore(const char* rom, bool lcd) : m_romName(rom), m_lcd(lcd) {}
    bool load(const std::vector<RomFile>& roms, std::string& error) override;
    void reset() override;
    void runFrame() override;
    const uint32_t* frameBuffer() const override { return m_out; }
    int frameWidth() const override { return 32; }
    int frameHeight() const override { return 32; }
    double refreshRate() const override { return 60.0; }
    int sampleRate() const override { return 44100; }
    size_t readAudio(int16_t* out, size_t maxSamples) override;
    void setInputs(uint32_t pressed) override { m_keys = pressed; }
    void keyEvent(int code, bool down) override { if (down) chessBoardKey(m_s.board, code); }
    bool saveState(std::vector<uint8_t>& out) override;
    bool loadState(const std::vector<uint8_t>& in) override;

private:
    void flushDigits();
    const char* m_romName;
    bool m_lcd;
    int m_hz = 8000000;
    std::vector<uint8_t> m_rom, m_cgrom;
    AVR8 m_cpu;
    HD44780 m_hd;
    uint32_t m_keys = 0;
    uint32_t m_out[32 * 32];
    std::vector<int16_t> m_audio;
    uint64_t m_acc[32], m_lastFlush = 0;
    uint8_t m_held[4] = {}, m_age[4] = { 255, 255, 255, 255 };

    struct State {
        uint8_t inpMux, digitSel, segs, shiftReg, shiftClk, lcdE;
        ChessBoard board;
    } m_s;
};
