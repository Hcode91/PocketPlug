// PocketPlug - SunPlus u'nSP (ISA 1.0) CPU
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Interpreter ported from MAME's unsp device (Segher Boessenkool, Ryan Holtz,
// David Haywood; GPL-2.0+). The instruction bodies in unsp.cpp are MAME's,
// copied with only the logging removed; the device framework is replaced by a
// plain bus interface and the scheduler hooks below.

#pragma once
#include <cstdint>
#include <cstring>

#ifndef BIT
#define BIT(x, n) (((x) >> (n)) & 1)
#endif

#define UNSP_LPC            (((m_core->m_r[REG_SR] & 0x3f) << 16) | m_core->m_r[REG_PC])
#define UNSP_LREG_I(reg)    (((m_core->m_r[REG_SR] << 6) & 0x3f0000) | m_core->m_r[reg])

#define UNSP_N  0x0200
#define UNSP_Z  0x0100
#define UNSP_S  0x0080
#define UNSP_C  0x0040

#define UNSP_N_SHIFT    9
#define UNSP_Z_SHIFT    8
#define UNSP_S_SHIFT    7
#define UNSP_C_SHIFT    6

enum
{
    UNSP_FIQ_LINE = 0,
    UNSP_IRQ0_LINE,
    UNSP_IRQ1_LINE,
    UNSP_IRQ2_LINE,
    UNSP_IRQ3_LINE,
    UNSP_IRQ4_LINE,
    UNSP_IRQ5_LINE,
    UNSP_IRQ6_LINE,
    UNSP_IRQ7_LINE,
    UNSP_BRK_LINE,
    UNSP_NUM_LINES
};

class unsp_device
{
public:
    // bus: 16-bit words, 23-bit word address
    struct Bus {
        virtual uint16_t read_word(uint32_t address) = 0;
        virtual void write_word(uint32_t address, uint16_t data) = 0;
        virtual ~Bus() = default;
    };

    void set_bus(Bus* b) { m_bus = b; }
    void reset();
    // runs until icount drops below 0 (MAME execute_run semantics)
    void execute_run();

    uint8_t get_csb();
    void set_ds(uint16_t ds);
    uint16_t get_ds();
    void set_fr(uint16_t fr);
    uint16_t get_fr();
    void set_state_unsynced(int inputnum, int state);

    enum : uint32_t
    {
        REG_SP = 0, REG_R1, REG_R2, REG_R3, REG_R4, REG_BP, REG_SR, REG_PC,
        REG_SR1 = 0, REG_SR2, REG_SR3, REG_SR4
    };

    struct internal_unsp_state
    {
        uint32_t m_r[16];
        uint32_t m_secbank[4];
        uint32_t m_enable_irq;
        uint32_t m_enable_fiq;
        uint32_t m_fir_move;
        uint32_t m_fiq;
        uint32_t m_irq;
        uint32_t m_sirq;
        uint32_t m_sb;
        uint32_t m_aq;
        uint32_t m_fra;
        uint32_t m_bnk;
        uint32_t m_ine;
        uint32_t m_pri;
        uint32_t m_divq_bit;
        uint32_t m_divq_dividend;
        uint32_t m_divq_divisor;
        uint32_t m_divq_a;
        uint32_t m_arg0;
        uint32_t m_arg1;
        uint32_t m_jmpdest;
        int m_icount;
    };
    internal_unsp_state m_local_core;
    internal_unsp_state* m_core = &m_local_core;

    uint32_t pc() { return UNSP_LPC; }

protected:
    Bus* m_bus = nullptr;
    int m_iso = 10;
    int m_numregs = 8;
    uint16_t m_vectorbase = 0xfff0;
    uint16_t m_bootvectorbase = 0xfff0;

    uint16_t read16(uint32_t address) { return m_bus->read_word(address); }
    void write16(uint32_t address, uint16_t data) { m_bus->write_word(address, data); }

    void add_lpc(const int32_t offset)
    {
        const uint32_t new_lpc = UNSP_LPC + offset;
        m_core->m_r[REG_PC] = (uint16_t)new_lpc;
        m_core->m_r[REG_SR] &= 0xffc0;
        m_core->m_r[REG_SR] |= (new_lpc >> 16) & 0x3f;
    }

    void execute_fxxx_000_group(uint16_t op);
    void execute_fxxx_001_group(uint16_t op);
    void execute_fxxx_010_group(uint16_t op);
    void execute_fxxx_011_group(uint16_t op);
    void execute_fxxx_101_group(uint16_t op);
    void execute_fxxx_110_group(uint16_t op);
    void execute_fxxx_111_group(uint16_t op);
    void execute_fxxx_group(uint16_t op);
    void execute_fxxx_100_group(uint16_t op);
    void execute_extended_group(uint16_t op);
    void execute_exxx_group(uint16_t op);
    void execute_muls_ss(const uint16_t rd, const uint16_t rs, const uint16_t size);
    void unimplemented_opcode(uint16_t op) {}
    void unimplemented_opcode(uint16_t op, uint16_t ximm) {}
    void unimplemented_opcode(uint16_t op, uint16_t ximm, uint16_t ximm_2) {}
    bool op_is_divq(const uint16_t op) { return false; }
    void execute_divq(uint16_t op) {}

    void push(uint32_t value, uint32_t *reg);
    uint16_t pop(uint32_t *reg);
    void update_nz(uint32_t value);
    void update_nzsc(uint32_t value, uint16_t r0, uint16_t r1);
    bool do_basic_alu_ops(const uint16_t& op0, uint32_t& lres, uint16_t& r0, uint16_t& r1, uint32_t& r2, bool update_flags);

    void execute_jumps(const uint16_t op);
    void execute_remaining(const uint16_t op);
    void execute_one(const uint16_t op);

    void trigger_fiq();
    void trigger_irq(int line);
    void check_irqs();
    void standard_irq_callback(int, uint32_t) {}
};
