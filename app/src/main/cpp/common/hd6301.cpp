// PocketPlug - Hitachi HD6301X0 microcontroller (single-chip mode)
// SPDX-License-Identifier: BSD-3-Clause
//
// Instruction set: MAME's m6800 family core (Aaron Giles). Peripherals
// follow MAME's hd6301x_cpu_device. See hd6301.h.

#include "hd6301.h"
#include <cstring>
#include <algorithm>
#include <cstdio>

#include "hd6301_ops.inc"

#define CT      m_st.counter.w.l
#define CTH     m_st.counter.w.h
#define CTD     m_st.counter.d
#define OC      m_st.oc[0].w.l
#define OCH     m_st.oc[0].w.h
#define OCD     m_st.oc[0].d
#define OC2     m_st.oc[1].w.l
#define OC2H    m_st.oc[1].w.h
#define OC2D    m_st.oc[1].d
#define TOH     m_st.timerOver.w.h
#define TOD     m_st.timerOver.d

enum {
    TCSR_OLVL = 0x01, TCSR_IEDG = 0x02, TCSR_ETOI = 0x04, TCSR_EOCI = 0x08, TCSR_EICI = 0x10,
    TCSR_TOF = 0x20, TCSR_OCF = 0x40, TCSR_ICF = 0x80,
    TCSR2_OE1 = 0x01, TCSR2_OE2 = 0x02, TCSR2_OLVL2 = 0x04, TCSR2_EOCI2 = 0x08, TCSR2_OCF2 = 0x20,
};

void Hd6301::reset()
{
    // m6800 / m6801 / hd6301x device_reset
    u8 ram[192];
    std::memcpy(ram, m_st.ram, sizeof(ram));
    u8 ramCtrl = m_st.ramCtrl;
    std::memset(&m_st, 0, sizeof(m_st));
    std::memcpy(m_st.ram, ram, sizeof(ram));
    m_cc = 0xc0;
    SEI;
    PCD = RM16(0xfffe);
    CTD = 0x0000;
    OCD = 0xffff;
    TOD = 0xffff;
    OC2D = 0xffff;
    m_st.timerNext = 0xffff;
    m_st.ramCtrl = u8(((ramCtrl | 0x40) & 0x80) | 0x7c);
    m_st.trcsr = 0x20;   // TDRE
    m_st.tconr = 0xff;
}

int Hd6301::run(int cycles)
{
    m_icount = cycles;
    check_irq_lines();
    cleanup_counters();
    do {
        if (m_wai_state & (M6800_WAI | M6800_SLP)) eat_cycles();
        else execute_one();
    } while (m_icount > 0);
    return cycles - m_icount;
}

void Hd6301::execute_one()
{
    m_ppc = m_pc;
    u8 ireg = M_RDOP(PCD);
    PC++;
    (this->*s_insn[ireg])();
    increment_counter(s_cycles[ireg]);
}

void Hd6301::enter_interrupt(const char*, u16 irq_vector)
{
    int eat;
    if (m_wai_state & M6800_WAI) { eat = 4; m_wai_state &= ~M6800_WAI; }
    else {
        PUSHWORD(pPC);
        PUSHWORD(pX);
        PUSHBYTE(A);
        PUSHBYTE(B);
        PUSHBYTE(CC);
        eat = 12;
    }
    SEI;
    PCD = RM16(irq_vector);
    increment_counter(eat);
}

void Hd6301::check_irq_lines()
{
    if (m_nmi_pending) {
        m_wai_state &= ~M6800_SLP;
        m_nmi_pending = 0;
        enter_interrupt("NMI", 0xfffc);
    } else if (check_irq1_enabled()) {
        m_wai_state &= ~M6800_SLP;
        if (!(CC & 0x10)) enter_interrupt("IRQ1", 0xfff8);
    } else
        check_irq2();
}

