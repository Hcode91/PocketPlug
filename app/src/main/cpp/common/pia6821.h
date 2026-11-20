// PocketPlug - Motorola MC6821 PIA
// SPDX-License-Identifier: BSD-3-Clause
//
// Port of MAME's pia6821_device (src/devices/machine/6821pia.cpp, 0.264;
// copyright-holders Aaron Giles, Nathan Woods). The machine supplies the
// port and line handlers through Host; anything it leaves out behaves as in
// MAME with that handler unset (port A pulled up, port B read as 0).

#pragma once
#include <cstdint>

class PIA6821 {
public:
    struct Host {
        virtual ~Host() = default;
        virtual bool hasInA() { return false; }
        virtual uint8_t inA() { return 0xff; }
        virtual bool hasInB() { return false; }
        virtual uint8_t inB() { return 0; }
        virtual void outA(uint8_t) {}
        virtual void outB(uint8_t, uint8_t /*ddr*/) {}
        virtual void ca2(int) {}
        virtual void cb2(int) {}
        virtual void irqA(int) {}
        virtual void irqB(int) {}
    };

    struct State {
        uint8_t inA = 0xff, inCa1 = 1, inCa2 = 1, outA = 0, outCa2 = 0, ddrA = 0, ctlA = 0, irqA1 = 0, irqA2 = 0, irqAState = 0;
        uint8_t inB = 0, inCb1 = 0, inCb2 = 0, outB = 0, outCb2 = 0, lastOutCb2Z = 0, ddrB = 0, ctlB = 0, irqB1 = 0, irqB2 = 0, irqBState = 0;
    } s;

    explicit PIA6821(Host* h) : m_h(h) {}

    void reset()
    {
        s.outA = 0; s.outCa2 = 0; s.ddrA = 0; s.ctlA = 0; s.irqA1 = s.irqA2 = 0; s.irqAState = 0;
        s.outB = 0; s.outCb2 = 0; s.lastOutCb2Z = 0; s.ddrB = 0; s.ctlB = 0; s.irqB1 = s.irqB2 = 0; s.irqBState = 0;
        m_h->irqA(0);
        m_h->irqB(0);
        m_h->outA(0xff);
        m_h->ca2(1);
    }

    uint8_t read(int offset)
    {
        switch (offset & 3) {
        default:
        case 0: return outputSelected(s.ctlA) ? portAR() : s.ddrA;
        case 1: {
            uint8_t r = s.ctlA;
            if (s.irqA1) r |= 0x80;
            if (s.irqA2 && c2Input(s.ctlA)) r |= 0x40;
            return r;
        }
        case 2: return outputSelected(s.ctlB) ? portBR() : s.ddrB;
        case 3: {
            uint8_t r = s.ctlB;
            if (s.irqB1) r |= 0x80;
            if (s.irqB2 && c2Input(s.ctlB)) r |= 0x40;
            return r;
        }
        }
    }

    void write(int offset, uint8_t data)
    {
        switch (offset & 3) {
        default:
        case 0:
            if (outputSelected(s.ctlA)) { s.outA = data; m_h->outA(outAValue()); }
            else if (s.ddrA != data) { s.ddrA = data; m_h->outA(outAValue()); }
            break;
        case 1: controlAW(data); break;
        case 2:
            if (outputSelected(s.ctlB)) {
                s.outB = data;
                m_h->outB(uint8_t(s.outB & s.ddrB), s.ddrB);
                if (c2StrobeMode(s.ctlB)) {
                    setOutCb2(0);
                    if (strobeEReset(s.ctlB)) setOutCb2(1);
                }
            } else if (s.ddrB != data) { s.ddrB = data; m_h->outB(uint8_t(s.outB & s.ddrB), s.ddrB); }
            break;
        case 3: controlBW(data); break;
        }
    }

    void ca1W(int state)
    {
        if (s.inCa1 != state && ((state && c1LowToHigh(s.ctlA)) || (!state && !c1LowToHigh(s.ctlA)))) {
            s.irqA1 = 1;
            updateInterrupts();
            if (c2Output(s.ctlA) && c2StrobeMode(s.ctlA) && !strobeEReset(s.ctlA) && !s.outCa2) setOutCa2(1);
        }
        s.inCa1 = uint8_t(state);
    }

