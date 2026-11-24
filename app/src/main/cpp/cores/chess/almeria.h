// PocketPlug - Mephisto Almeria 16 Bit / 32 Bit chess modules (Hegener + Glaser, 1988)
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Port of MAME's hegenerglaser/modular.cpp with the Mephisto sensors board
// (mboard.cpp) and the 16/32 bit display module (mdisplay2.cpp) (Dirk Verwiebe,
// Cowering, Sandro Ronco, hap; BSD-3-Clause):
//   16 Bit: 68000 at 12.288 MHz, 128 KB ROM, 512 KB RAM, IRQ 3 at 600 Hz
//   32 Bit: 68020 at 12.288 MHz, 128 KB ROM, 1 MB RAM, IRQ 3 at 750 Hz
// Magnet sensor board with 64 LEDs, HD44780 2x16 LCD, 6 keys, 8 KB battery
// RAM, 2-bit sound DAC. The 68k is Musashi (shared with the UNIX PC core).
//
// The frame buffer is a status block for the skin (as the Academy's):
//   [0..63] pieces, [64] piece in hand, [65] its square + 1 (0 = none),
//   [66..129] board LED brightness (0-255, rank * 8 + file),
//   [160..415] LCD: 2 lines x 16 characters x 8 rows of 5 pixels.

#pragma once
#include "../../core_api.h"
#include "../../common/hd44780.h"
#include "../../common/chessboard.h"
#include <string>
#include <vector>

class AlmeriaCore : public Core {
public:
    // romEven/romOdd: the 16 bit module's two EPROMs; romOdd empty = the 32 bit module's single ROM.
    // berlin: the Mephisto Berlin 68000 (is32 false) / Berlin Professional 68020 (is32 true): the
    // same display module and LCD in a self-contained board with buttons and 64 LEDs, an 8-key
    // keypad read with the board, IRQ 2 at 750 Hz, other address maps
    AlmeriaCore(bool is32, std::string romEven, std::string romOdd, bool berlin = false);
    ~AlmeriaCore() override;
    bool load(const std::vector<RomFile>& roms, std::string& error) override;
    void reset() override;
    void runFrame() override;
    const uint32_t* frameBuffer() const override { return m_out; }
    int frameWidth() const override { return 32; }
    int frameHeight() const override { return 16; }
    double refreshRate() const override { return 60.0; }
    int sampleRate() const override { return 44100; }
    size_t readAudio(int16_t* out, size_t maxSamples) override;
    // bits 0-5: ENT, UP, CL, LEFT, RIGHT, DOWN; Berlin also bit 6: NEW GAME
    void setInputs(uint32_t pressed) override { m_keys = uint8_t(pressed); }
    void keyEvent(int code, bool down) override { if (down) chessBoardKey(m_s.board, code); }
    bool saveState(std::vector<uint8_t>& out) override;
    bool loadState(const std::vector<uint8_t>& in) override;

    // bus (called from Musashi through m68k_bus)
    uint8_t read8(uint32_t a);
    void write8(uint32_t a, uint8_t d);
    uint16_t read16(uint32_t a);
    void write16(uint32_t a, uint16_t d);
    uint32_t read32(uint32_t a);
    void write32(uint32_t a, uint32_t d);
    int intAck(int level);

private:
    void sampleOutputs();
    uint8_t keyByte(int port) const;   // the KEYn port's active byte
    void displayIo(uint8_t d);
    uint8_t boardInput() const;

    bool m_is32;
    bool m_berlin;
    int m_hz;
    int m_irqLevel;
    std::string m_romEven, m_romOdd;
    std::vector<uint8_t> m_rom;     // 128 KB, big-endian
    std::vector<uint8_t> m_ram;     // 512 KB / 1 MB
    uint32_t m_ramMask;
    uint8_t m_cgrom[0x1000];
    HD44780 m_lcd;
    uint8_t m_keys = 0;
    uint32_t m_out[32 * 16];
    std::vector<int16_t> m_audio;
    uint32_t m_ledAcc[64];
    uint32_t m_slices = 0;
    double m_carry = 0;
    int m_irqPeriod;

    struct State {
        uint8_t nvram[0x2000];
        uint8_t mux, ledData, latch, ctrl;
        uint8_t irqLine, pad[3];
        int32_t irqCount;
        ChessBoard board;
    } m_s;
};
