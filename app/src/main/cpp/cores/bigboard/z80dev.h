// PocketPlug - Z80 family support chips (PIO, CTC, SIO) for the CP/M boards
// SPDX-License-Identifier: BSD-3-Clause
//
// Small models of the Z80 PIO, CTC and SIO written from the Zilog data sheets
// and MAME's z80pio / z80ctc / z80sio behaviour (Curt Coder, BSD-3): enough for
// the Big Board family's monitors and CP/M BIOSes. Each chip takes part in the
// IM 2 daisy chain through pending() / ack() / reti().

#pragma once
#include <cstdint>
#include <functional>

struct Z80DaisyDev {
    virtual ~Z80DaisyDev() {}
    // the highest-priority interrupt this chip has waiting (-1 = none); its vector
    virtual int pending() = 0;
    virtual void ack() = 0;        // the CPU took it: it moves in service
    virtual bool reti() = 0;       // RETI: clears the in-service one (true if it had one)
    virtual bool inService() = 0;
};

class Z80Pio : public Z80DaisyDev {
public:
    struct Port {
        uint8_t mode = 1, vector = 0, ioDir = 0xff, mask = 0xff, icw = 0;
        bool ie = false, expectIo = false, expectMask = false;
        uint8_t out = 0, in = 0;    // output register, input latch
        bool rdy = false, ip = false, ius = false;
        bool matched = false;
    } p[2];
    std::function<uint8_t(int)> readPort;            // the pins
    std::function<void(int, uint8_t)> writePort;

    void reset() { for (auto& q : p) q = Port(); }
    uint8_t read(int port, bool control) {
        if (control) return 0xff;
        Port& q = p[port];
        switch (q.mode) {
        case 0: return q.out;
        case 1: { uint8_t v = q.in; q.rdy = true; return v; }   // input: reading frees the latch
        case 2: { uint8_t v = q.in; q.rdy = true; return v; }
        default: {
            uint8_t pins = readPort ? readPort(port) : 0xff;
            return uint8_t((pins & q.ioDir) | (q.out & ~q.ioDir));
        }
        }
    }
    void write(int port, bool control, uint8_t v) {
        Port& q = p[port];
        if (!control) {
            q.out = v;
            if (q.mode == 0 || q.mode == 2) { if (writePort) writePort(port, v); q.rdy = true; }
            else if (q.mode == 3 && writePort) writePort(port, uint8_t(v & ~q.ioDir));
            return;
        }
        if (q.expectIo) { q.ioDir = v; q.expectIo = false; if (writePort) writePort(port, uint8_t(q.out & ~q.ioDir)); return; }
        if (q.expectMask) { q.mask = v; q.expectMask = false; return; }
        if (!(v & 1)) { q.vector = v; return; }
        switch (v & 0x0f) {
        case 0x0f:
            q.mode = uint8_t(v >> 6);
            if (q.mode == 3) q.expectIo = true;
            if (q.mode == 1) q.rdy = true;
            if (q.mode == 0 && writePort) writePort(port, q.out);
            break;
        case 0x07:
            q.icw = v;
            q.ie = v & 0x80;
            if (v & 0x10) { q.expectMask = true; q.ip = false; }
            break;
        case 0x03:
            q.ie = v & 0x80;
            break;
        }
    }
    // strobe from the device (input mode: data latched; output mode: taken)
    void strobe(int port, uint8_t data) {
        Port& q = p[port];
        if (q.mode == 1 || q.mode == 2) { q.in = data; q.rdy = false; }
        if (q.ie && q.mode != 3) q.ip = true;
    }
    // mode 3: the bit-control interrupt (call when the input pins change)
    void pinsChanged(int port) {
        Port& q = p[port];
        if (q.mode != 3 || !q.ie || !readPort) return;
        uint8_t pins = readPort(port) & q.ioDir & ~q.mask;
        uint8_t want = (q.icw & 0x20) ? pins : uint8_t(~pins & q.ioDir & ~q.mask);
        bool m = (q.icw & 0x40) ? (want == uint8_t(q.ioDir & ~q.mask)) : (want != 0);
        if (m && !q.matched) q.ip = true;
        q.matched = m;
    }
    bool rdy(int port) const { return p[port].rdy; }

