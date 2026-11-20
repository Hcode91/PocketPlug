// PocketPlug - Motorola MC6801 microcontroller (mode 7)
// SPDX-License-Identifier: BSD-3-Clause
//
// Instruction set: MAME's m6800 family core (Aaron Giles). Peripherals
// follow MAME's m6801_cpu_device. See m6801.h.

#include "m6801.h"
#include <cstring>
#include <algorithm>
#include <cstdio>
#ifdef M6801_TRACE
double g_m6801Now = 0; int g_m6801Verbose = 0;
#endif

#include "m6801_ops.inc"

#define CT      m_st.counter.w.l
#define CTH     m_st.counter.w.h
#define CTD     m_st.counter.d
#define OC      m_st.oc.w.l
#define OCH     m_st.oc.w.h
#define OCD     m_st.oc.d
#define TOH     m_st.timerOver.w.h
#define TOD     m_st.timerOver.d

enum {
    TCSR_OLVL = 0x01, TCSR_IEDG = 0x02, TCSR_ETOI = 0x04, TCSR_EOCI = 0x08, TCSR_EICI = 0x10,
    TCSR_TOF = 0x20, TCSR_OCF = 0x40, TCSR_ICF = 0x80,
    TRCSR_RDRF = 0x80, TRCSR_ORFE = 0x40, TRCSR_TDRE = 0x20, TRCSR_RIE = 0x10, TRCSR_RE = 0x08,
    TRCSR_TIE = 0x04, TRCSR_TE = 0x02, TRCSR_WU = 0x01,
    P3CSR_LE = 0x08, P3CSR_OSS = 0x10, P3CSR_IS3_ENABLE = 0x40, P3CSR_IS3_FLAG = 0x80,
    TX_INIT = 0, TX_READY = 1, SERIAL_START = 0, SERIAL_STOP = 9,
};
static const int RMCR_SS[4] = { 16, 128, 1024, 4096 };

void M6801::reset()
{
    u8 ram[128];
    std::memcpy(ram, m_st.ram, sizeof(ram));
    u8 ramCtrl = m_st.ramCtrl;
    u8 irq1 = m_st.irq1;
    std::memset(&m_st, 0, sizeof(m_st));
    std::memcpy(m_st.ram, ram, sizeof(ram));
    m_st.irq1 = irq1;
    m_cc = 0xc0;
    SEI;
    PCD = RM16(0xfffe);
    CTD = 0x0000;
    OCD = 0xffff;
    TOD = 0xffff;
    m_st.timerNext = 0xffff;
    m_st.ramCtrl = u8(ramCtrl | 0x40);
    m_st.trcsr = TRCSR_TDRE;
    m_st.txState = TX_INIT;
    m_st.tx = 1;
    m_st.txNext = INT64_MAX;
    m_st.rxActive = 0;
    m_st.wuIdleFrom = -1;
}

int M6801::run(int cycles)
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

void M6801::execute_one()
{
    m_ppc = m_pc;
    u8 ireg = M_RDOP(PCD);
    PC++;
    (this->*s_insn[ireg])();
    increment_counter(s_cycles[ireg]);
    if (m_st.is3Pending) { m_st.is3Pending = 0; is3(1); is3(0); }
}

void M6801::enter_interrupt(const char*, u16 irq_vector)
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

void M6801::check_irq_lines()
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

void M6801::check_irq2()
{
    u8 t = m_st.tcsr, s = m_st.trcsr;
    if ((m_st.p3csr & (P3CSR_IS3_ENABLE | P3CSR_IS3_FLAG)) == (P3CSR_IS3_ENABLE | P3CSR_IS3_FLAG)) {
        if (!(m_cc & 0x10)) take_irq2(0xfff8);
    } else if ((t & (TCSR_EICI | TCSR_ICF)) == (TCSR_EICI | TCSR_ICF)) {
        if (!(m_cc & 0x10)) take_irq2(0xfff6);
    } else if ((t & (TCSR_EOCI | TCSR_OCF)) == (TCSR_EOCI | TCSR_OCF)) take_irq2(0xfff4);
    else if ((t & (TCSR_ETOI | TCSR_TOF)) == (TCSR_ETOI | TCSR_TOF)) take_irq2(0xfff2);
    else if (((s & (TRCSR_RIE | TRCSR_RDRF)) == (TRCSR_RIE | TRCSR_RDRF)) ||
             ((s & (TRCSR_RIE | TRCSR_ORFE)) == (TRCSR_RIE | TRCSR_ORFE)) ||
             ((s & (TRCSR_TIE | TRCSR_TDRE)) == (TRCSR_TIE | TRCSR_TDRE))) take_irq2(0xfff0);
}

