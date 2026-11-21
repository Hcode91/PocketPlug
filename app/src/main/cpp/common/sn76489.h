// PocketPlug - Texas Instruments SN76489A sound chip
// SPDX-License-Identifier: BSD-3-Clause
//
// Port of MAME's sn76496.cpp (Nicola Salmoria et al.) for the SN76489A:
// three square-wave tones and a noise channel (16-bit LFSR, taps 0x04 / 0x08),
// clock divided by 8, 2 dB volume steps. clock() is called once per input clock.

#pragma once
#include <cstdint>
#include <cstring>

class Sn76489 {
public:
    struct State {
        int32_t reg[8], lastReg, volume[4], count[4], period[4], output[4];
        uint32_t rng;
        int32_t divider;
    } s;

    Sn76489()
    {
        double out = 0x7fff / 4.0;
        for (int i = 0; i < 15; i++) { m_volTable[i] = int(out); out /= 1.258925412; }
        m_volTable[15] = 0;
        reset();
    }
    // Sega's variant (SMS / Game Gear): 15-bit shift, taps 0x01 / 0x08 (MAME's segapsg)
    void setSega() { m_feedback = 0x8000; m_tap1 = 0x01; m_tap2 = 0x08; reset(); }
    void reset()
    {
        std::memset(&s, 0, sizeof(s));
        for (int i = 0; i < 4; i += 2) { s.reg[i] = 0; s.reg[i + 1] = 0x0f; }
        s.reg[4] = 0; s.reg[5] = 0x0f; s.reg[6] = 0; s.reg[7] = 0x0f;
        for (int i = 0; i < 4; i++) { s.volume[i] = 0; s.period[i] = (i == 3) ? 0 : 0x400; }
        s.rng = m_feedback;
        s.output[3] = s.rng & 1;
    }
    void write(uint8_t data)
    {
        int r;
        if (data & 0x80) {
            r = (data & 0x70) >> 4;
            s.lastReg = r;
            s.reg[r] = (s.reg[r] & 0x3f0) | (data & 0x0f);
        } else r = s.lastReg;
        int c = r >> 1;
        switch (r) {
        case 0: case 2: case 4:
            if (!(data & 0x80)) s.reg[r] = (s.reg[r] & 0x0f) | ((data & 0x3f) << 4);
            s.period[c] = s.reg[r] ? s.reg[r] : 0x400;
            if (r == 4 && (s.reg[6] & 3) == 3) s.period[3] = s.period[2] << 1;
            break;
        case 1: case 3: case 5: case 7:
            s.volume[c] = m_volTable[data & 0x0f];
            if (!(data & 0x80)) s.reg[r] = (s.reg[r] & 0x3f0) | (data & 0x0f);
            break;
        case 6: {
            if (!(data & 0x80)) s.reg[r] = (s.reg[r] & 0x3f0) | (data & 0x0f);
            int n = s.reg[6];
            s.period[3] = ((n & 3) == 3) ? (s.period[2] << 1) : (1 << (5 + (n & 3)));
            s.rng = m_feedback;
            break;
        }
        }
    }
    // one input clock; returns the current output (0 .. 0x7fff)
    int clock()
    {
        if (s.divider > 0) s.divider--;
        else {
            s.divider = 7;
            for (int i = 0; i < 3; i++)
                if (--s.count[i] <= 0) { s.output[i] ^= 1; s.count[i] = s.period[i]; }
            if (--s.count[3] <= 0) {
                bool fb = ((s.rng & m_tap1) != 0) != (((s.rng & m_tap2) != 0) && (s.reg[6] & 4));
                s.rng >>= 1;
                if (fb) s.rng |= m_feedback;
                s.output[3] = s.rng & 1;
                s.count[3] = s.period[3];
            }
        }
        return (s.output[0] ? s.volume[0] : 0) + (s.output[1] ? s.volume[1] : 0) +
               (s.output[2] ? s.volume[2] : 0) + (s.output[3] ? s.volume[3] : 0);
    }

private:
    uint32_t m_feedback = 0x10000, m_tap1 = 0x04, m_tap2 = 0x08;
    int m_volTable[16];
};
