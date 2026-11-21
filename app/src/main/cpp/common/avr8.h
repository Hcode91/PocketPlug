// PocketPlug - Atmel AVR8 (ATmega88) CPU
// SPDX-License-Identifier: BSD-3-Clause
//
// Written for PocketPlug from Atmel's instruction set manual and the ATmega48/88/168
// datasheet: the full ATmega88 instruction set with its cycle counts, 1 KB SRAM,
// the GPIO ports (callbacks), timer/counter 2 (normal, CTC and PWM counting with its
// overflow and compare interrupts), EEPROM, and the interrupt controller. Timers 0
// and 1, the UART, SPI, TWI and ADC are not emulated (the chess programs don't use them).

#pragma once
#include <cstdint>
#include <cstring>
#include <functional>

class AVR8 {
public:
    enum { PB = 0, PC = 1, PD = 2 };
    // gpio: port output (PORTx written) and input (PINx read)
    std::function<void(int port, uint8_t data)> portOut = [](int, uint8_t) {};
    std::function<uint8_t(int port)> portIn = [](int) { return uint8_t(0xff); };
    std::function<void(uint16_t addr, bool write, uint8_t data)> unknownIo;   // debugging

    void setRom(const uint8_t* rom, int bytes) { m_rom = rom; m_romWords = bytes / 2; }
    void reset();
    // run at least `cycles` cycles; returns the cycles used
    int run(int cycles);
    uint64_t cycles() const { return s.cycles; }

    struct State {
        uint8_t data[0x500];     // registers, I/O space and SRAM (0x100-0x4ff)
        uint8_t eeprom[0x200];
        uint16_t pc;             // word address
        uint8_t skipIrq;         // one instruction runs after SEI / RETI
        uint8_t sleeping;
        int32_t t2Count;         // cycles into the timer 2 prescaler
        uint64_t cycles;
    } s;

private:
    const uint8_t* m_rom = nullptr;
    int m_romWords = 0;

    uint16_t fetch() { uint16_t a = uint16_t(s.pc % (m_romWords ? m_romWords : 1)); s.pc++; return uint16_t(m_rom[a * 2] | m_rom[a * 2 + 1] << 8); }
    uint16_t peek(uint16_t pc) const { uint16_t a = uint16_t(pc % (m_romWords ? m_romWords : 1)); return uint16_t(m_rom[a * 2] | m_rom[a * 2 + 1] << 8); }
    uint8_t rd(uint16_t a);
    void wr(uint16_t a, uint8_t d);
    uint8_t& r(int n) { return s.data[n]; }
    uint8_t& sreg() { return s.data[0x5f]; }
    uint16_t sp() const { return uint16_t(s.data[0x5d] | s.data[0x5e] << 8); }
    void setSp(uint16_t v) { s.data[0x5d] = uint8_t(v); s.data[0x5e] = uint8_t(v >> 8); }
    void push(uint8_t v) { uint16_t p = sp(); wr(p, v); setSp(uint16_t(p - 1)); }
    uint8_t pop() { uint16_t p = uint16_t(sp() + 1); setSp(p); return rd(p); }
    void pushPc(uint16_t pc) { push(uint8_t(pc)); push(uint8_t(pc >> 8)); }
    uint16_t popPc() { uint16_t h = pop(); uint16_t l = pop(); return uint16_t(h << 8 | l); }
    int step();
    bool isTwoWord(uint16_t op) const;
    void timers(int n);
    bool irq();
};
