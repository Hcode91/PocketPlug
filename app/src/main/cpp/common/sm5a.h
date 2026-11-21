// PocketPlug - Sharp SM5A (and the Soviet KB1013VK1-2) 4-bit LCD game MCU
// SPDX-License-Identifier: BSD-3-Clause
//
// Port of MAME's sm510 family (src/devices/cpu/sm510: sm510base, sm510op,
// sm500, sm500op, sm5a; copyright-holders hap): 1.8K ROM, 5x13x4 RAM, the
// LCD shift registers W / W', a 15-bit divider clocked at 32.768 kHz and
// 2 clocks per machine cycle.
//
// run() executes instructions and steps the divider with them; the host gets
// lcd() every 32 divider ticks (1024 Hz) with the 9 O groups for both commons,
// and r() whenever the R output changes (inverted, as MAME's write_r).

#pragma once
#include <cstdint>
#include <cstring>

class SM5A {
public:
    struct Io {
        virtual ~Io() = default;
        virtual uint8_t readK() = 0;
        virtual int readBA() { return 1; }
        virtual int readB() { return 1; }
        virtual void writeR(uint8_t) {}
        // segs[o][h]: O group o (0-8), common h (0-1), 4 segment bits
        virtual void lcd(const uint8_t segs[9][2]) = 0;
        // once per 1024 Hz tick, after lcd()
        virtual void tick1k() {}
    };

    struct State {
        uint16_t pc, prevPc, op, prevOp, stack0, div;
        uint8_t param, acc, bl, bm, c, skip, r, rOut, gamma, extWakeup, halt, bp, bc, y;
        uint8_t ox[9], o[9], mx, cb, s, rsub;
        uint8_t ram[0x80];
        uint8_t lcdPhase;
        int32_t carry;                // CPU cycles over-run in the last call
    } s;

    explicit SM5A(Io* io) : m_io(io) { std::memset(&s, 0, sizeof(s)); }
    void setRom(const uint8_t* rom, int size);
    void reset();
    void setWakeup(bool on) { s.extWakeup = on ? 1 : 0; }
    // run for n machine cycles (16384 per second); returns cycles run
    int run(int n);

private:
    Io* m_io;
    uint8_t m_rom[0x800] = {};

    uint8_t rom(uint16_t a) const { a &= 0x7ff; return a >= 0x700 ? m_rom[0x700 | (a & 0x3f)] : m_rom[a]; }
    static uint8_t ramAddr(uint8_t a)
    {
        a &= 0x7f;
        if (a >= 0x40) a = uint8_t(0x40 | (a & 0x0f));
        if ((a & 0x0f) > 0x0c) a = uint8_t((a & 0xf0) | 0x0c);
        return a;
    }
    uint8_t ramR() const { return s.ram[ramAddr(uint8_t(s.bm << 4 | s.bl))] & 0xf; }
    void ramW(uint8_t d) { s.ram[ramAddr(uint8_t(s.bm << 4 | s.bl))] = d & 0xf; }

    void incPc();
    void branch(uint8_t pu, uint8_t pm, uint8_t pl) { s.pc = uint16_t(((pu << 10) | (pm << 6 & 0x3c0) | (pl & 0x3f)) & 0x7ff); }
    void push() { s.stack0 = s.pc; }
    void pop() { s.pc = s.stack0 & 0x7ff; }
    void setSu(uint8_t su) { s.stack0 = uint16_t((s.stack0 & ~0x3c0) | (su << 6 & 0x3c0)); }
    uint8_t su() const { return uint8_t(s.stack0 >> 6 & 0xf); }
    void shiftW() { for (int i = 0; i < 8; i++) s.ox[i] = s.ox[i + 1]; }
    uint8_t digit() const;
    void clockMelody();
    void tick(int n);
    void execute();
    void exc();
    void incb();
    void decb();
};
