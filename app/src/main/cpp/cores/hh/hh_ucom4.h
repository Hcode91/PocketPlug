// PocketPlug - NEC uCOM-4 handhelds (port of MAME's handheld/hh_ucom4.cpp, hap)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once
#include "hh_base.h"
#include "../../common/ucom4.h"

class HhUcom4Core : public HhBase, private UCOM4::Io {
public:
    explicit HhUcom4Core(const std::string& game);
    bool load(const std::vector<RomFile>& roms, std::string& error) override;
    void reset() override;
    void runFrame() override { runFrameCommon(); }
    bool saveState(std::vector<uint8_t>& out) override;
    bool loadState(const std::vector<uint8_t>& in) override;
    static bool known(const std::string& game);

private:
    int runCpu(int n) override { return m_cpu.run(n); }
    uint64_t now() const override { return m_cpu.cycles(); }
    uint8_t input(int port) override;
    void output(int port, uint8_t data) override;

    const struct Ucom4Game* m_g;
    UCOM4 m_cpu;
    struct State { uint8_t port[9]; uint16_t inpMux; uint32_t grid, plate; } m_s;
};