void Hd6301::check_irq2()
{
    u8 t = m_st.tcsr;
    if ((t & (TCSR_EICI | TCSR_ICF)) == (TCSR_EICI | TCSR_ICF)) take_irq2(0xfff6);
    else if ((t & (TCSR_EOCI | TCSR_OCF)) == (TCSR_EOCI | TCSR_OCF) ||
             (m_st.tcsr2 & (TCSR2_EOCI2 | TCSR2_OCF2)) == (TCSR2_EOCI2 | TCSR2_OCF2)) take_irq2(0xfff4);
    else if ((t & (TCSR_ETOI | TCSR_TOF)) == (TCSR_ETOI | TCSR_TOF)) take_irq2(0xfff2);
    else if ((m_st.tcsr3 & 0xc0) == 0xc0) take_irq2(0xffec);
}

void Hd6301::set_timer_event()
{
    m_st.timerNext = (OCD < TOD) ? OCD : TOD;
    if (OC2D < m_st.timerNext) m_st.timerNext = OC2D;
}

void Hd6301::modified_counters()
{
    OCH = (OC >= CT) ? CTH : u16(CTH + 1);
    OC2H = (OC2 >= CT) ? CTH : u16(CTH + 1);
    set_timer_event();
}

void Hd6301::check_timer_event()
{
    if (CTD >= OCD) {
        OCH++;
        m_st.tcsr |= TCSR_OCF;
        m_st.pendingTcsr |= TCSR_OCF;
        if (m_st.tcsr2 & TCSR2_OE1) {
            m_st.portData[1] = u8((m_st.portData[1] & ~2) | ((m_st.tcsr & TCSR_OLVL) << 1));
            m_st.port2Written = 1;
            write_port2();
        }
    }
    if (CTD >= OC2D) {
        OC2H++;
        m_st.tcsr2 |= TCSR2_OCF2;
        m_st.pendingTcsr2 |= TCSR2_OCF2;
        if (m_st.tcsr2 & TCSR2_OE2) {
            if (m_st.tcsr2 & TCSR2_OLVL2) m_st.portData[1] |= 0x20;
            else m_st.portData[1] &= ~0x20;
            m_st.port2Written = 1;
            write_port2();
        }
    }
    if (CTD >= TOD) {
        TOH++;
        m_st.tcsr |= TCSR_TOF;
        m_st.pendingTcsr |= TCSR_TOF;
    }
    check_irq2();
    set_timer_event();
}

void Hd6301::increment_t2cnt(int amount)
{
    while (amount--) {
        if (++m_st.t2cnt == u8(m_st.tconr + 1)) {
            m_st.t2cnt = 0;
            if (m_st.tcsr3 & 0x08) {
                if (m_st.tout3 != ((m_st.tcsr3 >> 2) & 1)) {
                    m_st.tout3 = (m_st.tcsr3 >> 2) & 1;
                    m_st.port2Written = 1;
                    write_port2();
                }
            } else if (m_st.tcsr3 & 0x04) {
                m_st.tout3 = !m_st.tout3;
                m_st.port2Written = 1;
                write_port2();
            }
            m_st.tcsr3 |= 0x80;
            m_st.timerNext = 0;   // as MAME: recheck now
        }
    }
}

void Hd6301::increment_counter(int amount)
{
    m_icount -= amount;
    if (m_st.t2cntWritten) m_st.t2cntWritten = 0;
    else if (m_st.tcsr3 & 0x10) {
        switch (m_st.tcsr3 & 3) {
        case 0: increment_t2cnt(amount); break;
        case 1: increment_t2cnt(int((amount + (CTD & 7)) >> 3)); break;
        case 2: increment_t2cnt(int((amount + (CTD & 0x7f)) >> 7)); break;
        default: break;
        }
    }
    CTD += amount;
    if (CTD >= m_st.timerNext || (m_st.tcsr3 & 0xc0) == 0xc0) check_timer_event();
}

void Hd6301::eat_cycles()
{
    if (m_st.tcsr3 & 0x10) {
        while (m_icount > 0 && (m_wai_state & (M6800_WAI | M6800_SLP))) {
            increment_counter(1);
            if (m_wai_state & M6800_SLP) check_irq_lines();
        }
    } else {
        int n = std::min(int(m_st.timerNext - CTD), m_icount);
        if (n > 0) increment_counter(n);
        else if (m_icount > 0 && int(m_st.timerNext - CTD) <= 0) increment_counter(1);
    }
}

