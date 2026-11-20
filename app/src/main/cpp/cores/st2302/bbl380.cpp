// PocketPlug - Sitronix ST2302U handhelds (MAME's bbl380 family)
// SPDX-License-Identifier: BSD-3-Clause
//
// Port of MAME's st2302u_bbl_spi.cpp, st2xxx.cpp, st2205u.cpp (ST2302U parts),
// st7735_lcdc.cpp, bl_handhelds_menucontrol.cpp and generic_spi_flash.cpp.
// See bbl380.h.

#include "bbl380.h"
#include <cstring>
#include <cmath>
#include <algorithm>

namespace {
const uint16_t PRR_MASK = 0x0fff;    // 26 data bits, no banked RAM
const uint16_t DRR_MASK = 0x07ff;
const uint16_t IREQ_MASK = 0xd37f;
inline int bit(unsigned v, int n) { return (v >> n) & 1; }
}

static bool s_running = false;

double Bbl380Core::now() const { return double(m_s.cycles) + (s_running ? m_cpu.executed() : 0); }

Bbl380Core::Bbl380Core() : m_cpu(this)
{
    std::memset(&m_s, 0, sizeof(m_s));
    std::memset(m_bios, 0xff, sizeof(m_bios));
    std::memset(m_frame, 0, sizeof(m_frame));
    m_lcd.assign(256 * 256, 0);
}

bool Bbl380Core::load(const std::vector<RomFile>& roms, std::string& error)
{
    bool bios = false, spi = false;
    m_flash.assign(0x800000, 0xff);
    for (auto& r : roms) {
        if (r.name == "st2x_internal_type1.bin" && r.data.size() == 0x2000) { std::memcpy(m_bios, r.data.data(), 0x2000); bios = true; }
        else if (r.name == "25q40.bin" && r.data.size() <= 0x800000) { std::memcpy(m_flash.data(), r.data.data(), r.data.size()); spi = true; }
    }
    if (!bios) { error = "Missing st2x_internal_type1.bin"; return false; }
    if (!spi) { error = "Missing 25q40.bin"; return false; }
    reset();
    return true;
}

// ---------------------------------------------------------------------------
// reset
// ---------------------------------------------------------------------------
void Bbl380Core::reset()
{
    int64_t cyc = m_s.cycles;
    std::memset(&m_s, 0, sizeof(m_s));
    m_s.cycles = cyc;
    std::fill(m_lcd.begin(), m_lcd.end(), 0);
    // device_start state of the board devices
    m_s.lsleep = 1;
    m_s.mState = 0;
    flashReset();

    // st2xxx_device::device_reset
    std::fill(std::begin(m_s.pdata), std::end(m_s.pdata), 0xff);
    std::fill(std::begin(m_s.psel), std::end(m_s.psel), 0xff);
    for (int i = 0; i < 7; i++) portOut(i, 0xff, 0xff);
    m_s.pmcr = 0x80;
    m_s.irrEnable = 0; m_s.irr = m_s.prr = m_s.drr = 0;
    m_s.ireq = m_s.iena = 0;
    m_s.irqLevel = 0xff;
    for (auto& t : m_s.btAt) t = NEVER;
    m_s.bten = 0; m_s.btsr = 0;
    prsW(0x80);
    m_s.sys = 0;
    m_s.misc = 0x0c;     // watchdog on at reset
    m_s.spiAt = NEVER;
    // st2205u_base_device::device_reset
    for (auto& t : m_s.tAt) t = NEVER;
    m_s.usbien = 0x20;
    // bbl380_state::machine_reset
    m_s.output2val = 0;
    updateIrq();
    m_cpu.reset();
}

// ---------------------------------------------------------------------------
// memory
// ---------------------------------------------------------------------------
uint8_t Bbl380Core::dataRead(uint32_t a)
{
    a &= 0x3ffffff;
    if (a >= 0x2000 && a < 0x4000) return m_bios[a - 0x2000];
    return 0;
}

void Bbl380Core::dataWrite(uint32_t a, uint8_t d)
{
    a &= 0x3ffffff;
    if (a == 0x1800000) lcdCommand(d);
    else if (a == 0x1804000) lcdData(d);
}

uint8_t Bbl380Core::pread(uint16_t a)
{
    uint16_t bank = (m_s.irqService && m_s.irrEnable) ? m_s.irr : m_s.prr;
    return dataRead(uint32_t(bank) << 14 | (a & 0x3fff));
}

void Bbl380Core::pwrite(uint16_t a, uint8_t d)
{
    uint16_t bank = (m_s.irqService && m_s.irrEnable) ? m_s.irr : m_s.prr;
    dataWrite(uint32_t(bank) << 14 | (a & 0x3fff), d);
}

uint8_t Bbl380Core::read(uint16_t a)
{
    if (a < 0x80) return regRead(uint8_t(a));
    if (a < 0x800) return m_s.ram[a];
    if (a < 0x4000) return 0;
    if (a < 0x8000) return pread(a);
    return dataRead(uint32_t(m_s.drr) << 15 | (a & 0x7fff));
}

void Bbl380Core::write(uint16_t a, uint8_t d)
{
    if (a < 0x80) { regWrite(uint8_t(a), d); return; }
    if (a < 0x800) { m_s.ram[a] = d; return; }
    if (a < 0x4000) return;
    if (a < 0x8000) { pwrite(a, d); return; }
    dataWrite(uint32_t(m_s.drr) << 15 | (a & 0x7fff), d);
}

