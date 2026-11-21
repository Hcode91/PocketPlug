// PocketPlug - Fidelity Voice Chess Challenger / Advanced Voice Chess Challenger
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Port of MAME's fidelity/vcc.cpp (Kevin Horton, Jonathan Gevaryahu, Sandro
// Ronco, hap; BSD-3-Clause): Z80 at 4 MHz, an 8255 PPI, a 4-digit LED display,
// 16 keys plus RE (reset) and the S14001A speech chip (~25 kHz) with its ROM.
// The plastic board has no sensors: pieces are moved by hand, for show.
//
// The frame is a status block for the skin (like the other chess cores):
//   [0..63] pieces, [64] piece in hand, [65] its square + 1,
//   [130 + d * 8 + s] digit d (0 = left) segment s (a..g, 7 = the dot) brightness.
// Input bits: row * 4 + col of MAME's IN.0-IN.3 (IN.0 bit 0 unused), 16 = RE.
//   IN.0: -, LV, A1, E5   IN.1: Speaker, DM, B2, F6
//   IN.2: CL, PB, C3, G7  IN.3: EN, PV, D4, H8
// Option 0: voice language 0 English, 1 German, 2 French, 3 Spanish (needs that
// speech ROM from the vcc zip; applies after a reset).

#pragma once
#include "../../core_api.h"
#include "../../common/z80/z80.h"
#include "../../common/i8255.h"
#include "../../common/s14001a.h"
#include "../../common/chessboard.h"
#include <vector>

class VccCore : public Core {
public:
    explicit VccCore(bool advanced) : m_advanced(advanced) {}
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
    void setOption(int id, int value) override;
    bool saveState(std::vector<uint8_t>& out) override;
    bool loadState(const std::vector<uint8_t>& in) override;

    uint8_t memRead(uint16_t a);
    void memWrite(uint16_t a, uint8_t d);
    uint8_t ioRead(uint8_t p) { return m_ppi.read(p & 3); }
    void ioWrite(uint8_t p, uint8_t d) { m_ppi.write(p & 3, d); }

private:
    void updateDisplay();
    void flushDisplay();
    void selectSpeech();

    bool m_advanced;
    uint8_t m_rom[0x3000];
    std::vector<uint8_t> m_speechRom[4];
    int m_lang = 0;
    z80 m_z80;
    I8255 m_ppi;
    S14001A m_speech;
    uint32_t m_keys = 0;
    bool m_inReset = false;
    uint32_t m_out[32 * 16];
    std::vector<int16_t> m_audio;
    uint64_t m_time = 0, m_lastFlush = 0, m_frameStart = 0;
    uint64_t m_acc[32];
    uint8_t m_held[4] = {}, m_age[4] = { 255, 255, 255, 255 };

    struct State {
        uint8_t ram[0x400];
        uint8_t ledSelect, segData, inpMux, language;
        int32_t speechCarry;
        ChessBoard board;
    } m_s;
};
