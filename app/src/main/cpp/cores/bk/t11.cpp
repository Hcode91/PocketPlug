// PocketPlug - K1801VM1 (PDP-11 compatible) CPU
// SPDX-License-Identifier: BSD-3-Clause
//
// Port of MAME's src/devices/cpu/t11/t11.cpp (Aaron Giles), K1801VM1 parts only.

#include "t11.h"

#define ASSERT_LINE 1
#define CLEAR_LINE 0
#define BIT(x, n) (((x) >> (n)) & 1)

#define REGD(x) m_reg[x].d
#define REGW(x) m_reg[x].w.l
#define REGB(x) m_reg[x].b.l
#define SP      REGW(6)
#define PC      REGW(7)
#define SPD     REGD(6)
#define PCD     REGD(7)
#define PSW     m_psw.b.l

t11_device::t11_device()
{
    for (auto& r : m_reg) r.d = 0;
    m_psw.d = 0;
    m_ppc.d = 0;
}

// ---- memory ----
// The BK's I/O registers are all word-wide, so odd word addresses are simply aligned.

int t11_device::ROPCODE()
{
    int val = m_bus->read_word(PC & 0xfffe);
    PC += 2;
    return val;
}
int t11_device::RBYTE(int addr) { return m_bus->read_byte(uint16_t(addr)); }
void t11_device::WBYTE(int addr, int data) { m_bus->write_byte(uint16_t(addr), uint8_t(data)); }
int t11_device::RWORD(int addr) { return m_bus->read_word(uint16_t(addr & 0xfffe)); }
void t11_device::WWORD(int addr, int data) { m_bus->write_word(uint16_t(addr & 0xfffe), uint16_t(data)); }
void t11_device::PUSH(int val) { SP -= 2; WWORD(SPD, val); }
int t11_device::POP() { int r = RWORD(SPD); SP += 2; return r; }

#define CFLAG 1
#define VFLAG 2
#define ZFLAG 4
#define NFLAG 8
#define GET_C (PSW & CFLAG)
#define GET_V (PSW & VFLAG)
#define GET_Z (PSW & ZFLAG)
#define GET_N (PSW & NFLAG)
#define GET_T (PSW & (1 << 4))
#define GET_I (PSW & (1 << 7))
#define CLR_C (PSW &= ~CFLAG)
#define CLR_V (PSW &= ~VFLAG)
#define CLR_Z (PSW &= ~ZFLAG)
#define CLR_N (PSW &= ~NFLAG)
#define SET_C (PSW |= CFLAG)
#define SET_V (PSW |= VFLAG)
#define SET_Z (PSW |= ZFLAG)
#define SET_N (PSW |= NFLAG)

// ---- interrupts (k1801vm1_device::t11_check_irqs) ----

void t11_device::t11_check_irqs()
{
    int8_t mcir = MCIR_NONE;
    uint16_t vsel = 0;

    if (m_mcir != MCIR_NONE) { mcir = m_mcir; vsel = m_vsel; }
    else if (m_bus_error) { m_bus_error = false; mcir = MCIR_IRQ; vsel = T11_TIMEOUT; }
    else if (m_trace_trap) {
        if (GET_T) { mcir = MCIR_IRQ; vsel = T11_BPT; }
        else m_trace_trap = false;
    }
    else if (GET_T) m_trace_trap = true;
    else if (m_power_fail) { m_power_fail = false; mcir = MCIR_IRQ; vsel = T11_PWRFAIL; }
    else if (m_hlt_active) { m_hlt_active = false; mcir = MCIR_HALT; vsel = VM1_HALT; }
    else if (m_cp[2] && !GET_I) { m_cp[2] = false; mcir = MCIR_IRQ; vsel = VM1_EVNT; }
    else if (m_cp[3] && !GET_I) { m_cp[3] = false; mcir = MCIR_IRQ; vsel = VM1_IRQ3; }
    else if (m_vec_active && !GET_I) {
        int vec = m_bus->iack();
        if (vec == -1 || vec == 0) { m_vec_active = false; return; }
        mcir = MCIR_IRQ; vsel = uint16_t(vec);
    }

    if (mcir == MCIR_ILL) take_interrupt(uint8_t(vsel));
    else if (mcir == MCIR_IRQ) take_interrupt(uint8_t(vsel));
    else if (mcir == MCIR_HALT) take_interrupt_halt(vsel);
    m_mcir = MCIR_NONE;
}

void t11_device::take_interrupt_halt(uint16_t vector)
{
    WWORD(VM1_SEL1, RWORD(VM1_SEL1) | SEL1_HALT);
    WWORD(VM1_STACK, PC);
    WWORD(VM1_STACK + 2, PSW);
    PCD = RWORD(vector);
    PSW = RWORD(vector + 2);
    m_icount -= 114;
    m_wait_state = 0;
}

void t11_device::take_interrupt(uint8_t vector)
{
    uint16_t new_pc = RWORD(vector);
    uint16_t new_psw = RWORD(vector + 2);
    PUSH(PSW);
    PUSH(PC);
    PCD = new_pc;
    PSW = new_psw;
    m_icount -= 114;
    m_wait_state = 0;
}

#include "t11table.hxx"
#include "t11ops.hxx"

// ---- reset / lines / run ----

void t11_device::reset()
{
    m_initial_pc = 0xc000;
    SP = 0376;
    PC = m_initial_pc;
    PSW = 0340;
    m_wait_state = 0;
    m_power_fail = false;
    m_bus_error = false;
    m_ext_halt = false;
    m_trace_trap = false;
    m_check_irqs = false;
    // k1801vm1_device::device_reset
    PC = RWORD(VM1_SEL1) & 0177400;
    WWORD(VM1_SEL1, RWORD(VM1_SEL1) | SEL1_HALT);
    m_mcir = MCIR_NONE;
    m_vsel = 0;
}

void t11_device::set_input_line(int irqline, int state)
{
    switch (irqline) {
    case CP0_LINE: case CP1_LINE: case CP2_LINE: case CP3_LINE:
        if (!state) m_cp_state &= ~(1 << irqline);
        else { m_cp_state |= 1 << irqline; m_cp[irqline] = true; }
        break;
    case VEC_LINE: m_vec_active = state != 0; break;
    case PF_LINE:
        if (state && !m_pf_active) m_power_fail = true;
        m_pf_active = state != 0;
        break;
    case BUS_ERROR:
        if (state && !m_berr_active) m_bus_error = true;
        m_berr_active = state != 0;
        break;
    case HLT_LINE:
        if (state && !m_hlt_active) m_ext_halt = true;
        m_hlt_active = state != 0;
        break;
    }
    // MAME re-checks the lines at the next execute_run; the BK core runs in short slices
    m_check_irqs = true;
}

void t11_device::run(int cycles)
{
    m_icount += cycles;
    t11_check_irqs();
    if (m_wait_state) { m_icount = 0; return; }
    while (m_icount > 0) {
        m_ppc = m_reg[7];
        uint16_t op = uint16_t(ROPCODE());
        (this->*s_opcode_table[op >> 3])(op);
        if (m_check_irqs || m_trace_trap || GET_T) {
            m_check_irqs = false;
            t11_check_irqs();
        }
    }
}