void M6801::set_timer_event() { m_st.timerNext = (OCD < TOD) ? OCD : TOD; }

void M6801::modified_counters()
{
    OCH = (OC >= CT) ? CTH : u16(CTH + 1);
    set_timer_event();
}

void M6801::check_timer_event()
{
    if (CTD >= OCD) {
        OCH++;
        m_st.tcsr |= TCSR_OCF;
        m_st.pendingTcsr |= TCSR_OCF;
        if (m_st.portDdr[1] & 2) {
            m_st.portData[1] = u8((m_st.portData[1] & ~2) | ((m_st.tcsr & TCSR_OLVL) << 1));
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

void M6801::increment_counter(int amount)
{
    m_icount -= amount;
    CTD += amount;
    if (CTD >= m_st.timerNext) check_timer_event();
    m_st.cycles += amount;
}

void M6801::eat_cycles()
{
    // waiting: run in small steps so the SCI keeps its timing
    while (m_icount > 0 && (m_wai_state & (M6800_WAI | M6800_SLP))) {
        int n = std::min(m_icount, 4);
        if (m_st.timerNext > CTD) n = std::min<int>(n, int(m_st.timerNext - CTD));
        increment_counter(std::max(n, 1));
        check_irq_lines();
    }
}

void M6801::cleanup_counters()
{
    OCH -= CTH;
    TOH -= CTH;
    CTH = 0;
    set_timer_event();
    if (CTD >= m_st.timerNext) check_timer_event();
}

void M6801::write_port2()
{
    if (!m_st.port2Written) return;
    u8 data = m_st.portData[1];
    u8 ddr = m_st.portDdr[1] & 0x1f;
    if (ddr != 0x1f && ddr) data = u8((m_st.portData[1] & ddr) | (ddr ^ 0xff));
    if (m_st.trcsr & TRCSR_TE) { data = u8((data & 0xef) | (m_st.tx << 4)); ddr |= 0x10; }
    data &= 0x1f;
    m_ports->portOut(2, data, ddr);
}

// ---- serial interface ----
void M6801::setRmcr(u8 d)
{
    if (m_st.rmcr == d) return;
    m_st.rmcr = d;
    int cc = (d & 0x0c) >> 2;
    m_st.txNext = (cc == 1 || cc == 2) ? m_st.cycles + RMCR_SS[d & 3] : INT64_MAX;
}

int64_t M6801::nextSciEvent() const
{
    int64_t t = m_st.txNext;
    if ((m_st.trcsr & TRCSR_RE) && m_st.rxActive) t = std::min(t, m_st.rxNext);
    if ((m_st.trcsr & (TRCSR_RE | TRCSR_WU)) == (TRCSR_RE | TRCSR_WU) && m_st.wuIdleFrom >= 0)
        t = std::min(t, m_st.wuIdleFrom + 10 * RMCR_SS[m_st.rmcr & 3]);
    return t;
}

void M6801::sciEvent(int64_t t)
{
    const int bit = RMCR_SS[m_st.rmcr & 3];
    if ((m_st.trcsr & (TRCSR_RE | TRCSR_WU)) == (TRCSR_RE | TRCSR_WU) && m_st.wuIdleFrom >= 0 &&
        m_st.wuIdleFrom + 10 * bit <= t) {
        m_st.trcsr &= ~TRCSR_WU;
        m_st.wuIdleFrom = -1;
    }
    if ((m_st.trcsr & TRCSR_RE) && m_st.rxActive && m_st.rxNext <= t) {
        m_st.rxNext += bit;
        serialReceiveBit();
    }
    if (m_st.txNext <= t) {
        m_st.txNext += bit;
        serialTransmit();
    }
}

void M6801::lineChanged(int64_t t, int level)
{
    if (!(m_st.trcsr & TRCSR_RE)) return;
    if (m_st.trcsr & TRCSR_WU) { m_st.wuIdleFrom = level ? t : -1; return; }
    if (!m_st.rxActive && level == 0) {
        // start bit: sample it and the following bits in their middles
#ifdef M6801_TRACE
        if (dbgName && g_m6801Verbose) printf("%10.1f %s rx start\n", double(t), dbgName);
#endif
        m_st.rxActive = 1;
        m_st.rxBits = SERIAL_START;
        m_st.rxNext = t + RMCR_SS[m_st.rmcr & 3] / 2;
    }
}

void M6801::serialReceiveBit()
{
    int in = m_ports->serialIn();
#ifdef M6801_TRACE
    if (dbgName && g_m6801Verbose) printf("%10.1f %s rx sample %d = %d\n", double(m_st.rxNext - RMCR_SS[m_st.rmcr & 3]), dbgName, m_st.rxBits, in);
#endif
    switch (m_st.rxBits) {
    case SERIAL_START:
        if (in == 0) m_st.rxBits++;
        else m_st.rxActive = 0;                         // a glitch, not a start bit
        break;
    case SERIAL_STOP:
#ifdef M6801_TRACE
        if (dbgName) printf("%s RX %02x%s%s\n", dbgName, m_st.rsr, in ? "" : " FRAMING", (m_st.trcsr & TRCSR_RDRF) ? " OVERRUN" : "");
#endif
        if (in == 1) {
            if (m_st.trcsr & TRCSR_RDRF) { m_st.trcsr |= TRCSR_ORFE; check_irq_lines(); }
            else if (!(m_st.trcsr & TRCSR_ORFE)) {
                m_st.rdr = m_st.rsr;
                m_st.trcsr |= TRCSR_RDRF;
                check_irq_lines();
            }
        } else {
            if (!(m_st.trcsr & TRCSR_ORFE)) m_st.rdr = m_st.rsr;
            m_st.trcsr |= TRCSR_ORFE;
            m_st.trcsr &= ~TRCSR_RDRF;
            check_irq_lines();
        }
        m_st.rxBits = SERIAL_START;
        m_st.rxActive = 0;
        break;
    default:
        m_st.rsr >>= 1;
        m_st.rsr |= u8(in << 7);
        m_st.rxBits++;
        break;
    }
}

void M6801::serialTransmit()
{
    if (!(m_st.trcsr & TRCSR_TE)) return;
    int old = m_st.tx;
    m_st.portDdr[1] |= 0x10;
    if (m_st.txState == TX_INIT) {
        m_st.tx = 1;
        if (++m_st.txBits == 10) { m_st.txState = TX_READY; m_st.txBits = SERIAL_START; }
    } else {
        switch (m_st.txBits) {
        case SERIAL_START:
            if (m_st.trcsr & TRCSR_TDRE) return;
            m_st.tshr = m_st.tdr;
#ifdef M6801_TRACE
            if (dbgName) printf("%s TX %02x\n", dbgName, m_st.tdr);
#endif
            m_st.trcsr |= TRCSR_TDRE;
            m_st.tx = 0;
            m_st.txBits++;
            break;
        case SERIAL_STOP:
            m_st.tx = 1;
            check_irq_lines();
            m_st.txBits = SERIAL_START;
            break;
        default:
            m_st.tx = m_st.tshr & 1;
            m_st.tshr >>= 1;
            m_st.txBits++;
            break;
        }
    }
    if (old != m_st.tx) m_ports->serialOut(m_st.tx);
#ifdef M6801_TRACE
    if (dbgName && g_m6801Verbose) printf("%10.1f %s tx bit%d=%d\n", double(m_st.txNext - RMCR_SS[m_st.rmcr & 3]), dbgName, m_st.txBits, m_st.tx);
#endif
    m_st.port2Written = 1;
    write_port2();
}

void M6801::is3(int state)
{
    if (!m_st.is3State && state) {
        if (!m_st.port3Latched && (m_st.p3csr & P3CSR_LE)) {
            u8 ddr = m_st.portDdr[2];
            m_st.portData[2] = u8((m_ports->portIn(3) & ~ddr) | (m_st.portData[2] & ddr));
            m_st.port3Latched = 1;
            m_st.p3csr |= P3CSR_IS3_FLAG;
        }
    }
    m_st.is3State = u8(state);
}

// ---- memory ----
M6801::u8 M6801::rd(u32 a)
{
    a &= 0xffff;
    if (a < 0x20) return regRead(int(a));
    if (a >= 0x80 && a < 0x100) return m_st.ram[a - 0x80];
    if (a >= 0xf800) return m_rom ? m_rom[a - 0xf800] : 0xff;
    return 0xff;
}

void M6801::wr(u32 a, u8 d)
{
    a &= 0xffff;
    if (a < 0x20) { regWrite(int(a), d); return; }
    if (a >= 0x80 && a < 0x100) { m_st.ram[a - 0x80] = d; return; }
}

M6801::u8 M6801::regRead(int r)
{
    auto portRead = [&](int idx, int port) -> u8 {
        u8 ddr = m_st.portDdr[idx];
        if (ddr == 0xff) return m_st.portData[idx];
        return u8((m_ports->portIn(port) & ~ddr) | (m_st.portData[idx] & ddr));
    };
    switch (r) {
    case 0x02: return portRead(0, 1);
    case 0x03: return portRead(1, 2);
    case 0x06: {
        if (m_st.pendingIsfClear) { m_st.p3csr &= ~P3CSR_IS3_FLAG; m_st.pendingIsfClear = 0; }
        if (!(m_st.p3csr & P3CSR_OSS)) m_ports->os3(1);
        u8 ddr = m_st.portDdr[2], data;
        if ((m_st.p3csr & P3CSR_LE) || ddr == 0xff) data = m_st.portData[2];
        else data = u8((m_ports->portIn(3) & ~ddr) | (m_st.portData[2] & ddr));
        m_st.port3Latched = 0;
        if (!(m_st.p3csr & P3CSR_OSS)) m_ports->os3(0);
        return data;
    }
    case 0x07: return portRead(3, 4);
    case 0x08: m_st.pendingTcsr = 0; return m_st.tcsr;
    case 0x09:
        if (!(m_st.pendingTcsr & TCSR_TOF)) m_st.tcsr &= ~TCSR_TOF;
        return m_st.counter.b.h;
    case 0x0a: return m_st.counter.b.l;
    case 0x0b: return m_st.oc.b.h;
    case 0x0c: return m_st.oc.b.l;
    case 0x0d: case 0x0e: return 0;
    case 0x0f:
        if (m_st.p3csr & P3CSR_IS3_FLAG) m_st.pendingIsfClear = 1;
        return m_st.p3csr;
    case 0x10: return m_st.rmcr;
    case 0x11:
        if (m_st.trcsr & TRCSR_TDRE) m_st.readTdre = 1;
        if (m_st.trcsr & TRCSR_ORFE) m_st.readOrfe = 1;
        if (m_st.trcsr & TRCSR_RDRF) m_st.readRdrf = 1;
        return m_st.trcsr;
    case 0x12:
        if (m_st.readOrfe) { m_st.readOrfe = 0; m_st.trcsr &= ~TRCSR_ORFE; }
        if (m_st.readRdrf) { m_st.readRdrf = 0; m_st.trcsr &= ~TRCSR_RDRF; }
        return m_st.rdr;
    case 0x14: return u8(m_st.ramCtrl | 0x3f);
    default: return 0xff;
    }
}

void M6801::regWrite(int r, u8 data)
{
    auto out = [&](int idx, int port) {
        m_ports->portOut(port, u8((m_st.portData[idx] & m_st.portDdr[idx]) | (m_st.portDdr[idx] ^ 0xff)), m_st.portDdr[idx]);
    };
    switch (r) {
    case 0x00: if (m_st.portDdr[0] != data) { m_st.portDdr[0] = data; out(0, 1); } break;
    case 0x01: if (m_st.portDdr[1] != data) { m_st.portDdr[1] = data; write_port2(); } break;
    case 0x02: m_st.portData[0] = data; out(0, 1); break;
    case 0x03: m_st.portData[1] = data; m_st.port2Written = 1; write_port2(); break;
    case 0x04: if (m_st.portDdr[2] != data) { m_st.portDdr[2] = data; out(2, 3); } break;
    case 0x05: if (m_st.portDdr[3] != data) { m_st.portDdr[3] = data; out(3, 4); } break;
    case 0x06:
        if (m_st.pendingIsfClear) { m_st.p3csr &= ~P3CSR_IS3_FLAG; m_st.pendingIsfClear = 0; }
        if (m_st.p3csr & P3CSR_OSS) m_ports->os3(1);
        m_st.portData[2] = data;
        out(2, 3);
        if (m_st.p3csr & P3CSR_OSS) m_ports->os3(0);
        break;
    case 0x07: m_st.portData[3] = data; out(3, 4); break;
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
        if (r == 0x0b ? m_st.oc.b.h != data : m_st.oc.b.l != data) {
            if (r == 0x0b) m_st.oc.b.h = data; else m_st.oc.b.l = data;
            modified_counters();
        }
        break;
    case 0x0f:
        m_st.p3csr = u8((m_st.p3csr & P3CSR_IS3_FLAG) | (data & ~P3CSR_IS3_FLAG));
        check_irq2();
        break;
    case 0x10: setRmcr(data); break;
    case 0x11:
        if ((data & TRCSR_TE) && !(m_st.trcsr & TRCSR_TE)) { m_st.txState = TX_INIT; m_st.txBits = 0; m_st.tx = 1; }
        if ((data & TRCSR_RE) && !(m_st.trcsr & TRCSR_RE)) { m_st.rxBits = 0; m_st.rxActive = 0; }
        if ((data & TRCSR_WU) && !(m_st.trcsr & TRCSR_WU)) m_st.wuIdleFrom = m_ports->serialIn() ? m_st.cycles : -1;
        m_st.trcsr = u8((m_st.trcsr & 0xe0) | (data & 0x1f));
        break;
    case 0x13:
        if (m_st.readTdre) { m_st.readTdre = 0; m_st.trcsr &= ~TRCSR_TDRE; }
        m_st.tdr = data;
        break;
    case 0x14: m_st.ramCtrl = data; break;
    default: break;
    }
}