uint8_t Bbl380Core::activeIrqLevel() const
{
    uint16_t v = uint16_t(m_s.ireq & m_s.iena);
    uint16_t sw = uint16_t((v >> 8) | (v << 8));
    if (!sw) return 0xff;
    uint32_t low = uint32_t(sw & (0u - sw)) & 0xffff;
    return uint8_t(31 - (8 ^ __builtin_clz(low)));
}

uint8_t Bbl380Core::readVector(uint16_t adr, bool irq)
{
    if (adr >= 0xfffe) {
        if (adr == 0xfffe) {
            m_s.irqService = 1;
            m_s.irqLevel = irq ? activeIrqLevel() : 0xff;
        }
        if (m_s.irqLevel != 0xff) {
            adr = uint16_t(adr - ((m_s.irqLevel + 3) << 1));
            if (adr & 1) { m_s.ireq &= uint16_t(~(1 << m_s.irqLevel)); updateIrq(); }
        }
    }
    return pread(adr);
}

void Bbl380Core::updateIrq() { m_cpu.setIrq((m_s.ireq & m_s.iena) != 0); }

// ---------------------------------------------------------------------------
// ports
// ---------------------------------------------------------------------------
uint8_t Bbl380Core::pdataR(int port)
{
    uint8_t pdata = m_s.pdata[port];
    uint8_t pinmask = uint8_t(~m_s.pctrl[port] | (pdata & ~m_s.psel[port]));
    if (pinmask) {
        uint8_t in = 0xff;
        if (port == 0) {
            // IN0, active low: up 02, left 04, right 08, down 10, start 20, sound 40, B 80
            in = 0xff;
            if (m_inputs & 0x01) in &= ~0x02;
            if (m_inputs & 0x04) in &= ~0x04;
            if (m_inputs & 0x08) in &= ~0x08;
            if (m_inputs & 0x02) in &= ~0x10;
            if (m_inputs & 0x40) in &= ~0x20;
            if (m_inputs & 0x80) in &= ~0x40;
            if (m_inputs & 0x20) in &= ~0x80;
        } else if (port == 1) {
            // IN1: A (active low) 01, the menu controller's data 08 and status 10 (active high)
            in = uint8_t(0xe7 | (m_s.mResponseBit << 3) | (m_s.mClock << 4));
            if (m_inputs & 0x10) in &= ~0x01;
        }
        pdata = uint8_t((pdata & ~pinmask) | (in & pinmask));
    }
    return pdata;
}

void Bbl380Core::portOut(int port, uint8_t data, uint8_t)
{
    if (port == 0) {
        // output_w: the SPI transaction resets on every port A write
        flashReset();
    } else if (port == 1) {
        // output2_w: the menu controller
        if ((data & 0x40) != (m_s.output2val & 0x40) && (data & 0x40)) m_s.mShift = 0;
        m_s.mDataBit = uint8_t(bit(data, 3));
        menuClock(bit(data, 2));
        m_s.output2val = data;
    }
}

// ---------------------------------------------------------------------------
// registers (st2302u_device::int_map)
// ---------------------------------------------------------------------------
uint8_t Bbl380Core::regRead(uint8_t r)
{
    switch (r) {
    case 0x00: case 0x01: case 0x02: case 0x03: case 0x04: case 0x05: return pdataR(r);
    case 0x06: return m_s.psel[2];
    case 0x08: case 0x09: case 0x0a: case 0x0b: case 0x0c: case 0x0d: return m_s.pctrl[r - 8];
    case 0x0e: return m_s.pfun[0];
    case 0x0f: return m_s.pfun[1];
    case 0x10: m_s.ssr &= ~0x42; return uint8_t(m_s.sdataRx);
    case 0x11: return uint8_t(m_s.sdataRx >> 8);
    case 0x12: return m_s.sctr;
    case 0x13: return uint8_t(m_s.sckr | 0x80);
    case 0x14: return uint8_t(m_s.ssr | 0x88);
    case 0x15: return uint8_t(m_s.smod | 0xf0);
    case 0x16: return uint8_t(m_s.mul);
    case 0x17: return uint8_t(m_s.mul >> 8);
    case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x25: case 0x26: case 0x27: {
        int t = (r - 0x20) >> 1;
        if (r & 1) return uint8_t((t12Count(t) | (m_s.tc[t] & 0xf000)) >> 8);
        return uint8_t(t12Count(t) & 0xff);
    }
    case 0x28: return m_s.tien;
    case 0x29: return uint8_t(presCount() & 0xff);
    case 0x2a: return m_s.bten;
    case 0x2b: return m_s.btsr;
    case 0x2c: return m_s.btc;
    case 0x2d: return m_s.t4c;
    case 0x2e: return uint8_t((m_s.rctr & 0xe0) | 0x10);
    case 0x30: return uint8_t(m_s.irr);
    case 0x31: return uint8_t(m_s.irr >> 8);
    case 0x32: return uint8_t(m_s.prr);
    case 0x33: return uint8_t(m_s.prr >> 8);
    case 0x34: return uint8_t(m_s.drr);
    case 0x35: return uint8_t(m_s.drr >> 8);
    case 0x38: return m_s.misc;
    case 0x39: return uint8_t(m_s.sys | 0x01);
    case 0x3a: return 0x01;
    case 0x3c: return uint8_t(m_s.ireq);
    case 0x3d: return uint8_t(m_s.ireq >> 8);
    case 0x3e: return uint8_t(m_s.iena);
    case 0x3f: return uint8_t(m_s.iena >> 8);
    case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46: case 0x47: {
        int ch = (r - 0x40) >> 1;
        uint8_t idx = m_s.fifoPos[ch];
        if (r & 1) {
            bool fwra = m_s.fifoFilled[ch] < 8;
            return uint8_t(((m_s.fifo[ch][idx] & 0x100) >> 1) | (fwra ? 0x60 : 0x40) | m_s.fifoFilled[ch]);
        }
        return uint8_t(m_s.fifo[ch][idx]);
    }
    case 0x48: case 0x49: case 0x4a: case 0x4b: return uint8_t(m_s.vol[r - 0x48] | 0x40);
    case 0x4c: return uint8_t(m_s.volm[0] | 0xc0);
    case 0x4d: return uint8_t(m_s.volm[1] | 0x80);
    case 0x4e: return m_s.psgc;
    case 0x4f: return m_s.psgm;
    case 0x58: return uint8_t(m_s.dptr[m_s.dctr]);
    case 0x59: return uint8_t((m_s.dptr[m_s.dctr] >> 8) | 0x80);
    case 0x5a: return uint8_t(m_s.dbkr[m_s.dctr]);
    case 0x5b: return uint8_t((m_s.dbkr[m_s.dctr] >> 8) | 0x78);
    case 0x5c: return uint8_t(m_s.dcnt[m_s.dctr >> 1]);
    case 0x5d: return uint8_t((m_s.dcnt[m_s.dctr >> 1] >> 8) | 0x80);
    case 0x5e: return uint8_t(m_s.dctr | 0xfc);
    case 0x5f: return uint8_t(m_s.dmod[m_s.dctr >> 1] | 0xc0);
    case 0x70: return uint8_t(m_s.usbcon | 0x03);
    case 0x71: return uint8_t(m_s.usbien | 0x40);
    case 0x7b: return 0x08;
    default: return 0;
    }
}

