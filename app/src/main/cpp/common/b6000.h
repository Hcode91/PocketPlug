// PocketPlug - Rockwell B6000 4-bit MCU (Mattel's first handhelds)
// SPDX-License-Identifier: BSD-3-Clause
//
// Port of MAME's rw5000 family (src/devices/cpu/rw5000: rw5000base, b5000,
// b5000op, b6000; copyright-holders hap). 512 bytes of ROM, 45 nibbles of RAM,
// 4 clocks per instruction.

#pragma once
#include <cstdint>
#include <cstring>
#include <cstddef>

class B6000 {
public:
    struct Io {
        virtual ~Io() = default;
        virtual uint8_t readKB() = 0;
        virtual uint8_t readDIN() { return 0; }
        virtual void writeSTR(uint16_t) {}
        virtual void writeSEG(uint16_t) {}
        virtual void writeSPK(int) {}
    };
    struct State {
        uint16_t pc, prevPc, sreg;
        uint8_t op, prevOp, a, bl, bu, prevBl, prevBu, blDelay, buDelay, ramAddr;
        uint8_t c, prevC, prev2C, prev3C, sr, skip;
        uint16_t seg;
        uint8_t atbStep, traStep, retStep;
        uint8_t ram[0x40];
        uint64_t cycles;
    } s;

    explicit B6000(Io* io) : m_io(io) { std::memset(&s, 0, sizeof(s)); std::memset(m_rom, 0, sizeof(m_rom)); }
    void setRom(const uint8_t* rom, size_t n) { std::memset(m_rom, 0, sizeof(m_rom)); std::memcpy(m_rom, rom, n > 0x200 ? 0x200 : n); }
    void reset();
    int run(int n);
    uint64_t cycles() const { return s.cycles; }

private:
    Io* m_io;
    uint8_t m_rom[0x200];

    static bool ramMapped(uint8_t a)
    {
        uint8_t lo = a & 0xf, hi = a >> 4;
        return hi == 0 ? lo <= 0xb : lo <= 0xa;
    }
    uint8_t ramR() const { return ramMapped(s.ramAddr) ? s.ram[s.ramAddr] & 0xf : 0; }
    void ramW(uint8_t d) { if (ramMapped(s.ramAddr)) s.ram[s.ramAddr] = d & 0xf; }
    void setPc(uint8_t pu, uint8_t pl) { s.pc = uint16_t(((pu << 6) | (pl & 0x3f)) & 0x1ff); }
    void setBu(uint8_t bu) { s.bu = bu & 3; if (bool(s.bu) != bool(s.prevBu)) s.buDelay = 1; }
    void segW(uint16_t seg) { s.seg = seg; m_io->writeSEG(seg); }
    void incPc();
    void execute();
    void exc0();
    void lb(uint8_t bl);
    void atb();
    static bool isLb(uint8_t op) { return (op & 0xf0) == 0x20 || (op & 0xfc) == 0x3c; }
    static bool isAtb(uint8_t op) { return op == 0x77; }
};
