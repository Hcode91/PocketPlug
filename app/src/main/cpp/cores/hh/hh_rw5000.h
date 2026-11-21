// PocketPlug - Rockwell B5000-family handhelds (port of MAME's handheld/hh_rw5000.cpp, hap)
// SPDX-License-Identifier: BSD-3-Clause

#pragma once
#include "hh_base.h"
#include "../../common/b6000.h"

class HhRw5000Core : public HhBase, private B6000::Io {
public:
    explicit HhRw5000Core(const std::string& game);
    bool load(const std::vector<RomFile>& roms, std::string& error) override;
    void reset() override;
    void runFrame() override { runFrameCommon(); }
    bool saveState(std::vector<uint8_t>& out) override;
    bool loadState(const std::vector<uint8_t>& in) override;
    static bool known(const std::string& game);

private:
    int runCpu(int n) override;
    uint64_t now() const override { return m_cpu.cycles(); }
    void inputsChanged() override;
    uint8_t readKB() override;
    uint8_t readDIN() override { return 0; }
    void writeSTR(uint16_t d) override { m_s.str = d; m_disp.matrix(now(), m_s.str, m_s.seg); }
    void writeSEG(uint16_t d) override { m_s.seg = d; m_disp.matrix(now(), m_s.str, m_s.seg); }
    void writeSPK(int on) override { speaker(on); }

    const struct Rw5000Game* m_g;
    B6000 m_cpu;
    struct State { uint16_t str, seg; uint8_t held; } m_s;
};