void Bbl380Core::regWrite(uint8_t r, uint8_t d)
{
    switch (r) {
    case 0x00: portOut(0, d, 0xff); break;     // the board's handler replaces the port A data write
    case 0x01: case 0x02: case 0x03: case 0x04: case 0x05:
        if (d != m_s.pdata[r]) { m_s.pdata[r] = d; portOut(r, d, m_s.pctrl[r]); }
        break;
    case 0x06: m_s.psel[2] = d; break;
    case 0x08: case 0x09: case 0x0a: case 0x0b: case 0x0c: case 0x0d: {
        int p = r - 8;
        if (d != m_s.pctrl[p]) { m_s.pctrl[p] = d; portOut(p, m_s.pdata[p], d); }
        break;
    }
    case 0x0e: m_s.pfun[0] = d; break;
    case 0x0f: m_s.pfun[1] = d; break;
    case 0x10:
        m_s.sdataTx = uint16_t((m_s.sdataTx & 0xff00) | d);
        m_s.spiTxPending = 1;
        m_s.ssr &= ~0x20;
        if (bit(m_s.sctr, 7) && bit(m_s.sctr, 0) && !m_s.spiBusy) spiStart();
        break;
    case 0x11: m_s.sdataTx = uint16_t((m_s.sdataTx & 0x00ff) | (d << 8)); break;
    case 0x12: sctrW(d); break;
    case 0x13: m_s.sckr = d & 0x7f; break;
    case 0x14: m_s.ssr = m_s.spiBusy ? 0x10 : 0; break;
    case 0x15: m_s.smod = d & 0x0f; break;
    case 0x16: m_s.mul = uint16_t((int32_t(int16_t(m_s.mul)) * int8_t(d)) >> 8); break;
    case 0x17: m_s.mul = uint16_t((d << 8) | (m_s.mul >> 8)); break;
    case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x25: case 0x26: case 0x27: {
        int t = (r - 0x20) >> 1;
        if (r & 1) m_s.tc[t] = uint16_t((m_s.tc[t] & 0x00ff) | (d << 8));
        else m_s.tc[t] = uint16_t((m_s.tc[t] & 0xff00) | d);
        break;
    }
    case 0x28: tienW(d); break;
    case 0x29: prsW(d); break;
    case 0x2a: btenW(d); break;
    case 0x2b: m_s.btsr &= uint8_t(~d); break;
    case 0x2c: m_s.btc = d; break;
    case 0x2d: m_s.t4c = d; break;
    case 0x2e: m_s.rctr = d & 0xef; break;
    case 0x30: m_s.irr = uint16_t((d & PRR_MASK) | (m_s.irr & 0xff00)); break;
    case 0x31: m_s.irr = uint16_t(((d << 8) & PRR_MASK) | (m_s.irr & 0x00ff)); break;
    case 0x32: m_s.prr = uint16_t((d & PRR_MASK) | (m_s.prr & 0xff00)); break;
    case 0x33: m_s.prr = uint16_t(((d << 8) & PRR_MASK) | (m_s.prr & 0x00ff)); break;
    case 0x34: m_s.drr = uint16_t((d & DRR_MASK) | (m_s.drr & 0xff00)); break;
    case 0x35: m_s.drr = uint16_t(((d << 8) & DRR_MASK) | (m_s.drr & 0x00ff)); break;
    case 0x38: m_s.misc = d & 0x0f; break;
    case 0x39: m_s.sys = d & 0xfe; m_s.irrEnable = uint8_t(bit(d, 1)); break;
    case 0x3c: if (m_s.ireq & ~d & 0x00ff) { m_s.ireq &= uint16_t(d | 0xff00); updateIrq(); } break;
    case 0x3d: if (m_s.ireq & ~(d << 8) & 0xff00) { m_s.ireq &= uint16_t((d << 8) | 0x00ff); updateIrq(); } break;
    case 0x3e: m_s.iena = uint16_t((m_s.iena & 0xff00) | (d & IREQ_MASK & 0xff)); updateIrq(); break;
    case 0x3f: m_s.iena = uint16_t((m_s.iena & 0x00ff) | ((d & (IREQ_MASK >> 8)) << 8)); updateIrq(); break;
    case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46: case 0x47: {
        int ch = (r - 0x40) >> 1;
        if (m_s.fifoFilled[ch] < 16) {
            uint8_t idx = uint8_t((m_s.fifoPos[ch] + m_s.fifoFilled[ch]++) & 15);
            m_s.fifo[ch][idx] = uint16_t(d | ((r & 1) ? 0x100 : 0));
        }
        break;
    }
    case 0x48: case 0x49: case 0x4a: case 0x4b: audioTo(now()); m_s.vol[r - 0x48] = d & 0xbf; break;
    case 0x4c: audioTo(now()); m_s.volm[0] = d & 0x3f; break;
    case 0x4d: audioTo(now()); m_s.volm[1] = d & 0x7f; break;
    case 0x4e: psgcW(d); break;
    case 0x4f: audioTo(now()); m_s.psgm = d; break;
    case 0x58: m_s.dptr[m_s.dctr] = uint16_t((m_s.dptr[m_s.dctr] & 0x7f00) | d); break;
    case 0x59: m_s.dptr[m_s.dctr] = uint16_t(((d & 0x7f) << 8) | (m_s.dptr[m_s.dctr] & 0x00ff)); break;
    case 0x5a: m_s.dbkr[m_s.dctr] = uint16_t((m_s.dbkr[m_s.dctr] & 0x8700) | d); break;
    case 0x5b: m_s.dbkr[m_s.dctr] = uint16_t(((d & 0x87) << 8) | (m_s.dbkr[m_s.dctr] & 0x00ff)); break;
    case 0x5c: m_s.dcnt[m_s.dctr >> 1] = uint16_t((m_s.dcnt[m_s.dctr >> 1] & 0x7f00) | d); break;
    case 0x5d: dmaStart(d); break;
    case 0x5e: m_s.dctr = d & 0x03; break;
    case 0x5f: m_s.dmod[m_s.dctr >> 1] = d & 0x3f; break;
    case 0x70: m_s.usbcon = d & 0xfc; break;
    case 0x71: m_s.usbien = d & 0xbf; break;
    default: break;
    }
}

