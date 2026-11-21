// PocketPlug - Intel 8255 PPI, mode 0 only
// SPDX-License-Identifier: BSD-3-Clause
//
// Mode 0 of MAME's i8255_device (src/devices/machine/i8255.cpp; copyright-holders
// Curt Coder, AJR): the mode set and port C bit set/reset, output latches read
// back on output ports, the "tri-state" value sent while a port is an input.

#pragma once
#include <cstdint>
#include <functional>

class I8255 {
public:
    std::function<uint8_t()> inA = [] { return uint8_t(0); }, inB = [] { return uint8_t(0); }, inC = [] { return uint8_t(0); };
    std::function<void(uint8_t)> outA = [](uint8_t) {}, outB = [](uint8_t) {}, outC = [](uint8_t) {};
    uint8_t triA = 0xff, triB = 0xff, triC = 0xff;

    struct State { uint8_t control, out[3]; } s{};

    void reset() { setMode(0x9b); }

    uint8_t read(int offset)
    {
        switch (offset & 3) {
        case 0: return (s.control & 0x10) ? inA() : s.out[0];
        case 1: return (s.control & 0x02) ? inB() : s.out[1];
        case 2: {
            uint8_t data = 0, mask = 0;
            if (s.control & 0x08) mask |= 0xf0; else data |= s.out[2] & 0xf0;
            if (s.control & 0x01) mask |= 0x0f; else data |= s.out[2] & 0x0f;
            if (mask) data |= inC() & mask;
            return data;
        }
        default: return s.control;
        }
    }

    void write(int offset, uint8_t data)
    {
        switch (offset & 3) {
        case 0: if (!(s.control & 0x10)) { s.out[0] = data; outA(data); } break;
        case 1: if (!(s.control & 0x02)) { s.out[1] = data; outB(data); } break;
        case 2: s.out[2] = data; outputC(); break;
        default:
            if (data & 0x80) setMode(data);
            else {
                int bit = data >> 1 & 7;
                s.out[2] = uint8_t((s.out[2] & ~(1 << bit)) | ((data & 1) << bit));
                outputC();
            }
            break;
        }
    }

private:
    void setMode(uint8_t data)
    {
        s.control = data;
        s.out[0] = s.out[1] = s.out[2] = 0;
        outA((s.control & 0x10) ? triA : s.out[0]);
        outB((s.control & 0x02) ? triB : s.out[1]);
        outputC();
    }
    void outputC()
    {
        uint8_t data = 0, mask = 0;
        if (s.control & 0x08) data |= triC & 0xf0; else mask |= 0xf0;
        if (s.control & 0x01) data |= triC & 0x0f; else mask |= 0x0f;
        data |= s.out[2] & mask;
        outC(data);
    }
};