void Hd6301::cleanup_counters()
{
    OC2H -= CTH;
    OCH -= CTH;
    TOH -= CTH;
    CTH = 0;
    set_timer_event();
    if (CTD >= m_st.timerNext) check_timer_event();
}

void Hd6301::write_port2()
{
    if (!m_st.port2Written) return;
    u8 ddr = m_st.portDdr[1];
    if (m_st.tcsr2 & TCSR2_OE1) ddr |= 0x02;
    if (m_st.tcsr2 & TCSR2_OE2) ddr |= 0x20;
    u8 data = u8((m_st.portData[1] & ddr) | (ddr ^ 0xff));
    if ((m_st.tcsr3 & 0x0c) != 0) { data = u8((data & 0xbf) | (m_st.tout3 << 6)); ddr |= 0x40; }
    m_ports->portOut(2, data, ddr);
}

// ---- memory ----
Hd6301::u8 Hd6301::rd(u32 a)
{
    a &= 0xffff;
    if (a < 0x20) return regRead(int(a));
    if (a >= 0x40 && a < 0x100) return m_st.ram[a - 0x40];
    if (a >= 0xf000) return m_rom ? m_rom[a - 0xf000] : 0xff;
    return 0xff;
}

void Hd6301::wr(u32 a, u8 d)
{
    a &= 0xffff;
    if (a < 0x20) { regWrite(int(a), d); return; }
    if (a >= 0x40 && a < 0x100) { m_st.ram[a - 0x40] = d; return; }
}

Hd6301::u8 Hd6301::regRead(int r)
{
    auto portRead = [&](int idx, int port, u8 ddr) -> u8 {
        if (ddr == 0xff) return m_st.portData[idx];
        return u8((m_ports->portIn(port) & ~ddr) | (m_st.portData[idx] & ddr));
    };
    switch (r) {
    case 0x02: return portRead(0, 1, m_st.portDdr[0]);
    case 0x03: return portRead(1, 2, m_st.portDdr[1]);
    case 0x06: return portRead(2, 3, m_st.portDdr[2]);
    case 0x07: return portRead(3, 4, m_st.portDdr[3]);
    case 0x08: m_st.pendingTcsr = 0; return m_st.tcsr;
    case 0x09:
        if (!(m_st.pendingTcsr & TCSR_TOF)) m_st.tcsr &= ~TCSR_TOF;
        return m_st.counter.b.h;
    case 0x0a: return m_st.counter.b.l;
    case 0x0b: return m_st.oc[0].b.h;
    case 0x0c: return m_st.oc[0].b.l;
    case 0x0d: case 0x0e: return 0;   // input capture (unused)
    case 0x0f:
        m_st.pendingTcsr &= ~(TCSR_ICF | TCSR_OCF);
        m_st.pendingTcsr2 = 0;
        return u8(m_st.tcsr2 | (m_st.tcsr & (TCSR_ICF | TCSR_OCF)) | 0x10);
    case 0x10: return m_st.rmcr;
    case 0x11: return m_st.trcsr;
    case 0x14: return u8(m_st.ramCtrl | 0x30);
    case 0x15: return m_ports->portIn(5);
    case 0x17: {
        u8 ddr = m_st.portxDdr[1];
        if (ddr == 0xff) return m_st.portxData[1];
        return u8((m_ports->portIn(6) & ~ddr) | (m_st.portxData[1] & ddr));
    }
    case 0x18: return u8(0xe0 | m_st.portxData[2]);
    case 0x19: return m_st.oc[1].b.h;
    case 0x1a: return m_st.oc[1].b.l;
    case 0x1b: return m_st.tcsr3;
    case 0x1d: return m_st.t2cnt;
    default: return 0xff;
    }
}