// ---------------------------------------------------------------------------
// base timer, prescaler, 12-bit timers
// ---------------------------------------------------------------------------
unsigned Bbl380Core::btDivider(int n) const
{
    if (n == 0) return 16384;
    if (n <= 5) return 2048u >> n;
    if (n == 6) return 16;
    return 4u * (m_s.btc ? m_s.btc : 256u);
}

void Bbl380Core::btenW(uint8_t d)
{
    for (int n = 0; n < 8; n++) {
        if (bit(d, n) && !bit(m_s.bten, n)) { m_s.btAt[n] = now() + btDivider(n) * CLOCK / 32768.0; if (s_running) m_cpu.yield(); }
        else if (!bit(d, n) && bit(m_s.bten, n)) m_s.btAt[n] = NEVER;
    }
    m_s.bten = d;
}

void Bbl380Core::btFire(int n)
{
    bool irq = m_s.btsr == 0;
    m_s.btsr |= uint8_t(1 << n);
    m_s.btAt[n] += btDivider(n) * CLOCK / 32768.0;
    if (irq) { m_s.ireq |= 0x0040; updateIrq(); }
}

uint16_t Bbl380Core::presCount() const
{
    return uint16_t(m_s.presBase + (((m_s.prs & 0x60) == 0x40) ? int64_t(now() - m_s.presStarted) : 0));
}

static const uint32_t TCLK_DIV[6] = { 2, 4, 8, 32, 1024, 4096 };

void Bbl380Core::tclkStart()
{
    for (int t = 0; t < 4; t++)
        if (bit(m_s.tien, t) && (m_s.tc[t] & 0x7000) < 0x6000) t12StartTclk(t);
}

void Bbl380Core::prsW(uint8_t d)
{
    d &= 0xc0;
    if (bit(d, 7)) {
        // st2xxx_tclk_stop
        for (int t = 0; t < 4; t++)
            if (bit(m_s.tien, t) && (m_s.tc[t] & 0x7000) < 0x6000) { m_s.count[t] = t12Count(t); m_s.tAt[t] = NEVER; }
        m_s.presBase = 0;
        if ((m_s.prs & 0x60) == 0x40) { m_s.presStarted = now(); tclkStart(); }
        d &= 0x7f;
    }
    if ((d & 0x60) == 0x40 && (m_s.prs & 0x60) != 0x40) { m_s.presStarted = now(); tclkStart(); }
    m_s.prs = d;
}

uint16_t Bbl380Core::t12Count(int t) const
{
    uint16_t count = m_s.count[t];
    if (bit(m_s.tien, t) && m_s.tAt[t] < NEVER) {
        uint8_t tck = uint8_t((m_s.tc[t] & 0x7000) >> 12);
        double rem = m_s.tAt[t] - now();
        if (rem < 0) rem = 0;
        if (bit(m_s.prs, 6) && tck < 6) count = uint16_t(0x0fff - uint32_t(int64_t(rem) / TCLK_DIV[tck]));
        else if (tck == 7 && (t & 1)) count = uint16_t(0x0fff - uint32_t(rem * 32768.0 / CLOCK));
    }
    return count & 0x0fff;
}

