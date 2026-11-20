// PocketPlug - Mephisto Amsterdam / Dallas / Roma (Hegener + Glaser, 1985-1987)
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Port of MAME's hegenerglaser/amsterdam.cpp with the sensors board
// (mboard.cpp) and the first display module (mdisplay1.cpp, a 4-digit LCD
// fed through shift registers) (hap, Dirk Verwiebe, Cowering, Sandro Ronco;
// BSD-3-Clause):
//   68000 versions (Amsterdam, Dallas 68000, Roma 68000): 12 MHz, IRQ 5 at 50 Hz, 16 KB RAM
//   68020 versions (Dallas 68020, Roma 68020): 14 MHz, IRQ 4 at 50 Hz, 64 KB RAM
// The 68k is Musashi (shared with the UNIX PC core through m68k_bus).
//
// The frame is a status block for the skin:
//   [0..63] pieces, [64] piece in hand, [65] its square + 1,
//   [66..129] board LED brightness (rank * 8 + file),
//   [130..161] LCD segments, 4 digits x 8 (a-g, dp), 1 = dark segment shown.
// Input bits: 0-7 INFO POS LEV MEM CL ENT G/7 H/8, 8-15 A/1 B/2 C/3 D/4
// E/5 F/6 Left/9 Right/0, 16-17 the two RES keys (both together reset).

#pragma once
#include "../../core_api.h"
#include "../../common/chessboard.h"
#include "../../common/pwmdisplay.h"
#include <string>
#include <vector>

class MephMod1Core : public Core {
public:
    // is32: 68020 version; romHi/romLo: the two 32 KB EPROMs (16 bit versions), or rom32 alone
    MephMod1Core(bool is32, std::string romHi, std::string romLo);
    ~MephMod1Core() override;
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

    uint8_t read8(uint32_t a);
    void write8(uint32_t a, uint8_t d);
    uint16_t read16(uint32_t a);
    void write16(uint32_t a, uint16_t d);
    int intAck(int level);

private:
    void updateLcd();
    uint8_t boardInput() const;

    bool m_is32;
    std::string m_romHi, m_romLo;
    std::vector<uint8_t> m_rom;
    int m_hz, m_irqLevel, m_irqPeriod;
    uint32_t m_keys = 0;
    uint32_t m_out[32 * 16];
    std::vector<int16_t> m_audio;
    uint32_t m_ledAcc[64];
    uint32_t m_slices = 0;
    double m_carry = 0;
    uint64_t m_time = 0;

    struct State {
        uint8_t ram[0x10000];
        uint8_t mux, ledData, common, dac, irqLine, pad[3];
        uint32_t digitData;
        int32_t irqCount;
        ChessBoard board;
        PwmDisplay lcd;
    } m_s;
};
