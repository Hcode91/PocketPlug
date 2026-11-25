// PocketPlug - AT&T Blit (Bell Labs) bitmap terminal
// SPDX-License-Identifier: BSD-3-Clause
//
// Port of MAME's att/blit.cpp (Sergey Svishchev, BSD-3), with the I/O map from 9front's
// games/blit notes: a 68000 at 8.25 MHz, 24 KB of ROM at 40000h (its first 8 bytes shadow
// 0-7 until the ROM is first read), 256 KB of RAM holding the 800 x 1024 bitmap (start
// address in 60018h, invert bit in 60020h), the host 6850 ACIA at 60009h (IRQ 5), the
// keyboard ACIA at 60031h (IRQ 2), the mouse at 60000h (y) / 60002h (x) with its buttons
// at 60011h (IRQ 4) and the vertical blank on IRQ 1 (acknowledged at 60038h).
//
// The keyboard is high-level: keyEvent(ascii, true) sends the character. The host port has
// a loopback plug by default (option 0 = 1): what the terminal sends comes back, so typing
// shows; 0 = nothing connected. setAnalog(0, x, y) puts the mouse at x, y (0-1 across the
// screen) and setInputs() bits 0-2 are its right, middle and left buttons.

#pragma once
#include "../../core_api.h"
#include <deque>
#include <string>
#include <vector>

class BlitCore : public Core {
public:
    BlitCore();
    ~BlitCore() override;
    bool load(const std::vector<RomFile>& roms, std::string& error) override;
    void reset() override;
    void runFrame() override;
    const uint32_t* frameBuffer() const override { return m_out.data(); }
    int frameWidth() const override { return 800; }
    int frameHeight() const override { return 1024; }
    double refreshRate() const override { return 60.0; }
    int sampleRate() const override { return 44100; }
    size_t readAudio(int16_t* out, size_t maxSamples) override;
    void setInputs(uint32_t pressed) override;
    void setAnalog(int stick, float x, float y) override;
    void keyEvent(int code, bool down) override;
    void setOption(int id, int value) override { if (id == 0) m_loop = value != 0; }
    bool saveState(std::vector<uint8_t>&) override { return false; }
    bool loadState(const std::vector<uint8_t>&) override { return false; }

    uint8_t read8(uint32_t a);
    uint16_t read16(uint32_t a);
    void write8(uint32_t a, uint8_t d);
    void write16(uint32_t a, uint16_t d);
    int intAck(int level);

private:
    struct Acia {
        uint8_t ctl = 0x03, status = 0x02, rx = 0;
        std::deque<uint8_t> in;
        int gap = 0;
        bool reset = true;
        bool irq() const { return !reset && (((ctl & 0x80) && (status & 0x01)) || ((ctl & 0x60) == 0x20 && (status & 0x02))); }
    };
    uint8_t aciaRead(Acia& c, bool data);
    void aciaWrite(int n, Acia& c, bool data, uint8_t d);
    void serviceSerial(int cycles);
    void updateIrq();
    void render();
    void bindBus();

    bool m_loop = true;
    bool m_shadow = true;
    std::vector<uint8_t> m_rom;      // 0x6000
    std::vector<uint8_t> m_ram;      // 0x40000
    Acia m_acia[2];                  // 0 host, 1 keyboard
    uint16_t m_start = 0, m_misc = 0;
    bool m_vblank = false, m_mouseIrq = false;
    uint16_t m_mouseX = 400, m_mouseY = 512;
    uint8_t m_buttons = 0;
    int m_bell = 0;
    std::vector<uint32_t> m_out;
    std::vector<int16_t> m_audio;
};
