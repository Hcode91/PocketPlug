// PocketPlug - APF M-1000 (MP1000) TV game
// SPDX-License-Identifier: BSD-3-Clause
//
// Port of MAME's APF driver (src/mame/apf/apf.cpp, 0.264; copyright-holders
// Robbbert) and its cartridge slot (src/devices/bus/apf, Fabio Priuli):
// MC6800 at 3.579545 MHz / 4, MC6847 VDG, one MC6821 PIA for the hand
// controllers, the video mode lines and the speaker. Rocket Patrol is built
// into the BIOS, so the console runs without a cartridge.

#pragma once
#include "../../core_api.h"
#include "../../common/m6801.h"
#include "../../common/mc6847.h"
#include "../../common/pia6821.h"
#include <cstdint>
#include <vector>

// Input bits (1 = pressed). Player 1 is the left controller (MAME's player 1).
//  0 up, 1 down, 2 left, 3 right, 4 fire / ENTER, 5..14 keys 0..9, 15 CLEAR
// 16..31 the same for player 2 (the right controller)
class ApfCore : public Core, M6801::Ports, MC6847::Host, PIA6821::Host {
public:
    ApfCore();
    bool load(const std::vector<RomFile>& roms, std::string& error) override;
    void reset() override;
    void runFrame() override;
    const uint32_t* frameBuffer() const override { return m_frame; }
    int frameWidth() const override { return MC6847::WIDTH; }
    int frameHeight() const override { return MC6847::HEIGHT; }
    double refreshRate() const override { return 3579545.0 / (228.0 * 262.0); }
    int sampleRate() const override { return 44100; }
    size_t readAudio(int16_t* out, size_t maxSamples) override;
    void setInputs(uint32_t pressed) override { m_inputs = pressed; }
    bool saveState(std::vector<uint8_t>& out) override;
    bool loadState(const std::vector<uint8_t>& in) override;

    // M6801::Ports (plain 6800 bus)
    uint8_t extRead(uint16_t a) override;
    void extWrite(uint16_t a, uint8_t d) override;
    // MC6847::Host
    uint8_t vdgInput(uint16_t address) override;
    void vdgFsync(int state) override { m_pia.cb1W(state); }
    int64_t vdgNow() override { return m_cpu.cycles() * 4; }
    // PIA6821::Host
    bool hasInA() override { return true; }
    uint8_t inA() override;
    void outB(uint8_t data, uint8_t ddr) override;
    void ca2(int state) override { m_s.ca2 = uint8_t(state); }
    void cb2(int state) override;
    void irqA(int state) override { m_cpu.setIrq1(state != 0); }
    void irqB(int state) override { m_cpu.setIrq1(state != 0); }

private:
    M6801 m_cpu;
    MC6847 m_vdg;
    PIA6821 m_pia;
    uint8_t m_bios[0x1000];
    std::vector<uint8_t> m_cart;
    bool m_cartRamFitted = false;      // Space Destroyers: 1K of RAM at 9800

    struct State {
        uint8_t videoram[0x400];
        uint8_t cartRam[0x400];
        uint8_t latch, padData, ca2;
        uint8_t speaker;
        int64_t nextHsync;
    } m_s;

    uint32_t m_inputs = 0;
    uint32_t m_frame[MC6847::WIDTH * MC6847::HEIGHT];

    // audio: the speaker level integrated over CPU cycles
    int64_t m_audioLast = 0;
    double m_audioPhase = 0, m_audioAcc = 0, m_dc = 0;
    std::vector<int16_t> m_audio;
    void audioTo(int64_t cycle);
};
