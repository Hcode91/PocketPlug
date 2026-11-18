// PocketPlug - shared NMOS 6502 CPU core
// SPDX-License-Identifier: BSD-3-Clause
//
// Cycle-counted NMOS 6502 interpreter used by the plug-and-play cores.
// Memory access goes through plain function pointers so each core can
// supply its own address map. An optional opcode translation table lets
// cores handle chips that scramble opcode bits (e.g. VT1682 variants).

#pragma once
#include <cstdint>

class M6502 {
public:
    typedef uint8_t (*ReadFn)(void* ctx, uint16_t addr);
    typedef void (*WriteFn)(void* ctx, uint16_t addr, uint8_t data);

    void setBus(void* ctx, ReadFn rd, WriteFn wr) { m_ctx = ctx; m_read = rd; m_write = wr; }
    void setOpcodeTable(const uint8_t* table) { m_optable = table; }
    bool decimalEnabled = true;   // false for the NES-style RP2A03 core

    void reset();               // runs the reset sequence (reads 0xFFFC)
    int step();                 // executes one instruction (or interrupt entry), returns cycles

    void setIrq(bool asserted) { if (asserted && !m_irqLine) m_irqSkip = interruptDelay; m_irqLine = asserted; }
    void pulseNmi() { m_nmiPending = true; m_nmiSkip = interruptDelay; }
    // MAME-style interrupt sampling: an interrupt raised between instructions is
    // only seen after one more instruction (the next opcode was already fetched).
    bool interruptDelay = false;
    // HOLD_LINE style IRQ: the line drops by itself once the interrupt is taken
    bool irqHold = false;
    // VT3xx sound CPU: extra opcodes (PHX/PHY/PLX/PLY, data bank, 24-bit bus loads)
    typedef uint8_t (*ExtReadFn)(void* ctx, uint32_t addr);
    ExtReadFn extRead = nullptr;     // non-null enables the VT3xx SPU opcode set
    uint8_t databank = 0;
    bool m_irqSkip = false, m_nmiSkip = false;

    // registers (public for state saving / debugging)
    uint16_t pc = 0;
    uint8_t a = 0, x = 0, y = 0, s = 0xfd, p = 0x24;

    bool m_irqLine = false;
    bool m_nmiPending = false;

private:
    void* m_ctx = nullptr;
    ReadFn m_read = nullptr;
    WriteFn m_write = nullptr;
    const uint8_t* m_optable = nullptr;

    inline uint8_t rd(uint16_t a_) { return m_read(m_ctx, a_); }
    inline void wr(uint16_t a_, uint8_t d) { m_write(m_ctx, a_, d); }
    inline uint16_t rd16(uint16_t a_) { return rd(a_) | (rd((uint16_t)(a_ + 1)) << 8); }
    inline void push(uint8_t v) { wr(0x100 | s, v); s--; }
    inline uint8_t pop() { s++; return rd(0x100 | s); }
    inline void setNZ(uint8_t v) { p = (p & ~0x82) | (v & 0x80) | (v ? 0 : 0x02); }

    void interrupt(uint16_t vector, bool brk);
    void adc(uint8_t v);
    void sbc(uint8_t v);
    void cmp(uint8_t r, uint8_t v);
    int stepSpu(uint8_t op);
};