void Hd6301::regWrite(int r, u8 data)
{
    switch (r) {
    case 0x00: m_st.portDdr[0] = data; break;
    case 0x01:
        // DDR2 is 2-bit on the 6301X
        m_st.portDdr[1] = u8(((data & 2) ? 0xfe : 0x00) | (data & 1));
        m_st.port2Written = 1; write_port2();
        break;
    case 0x02:
        m_st.portData[0] = data;
        m_ports->portOut(1, u8((data & m_st.portDdr[0]) | (m_st.portDdr[0] ^ 0xff)), m_st.portDdr[0]);
        break;
    case 0x03: m_st.portData[1] = data; m_st.port2Written = 1; write_port2(); break;
    case 0x04: m_st.portDdr[2] = (data & 1) ? 0xff : 0x00; break;
    case 0x05: m_st.portDdr[3] = data; break;
    case 0x06:
        m_st.portData[2] = data;
        m_ports->portOut(3, u8((data & m_st.portDdr[2]) | (m_st.portDdr[2] ^ 0xff)), m_st.portDdr[2]);
        break;
    case 0x07:
        m_st.portData[3] = data;
        m_ports->portOut(4, u8((data & m_st.portDdr[3]) | (m_st.portDdr[3] ^ 0xff)), m_st.portDdr[3]);
        break;
    case 0x08:
        m_st.tcsr = u8((data & 0x1f) | (m_st.tcsr & 0xe0));
        m_st.pendingTcsr &= m_st.tcsr;
        check_irq2();
        break;
    case 0x09:
        m_st.latch09 = data;
        CT = 0xfff8;
        TOH = CTH;
        modified_counters();
        break;
    case 0x0a:
        CT = u16((m_st.latch09 << 8) | data);
        TOH = CTH;
        modified_counters();
        break;
    case 0x0b: case 0x0c:
        if (!(m_st.pendingTcsr & TCSR_OCF)) m_st.tcsr &= ~TCSR_OCF;
        if (r == 0x0b ? m_st.oc[0].b.h != data : m_st.oc[0].b.l != data) {
            if (r == 0x0b) m_st.oc[0].b.h = data; else m_st.oc[0].b.l = data;
            modified_counters();
        }
        break;
    case 0x0f:
        m_st.tcsr2 = u8((data & (TCSR2_OE1 | TCSR2_OE2 | TCSR2_OLVL2 | TCSR2_EOCI2)) | (m_st.tcsr2 & TCSR2_OCF2));
        m_st.pendingTcsr2 &= m_st.tcsr2;
        check_irq2();
        break;
    case 0x10: m_st.rmcr = data; break;
    case 0x11: m_st.trcsr = u8((m_st.trcsr & 0xe0) | (data & 0x1f)); break;
    case 0x14: m_st.ramCtrl = data; check_irq_lines(); break;
    case 0x16:
        if (m_st.portxDdr[1] != data) {
            m_st.portxDdr[1] = data;
            m_ports->portOut(6, u8((m_st.portxData[1] & data) | (data ^ 0xff)), data);
        }
        break;
    case 0x17:
        m_st.portxData[1] = data;
        m_ports->portOut(6, u8((data & m_st.portxDdr[1]) | (m_st.portxDdr[1] ^ 0xff)), m_st.portxDdr[1]);
        break;
    case 0x18:
        m_st.portxData[2] = data & 0x1f;
        m_ports->portOut(7, data & 0x1f, 0x1f);
        break;
    case 0x19: case 0x1a:
        if (!(m_st.pendingTcsr2 & TCSR2_OCF2)) m_st.tcsr2 &= ~TCSR2_OCF2;
        if (r == 0x19 ? m_st.oc[1].b.h != data : m_st.oc[1].b.l != data) {
            if (r == 0x19) m_st.oc[1].b.h = data; else m_st.oc[1].b.l = data;
            modified_counters();
        }
        break;
    case 0x1b: {
        bool lastEnable = (m_st.tcsr3 & 0x0c) != 0;
        m_st.tcsr3 = u8(data & (0x5f | (m_st.tcsr3 & 0x80)));
        if (m_st.tout3 && !(data & 0x10)) { m_st.tout3 = 0; write_port2(); }
        else if (lastEnable ? (data & 0x0c) == 0 : (data & 0x0c) != 0) { m_st.port2Written = 1; write_port2(); }
        check_irq2();
        break;
    }
    case 0x1c: m_st.tconr = data; break;
    case 0x1d: m_st.t2cnt = data; m_st.t2cntWritten = 1; break;
    default: break;
    }
}
