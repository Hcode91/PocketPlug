// PocketPlug - Sharp SM511 LCD handhelds (port of MAME's handheld/hh_sm510.cpp, hap)
// SPDX-License-Identifier: BSD-3-Clause
//
// The SVG elements are named x.y.z (group a/b/bs, segment 0-15, common 0-3),
// lit through MAME's 1 kHz deflicker. Dual-screen units stack the top and
// bottom SVGs into one frame.

#pragma once
#include "hh_base.h"
#include "../../common/sm511.h"

class HhSm511Core : public HhBase, private SM511::Io {
public:
    explicit HhSm511Core(const std::string& game);
    bool load(const std::vector<RomFile>& roms, std::string& error) override;
    void reset() override;
    void runFrame() override;
    bool saveState(std::vector<uint8_t>& out) override;
    bool loadState(const std::vector<uint8_t>& in) override;
    static bool known(const std::string& game);

private:
    int runCpu(int n) override { int r = m_cpu.run(n); m_cycles += uint64_t(r); return r; }
    uint64_t now() const override { return m_cycles; }
    void inputsChanged() override;
    uint8_t readK() override;
    int readBA() override { return 1; }
    void writeS(uint8_t s) override { m_s.inpMux = s; updateK(); }
    void writeR(uint8_t r) override { speaker(r & 1); }
    void lcd(const uint16_t rows[3][4]) override;
    void tick1k() override;
    void updateK() { m_cpu.setWakeup(readK() != 0); }

    const struct Sm511Game* m_g;
    SM511 m_cpu;
    uint64_t m_cycles = 0;
    SvgScreen m_svg2;                     // the bottom screen
    std::vector<uint8_t> m_levels2;
    std::vector<uint32_t> m_tmp;
    std::vector<int16_t> m_key1, m_key2;  // (x * 16 + y) * 4 + z -> svg key
    bool m_aclHeld = false;
    struct State {
        uint8_t inpMux;
        uint16_t rows[3][4];
        uint8_t decay[3][16][4];
    } m_s;
};
