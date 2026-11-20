// PocketPlug - National Semiconductor COP410/COP411 microcontroller
// SPDX-License-Identifier: BSD-3-Clause
//
// Port of the COP410 part of MAME's cop400.cpp / cop400op.hxx (Curt Coder;
// BSD-3-Clause). 512 bytes of ROM, 32 nibbles of RAM, ports G, D, L.
// One step() = one instruction cycle (one byte fetched).

#pragma once
#include <cstdint>

class Cop410 {
public:
    struct Bus {
        virtual uint8_t copReadL() = 0;
        virtual void copWriteG(uint8_t v) = 0;
        virtual void copWriteD(uint8_t v) = 0;
        virtual uint8_t copReadG() { return 0; }
    };

    Cop410(Bus* bus, const uint8_t* rom, uint8_t gMask, uint8_t dMask)
        : m_bus(bus), m_rom(rom), m_gMask(gMask), m_dMask(dMask) {}
    void reset();
    void step();        // one instruction cycle

    struct State {
        uint16_t pc, sa, sb;
        uint8_t a, b, c, g, q, en, sio, skl;
        uint8_t skip, skipLbi, halt, secondByte, opcode;
        uint8_t ram[32];
    } s{};

private:
    uint8_t ramR(uint8_t a) const { return s.ram[((a >> 4) & 3) * 8 + (a & 7)]; }
    void ramW(uint8_t a, uint8_t v) { s.ram[((a >> 4) & 3) * 8 + (a & 7)] = v & 0x0f; }
    void push(uint16_t v) { s.sb = s.sa; s.sa = v; }
    void pop() { s.pc = s.sa; s.sa = s.sb; }
    void writeG(uint8_t v) { s.g = v; m_bus->copWriteG(v & m_gMask); }
    void execute(uint8_t op, uint8_t operand);
    static bool isTwoByte(uint8_t op) { return op == 0x23 || op == 0x33 || (op & 0xf8) == 0x60 || (op & 0xf8) == 0x68; }

    Bus* m_bus;
    const uint8_t* m_rom;
    uint8_t m_gMask, m_dMask;
};
