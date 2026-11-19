// PocketPlug - Hitachi HD6301X0 microcontroller (single-chip mode)
// SPDX-License-Identifier: BSD-3-Clause
//
// The 6301 instruction set is MAME's (m6800.cpp, 6800ops.hxx, m6801.cpp -
// Aaron Giles), generated into hd6301_ops.inc. The on-chip peripherals follow
// MAME's hd6301x_cpu_device: free-running counter with two output compares
// and overflow, timer 2 (with TOUT3 on P26), the RAM control register and
// ports 1-7. The serial interface is not emulated. 4 KB ROM at F000, 192
// bytes of RAM at 0040.

#pragma once
#include <cstdint>
#include <cstddef>

class Hd6301 {
public:
    typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef int8_t s8; typedef int16_t s16;
    union PAIR {
        struct { u8 l, h, h2, h3; } b;
        struct { u16 l, h; } w;
        u32 d;
    };

    struct Ports {
        virtual ~Ports() = default;
        virtual u8 portIn(int port) { (void)port; return 0xff; }       // 1-7
        virtual void portOut(int port, u8 data, u8 ddr) { (void)port; (void)data; (void)ddr; }
    };

    explicit Hd6301(Ports* ports) : m_ports(ports) { m_st = State(); }
    void setRom(const u8* rom) { m_rom = rom; }     // 4 KB, mapped at F000
    void reset();
    int run(int cycles);                            // returns cycles run
    void setNmi(bool on) { if (!m_nmi_state && on) m_nmi_pending = 1; m_nmi_state = on; }
    void setIrq1(bool on) { m_st.irq1 = on; }

    // everything that changes, for save states
    struct State {
        PAIR ppc, pc, s, x, d, ea;
        u8 cc, wai, nmiState, nmiPending, irq1;
        u8 ram[192];
        u8 portDdr[4], portData[4];        // ports 1-4
        u8 portxDdr[3], portxData[3];      // ports 5-7
        u8 tcsr, pendingTcsr, tcsr2, pendingTcsr2, tcsr3, tconr, t2cnt, t2cntWritten, tout3;
        u8 latch09, ramCtrl, rmcr, trcsr, port2Written;
        PAIR counter, oc[2], timerOver;
        u32 timerNext;
    };
    State* state() { return &m_st; }
    void afterLoad() {}

private:
    typedef void (Hd6301::*op_func)();
    static const op_func s_insn[256];
    static const uint8_t s_cycles[256];
    static const uint8_t flags8i[256];
    static const uint8_t flags8d[256];
    enum { M6800_WAI = 8, M6800_SLP = 0x10 };

    Ports* m_ports;
    const u8* m_rom = nullptr;
    int m_icount = 0;
    State m_st;
    // the names the MAME op handlers use
    PAIR& m_ppc = m_st.ppc; PAIR& m_pc = m_st.pc; PAIR& m_s = m_st.s; PAIR& m_x = m_st.x;
    PAIR& m_d = m_st.d; PAIR& m_ea = m_st.ea;
    u8& m_cc = m_st.cc; u8& m_wai_state = m_st.wai; u8& m_nmi_state = m_st.nmiState;
    u8& m_nmi_pending = m_st.nmiPending;

    u8 rd(u32 a);
    void wr(u32 a, u8 d);
    u8 regRead(int r);
    void regWrite(int r, u8 d);
    u32 RM16(u32 a) { return u32(rd(a) << 8) | rd((a + 1) & 0xffff); }
    void WM16(u32 a, PAIR* p) { wr(a, p->b.h); wr((a + 1) & 0xffff, p->b.l); }

    void execute_one();
    void enter_interrupt(const char*, u16 vector);
    void check_irq_lines();
    bool check_irq1_enabled() { return m_st.irq1 && (m_st.ramCtrl & 1); }
    void check_irq2();
    void take_irq2(u16 vector) { m_wai_state &= ~M6800_SLP; if (!(m_cc & 0x10)) enter_interrupt("", vector); }
    void increment_counter(int amount);
    void increment_t2cnt(int amount);
    void eat_cycles();
    void cleanup_counters();
    void take_trap() { enter_interrupt("TRAP", 0xffee); }
    void set_timer_event();
    void modified_counters();
    void check_timer_event();
    void write_port2();

#include "hd6301_decl.inc"
};
