// PocketPlug - Motorola MC6801 microcontroller (mode 7, single chip)
// SPDX-License-Identifier: BSD-3-Clause
//
// Instruction set: MAME's m6800 family core (Aaron Giles), shared with the
// HD6301 core (m6801_ops.inc is hd6301_ops.inc with MAME's 6803 cycle table).
// Peripherals follow MAME's m6801_cpu_device: ports 1-4, port 3 handshake
// (P3CSR, IS3 / OS3), the free-running counter with output compare and
// overflow, and the serial interface (SCI). 2 KB of ROM at F800, 128 bytes of
// RAM at 0080.
//
// The SCI transmitter shifts at the RMCR rate from the moment RMCR is set (as
// MAME). The receiver starts on the start bit's falling edge and samples each
// bit in its middle, like the real chip's 8x oversampling, so devices whose
// bit clocks are out of phase still talk to each other.

#pragma once
#include <cstdint>
#include <cstddef>
#include <climits>

class M6801 {
public:
    typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef int8_t s8; typedef int16_t s16;
    union PAIR {
        struct { u8 l, h, h2, h3; } b;
        struct { u16 l, h; } w;
        u32 d;
    };

    struct Ports {
        virtual ~Ports() = default;
        virtual u8 portIn(int port) { (void)port; return 0xff; }      // 1-4
        virtual void portOut(int port, u8 data, u8 ddr) { (void)port; (void)data; (void)ddr; }
        virtual void os3(int state) { (void)state; }                  // port 3 output strobe
        virtual int serialIn() { return 1; }                          // RXD line (P23)
        virtual void serialOut(int state) { (void)state; }            // TXD line (P24)
        virtual u8 extRead(u16 a) { (void)a; return 0; }              // expanded modes: the external bus
        virtual void extWrite(u16 a, u8 d) { (void)a; (void)d; }
    };

    explicit M6801(Ports* ports) : m_ports(ports) { m_st = State(); }
    void setRom(const u8* rom) { m_rom = rom; }     // 2 KB, mapped at F800
    // expanded mode (6803 / mode 2): everything but the registers and RAM is external
    void setExpanded(bool on) { m_expanded = on; }
    // a plain MC6800: no on-chip registers or RAM, every access is external, 6800 timings
    void setPlain6800(bool on) { m_plain = on; m_expanded = on; m_cyc = on ? s_cycles6800 : s_cycles; }
    void reset();
    int run(int cycles);                            // returns cycles run
    void setNmi(bool on) { if (!m_nmi_state && on) m_nmi_pending = 1; m_nmi_state = on; }
    void setIrq1(bool on) { m_st.irq1 = on; }
    void is3(int state);                            // IS3 input strobe (latches port 3)
    // an IS3 pulse caused by the chip's own port access: like MAME's synced
    // input line, it lands after the current instruction
    void is3PulseLater() { m_st.is3Pending = 1; }
    int txLine() const { return m_st.tx; }
    // The SCI runs on absolute time (E cycles), driven by the machine so that
    // shifts and samples land at their exact moments between the CPUs' steps:
    int64_t cycles() const { return m_st.cycles; }
    int64_t nextSciEvent() const;
    void sciEvent(int64_t t);
    void lineChanged(int64_t t, int level);     // the serial line changed level at t
    const char* dbgName = nullptr;

    struct State {
        PAIR ppc, pc, s, x, d, ea;
        u8 cc, wai, nmiState, nmiPending, irq1;
        u8 ram[128];
        u8 portDdr[4], portData[4];
        u8 tcsr, pendingTcsr, latch09, ramCtrl, port2Written;
        u8 p3csr, pendingIsfClear, port3Latched, is3State;
        PAIR counter, oc, timerOver;
        u32 timerNext;
        // SCI
        u8 rmcr, trcsr, rdr, tdr, rsr, tshr, tx, txState, txBits, rxBits;
        u8 readTdre, readOrfe, readRdrf;
        int64_t cycles;         // E cycles run since power-on
        int64_t txNext;         // next transmit shift (INT64_MAX = SCI clock off)
        int64_t rxNext;         // next receive sample
        int64_t wuIdleFrom;     // wake-up mode: line idle since (-1 = not idle)
        u8 rxActive;
        u8 is3Pending;
    };
    State* state() { return &m_st; }
    void afterLoad() {}

private:
    typedef void (M6801::*op_func)();
    static const op_func s_insn[256];
    static const uint8_t s_cycles[256];
    static const uint8_t s_cycles6800[256];
    const uint8_t* m_cyc = s_cycles;
    bool m_plain = false;
    static const uint8_t flags8i[256];
    static const uint8_t flags8d[256];
    enum { M6800_WAI = 8, M6800_SLP = 0x10 };

    Ports* m_ports;
    const u8* m_rom = nullptr;
    bool m_expanded = false;
    int m_icount = 0;
    State m_st;
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
    bool check_irq1_enabled() { return m_st.irq1; }
    void check_irq2();
    void take_irq2(u16 vector) { m_wai_state &= ~M6800_SLP; if (!(m_cc & 0x10)) enter_interrupt("", vector); }
    void increment_counter(int amount);
    void eat_cycles();
    void cleanup_counters();
    void take_trap() { enter_interrupt("TRAP", 0xffee); }
    void set_timer_event();
    void modified_counters();
    void check_timer_event();
    void write_port2();

    void serialTransmit();
    void serialReceiveBit();
    void setRmcr(u8 d);

#include "m6801_decl.inc"
};
