// PocketPlug - Interton VC 4000 family core (1292 Advanced Programmable Video System etc.)
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Port of MAME's VC 4000 driver (src/mame/interton/vc4000.cpp, vc4000_v.cpp,
// vc4000_a.cpp; copyright Peter Trauner, Manfred Schneider, Robbbert; GPL-2.0+)
// and its cartridge slot (src/devices/bus/vc4000, Fabio Priuli, BSD-3-Clause).

#pragma once
#include "../../core_api.h"
#include "../../common/s2650.h"
#include <cstdint>
#include <vector>

// Input bits (1 = pressed)
//  0..11  player 1 keypad: 1 2 3 4 5 6 7 8 9 CLEAR 0 ENTER
// 12..23  player 2 keypad, same order
// 24 START, 25 GAME SELECT
// The fire buttons on the controllers are wired to keypad "2".
namespace Vc4000Bits {
    enum { K1 = 0, K2, K3, K4, K5, K6, K7, K8, K9, KCLEAR, K0, KENTER };
    enum { P2 = 12, START = 24, SELECT = 25 };
}

class Vc4000Core : public Core {
public:
    Vc4000Core();

    bool load(const std::vector<RomFile>& roms, std::string& error) override;
    void reset() override;
    void runFrame() override;

    const uint32_t* frameBuffer() const override { return m_frame; }
    int frameWidth() const override { return VIS_W; }
    int frameHeight() const override { return VIS_H; }
    double refreshRate() const override { return 16536.0 / 312.0; }

    int sampleRate() const override { return 44100; }
    size_t readAudio(int16_t* out, size_t maxSamples) override;
    void setInputs(uint32_t pressed) override { m_inputs = pressed; }
    void setAnalog(int stick, float x, float y) override;

    bool saveState(std::vector<uint8_t>& out) override;
    bool loadState(const std::vector<uint8_t>& in) override;

    uint8_t read(uint16_t a);
    void write(uint16_t a, uint8_t d);
    int vsync() const { return m_line >= END_LINE ? 1 : 0; }

    static const int SCR_W = 226, SCR_H = 312;
    static const int VIS_X = 8, VIS_W = 177, VIS_H = 270;   // visarea 8..184 x 0..269
    static const int END_LINE = 269;

private:
    enum CartType { CART_STD, CART_ROM4K, CART_RAM1K, CART_CHESS2 };

    struct Sprite {
        int base;          // offset of its registers in m_reg
        int mask;
        int state, delay, size, y;
        uint8_t scolor;
        int finished, finished_now;
    };

    S2650 m_cpu;
    std::vector<uint8_t> m_cart;
    uint8_t m_cartRam[0x400];
    CartType m_type = CART_STD;

    uint8_t m_reg[0x100];
    Sprite m_sprites[4];
    int m_line = 0;
    uint8_t m_sprite_collision_flags = 0, m_background_collision_flags = 0;
    uint8_t m_objects[512];
    uint8_t m_irq_pause = 0;
    uint8_t m_bitmap[SCR_H][SCR_W];
    uint8_t m_sprite_collision_tab[0x20], m_background_collision_tab[0x20];

    double m_cycleAcc = 0;

    // sound
    uint8_t m_sndReg = 0;
    int m_sndSize = 0, m_sndPos = 0;
    double m_sampleAcc = 0;
    float m_hpIn = 0, m_hpOut = 0;
    std::vector<int16_t> m_audio;

    uint32_t m_inputs = 0;
    uint8_t m_joyX[2] = { 105, 105 }, m_joyY[2] = { 105, 105 };

    uint32_t m_frame[VIS_W * VIS_H];

    uint8_t key_r(int offset);
    uint8_t video_r(int offset);
    void video_w(int offset, uint8_t data);
    void video_line();
    void draw_grid(uint8_t* collision);
    void draw_digit(int x, int y, int d, int line);
    void collision_plot(uint8_t* collision, uint8_t data, uint8_t color, int scale);
    void sprite_update(uint8_t* collision, Sprite* s);
    void soundport_w(uint8_t data);
    void produceSamples();
};