    int pending() override {
        for (int i = 0; i < 2; i++) { if (p[i].ius) return -1; if (p[i].ip && p[i].ie) return p[i].vector; }
        return -1;
    }
    void ack() override { for (auto& q : p) if (q.ip && q.ie) { q.ip = false; q.ius = true; return; } }
    bool reti() override { for (auto& q : p) if (q.ius) { q.ius = false; return true; } return false; }
    bool inService() override { return p[0].ius || p[1].ius; }
};

class Z80Ctc : public Z80DaisyDev {
public:
    struct Chan {
        uint8_t ctl = 0x03, tc = 0;     // reset state: stopped
        int count = 256;
        int pre = 0;
        bool expectTc = false, running = false, waitTrg = false, ip = false, ius = false;
    } c[4];
    uint8_t vector = 0;
    std::function<void(int)> onZero;    // ZC/TO pulse of channels 0-2

    void reset() { for (auto& q : c) q = Chan(); }
    uint8_t read(int ch) { return uint8_t(c[ch].count & 0xff); }
    void write(int ch, uint8_t v) {
        Chan& q = c[ch];
        if (q.expectTc) {
            q.tc = v; q.expectTc = false;
            q.count = v ? v : 256;
            if (!q.running) { q.running = true; q.pre = 0; q.waitTrg = !(q.ctl & 0x40) && (q.ctl & 0x08); }
            return;
        }
        if (v & 1) {
            q.ctl = v;
            if (v & 0x04) q.expectTc = true;
            if (v & 0x02) { q.running = false; q.expectTc = (v & 0x04) != 0; }
            if (!(v & 0x80)) q.ip = false;
        } else if (ch == 0) vector = uint8_t(v & 0xf8);
    }
    // n system clocks pass
    void clock(int n) {
        for (int ch = 0; ch < 4; ch++) {
            Chan& q = c[ch];
            if (!q.running || (q.ctl & 0x40) || q.waitTrg) continue;
            int div = (q.ctl & 0x20) ? 256 : 16;
            q.pre += n;
            while (q.pre >= div) { q.pre -= div; tick(ch); }
        }
    }
    // a trigger edge (counter mode counts it)
    void trigger(int ch) {
        Chan& q = c[ch];
        if (!q.running) return;
        if (q.waitTrg) { q.waitTrg = false; return; }
        if (q.ctl & 0x40) tick(ch);
    }
    int pending() override {
        for (int i = 0; i < 4; i++) { if (c[i].ius) return -1; if (c[i].ip) return vector | (i << 1); }
        return -1;
    }
    void ack() override { for (auto& q : c) if (q.ip) { q.ip = false; q.ius = true; return; } }
    bool reti() override { for (auto& q : c) if (q.ius) { q.ius = false; return true; } return false; }
    bool inService() override { for (auto& q : c) if (q.ius) return true; return false; }
private:
    void tick(int ch) {
        Chan& q = c[ch];
        if (--q.count > 0) return;
        q.count = q.tc ? q.tc : 256;
        if (q.ctl & 0x80) q.ip = true;
        if (ch < 3 && onZero) onZero(ch);
    }
};

// The SIO, as far as a machine with nothing on its serial ports needs: the
// registers, an always-empty transmitter and no receiver.
class Z80Sio : public Z80DaisyDev {
public:
    struct Chan { uint8_t wr[8] = {}; int ptr = 0; } ch[2];
    void reset() { ch[0] = Chan(); ch[1] = Chan(); }
    uint8_t readData(int c) { (void)c; return 0; }
    void writeData(int c, uint8_t v) { (void)c; (void)v; }
    uint8_t readCtl(int c) {
        Chan& q = ch[c];
        int r = q.ptr; q.ptr = 0;
        switch (r) {
        case 0: return 0x2c;          // TX empty, DCD, CTS; nothing received
        case 1: return 0x01;          // all sent
        case 2: return c ? ch[1].wr[2] : 0;
        default: return 0;
        }
    }
    void writeCtl(int c, uint8_t v) {
        Chan& q = ch[c];
        if (q.ptr == 0) {
            q.ptr = v & 7;
            if (((v >> 3) & 7) == 3) { q = Chan(); }    // channel reset
            q.wr[0] = v;
            return;
        }
        q.wr[q.ptr] = v; q.ptr = 0;
    }
    int pending() override { return -1; }
    void ack() override {}
    bool reti() override { return false; }
    bool inService() override { return false; }
};