    void cb1W(int state)
    {
        if (s.inCb1 != state && ((state && c1LowToHigh(s.ctlB)) || (!state && !c1LowToHigh(s.ctlB)))) {
            s.irqB1 = 1;
            updateInterrupts();
        }
        s.inCb1 = uint8_t(state);
    }

    int cb2Output() const { return s.outCb2; }

private:
    Host* m_h;

    static bool irq1Enabled(uint8_t c) { return c & 0x01; }
    static bool c1LowToHigh(uint8_t c) { return c & 0x02; }
    static bool outputSelected(uint8_t c) { return c & 0x04; }
    static bool irq2Enabled(uint8_t c) { return c & 0x08; }
    static bool strobeEReset(uint8_t c) { return c & 0x08; }
    static bool c2Set(uint8_t c) { return c & 0x08; }
    static bool c2SetMode(uint8_t c) { return c & 0x10; }
    static bool c2StrobeMode(uint8_t c) { return !(c & 0x10); }
    static bool c2Output(uint8_t c) { return c & 0x20; }
    static bool c2Input(uint8_t c) { return !(c & 0x20); }

    void updateInterrupts()
    {
        int n = (s.irqA1 && irq1Enabled(s.ctlA)) || (s.irqA2 && irq2Enabled(s.ctlA));
        if (n != s.irqAState) { s.irqAState = uint8_t(n); m_h->irqA(n); }
        n = (s.irqB1 && irq1Enabled(s.ctlB)) || (s.irqB2 && irq2Enabled(s.ctlB));
        if (n != s.irqBState) { s.irqBState = uint8_t(n); m_h->irqB(n); }
    }

    uint8_t inAValue()
    {
        uint8_t d = m_h->hasInA() ? m_h->inA() : 0xff;
        return uint8_t((~s.ddrA & d) | (s.ddrA & s.outA));
    }
    uint8_t outAValue()
    {
        if (s.ddrA == 0xff) return s.outA;
        return uint8_t((s.outA & s.ddrA) | (inAValue() & ~s.ddrA));
    }
    uint8_t inBValue()
    {
        if (s.ddrB == 0xff) return s.outB;
        uint8_t d = m_h->hasInB() ? m_h->inB() : 0;
        return uint8_t((s.outB & s.ddrB) | (d & ~s.ddrB));
    }

    void setOutCa2(int d) { s.outCa2 = uint8_t(d); m_h->ca2(d); }
    void setOutCb2(int d)
    {
        int z = !c2Output(s.ctlB);
        if (d != s.outCb2 || z != s.lastOutCb2Z) {
            s.outCb2 = uint8_t(d);
            s.lastOutCb2Z = uint8_t(z);
            m_h->cb2(d);
        }
    }

    uint8_t portAR()
    {
        uint8_t r = inAValue();
        s.irqA1 = s.irqA2 = 0;
        updateInterrupts();
        if (c2Output(s.ctlA) && c2StrobeMode(s.ctlA)) {
            if (s.outCa2) setOutCa2(0);
            if (strobeEReset(s.ctlA)) setOutCa2(1);
        }
        return r;
    }

    uint8_t portBR()
    {
        uint8_t r = inBValue();
        if (s.irqB1 && c2StrobeMode(s.ctlB) && !strobeEReset(s.ctlB)) setOutCb2(1);
        s.irqB1 = s.irqB2 = 0;
        updateInterrupts();
        return r;
    }

    void controlAW(uint8_t data)
    {
        data &= 0x3f;
        bool wasOutput = c2Output(s.ctlA);
        s.ctlA = data;
        if (c2Output(s.ctlA)) {
            if (c2SetMode(s.ctlA)) {
                int set = c2Set(s.ctlA);
                if (!wasOutput || s.outCa2 != set) setOutCa2(set);
            } else if (!wasOutput || !s.outCa2) setOutCa2(1);
        } else if (wasOutput) m_h->ca2(1);
        updateInterrupts();
    }

    void controlBW(uint8_t data)
    {
        data &= 0x3f;
        s.ctlB = data;
        setOutCb2(c2SetMode(s.ctlB) ? c2Set(s.ctlB) : 1);
        updateInterrupts();
    }
};