void Bbl380Core::t12StartTclk(int t)
{
    uint32_t div = TCLK_DIV[(m_s.tc[t] & 0x7000) >> 12];
    m_s.tAt[t] = now() + double((0x0fff - m_s.count[t]) * div + div - (presCount() & (div - 1)));
    if (s_running) m_cpu.yield();
}

void Bbl380Core::tienW(uint8_t d)
{
    for (int t = 0; t < 4; t++) {
        if (bit(m_s.tien, t) && !bit(d, t)) {
            m_s.count[t] = t12Count(t);
            m_s.tAt[t] = NEVER;
        } else if (!bit(m_s.tien, t) && bit(d, t)) {
            m_s.count[t] = m_s.tc[t] & 0x0fff;
            uint8_t tck = uint8_t((m_s.tc[t] & 0x7000) >> 12);
            if (bit(m_s.prs, 6) && tck < 6) t12StartTclk(t);
            else if (tck == 7 && (t & 1)) { m_s.tAt[t] = now() + (0x1000 - m_s.count[t]) * CLOCK / 32768.0; if (s_running) m_cpu.yield(); }
        }
    }
    m_s.tien = d;
}

void Bbl380Core::t12Process(int t)
{
    double at = m_s.tAt[t];
    if (bit(m_s.psgc, t + 4)) {
        uint8_t mode = uint8_t((m_s.psgm >> (2 * t)) & 3);
        if (mode == 1) {
            audioTo(at);
            m_s.psgOn ^= uint8_t(1 << t);
            if (m_s.fifoFilled[t]) {
                uint16_t v = m_s.fifo[t][m_s.fifoPos[t]];
                m_s.adpcm[t] = 0;
                m_s.amp[t] = uint8_t(v);
                m_s.fifoFilled[t]--;
                m_s.fifoPos[t] = uint8_t((m_s.fifoPos[t] + 1) & 15);
            }
            m_s.out[t] = int16_t((bit(m_s.psgOn, t) ? 2 : -2) * m_s.amp[t]);
        } else if (m_s.fifoFilled[t]) {
            audioTo(at);
            m_s.psgOn &= uint8_t(~(1 << t));
            uint16_t v = m_s.fifo[t][m_s.fifoPos[t]];
            if (mode == 3) {
                int delta = v & 0xff;
                m_s.adpcm[t] = int16_t(std::clamp(m_s.adpcm[t] + (bit(v, 8) ? -delta : delta), -0x200, 0x1ff));
                m_s.out[t] = m_s.adpcm[t];
            } else if (mode == 0) {
                m_s.adpcm[t] = 0;
                m_s.out[t] = int16_t(int8_t(v & 0xff) * 4);
            } else m_s.out[t] = 0;
            m_s.fifoFilled[t]--;
            m_s.fifoPos[t] = uint8_t((m_s.fifoPos[t] + 1) & 15);
        }
    }
    if (m_s.fifoFilled[t] < 8) { m_s.ireq |= uint16_t(0x0002 << t); updateIrq(); }
    m_s.count[t] = bit(m_s.tc[t], 15) ? (m_s.tc[t] & 0x0fff) : 0;
    uint8_t tck = uint8_t((m_s.tc[t] & 0x7000) >> 12);
    if (tck < 6) m_s.tAt[t] = at + double((0x1000 - m_s.count[t]) * TCLK_DIV[tck]);
    else if (tck == 7 && (t & 1)) m_s.tAt[t] = at + (0x1000 - m_s.count[t]) * CLOCK / 32768.0;
    else m_s.tAt[t] = NEVER;
}

void Bbl380Core::psgcW(uint8_t d)
{
    audioTo(now());
    uint8_t disabled = uint8_t(((m_s.psgc & ~d) >> 4) & 0x0f);
    for (int c = 0; c < 4; c++)
        if (bit(disabled, c)) { m_s.adpcm[c] = 0; m_s.amp[c] = 0; m_s.out[c] = 0; }
    m_s.psgc = d;
    m_s.psgOn &= uint8_t((d & 0xf0) >> 4);
}

// ---------------------------------------------------------------------------
// SPI
// ---------------------------------------------------------------------------
void Bbl380Core::sctrW(uint8_t d)
{
    if (!bit(m_s.sctr, 7) && bit(d, 7)) {
        m_s.spiAt = NEVER;
        m_s.sdataTx = m_s.sdataRx = m_s.spiPendingRx = 0;
        m_s.spiBusy = 0; m_s.spiTxPending = 0;
        m_s.ssr = 0x20;
    } else if (bit(m_s.sctr, 7) && !bit(d, 7)) {
        m_s.spiAt = NEVER;
        m_s.spiPendingRx = 0;
        m_s.spiBusy = 0; m_s.spiTxPending = 0;
        m_s.ssr &= ~0x10;
    }
    m_s.sctr = d;
    if (bit(m_s.sctr, 7) && bit(m_s.sctr, 0) && !m_s.spiBusy && m_s.spiTxPending) spiStart();
}

void Bbl380Core::spiStart()
{
    uint8_t bits = uint8_t((m_s.sckr & 0x0f) + 1);
    uint16_t mask = bits == 16 ? 0xffff : uint16_t((1u << bits) - 1);
    m_s.spiPendingRx = uint16_t(spiExchange(uint16_t(m_s.sdataTx & mask), bits) & mask);
    m_s.spiTxPending = 0;
    m_s.spiBusy = 1;
    m_s.ssr |= 0x30;
    m_s.ireq |= 0x0100;
    updateIrq();
    unsigned clocks = bits * (2u << ((m_s.sckr >> 4) & 7));
    m_s.spiAt = now() + clocks;
    if (s_running) m_cpu.yield();
}

