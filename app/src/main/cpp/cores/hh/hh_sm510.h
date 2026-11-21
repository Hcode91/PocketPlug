// PocketPlug - Sharp SM5xx LCD handhelds (port of MAME's handheld/hh_sm510.cpp, hap)
// SPDX-License-Identifier: BSD-3-Clause
//
// The SVG screen's elements are named x.y.z (O group, segment, common); each
// one is lit through MAME's 1 kHz deflicker (8 ticks to turn on, 17 to fade).

#pragma once
#include "hh_base.h"
#include "../../common/sm5a.h"

class HhSm510Core : public HhBase, private SM5A::Io {
public:
    explicit HhSm510Core(const std::string& game);
    bool load(const std::vector<RomFile>& roms, std::string& error) override;
    void reset() override;
    void runFrame() override;
    bool saveState(std::vector<uint8_t>& out) override;
    bool loadState(const std::vector<uint8_t>& in) override;
    static bool known(const std::string& game);

private:
    int runCpu(int n) override { int r = m_cpu.run(n); m_cycles += uint64_t(r); return r; }
    uint64_t now() const override { return m_cycles; }
    void inputsChanged() override { updateK(); }
    uint8_t readK() override;
    int readBA() override { return 1; }
    void writeR(uint8_t r) override;
    void lcd(const uint8_t segs[9][2]) override;
    void tick1k() override;
    uint8_t inputLine(int line) const;
    void updateK() { m_cpu.setWakeup(readK() != 0); }

    const struct Sm510Game* m_g;
    SM5A m_cpu;
    uint64_t m_cycles = 0;
    std::vector<int16_t> m_segKey;       // (x * 4 + y) * 2 + z -> svg key
    struct State {
        uint8_t inpMux;
        uint8_t state[32];                // per x.z: the 4 segment bits
        uint8_t decay[4][32];
    } m_s;
};
