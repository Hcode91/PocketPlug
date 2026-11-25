// PocketPlug - BBN BitGraph rev A / rev B graphics terminal
// SPDX-License-Identifier: BSD-3-Clause
//
// Port of MAME's skeleton/bitgraph.cpp (Sergey Svishchev, BSD-3): a 68000 at
// 6.9 MHz, 32 KB of ROM, 128 KB of RAM holding the 1024 x 768 bitmap at 3E0000h
// (rev B: 384 KB more at 380000h), three 6850 ACIAs (host, keyboard, debugger;
// rev A has a fourth for a mouse), a 6821 PIA driving an AY-3-8912 and an ER2055
// EAROM (settings), the system clock on IRQ 6 (rev A 40 Hz, rev B 1040 Hz) and
// the serial ports on IRQ 1. Rev A's ROM has MAME's clksync() patch.
//
// The keyboard (a Key Tronic L2207 sending ASCII at 300 baud) is high-level:
// keyEvent(ascii, true) queues a character. The host port has a loopback plug
// by default (option 0 = 1): what the terminal sends comes back, so typing
// shows up and its escape sequences are acted on; 0 = nothing connected.
// Rev B runs ROM 2.33 alpha (MAME default), or 3.0P if only that set is there.
// Frame: the monitor stands upright, so the picture is 768 x 1024.

#pragma once
#include "../../core_api.h"
#include "../../common/pia6821.h"
#include <deque>
#include <string>
#include <vector>

class BitGraphCore : public Core, PIA6821::Host {
public:
    explicit BitGraphCore(bool revB);
    ~BitGraphCore() override;
    bool load(const std::vector<RomFile>& roms, std::string& error) override;
    void reset() override;
    void runFrame() override;
    const uint32_t* frameBuffer() const override { return m_out.data(); }
    int frameWidth() const override { return 768; }
    int frameHeight() const override { return 1024; }
    double refreshRate() const override { return 40.0; }
    int sampleRate() const override { return 44100; }
    size_t readAudio(int16_t* out, size_t maxSamples) override;
    void setInputs(uint32_t) override {}
    void keyEvent(int code, bool down) override;
    void setOption(int id, int value) override;
    bool saveState(std::vector<uint8_t>&) override { return false; }
    bool loadState(const std::vector<uint8_t>&) override { return false; }

    uint8_t read8(uint32_t a);
    uint16_t read16(uint32_t a);
    void write8(uint32_t a, uint8_t d);
    void write16(uint32_t a, uint16_t d);
    int intAck(int level);

    // PIA6821::Host
    bool hasInA() override { return true; }
    uint8_t inA() override;
    bool hasInB() override { return true; }
    uint8_t inB() override { return m_piaB; }
    void outA(uint8_t d) override { m_piaA = d; }
    void outB(uint8_t d, uint8_t) override;

private:
    struct Acia {
        uint8_t ctl = 0x03, status = 0x02, rx = 0;
        std::deque<uint8_t> in;
        int gap = 0;              // CPU cycles until the next byte may arrive
        bool reset = true;
        bool irq() const { return !reset && (((ctl & 0x80) && (status & 0x01)) || ((ctl & 0x60) == 0x20 && (status & 0x02))); }
    };
    uint8_t aciaRead(Acia& c, bool data);
    void aciaWrite(int n, Acia& c, bool data, uint8_t d);
    void serviceSerial(int cycles);
    void updateIrq();
    void psgWrite(uint8_t reg, uint8_t d);
    void render();
    void bindBus();

    bool m_revB;
    int m_bios = 0;
    bool m_loop = true;
    std::vector<uint8_t> m_rom;
    std::vector<uint8_t> m_ram;       // 380000h-3FFFFFh (rev A uses the top 128 KB)
    PIA6821 m_pia;
    Acia m_acia[4];
    uint8_t m_piaA = 0, m_piaB = 0;
    // ER2055
    uint8_t m_earom[64];
    uint8_t m_earomAddr = 0, m_earomData = 0, m_earomCtl = 0;
    // AY-3-8912
    uint8_t m_psgReg[16] = {}, m_psgAddr = 0;
    int m_toneCnt[3] = {}, m_toneOut[3] = {}, m_noiseCnt = 0, m_noiseOut = 0;
    uint32_t m_rng = 1;
    double m_psgAcc = 0, m_sampleAcc = 0;
    // the system clock
    double m_clkPhase = 0;
    bool m_clkLine = false, m_irq6 = false;
    int m_frame = 0;
    std::vector<uint32_t> m_out;
    std::vector<int16_t> m_audio;
};