void Bbl380Core::spiComplete()
{
    m_s.spiAt = NEVER;
    m_s.spiBusy = 0;
    m_s.ssr &= ~0x10;
    bool overrun = bit(m_s.ssr, 6);
    if (overrun) m_s.ssr |= 0x02;
    m_s.sdataRx = m_s.spiPendingRx;
    m_s.ssr |= 0x40;
    if (bit(m_s.sctr, 6) || (overrun && bit(m_s.sctr, 5))) { m_s.ireq |= 0x0200; updateIrq(); }
    if (bit(m_s.sctr, 7) && bit(m_s.sctr, 0) && m_s.spiTxPending) spiStart();
}

// ---------------------------------------------------------------------------
// DMA (st2205u_base_device::dcnth_w)
// ---------------------------------------------------------------------------
void Bbl380Core::dmaStart(uint8_t d)
{
    int ch = m_s.dctr >> 1;
    m_s.dcnt[ch] = uint16_t(((d & 0x7f) << 8) | (m_s.dcnt[ch] & 0x00ff));
    uint16_t srcp = m_s.dptr[m_s.dctr & 2], srcb = m_s.dbkr[m_s.dctr & 2];
    uint16_t dstp = m_s.dptr[m_s.dctr | 1], dstb = m_s.dbkr[m_s.dctr | 1];
    uint32_t count = m_s.dcnt[ch] + 1u;
    uint8_t mode = m_s.dmod[ch];
    while (count-- != 0) {
        uint8_t v = bit(srcb, 15) ? read(srcp) : dataRead(srcp | (uint32_t(srcb) << 15));
        if (!bit(mode, 1)) { if (srcp++ == 0x7fff) { srcp = 0; srcb++; } }
        if (bit(dstb, 15)) write(dstp, v);
        else dataWrite(dstp | (uint32_t(dstb) << 15), v);
        if (!bit(mode, 3)) { if (dstp++ == 0x7fff) { dstp = 0; dstb++; } }
    }
    if ((mode & 0x03) == 0) { m_s.dptr[m_s.dctr & 2] = srcp; m_s.dbkr[m_s.dctr & 2] = srcb & 0x87ff; }
    if ((mode & 0x0c) == 0) { m_s.dptr[m_s.dctr | 1] = dstp; m_s.dbkr[m_s.dctr | 1] = dstb & 0x87ff; }
}

// ---------------------------------------------------------------------------
// board: SPI flash (generic_spi_flash), menu controller, ST7735
// ---------------------------------------------------------------------------
uint16_t Bbl380Core::spiExchange(uint16_t data, uint8_t bits)
{
    uint16_t result = 0xffff;
    if (bits == 8 || bits == 16) {
        result = 0;
        for (int shift = bits - 8; shift >= 0; shift -= 8) {
            flashWrite(uint8_t(data >> shift));
            result = uint16_t((result << 8) | m_s.fLatch);
        }
    }
    return result;
}

void Bbl380Core::flashReset()
{
    m_s.fAddr = 0; m_s.fState = 0; m_s.fLatch = 0; m_s.fStep = 0;
}

void Bbl380Core::flashCommand(uint8_t d)
{
    switch (d) {
    case 0x01: case 0x03: case 0x05: case 0x0b: case 0x02: case 0x11: case 0x15: case 0x20:
    case 0x31: case 0x35: case 0x90: case 0x9f: case 0xeb: case 0xec:
        m_s.fState = d; break;
    case 0x06: m_s.fState = 0; m_s.fStatus |= 0x02; break;
    case 0x04: m_s.fState = 0; m_s.fStatus &= ~0x02; break;
    default: m_s.fState = 0; break;       // 50 66 99 AB B9 FF and anything unknown
    }
    m_s.fStep = 0;
}

void Bbl380Core::flashWrite(uint8_t d)
{
    const uint32_t lenMask = uint32_t(m_flash.size() - 1);
    auto addrStep = [&](int maxStep) -> bool {
        // the three address bytes, then any dummy bytes; true once data flows
        if (m_s.fStep == 0) { m_s.fAddr = (m_s.fAddr & 0x00ffff) | (uint32_t(d) << 16); m_s.fStep++; return false; }
        if (m_s.fStep == 1) { m_s.fAddr = (m_s.fAddr & 0xff00ff) | (uint32_t(d) << 8); m_s.fStep++; return false; }
        if (m_s.fStep == 2) { m_s.fAddr = (m_s.fAddr & 0xffff00) | d; m_s.fStep++; return false; }
        if (m_s.fStep < maxStep) { m_s.fStep++; return false; }
        return true;
    };
    switch (m_s.fState) {
    case 0x00: flashCommand(d); break;
    case 0x01:
        if (m_s.fStep == 0) { m_s.fConfig = d; m_s.fStep++; }
        else { m_s.fStatus = d; m_s.fState = 0; }
        break;
    case 0x02: if (addrStep(3) && (m_s.fStatus & 0x02)) m_flash[(m_s.fAddr++) & lenMask] = d; break;
    case 0x03: if (addrStep(3)) m_s.fLatch = m_flash[(m_s.fAddr++) & lenMask]; break;
    case 0x0b: if (addrStep(4)) m_s.fLatch = m_flash[(m_s.fAddr++) & lenMask]; break;
    case 0xeb: if (addrStep(6)) m_s.fLatch = m_flash[(m_s.fAddr++) & lenMask]; break;
    case 0x20: addrStep(3); break;
    case 0x05:
        if (m_s.fStep == 0) { m_s.fLatch = m_s.fStatus; m_s.fStep++; }
        else { m_s.fLatch = 0; m_s.fState = 0; }
        break;
    case 0x15: if (m_s.fStep == 0) { m_s.fLatch = m_s.fConfig; m_s.fState = 0; } break;
    case 0x35: m_s.fLatch = m_s.fStatus; m_s.fState = 0; break;
    case 0x90: if (m_s.fStep < 4) m_s.fStep++; else m_s.fState = 0; break;
    case 0x9f:
        if (m_s.fStep == 0) { m_s.fLatch = 0xfe; m_s.fStep++; }
        else if (m_s.fStep == 1) { m_s.fLatch = 0xfe; m_s.fStep++; }
        else m_s.fLatch = 0x00;
        break;
    default: break;
    }
}

