// PocketPlug - Hitachi HMCS40 handhelds (port of MAME's handheld/hh_hmcs40.cpp, hap)
// SPDX-License-Identifier: GPL-2.0-or-later
//
// VFD tabletop / handheld games on the HD388xx / HD448xx MCUs. Each game is a
// row in the table in hh_hmcs40.cpp: its MCU type, clock, ROM, SVG screen and
// how its R / D ports drive the display, speaker and input matrix.

#pragma once
#include "hh_base.h"
#include "../../common/hmcs40.h"

class HhHmcs40Core : public HhBase, private HMCS40::Io {
public:
    explicit HhHmcs40Core(const std::string& game);
    bool load(const std::vector<RomFile>& roms, std::string& error) override;
    void reset() override;
    void runFrame() override { runFrameCommon(); }
    bool saveState(std::vector<uint8_t>& out) override;
    bool loadState(const std::vector<uint8_t>& in) override;
    static bool known(const std::string& game);

private:
    int runCpu(int n) override { return m_cpu.run(n); }
    uint64_t now() const override { return m_cpu.cycles(); }
    void inputsChanged() override;

    uint8_t readR(int index) override;
    void writeR(int index, uint8_t data) override;
    uint16_t readD() override;
    void writeD(uint16_t data, uint16_t mask) override;

    uint16_t readInputs(int columns) const;   // OR of the selected rows of the game's input matrix
    void updateDisplay();

    const struct Hmcs40Game* m_g;
    HMCS40 m_cpu;
    struct State {
        uint8_t r[8];
        uint16_t d, inpMux;
        uint32_t grid;
        uint64_t plate;
        uint8_t intLine[2];
    } m_s;
    void setInterrupt(int line, bool state);
};
