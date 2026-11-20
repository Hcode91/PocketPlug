// PocketPlug - MOS / Rockwell 6522 VIA
// SPDX-License-Identifier: BSD-3-Clause
//
// Compact model following MAME's 6522via.cpp: ports A/B with DDRs and input
// latching, CA1/CB1 edge interrupts, CA2/CB2 input, handshake, pulse and
// manual output modes, timer 1 (one-shot / free-running, PB7 not driven),
// timer 2 (one-shot), IFR / IER. The shift register is not emulated.
// tick(n) advances n phi2 cycles.

#pragma once
#include <cstdint>
#include <functional>

class Via6522 {
public:
    std::function<uint8_t()> inA, inB;
    std::function<void(uint8_t)> outA, outB;
    std::function<void(int)> ca2Out, cb2Out;
    std::function<void(bool)> irqOut;

    struct State {
        uint8_t ora, orb, ddra, ddrb, ilA, ilB;
        uint8_t t1ll, t1lh, t2ll, acr, pcr, ifr, ier, sr;
        int32_t t1, t2;                 // counters (run below zero to time the underflow)
        uint8_t t1armed, t2armed, ca1, cb1, ca2o, cb2o, irq;
    } s{};

    void reset()
    {
        s = State();
        s.t1 = s.t2 = 0xffff;
        s.ca1 = s.cb1 = 1;
        s.ca2o = s.cb2o = 1;
        update();
    }

    void tick(int n)
    {
        while (n-- > 0) {
            if (--s.t1 < -1) {
                // underflow (N + 1.5 cycles after loading)
                if (s.t1armed) { setInt(0x40); if (!(s.acr & 0x40)) s.t1armed = 0; }
                s.t1 = (s.acr & 0x40) ? (s.t1ll | (s.t1lh << 8)) : 0xfffe;
            }
            if (!(s.acr & 0x20)) {
                if (--s.t2 < -1) {
                    if (s.t2armed) { setInt(0x20); s.t2armed = 0; }
                    s.t2 = 0xfffe;
                }
            }
        }
    }

    void setCa1(int v)
    {
        v = v ? 1 : 0;
        if (v == s.ca1) return;
        s.ca1 = uint8_t(v);
        bool pos = s.pcr & 0x01;
        if (pos ? v : !v) {
            if (s.acr & 0x01) s.ilA = inPa();
            setInt(0x02);
            // handshake output on CA2 goes back high
            if (((s.pcr >> 1) & 7) == 4 && !s.ca2o) { s.ca2o = 1; if (ca2Out) ca2Out(1); }
        }
    }
    void setCb1(int v)
    {
        v = v ? 1 : 0;
        if (v == s.cb1) return;
        s.cb1 = uint8_t(v);
        bool pos = s.pcr & 0x10;
        if (pos ? v : !v) {
            if (s.acr & 0x02) s.ilB = inPb();
            setInt(0x10);
            if (((s.pcr >> 5) & 7) == 4 && !s.cb2o) { s.cb2o = 1; if (cb2Out) cb2Out(1); }
        }
    }

    uint8_t read(int r)
    {
        switch (r & 15) {
        case 0x0: {
            uint8_t v = (s.acr & 0x02) ? s.ilB : inPb();
            clrPb();
            return v;
        }
        case 0x1: {
            uint8_t v = (s.acr & 0x01) ? s.ilA : inPa();
            clrPa();
            int m = (s.pcr >> 1) & 7;
            if (m == 4 || m == 5) { s.ca2o = 0; if (ca2Out) ca2Out(0); if (m == 5) { s.ca2o = 1; if (ca2Out) ca2Out(1); } }
            return v;
        }
        case 0x2: return s.ddrb;
        case 0x3: return s.ddra;
        case 0x4: clrInt(0x40); return uint8_t(c1());
        case 0x5: return uint8_t(c1() >> 8);
        case 0x6: return s.t1ll;
        case 0x7: return s.t1lh;
        case 0x8: clrInt(0x20); return uint8_t(c2());
        case 0x9: return uint8_t(c2() >> 8);
        case 0xa: clrInt(0x04); return s.sr;
        case 0xb: return s.acr;
        case 0xc: return s.pcr;
        case 0xd: return s.ifr;
        case 0xe: return uint8_t(s.ier | 0x80);
        default: return (s.acr & 0x01) ? s.ilA : inPa();
        }
    }