void Bbl380Core::menuClock(int state)
{
    if (state) { m_s.mClock = 1; return; }
    m_s.mClock = 0;
    m_s.mCommand = uint8_t((m_s.mCommand << 1) | m_s.mDataBit);
    m_s.mResponseBit = uint8_t((m_s.mResponse >> (7 - m_s.mShift)) & 1);
    if (++m_s.mShift == 8) { m_s.mShift = 0; menuCommand(); }
}

void Bbl380Core::menuCommand()
{
    uint8_t c = m_s.mCommand;
    enum { READY = 0, C00, C01, C02, C03, C04, C05 };
    switch (m_s.mState) {
    case READY:
        if (c == 0x00) { m_s.mState = C00; m_s.mResponse = uint8_t(m_s.mPos); }
        else if (c == 0x01) { m_s.mState = C01; m_s.mResponse = uint8_t(m_s.mPos >> 8); }
        else if (c == 0x02) m_s.mState = C02;
        else if (c == 0x03) m_s.mState = C03;
        else if (c == 0x04) m_s.mState = C04;
        else if (c == 0x05) m_s.mState = C05;
        else if (c == 0x10) m_s.mPos = 0;
        else if (c == 0x30) m_s.mPos++;
        else if (c == 0x37) m_s.mPos--;
        else if (c == 0x39) m_s.mPos = uint16_t(m_s.mPos - 4);
        else if (c == 0x2c) m_s.mPos = 1;
        break;
    case C00: case C01: m_s.mState = READY; break;
    case C02: m_s.mPos = uint16_t((m_s.mPos & 0xff00) | ((c - 0x8) & 0xff)); m_s.mState = READY; break;
    case C03: m_s.mPos = uint16_t((m_s.mPos & 0x00ff) | (((c - 0x9) & 0xff) << 8)); m_s.mState = READY; break;
    case C04: m_s.mPos = uint16_t(m_s.mPos + (c - 0x09)); m_s.mState = READY; break;
    case C05:
        if (c != 0xf0) m_s.mPos = uint16_t(m_s.mPos - (c - 0x0b));
        else m_s.mPos = uint16_t(m_s.mPos - (c - 0x0a));
        m_s.mState = READY;
        break;
    }
}

void Bbl380Core::lcdCommand(uint8_t d)
{
    m_s.lcmd = d;
    m_s.lstep = 0;
    if (d == 0x10) m_s.lsleep = 1;
    else if (d == 0x11) m_s.lsleep = 0;
    else if (d == 0x29) m_s.lon = 1;
}

void Bbl380Core::lcdData(uint8_t d)
{
    switch (m_s.lcmd) {
    case 0x2b:
        switch (m_s.lstep) {
        case 0: m_s.lminy = uint16_t((d << 8) | (m_s.lminy & 0xff)); break;
        case 1: m_s.lminy = uint16_t((m_s.lminy & 0xff00) | d); break;
        case 2: m_s.lmaxy = uint16_t((d << 8) | (m_s.lmaxy & 0xff)); break;
        case 3: m_s.lmaxy = uint16_t((m_s.lmaxy & 0xff00) | d); break;
        }
        m_s.ly = m_s.lminy;
        break;
    case 0x2a:
        switch (m_s.lstep) {
        case 0: m_s.lminx = uint16_t((d << 8) | (m_s.lminx & 0xff)); break;
        case 1: m_s.lminx = uint16_t((m_s.lminx & 0xff00) | d); break;
        case 2: m_s.lmaxx = uint16_t((d << 8) | (m_s.lmaxx & 0xff)); break;
        case 3: m_s.lmaxx = uint16_t((m_s.lmaxx & 0xff00) | d); break;
        }
        m_s.lx = m_s.lminx;
        break;
    case 0x2c:
        m_s.latch = uint16_t((m_s.latch << 8) | d);
        if (m_s.lstep & 1) {
            if (bit(m_s.madctl, 5)) m_lcd[(m_s.lx + m_s.ly * 0x200) & 0xffff] = m_s.latch;
            else m_lcd[(m_s.ly + (129 - m_s.lx) * 0x200) & 0xffff] = m_s.latch;
            m_s.lx++;
            if (m_s.lx > m_s.lmaxx) {
                m_s.lx = m_s.lminx;
                m_s.ly++;
                if (m_s.ly > m_s.lmaxy) m_s.ly = m_s.lminy;
            }
        }
        break;
    case 0x36: if (m_s.lstep == 0) m_s.madctl = d; break;
    default: break;
    }
    m_s.lstep++;
}

