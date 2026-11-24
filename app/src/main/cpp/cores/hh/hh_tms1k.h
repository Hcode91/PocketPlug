// PocketPlug - TI TMS1000-family handhelds (port of MAME's handheld/hh_tms1k.cpp, hap)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once
#include "hh_base.h"
#include "../../common/tms1k.h"
#include "../../common/tms5110/tms5110.h"

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

    void speechTo(uint64_t t);

    const struct Tms1kGame* m_g;
    // talking toys: TMS5110A speech + TMS6100 VSM, 8 kHz (one sample per 40 clocks at 320 kHz)
    Tms5110 m_tms;
    Tms6100 m_vsm;
    std::vector<uint8_t> m_vsmRom;
    bool m_speechOn = false;
    uint64_t m_spPos = 0;
    std::vector<int16_t> m_speech;
    TMS1K m_cpu;
    struct State {
        uint32_t r;
        uint32_t o2;          // Bingo Bear: the raw R outputs
        uint16_t o, inpMux;
        uint8_t power;
        uint64_t idle;        // clocks passed while powered off
        uint32_t prevIn;
    } m_s;
};
