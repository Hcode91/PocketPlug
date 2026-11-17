// PocketPlug - Signetics 2650 CPU
// SPDX-License-Identifier: BSD-3-Clause
//
// Port of MAME's s2650 core (src/devices/cpu/s2650/s2650.cpp,
// copyright Juergen Buchmueller, BSD-3-Clause) to a standalone class.

#pragma once
#include <cstdint>

class S2650 {
public:
    typedef uint8_t (*ReadFn)(void* ctx, uint16_t addr);
    typedef void (*WriteFn)(void* ctx, uint16_t addr, uint8_t data);
    typedef int (*SenseFn)(void* ctx);

    void setBus(void* ctx, ReadFn rd, WriteFn wr, SenseFn sense) { m_ctx = ctx; m_read = rd; m_write = wr; m_sense = sense; }
    void setIntackVector(uint8_t v) { m_intack = v; }

    void reset();
    // Run for a budget of clock cycles (carries over/under-run like MAME's icount).
    void run(int cycles);
    void setIrq(bool asserted) { m_irq_state = asserted ? 1 : 0; }
    bool flagOut() const { return (m_psu & 0x40) != 0; }
    uint16_t pc() const { return m_page + m_iar; }

    // state (public for save states)
    uint16_t m_ppc = 0, m_page = 0, m_iar = 0, m_ea = 0;
    uint8_t m_psl = 0, m_psu = 0, m_r = 0;
    uint8_t m_reg[7] = {};
    uint8_t m_halt = 0, m_ir = 0;
    uint16_t m_ras[8] = {};
    uint8_t m_irq_state = 0;
    int m_icount = 0;

private:
    void* m_ctx = nullptr;
    ReadFn m_read = nullptr;
    WriteFn m_write = nullptr;
    SenseFn m_sense = nullptr;
    uint8_t m_intack = 0;

    inline uint8_t RDMEM(uint16_t a) { return m_read(m_ctx, a & 0x7fff); }
    inline void WRMEM(uint16_t a, uint8_t d) { m_write(m_ctx, a & 0x7fff, d); }
    inline void set_psu(uint8_t v) { m_psu = v; }
    inline uint8_t get_psu();
    inline uint8_t get_sp() { return m_psu & 0x07; }
    inline void set_sp(uint8_t sp) { m_psu = (m_psu & ~0x07) | (sp & 0x07); }
    inline int check_irq_line();
    inline uint8_t ROP();
    inline uint8_t ARG();
};
