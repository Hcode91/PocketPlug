// PocketPlug - Coleco ADAM (1983 home computer)
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Port of MAME's coleco/adam.cpp and bus/adamnet (Curt Coder; BSD-3-Clause):
// Z80 at 3.58 MHz with 64 KB of RAM plus the 64 KB expansion, the boot ROMs
// (SmartWriter and EOS) and OS7, a TMS9928A with 16 KB of VRAM, an SN76489A,
// and ADAMnet: the master 6801 on the main board (it reads and writes the
// Z80's RAM to run the devices' control blocks) talking over a 62.5 kbit/s
// serial line to the keyboard's and the printer's own 6801s.
// With no tape or disk, EOS starts SmartWriter.
//
// Frame: 272 x 208, the 256 x 192 picture in an 8-pixel border.
// Keys: keyEvent(code) with code = row * 8 + bit of MAME's Y0-Y12 matrix.

#pragma once
#include "../../core_api.h"
#include "../../common/m6801.h"
#include "../../common/tms9928a.h"
#include "../../common/sn76489.h"
#include "../../common/z80/z80.h"
#include <vector>

class AdamCore : public Core {
public:
    AdamCore();
    bool load(const std::vector<RomFile>& roms, std::string& error) override;
    void reset() override;
    void runFrame() override;
    const uint32_t* frameBuffer() const override { return m_frame.data(); }
    int frameWidth() const override { return W; }
    int frameHeight() const override { return H; }
    double refreshRate() const override { return 59.922743; }
    int sampleRate() const override { return 44100; }
    size_t readAudio(int16_t* out, size_t maxSamples) override;
    // hand controller 1: up, down, left, right, left fire, right fire; keypad 0-9, #, * in bits 8-19
    void setInputs(uint32_t pressed) override { m_pad = pressed; }
    void keyEvent(int code, bool down) override;
    // drive 2: a ColecoVision cartridge (the machine restarts in game mode); eject restarts the computer
    bool mediaInsert(int drive, const std::string& path, std::string& error) override;
    void mediaEject(int drive) override;
    bool saveState(std::vector<uint8_t>& out) override;
    bool loadState(const std::vector<uint8_t>& in) override;

    static constexpr int W = 272, H = 208;

    // ---- buses (public for the CPU callbacks) ----
    uint8_t memRead(uint16_t a);
    void memWrite(uint16_t a, uint8_t d);
    uint8_t ioRead(uint8_t a);
    void ioWrite(uint8_t a, uint8_t d);

private:
    struct Master : M6801::Ports {
        AdamCore* c;
        M6801::u8 portIn(int port) override;
        void portOut(int port, M6801::u8 data, M6801::u8 ddr) override;
        void os3(int state) override;
        int serialIn() override { return c->netLine(); }
    };
    struct Keyboard : M6801::Ports {
        AdamCore* c;
        M6801::u8 portIn(int port) override;
        void portOut(int port, M6801::u8 data, M6801::u8 ddr) override;
        int serialIn() override { return c->netLine(); }
    };
    struct Printer : M6801::Ports {
        AdamCore* c;
        M6801::u8 portIn(int port) override;
        void portOut(int port, M6801::u8 data, M6801::u8 ddr) override;
        int serialIn() override { return c->netLine(); }
    };
    int netLine() const { return m_s.txMaster & m_s.txKb & m_s.txPrn; }
    void netReset();
    void setTx(uint8_t& tx, int v);
    int64_t m_now = 0;          // time of the current action (E cycles)
    uint8_t padRead(int n);

    uint8_t m_os7[0x2000];
    std::vector<uint8_t> m_cart;
    void powerOn(bool game);
    uint8_t m_boot[0xa000];
    uint8_t m_romMaster[0x800], m_romKb[0x800], m_romPrn[0x800];
    Master m_masterPorts; Keyboard m_kbPorts; Printer m_prnPorts;
    M6801 m_master, m_kb, m_prn;
    z80 m_z80;
    Tms9928a m_vdp;
    Sn76489 m_psg;
    uint32_t m_pad = 0;
    std::vector<uint32_t> m_frame;
    std::vector<int16_t> m_audio;
    double m_audioAcc = 0; int m_audioN = 0; double m_audioPhase = 0;
    double m_dcX = 0, m_dcY = 0;
    double m_traceBase = 0;

    struct State {
        uint8_t ram[0x10000], xram[0x10000];
        uint8_t mioc, game, an, dma, bwr, dataIn, dataOut;
        uint16_t ba;
        uint8_t txMaster, txKb, txPrn;
        uint16_t keyY;              // keyboard 6801: selected rows (active low)
        uint8_t prnP1, prnSensors;
        uint8_t keys[13];           // key matrix, 1 = pressed
        uint8_t joyMode;            // 0 = keypad, 1 = joystick
        uint8_t vdpIrq;
        int32_t line;
        double tZ80;                // Z80 time in this frame (microseconds)
        double base;                // 6801 cycle count at the start of this frame
    } m_s;
};
