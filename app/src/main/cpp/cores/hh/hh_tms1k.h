// PocketPlug - TI TMS1000-family handhelds (port of MAME's handheld/hh_tms1k.cpp, hap)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once
#include "hh_base.h"
#include "../../common/tms1k.h"

class HhTms1kCore : public HhBase, private TMS1K::Io {
public:
    explicit HhTms1kCore(const std::string& game);
    bool load(const std::vector<RomFile>& roms, std::string& error) override;
    void reset() override;
    void runFrame() override;
    bool saveState(std::vector<uint8_t>& out) override;
    bool loadState(const std::vector<uint8_t>& in) override;
    static bool known(const std::string& game);

private:
    int runCpu(int n) override;
    uint64_t now() const override { return m_cpu.cycles() + m_s.idle; }
    void inputsChanged() override;
    uint8_t readK() override;
    void writeO(uint16_t o) override;
    void writeR(uint32_t r) override;
    void setPower(bool on);

    const struct Tms1kGame* m_g;
    TMS1K m_cpu;
    struct State {
        uint32_t r;
        uint16_t o, inpMux;
        uint8_t power;
        uint64_t idle;        // clocks passed while powered off
        uint32_t prevIn;
    } m_s;
};