// ---------------------------------------------------------------------------
// frame loop
// ---------------------------------------------------------------------------
void Bbl380Core::runFrame()
{
    const double frameEnd = double(m_s.cycles) + CLOCK / 60.0;
    while (double(m_s.cycles) < frameEnd) {
        double next = frameEnd;
        for (double t : m_s.btAt) next = std::min(next, t);
        for (double t : m_s.tAt) next = std::min(next, t);
        next = std::min(next, m_s.spiAt);
        int64_t want = int64_t(std::ceil(next - double(m_s.cycles)));
        if (want < 1) want = 1;
        if (want > 4096) want = 4096;      // keep register reads of the timers close to time
        s_running = true;
        int ran = m_cpu.run(int(want));
        s_running = false;
        m_s.cycles += ran;
        // due events, earliest first
        for (;;) {
            double t = NEVER; int kind = -1, idx = 0;
            for (int n = 0; n < 8; n++) if (m_s.btAt[n] < t) { t = m_s.btAt[n]; kind = 0; idx = n; }
            for (int n = 0; n < 4; n++) if (m_s.tAt[n] < t) { t = m_s.tAt[n]; kind = 1; idx = n; }
            if (m_s.spiAt < t) { t = m_s.spiAt; kind = 2; }
            if (kind < 0 || t > double(m_s.cycles)) break;
            if (kind == 0) btFire(idx);
            else if (kind == 1) t12Process(idx);
            else spiComplete();
        }
    }
    // the LCD
    if (m_s.lon && !m_s.lsleep) {
        for (int y = 0; y < 128; y++)
            for (int x = 0; x < 160; x++) {
                uint16_t v = m_lcd[(y * 0x200 + x) & 0xffff];
                int b = (v & 0x1f) << 3, g = ((v >> 5) & 0x3f) << 2, r = ((v >> 11) & 0x1f) << 3;
                m_frame[y * 160 + x] = 0xff000000u | uint32_t(r << 16) | uint32_t(g << 8) | uint32_t(b);
            }
    } else std::fill(std::begin(m_frame), std::end(m_frame), 0xff000000u);
    audioTo(double(m_s.cycles));
}

void Bbl380Core::audioTo(double cycle)
{
    double level = 0;
    if (!bit(m_s.psgc, 0) && ((m_s.psgc >> 1) & 3) != 3)
        for (int c = 0; c < 4; c++)
            if (bit(m_s.psgc, c + 4)) level += double(m_s.out[c] * (m_s.vol[c] & 0x3f)) / (0x200 * 0x3f * 4);
    const double step = 44100.0 / CLOCK;
    double n = cycle - m_audioLast;
    if (n <= 0) return;
    m_audioLast = cycle;
    double ph = m_audioPhase + n * step;
    if (ph < 1.0) { m_audioAcc += level * n * step; m_audioPhase = ph; return; }
    m_audioAcc += level * (1.0 - m_audioPhase);
    auto emit = [&](double v) {
        m_dc += (v - m_dc) * 0.0005;
        double o = (v - m_dc) * 32767.0;
        o = std::max(-32768.0, std::min(32767.0, o));
        if (m_audio.size() < 44100) m_audio.push_back(int16_t(o));
    };
    emit(m_audioAcc);
    ph -= 1.0;
    while (ph >= 1.0) { emit(level); ph -= 1.0; }
    m_audioAcc = level * ph;
    m_audioPhase = ph;
}

size_t Bbl380Core::readAudio(int16_t* out, size_t maxSamples)
{
    size_t n = std::min(maxSamples, m_audio.size());
    std::memcpy(out, m_audio.data(), n * 2);
    m_audio.erase(m_audio.begin(), m_audio.begin() + long(n));
    return n;
}

// ---------------------------------------------------------------------------
// save states
// ---------------------------------------------------------------------------
bool Bbl380Core::saveState(std::vector<uint8_t>& out)
{
    out.clear();
    auto put = [&](const void* p, size_t n) { out.insert(out.end(), (const uint8_t*)p, (const uint8_t*)p + n); };
    uint32_t magic = 0x31424242;   // 'BBB1'
    put(&magic, 4);
    put(&m_s, sizeof(m_s));
    put(m_lcd.data(), m_lcd.size() * 2);
    put(&m_cpu.pc, 2); put(&m_cpu.a, 1); put(&m_cpu.x, 1); put(&m_cpu.y, 1); put(&m_cpu.s, 1); put(&m_cpu.p, 1);
    uint8_t f[4] = { m_cpu.waiting, m_cpu.stopped, m_cpu.m_irqLine, m_cpu.m_nmiPending };
    put(f, 4);
    return true;
}

bool Bbl380Core::loadState(const std::vector<uint8_t>& in)
{
    size_t need = 4 + sizeof(m_s) + m_lcd.size() * 2 + 7 + 4;
    if (in.size() != need) return false;
    uint32_t magic; std::memcpy(&magic, in.data(), 4);
    if (magic != 0x31424242) return false;
    const uint8_t* p = in.data() + 4;
    std::memcpy(&m_s, p, sizeof(m_s)); p += sizeof(m_s);
    std::memcpy(m_lcd.data(), p, m_lcd.size() * 2); p += m_lcd.size() * 2;
    std::memcpy(&m_cpu.pc, p, 2); p += 2;
    m_cpu.a = *p++; m_cpu.x = *p++; m_cpu.y = *p++; m_cpu.s = *p++; m_cpu.p = *p++;
    m_cpu.waiting = p[0]; m_cpu.stopped = p[1]; m_cpu.m_irqLine = p[2]; m_cpu.m_nmiPending = p[3];
    m_audioLast = double(m_s.cycles);
    return true;
}

