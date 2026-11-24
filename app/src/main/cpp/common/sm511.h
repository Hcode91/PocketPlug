// PocketPlug - Sharp SM511 4-bit LCD game MCU (4K ROM, 128x4 RAM, melody)
// SPDX-License-Identifier: BSD-3-Clause
//
// Port of MAME's sm510 family (src/devices/cpu/sm510: sm510base, sm510op,
// sm511; copyright-holders hap). The divider runs on the 32.768 kHz crystal
// and an instruction cycle takes 2 or 4 crystal clocks (CLKHI / CLKLO, 4 after
// reset). run() counts crystal clocks.
//
// The LCD is two 16x4 matrices in RAM (a at 0x60, b at 0x70) plus the BS
// lines; lcd() is called at 1024 Hz with them for the host's deflicker.

#pragma once
#include <cstdint>
#include <cstring>

class SM511 {
public:
    struct Io {
        virtual ~Io() = default;
        virtual uint8_t readK() = 0;
        virtual int readBA() { return 1; }
        virtual int readB() { return 1; }
        virtual void writeS(uint8_t) {}
        virtual void writeR(uint8_t) {}
        // rows[group][common]: group 0 = a, 1 = b, 2 = bs; 16 segment bits
        virtual void lcd(const uint16_t rows[3][4]) = 0;
        virtual void tick1k() {}
    };

    struct State {
        uint16_t pc, prevPc, op, prevOp, div, stack[2];
        uint8_t param, acc, bl, bm, bmask, c, skip, w, r, rOut, gamma, extWakeup;
        uint8_t l, x, y, bp, bc, halt, clkDiv;
        uint8_t melodyRd, melodyStep, melodyDuty, melodyDutyIdx, melodyAddr;
        uint8_t ram[0x80];
        uint8_t lcdPhase;
        int32_t carry;
    } s;

    explicit SM511(Io* io) : m_io(io) { std::memset(&s, 0, sizeof(s)); }
    void setRom(const uint8_t* rom, int size);
    void setMelody(const uint8_t* rom, int size);
    void reset();
    void setWakeup(bool on) { s.extWakeup = on ? 1 : 0; }
    // run for about n crystal clocks (32768 per second); returns clocks run
    int run(int n);

private:
    Io* m_io;
    uint8_t m_rom[0x1000] = {};
    uint8_t m_melody[0x100] = {};
    bool m_hasMelody = false;

    uint8_t ramR() const { return s.ram[(s.bmask | s.bm << 4 | s.bl) & 0x7f] & 0xf; }
    void ramW(uint8_t d) { s.ram[(s.bmask | s.bm << 4 | s.bl) & 0x7f] = d & 0xf; }
    void incPc() { int feed = ((s.pc >> 1 ^ s.pc) & 1) ? 0 : 0x20; s.pc = uint16_t(feed | (s.pc >> 1 & 0x1f) | (s.pc & ~0x3f)); }
    void branch(uint8_t pu, uint8_t pm, uint8_t pl) { s.pc = uint16_t(((pu << 10) | (pm << 6 & 0x3c0) | (pl & 0x3f)) & 0xfff); }
    void push() { s.stack[1] = s.stack[0]; s.stack[0] = s.pc; }
    void pop() { s.pc = s.stack[0] & 0xfff; s.stack[0] = s.stack[1]; }
    void exc() { uint8_t a = s.acc; s.acc = ramR(); ramW(a); s.bm ^= (s.op & 3); }
    void incb() { s.bl = (s.bl + 1) & 0xf; s.skip = s.bl == 0; }
    void decb() { s.bl = (s.bl - 1) & 0xf; s.skip = s.bl == 0xf; }
    void clockMelody();
    void tick(int clocks);
    void execute();
};