    void write(int r, uint8_t d)
    {
        switch (r & 15) {
        case 0x0:
            s.orb = d; outPb(); clrPb();
            if (((s.pcr >> 5) & 7) == 4 || ((s.pcr >> 5) & 7) == 5) {
                s.cb2o = 0; if (cb2Out) cb2Out(0);
                if (((s.pcr >> 5) & 7) == 5) { s.cb2o = 1; if (cb2Out) cb2Out(1); }
            }
            break;
        case 0x1: case 0xf:
            s.ora = d; outPa();
            if ((r & 15) == 1) {
                clrPa();
                int m = (s.pcr >> 1) & 7;
                if (m == 4 || m == 5) { s.ca2o = 0; if (ca2Out) ca2Out(0); if (m == 5) { s.ca2o = 1; if (ca2Out) ca2Out(1); } }
            }
            break;
        case 0x2: if (s.ddrb != d) { s.ddrb = d; outPb(); } break;
        case 0x3: if (s.ddra != d) { s.ddra = d; outPa(); } break;
        case 0x4: case 0x6: s.t1ll = d; break;
        case 0x5:
            s.t1lh = d;
            clrInt(0x40);
            s.t1 = s.t1ll | (s.t1lh << 8);
            s.t1armed = 1;
            break;
        case 0x7: s.t1lh = d; clrInt(0x40); break;
        case 0x8: s.t2ll = d; break;
        case 0x9:
            clrInt(0x20);
            s.t2 = s.t2ll | (d << 8);
            s.t2armed = 1;
            break;
        case 0xa: s.sr = d; clrInt(0x04); break;
        case 0xb: s.acr = d; break;
        case 0xc: {
            s.pcr = d;
            // manual output modes drive CA2 / CB2 straight away
            int ma = (d >> 1) & 7, mb = (d >> 5) & 7;
            if (ma >= 6 && s.ca2o != (ma & 1)) { s.ca2o = uint8_t(ma & 1); if (ca2Out) ca2Out(s.ca2o); }
            if (mb >= 6 && s.cb2o != (mb & 1)) { s.cb2o = uint8_t(mb & 1); if (cb2Out) cb2Out(s.cb2o); }
            break;
        }
        case 0xd: s.ifr &= uint8_t(~(d & 0x7f)); update(); break;
        case 0xe:
            if (d & 0x80) s.ier |= (d & 0x7f); else s.ier &= uint8_t(~(d & 0x7f));
            update();
            break;
        }
    }

private:
    uint8_t inPa() { uint8_t i = inA ? inA() : 0xff; return uint8_t((i & ~s.ddra) | (s.ora & s.ddra)); }
    uint8_t inPb() { uint8_t i = inB ? inB() : 0xff; return uint8_t((i & ~s.ddrb) | (s.orb & s.ddrb)); }
    void outPa() { if (outA) outA(uint8_t((s.ora & s.ddra) | ~s.ddra)); }
    void outPb() { if (outB) outB(uint8_t((s.orb & s.ddrb) | ~s.ddrb)); }
    int c1() const { return s.t1 < 0 ? 0xffff : s.t1; }
    int c2() const { return s.t2 < 0 ? 0xffff : s.t2; }
    void clrPa() { clrInt(0x02); if ((((s.pcr >> 1) & 7) & 5) != 1) clrInt(0x01); }   // CA2 flag unless "independent"
    void clrPb() { clrInt(0x10); if ((((s.pcr >> 5) & 7) & 5) != 1) clrInt(0x08); }
    void setInt(uint8_t b) { s.ifr |= b; update(); }
    void clrInt(uint8_t b) { s.ifr &= uint8_t(~b); update(); }
    void update()
    {
        if (s.ifr & s.ier & 0x7f) s.ifr |= 0x80; else s.ifr &= 0x7f;
        uint8_t irq = (s.ifr & 0x80) ? 1 : 0;
        if (irq != s.irq) { s.irq = irq; if (irqOut) irqOut(irq); }
    }
};
