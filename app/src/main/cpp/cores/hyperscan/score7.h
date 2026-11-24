// PocketPlug - Sunplus S+core 7 CPU (the HyperScan's SPG290)
// SPDX-License-Identifier: BSD-3-Clause
//
// Ported from MAME's S+core core (license BSD-3-Clause, copyright-holders: Sandro Ronco).
#pragma once
#include <cstdint>

class Score7 {
public:
    struct Bus {
        virtual ~Bus() {}
        virtual uint8_t read8(uint32_t a) = 0;
        virtual uint16_t read16(uint32_t a) = 0;
        virtual uint32_t read32(uint32_t a) = 0;
        virtual void write8(uint32_t a, uint8_t v) = 0;
        virtual void write16(uint32_t a, uint16_t v) = 0;
        virtual void write32(uint32_t a, uint32_t v) = 0;
    };

    explicit Score7(Bus* b) : bus(b) {}
    void reset();
    void run(int cycles);                 // runs at least this many cycles
    void set_input(int inputnum);         // interrupt request (edge: latched until taken)
    void stop() { m_icount = 0; }         // end the current run() early

    Bus* bus;
    bool halted = false;
    bool verbose = false;
    char failMsg[160] = {};
    uint64_t insns = 0;
    void (*traceHook)(Score7*, uint32_t pc) = nullptr;
    void* user = nullptr;

    uint32_t pc() const { return m_pc; }
    void setPc(uint32_t v) { m_pc = v; }
    uint32_t gpr(int i) const { return m_gpr[i & 31]; }
    uint32_t& cr(int i) { return m_cr[i & 31]; }
    int cyclesLeft() const { return m_icount; }

    // state (public for save states)
    int      m_icount = 0;
    uint32_t m_pc = 0;
    uint32_t m_ppc = 0;
    uint32_t m_op = 0;
    uint32_t m_gpr[0x20] = {};
    uint32_t m_cr[0x20] = {};
    uint32_t m_sr[3] = {};
    uint32_t m_ce[2] = {};
    uint64_t m_pending_interrupt = 0;

private:
    void fail(const char* fmt, ...);
    void note(const char* fmt, ...);

    bool check_condition_branch(uint8_t bc);
    bool check_condition(uint8_t bc);
    uint32_t fetch();
    uint8_t read_byte(uint32_t offset);
    uint16_t read_word(uint32_t offset);
    uint32_t read_dword(uint32_t offset);
    void write_byte(uint32_t offset, uint8_t data);
    void write_word(uint32_t offset, uint16_t data);
    void write_dword(uint32_t offset, uint32_t data);
    void check_irq();
    void gen_exception(int cause, uint32_t param = 0);
    void unemulated_op(const char * op);

    // 32-bit opcodes
    void op_specialform();
    void op_iform1();
    void op_jump();
    void op_rixform1();
    void op_branch();
    void op_iform2();
    void op_crform();
    void op_rixform2();
    void op_addri();
    void op_andri();
    void op_orri();
    void op_lw();
    void op_lh();
    void op_lhu();
    void op_lb();
    void op_sw();
    void op_sh();
    void op_lbu();
    void op_sb();
    void op_cache();
    void op_cenew();
    void op_undef();

    // 16-bit opcodes
    void op_rform1();
    void op_rform2();
    void op_jform();
    void op_branch16();
    void op_ldiu();
    void op_iform1a();
    void op_iform1b();

    typedef void (Score7::*op_handler)();
    static const op_handler s_opcode32_table[4*8];
    static const op_handler s_opcode16_table[8];
};
