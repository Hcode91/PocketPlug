// PocketPlug - Tiger Telematics Gizmondo. See gizmondo.h.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// S3C2440 peripherals follow Samsung's S3C2440A user manual; the board wiring
// (keypad matrix, boot/test strap, DiskOnChip and GoForce placement) follows
// MAME's gizmondo.cpp (Tim Schuerewegen, BSD-3-Clause).

#include "gizmondo.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace {
int dbg() { static int v = getenv("GIZDBG") ? atoi(getenv("GIZDBG")) : 0; return v; }
uint32_t rd32(const std::vector<uint8_t>& d, size_t o) { uint32_t v; std::memcpy(&v, &d[o], 4); return v; }
uint8_t bcd(int v) { return uint8_t(((v / 10) << 4) | (v % 10)); }

// Interrupt sources (SRCPND bits)
enum { INT_EINT0 = 0, INT_EINT4_7 = 4, INT_EINT8_23 = 5, INT_TICK = 8, INT_WDT_AC97 = 9, INT_TIMER0 = 10,
       INT_UART2 = 15, INT_UART1 = 23, INT_UART0 = 28, INT_RTC = 30, INT_ADC = 31 };
// Sub-sources (SUBSRCPND bits)
enum { SUB_RXD0 = 0, SUB_TXD0 = 1, SUB_TXD1 = 4, SUB_TXD2 = 7, SUB_TC = 9, SUB_ADC = 10, SUB_WDT = 13 };

// Pulls the firmware files out of an official Gizmondo update package (install.bin):
// every byte is inverted, then a table of (record length, UTF-16 name, size, offset).
bool unpackInstall(const std::vector<uint8_t>& raw, const std::string& want, std::vector<uint8_t>& out)
{
    if (raw.size() < 64) return false;
    auto b = [&](size_t i) { return uint8_t(~raw[i]); };
    auto u32 = [&](size_t i) { return uint32_t(b(i) | b(i + 1) << 8 | b(i + 2) << 16 | uint32_t(b(i + 3)) << 24); };
    size_t pos = 7, firstData = 0;
    while (firstData == 0 || pos < firstData) {
        if (pos + 12 > raw.size()) return false;
        uint32_t rl = u32(pos);
        if (rl < 12 || rl > 1024 || pos + rl > raw.size()) return false;
        std::string name;
        for (size_t i = pos + 4; i + 1 < pos + rl - 8; i += 2) {
            uint16_t c = uint16_t(b(i) | b(i + 1) << 8);
            if (!c) break;
            name += char(c < 128 ? c : '?');
        }
        uint32_t size = u32(pos + rl - 8), off = u32(pos + rl - 4);
        if (!firstData) firstData = off;
        if (name == want) {
            if (size_t(off) + size > raw.size()) return false;
            out.resize(size);
            for (uint32_t i = 0; i < size; i++) out[i] = uint8_t(~raw[off + i]);
            return true;
        }
        pos += rl;
    }
    return false;
}
}

GizmondoCore::GizmondoCore() : m_ram(RAM_SIZE, 0), m_fb(320 * 240, 0xff000000u) {}
GizmondoCore::~GizmondoCore() { delete m_cpu; }

bool GizmondoCore::load(const std::vector<RomFile>& roms, std::string& error)
{
    std::vector<uint8_t> eboot;
    for (auto& r : roms) {
        std::string n = r.name;
        std::transform(n.begin(), n.end(), n.begin(), ::tolower);
        if (n == "fboot.bin" || r.data.size() == 0x800) m_fboot = r.data;
        else if (n == "eboot2.nb0" || n == "eboot.nb0") eboot = r.data;
        else if (eboot.empty() && r.data.size() > 0x100000) unpackInstall(r.data, "OS\\EBOOT2.NB0", eboot);
    }
    if (m_fboot.size() != 0x800) { error = "Missing fboot.bin (the 2 KB boot ROM from MAME's gizmondo set)"; return false; }
    if (eboot.empty()) { error = "Missing install.bin (an official Gizmondo firmware update package)"; return false; }
    if (!buildDoc(eboot, error)) return false;
    delete m_cpu;
    m_cpu = new didj::ARM926(this);
    reset();
    return true;
}

// Lays the bootloader out the way fboot looks for it: from block 6 on, each block's
// even pages hold 512 bytes of the image in both planes, and the first four bytes of
// every page's spare area are the partition tag "BOOT". EBOOT2.NB0 in the update
// package already stores every 512-byte page twice (once per plane), so a 512 KB
// file holds a 256 KB image.
bool GizmondoCore::buildDoc(const std::vector<uint8_t>& nb0, std::string& error)
{
    std::vector<uint8_t> img;
    bool doubled = nb0.size() >= 0x400 && nb0.size() % 0x400 == 0;
    for (size_t i = 0; doubled && i < nb0.size(); i += 0x400)
        if (std::memcmp(&nb0[i], &nb0[i + 0x200], 0x200)) doubled = false;
    if (doubled) {
        for (size_t i = 0; i < nb0.size(); i += 0x400) img.insert(img.end(), nb0.begin() + long(i), nb0.begin() + long(i + 0x200));
    } else img = nb0;
    if (img.size() < 0x1000) { error = "The bootloader image is too small"; return false; }
    img.resize(0x40000, 0);              // fboot copies 512 pages = 256 KB
    m_doc.erase();
    uint32_t block = 6;
    for (uint32_t done = 0; done < img.size(); block++)
        for (uint32_t pg = 0; pg < DocG3::PAGES && done < img.size(); pg += 2, done += DocG3::DATA)
            for (uint32_t pl = 0; pl < DocG3::PLANES; pl++) {
                uint8_t* p = m_doc.page(pl, block, pg);
                std::memcpy(p, &img[done], DocG3::DATA);
                std::memcpy(p + DocG3::DATA, "BOOT", 4);
            }
    if (dbg()) fprintf(stderr, "DoC: boot partition in blocks 6-%u\n", block - 1);
    return true;
}

void GizmondoCore::reset()
{
    std::fill(m_ram.begin(), m_ram.end(), 0);
    std::fill(m_sram.begin(), m_sram.end(), 0);
    std::fill(m_gf.begin(), m_gf.end(), 0u);
    m_gfx = m_gfy = m_gfxMax = m_gfyMax = m_gfxMin = m_gfyMin = 0;
    m_regs.clear();
    m_srcpnd = m_intmod = m_intpnd = m_intoffset = m_subsrcpnd = 0;
    m_intmsk = 0xffffffff; m_intsubmsk = 0x7fff;
    m_eintpend = 0; m_eintmask = 0xfffff0;
    for (auto& t : m_t) t = Timer();
    m_tcfg0 = m_tcfg1 = m_tcon = 0;
    std::memset(m_gpdat, 0, sizeof m_gpdat);
    // as MAME's machine_start: the board's idle levels on ports B, C, D
    m_gpdat[1] = 0x055e; m_gpdat[2] = 0x5f20; m_gpdat[3] = 0x4f60;
    // clocks at reset: FCLK = 12 MHz crystal until the PLL is set
    reg(0x4c000004) = 0x0005c080; reg(0x4c000008) = 0x00028080; reg(0x4c00000c) = 0x7fff0; reg(0x4c000014) = 4;
    reg(0x560000b0) = 0x32440001;      // GSTATUS1: S3C2440A
    reg(0x560000b4) = 1;               // GSTATUS2: power-on reset
    m_irqOut = false;
    serialLog.clear();
    m_cpu->reset();
    m_idle = 0; m_lastSync = m_cpu->insn_count;
    m_cpu->set_irq(false);
    m_cpu->r[15] = 0;
    if (getenv("GIZTRACE")) {          // debugging: print every instruction's address
        m_cpu->trace_user = this;
        m_cpu->trace_hook = [](didj::ARM926* c, uint32_t pc, void*) {
            static long n = atol(getenv("GIZTRACE"));
            if (n-- > 0) fprintf(stderr, "T %08x r0 %08x r1 %08x r3 %08x sp %08x lr %08x\n", pc, c->r[0], c->r[1], c->r[3], c->r[13], c->r[14]);
        };
    }
}

// ---- interrupts ----

void GizmondoCore::setIrq(int n, bool on)
{
    if (on) m_srcpnd |= 1u << n;
    updateIrq();
}

void GizmondoCore::setSubIrq(int n)
{
    m_subsrcpnd |= 1u << n;
    int parent = n <= 2 ? INT_UART0 : n <= 5 ? INT_UART1 : n <= 8 ? INT_UART2 : n <= 10 ? INT_ADC : INT_WDT_AC97;
    if (!(m_intsubmsk & (1u << n))) m_srcpnd |= 1u << parent;
    updateIrq();
}

void GizmondoCore::eint(int n)
{
    if (n < 4) { m_srcpnd |= 1u << n; updateIrq(); return; }
    m_eintpend |= 1u << n;
    if (!(m_eintmask & (1u << n))) m_srcpnd |= 1u << (n < 8 ? INT_EINT4_7 : INT_EINT8_23);
    updateIrq();
}

void GizmondoCore::updateIrq()
{
    // the highest-priority pending interrupt is latched into INTPND (fixed order: lowest bit first)
    uint32_t pend = m_srcpnd & ~m_intmsk & ~m_intmod;
    if (!m_intpnd && pend) {
        int n = __builtin_ctz(pend);
        m_intpnd = 1u << n;
        m_intoffset = uint32_t(n);
    }
    m_irqOut = m_intpnd != 0;
    m_cpu->set_irq(m_irqOut);
    m_cpu->set_fiq((m_srcpnd & ~m_intmsk & m_intmod) != 0);
}

// ---- clocks and timers ----

uint32_t GizmondoCore::fclk() const
{
    auto it = m_regs.find(0x4c000004);
    uint32_t v = it == m_regs.end() ? 0 : it->second;
    uint32_t m = ((v >> 12) & 0xff) + 8, p = ((v >> 4) & 0x3f) + 2, s = v & 3;
    uint64_t f = 2ull * m * 12000000ull / (uint64_t(p) << s);
    return f ? uint32_t(f) : 12000000u;
}

uint32_t GizmondoCore::pclk() const
{
    auto it = m_regs.find(0x4c000014);
    uint32_t d = it == m_regs.end() ? 0 : it->second;
    static const uint32_t hdiv[4] = { 1, 2, 4, 3 };
    uint32_t h = fclk() / hdiv[(d >> 1) & 3];
    return (d & 1) ? h / 2 : h;
}

void GizmondoCore::tickTimers(uint64_t cpuCycles)
{
    const uint32_t f = fclk(), p = pclk();
    for (int i = 0; i < 5; i++) {
        Timer& t = m_t[i];
        if (!t.on) continue;
        uint32_t pre = (i < 2 ? m_tcfg0 : m_tcfg0 >> 8) & 0xff;
        uint32_t mux = (m_tcfg1 >> (i * 4)) & 0xf;
        uint32_t div = 2u << std::min<uint32_t>(mux, 3);
        uint64_t rate = uint64_t(p) / ((pre + 1) * div);    // timer ticks per second
        if (!rate) continue;
        t.acc += cpuCycles * rate;
        uint64_t ticks = t.acc / f;
        t.acc %= f;
        while (ticks) {
            if (ticks <= t.cnt) { t.cnt -= uint32_t(ticks); break; }
            ticks -= t.cnt + 1;
            setIrq(INT_TIMER0 + i, true);
            if (t.reload) t.cnt = t.cntb;
            else { t.cnt = 0; t.on = false; break; }
            if (!t.cntb) break;
        }
    }
}

// ---- GPIO: keypad matrix and straps (after MAME's gizmondo_state::gpio_port_r) ----

uint32_t GizmondoCore::gpioRead(int port)
{
    uint32_t d = m_gpdat[port];
    switch (port) {
    case 3:     // D: bit 4 = 1 boots "BOOT" (0 = factory test)
        d |= 1u << 4;
        break;
    case 5: {   // F: key lines (active low), rows on port C bits 0-4
        uint32_t c = m_gpdat[2];
        d = (d & ~0xf2u) | 0xf2u;
        auto row = [&](int bit, uint32_t lines) { if (!(c & (1u << bit))) d &= ~lines; };
        uint32_t k = m_keys;
        row(0, ((k & STOP) ? 0x20u : 0) | ((k & UP) ? 0x40u : 0));
        row(1, ((k & F2) ? 0x10u : 0) | ((k & FORWARD) ? 0x20u : 0) | ((k & RIGHT) ? 0x40u : 0));
        row(2, ((k & F3) ? 0x10u : 0) | ((k & PLAY) ? 0x20u : 0) | ((k & DOWN) ? 0x40u : 0));
        row(3, ((k & F4) ? 0x10u : 0) | ((k & REWIND) ? 0x20u : 0) | ((k & LEFT) ? 0x40u : 0));
        row(4, ((k & L) ? 0x10u : 0) | ((k & R) ? 0x20u : 0));
        if (k & F5) d &= ~0x02u;
        break;
    }
    case 6:     // G: bit 15 = F1 key (active low); bit 0 = 1: no SD card
        d = (d & ~0x8001u) | 0x8001u;
        if (m_keys & F1) d &= ~0x8000u;
        break;
    }
    return d;
}

void GizmondoCore::setInputs(uint32_t pressed)
{
    if (pressed != m_keys && m_cpu) {
        m_keys = pressed;
        eint(4);        // any key change pulses EINT4
        return;
    }
    m_keys = pressed;
}

// ---- GoForce 4500 ----

void GizmondoCore::gfVram16(uint16_t v)
{
    if (m_gfx < m_gfxMax && m_gfy < m_gfyMax) {
        uint32_t o = 0x20000 + (((m_gfyMin + m_gfy) * 321) + (m_gfxMin + m_gfx)) * 2;
        if (o + 2 <= 0x140000) {
            uint8_t* b = reinterpret_cast<uint8_t*>(m_gf.data());
            std::memcpy(b + o, &v, 2);
        }
        m_gfx++;
    }
}

void GizmondoCore::gfWrite(uint32_t off, uint32_t v)
{
    switch (off) {
    case 0x300: m_gfx = m_gfy = 0; break;
    case 0x304:
        m_gfxMax = v & 0xffff; m_gfyMax = v >> 16;
        if (m_gfxMax & 1) m_gfxMin++;
        break;
    case 0x308:
        m_gfxMin = v & 0xffff; m_gfyMin = v >> 16;
        if (m_gfxMin & 1) m_gfxMin--;
        break;
    }
    if (off >= 0x200 && off < 0x280) {
        gfVram16(uint16_t(v));
        gfVram16(uint16_t(v >> 16));
        if (m_gfx >= m_gfxMax) { m_gfx = 0; m_gfy++; }
    }
}

void GizmondoCore::renderScreen()
{
    const uint8_t* b = reinterpret_cast<const uint8_t*>(m_gf.data()) + 0x20000;
    for (int y = 0; y < 240; y++)
        for (int x = 0; x < 320; x++) {
            uint16_t c; std::memcpy(&c, b + (y * 321 + x) * 2, 2);
            uint32_t r = (c >> 11) & 0x1f, g = (c >> 5) & 0x3f, bl = c & 0x1f;
            m_fb[y * 320 + x] = 0xff000000u | (r << 19 | (r >> 2) << 16) | (g << 10 | (g >> 4) << 8) | (bl << 3 | bl >> 2);
        }
}

// ---- bus ----

uint8_t* GizmondoCore::page_ptr(uint32_t pa)
{
    if (pa - RAM_BASE < RAM_SIZE) return m_ram.data() + ((pa - RAM_BASE) & ~0xfffu);
    if ((pa & 0xfffff000u) == 0x40000000u) return m_sram.data();
    return nullptr;
}

uint32_t GizmondoCore::read32(uint32_t pa)
{
    if (pa - RAM_BASE < RAM_SIZE) return rd32(m_ram, (pa - RAM_BASE) & ~3u);
    if (pa < 0x800) return rd32(m_fboot, pa & 0x7fc);
    if ((pa & 0xfffff000u) == 0x40000000u) return rd32(m_sram, pa & 0xffc);
    return ioRead(pa & ~3u, 4);
}

uint16_t GizmondoCore::read16(uint32_t pa)
{
    if (pa - RAM_BASE < RAM_SIZE) { uint16_t v; std::memcpy(&v, &m_ram[(pa - RAM_BASE) & ~1u], 2); return v; }
    if (pa < 0x800) { uint16_t v; std::memcpy(&v, &m_fboot[pa & 0x7fe], 2); return v; }
    if (pa < 0x1000) return m_doc.sec1Read();
    if (pa < 0x1800) return m_doc.read16(pa - 0x1000);
    if (pa < 0x2000) return 0;
    return uint16_t(ioRead(pa & ~1u, 2));
}

uint8_t GizmondoCore::read8(uint32_t pa)
{
    if (pa - RAM_BASE < RAM_SIZE) return m_ram[pa - RAM_BASE];
    if (pa < 0x800) return m_fboot[pa];
    if (pa < 0x1000) return uint8_t(m_doc.sec1Read());
    if (pa < 0x1800) return m_doc.read8(pa - 0x1000);
    if (pa < 0x2000) return 0;
    return uint8_t(ioRead(pa, 1));
}

void GizmondoCore::write32(uint32_t pa, uint32_t v)
{
    if (pa - RAM_BASE < RAM_SIZE) { std::memcpy(&m_ram[(pa - RAM_BASE) & ~3u], &v, 4); return; }
    if ((pa & 0xfffff000u) == 0x40000000u) { std::memcpy(&m_sram[pa & 0xffc], &v, 4); return; }
    if (pa < 0x2000) { write16(pa, uint16_t(v)); write16(pa + 2, uint16_t(v >> 16)); return; }
    ioWrite(pa & ~3u, v, 4);
}

void GizmondoCore::write16(uint32_t pa, uint16_t v)
{
    if (pa - RAM_BASE < RAM_SIZE) { std::memcpy(&m_ram[(pa - RAM_BASE) & ~1u], &v, 2); return; }
    if ((pa & 0xfffff000u) == 0x40000000u) { std::memcpy(&m_sram[pa & 0xffe], &v, 2); return; }
    if (pa < 0x800) return;
    if (pa < 0x1000) { m_doc.sec1Write(v); return; }
    if (pa < 0x1800) { m_doc.write16(pa - 0x1000, v); return; }
    if (pa < 0x2000) return;
    ioWrite(pa & ~1u, v, 2);
}

void GizmondoCore::write8(uint32_t pa, uint8_t v)
{
    if (pa - RAM_BASE < RAM_SIZE) { m_ram[pa - RAM_BASE] = v; return; }
    if ((pa & 0xfffff000u) == 0x40000000u) { m_sram[pa & 0xfff] = v; return; }
    if (pa < 0x800) return;
    if (pa < 0x1000) { m_doc.sec1Write(v); return; }
    if (pa < 0x1800) { m_doc.write8(pa - 0x1000, v); return; }
    if (pa < 0x2000) return;
    ioWrite(pa, v, 1);
}

// ---- S3C2440 peripherals ----

uint32_t GizmondoCore::ioRead(uint32_t pa, int size)
{
    uint32_t a = pa & ~3u, sh = (pa & 3) * 8, v = 0;
    // GoForce 4500
    if (pa - 0x34000000u < 0x140000u) {
        v = (a - 0x34000000u) == 0x4c ? 0x00145000u : m_gf[(a - 0x34000000u) / 4];
        return v >> sh;
    }
    switch (a) {
    // interrupt controller
    case 0x4a000000: v = m_srcpnd; break;
    case 0x4a000004: v = m_intmod; break;
    case 0x4a000008: v = m_intmsk; break;
    case 0x4a000010: v = m_intpnd; break;
    case 0x4a000014: v = m_intoffset; break;
    case 0x4a000018: v = m_subsrcpnd; break;
    case 0x4a00001c: v = m_intsubmsk; break;
    // timers
    case 0x51000000: v = m_tcfg0; break;
    case 0x51000004: v = m_tcfg1; break;
    case 0x51000008: v = m_tcon; break;
    case 0x51000014: syncTimers(); v = m_t[0].cnt; break;
    case 0x51000020: syncTimers(); v = m_t[1].cnt; break;
    case 0x5100002c: syncTimers(); v = m_t[2].cnt; break;
    case 0x51000038: syncTimers(); v = m_t[3].cnt; break;
    case 0x51000040: syncTimers(); v = m_t[4].cnt; break;
    // UARTs: always ready to send, nothing received
    case 0x50000010: case 0x50004010: case 0x50008010: v = 6; break;
    case 0x50000018: case 0x50004018: case 0x50008018: v = 0; break;
    // GPIO
    case 0x56000004: v = gpioRead(0); break;
    case 0x56000014: v = gpioRead(1); break;
    case 0x56000024: v = gpioRead(2); break;
    case 0x56000034: v = gpioRead(3); break;
    case 0x56000044: v = gpioRead(4); break;
    case 0x56000054: v = gpioRead(5); break;
    case 0x56000064: v = gpioRead(6); break;
    case 0x56000074: v = gpioRead(7); break;
    case 0x560000d4: v = gpioRead(8); break;
    case 0x560000a4: v = m_eintmask; break;
    case 0x560000a8: v = m_eintpend; break;
    // RTC (BCD, from the phone's clock)
    case 0x57000070: case 0x57000074: case 0x57000078: case 0x5700007c:
    case 0x57000080: case 0x57000084: case 0x57000088: {
        time_t t = m_wallClock ? time_t(m_wallClock) : time(nullptr);
        struct tm tmv; gmtime_r(&t, &tmv);
        switch (a) {
        case 0x57000070: v = bcd(tmv.tm_sec); break;
        case 0x57000074: v = bcd(tmv.tm_min); break;
        case 0x57000078: v = bcd(tmv.tm_hour); break;
        case 0x5700007c: v = bcd(tmv.tm_mday); break;
        case 0x57000080: v = uint32_t(tmv.tm_wday + 1); break;
        case 0x57000084: v = bcd(tmv.tm_mon + 1); break;
        case 0x57000088: v = bcd(tmv.tm_year % 100); break;
        }
        break;
    }
    // ADC: conversions finish at once; every channel reads mid-scale except the battery (full)
    case 0x58000000: v = m_adccon | 0x8000; break;
    case 0x5800000c: v = m_adcdat0; break;
    // SPI: transfer always complete
    case 0x59000004: case 0x59000024: v = 1; break;
    default: {
        auto it = m_regs.find(a);
        v = it == m_regs.end() ? 0 : it->second;
        if (dbg() > 1) fprintf(stderr, "IO R%d %08x -> %08x (pc %08x)\n", size, pa, v, m_cpu->r[15]);
    }
    }
    return v >> sh;
}

void GizmondoCore::ioWrite(uint32_t pa, uint32_t v, int size)
{
    uint32_t a = pa & ~3u, sh = (pa & 3) * 8;
    if (size < 4) {
        uint32_t mask = (size == 1 ? 0xffu : 0xffffu) << sh;
        auto it = m_regs.find(a);
        uint32_t old = it == m_regs.end() ? 0 : it->second;
        v = (old & ~mask) | ((v << sh) & mask);
    }
    if (pa - 0x34000000u < 0x140000u) {
        uint32_t off = a - 0x34000000u;
        uint32_t mask = size == 4 ? 0xffffffffu : (size == 1 ? 0xffu : 0xffffu) << sh;
        m_gf[off / 4] = (m_gf[off / 4] & ~mask) | (v & mask);
        gfWrite(off, m_gf[off / 4]);
        return;
    }
    switch (a) {
    case 0x4a000000: m_srcpnd &= ~v; updateIrq(); return;
    case 0x4a000004: m_intmod = v; updateIrq(); return;
    case 0x4a000008: m_intmsk = v; updateIrq(); return;
    case 0x4a000010:
        m_intpnd &= ~v;
        if (!m_intpnd) m_intoffset = 0;
        updateIrq();
        return;
    case 0x4a000018: m_subsrcpnd &= ~v; updateIrq(); return;
    case 0x4a00001c: m_intsubmsk = v; updateIrq(); return;
    case 0x51000000: m_tcfg0 = v; return;
    case 0x51000004: m_tcfg1 = v; return;
    case 0x51000008: {
        syncTimers();
        // timer 0: bits 0-3; timers 1-3: 8-11, 12-15, 16-19; timer 4: 20-22 (no inverter bit)
        static const int base[5] = { 0, 8, 12, 16, 20 };
        for (int i = 0; i < 5; i++) {
            uint32_t b = v >> base[i];
            Timer& t = m_t[i];
            if (b & 2) { t.cnt = t.cntb; t.acc = 0; }                  // manual update
            t.reload = i < 4 ? (b & 8) != 0 : (b & 4) != 0;
            bool start = b & 1;
            if (start && !t.on) t.acc = 0;
            t.on = start;
        }
        m_tcon = v;
        if (dbg() > 1) fprintf(stderr, "TCON %08x cfg0 %08x cfg1 %08x cntb3 %u\n", v, m_tcfg0, m_tcfg1, m_t[3].cntb);
        return;
    }
    case 0x5100000c: m_t[0].cntb = v & 0xffff; return;
    case 0x51000010: m_t[0].cmpb = v & 0xffff; return;
    case 0x51000018: m_t[1].cntb = v & 0xffff; return;
    case 0x5100001c: m_t[1].cmpb = v & 0xffff; return;
    case 0x51000024: m_t[2].cntb = v & 0xffff; return;
    case 0x51000028: m_t[2].cmpb = v & 0xffff; return;
    case 0x51000030: m_t[3].cntb = v & 0xffff; return;
    case 0x51000034: m_t[3].cmpb = v & 0xffff; return;
    case 0x5100003c: m_t[4].cntb = v & 0xffff; return;
    // UART transmit: the bootloader's debug messages
    case 0x50000020: case 0x50004020: case 0x50008020: {
        char c = char(v & 0xff);
        serialLog += c;
        if (dbg()) fputc(c, stderr);
        if (serialLog.size() > 65536) serialLog.erase(0, 32768);
        return;
    }
    case 0x56000004: m_gpdat[0] = v; break;
    case 0x56000014: m_gpdat[1] = v; break;
    case 0x56000024: m_gpdat[2] = v; break;
    case 0x56000034: m_gpdat[3] = v; break;
    case 0x56000044: m_gpdat[4] = v; break;
    case 0x56000054: m_gpdat[5] = v; break;
    case 0x56000064: m_gpdat[6] = v; break;
    case 0x56000074: m_gpdat[7] = v; break;
    case 0x560000d4: m_gpdat[8] = v; break;
    case 0x560000a4: m_eintmask = v; return;
    case 0x560000a8: m_eintpend &= ~v; return;
    case 0x58000000:
        m_adccon = v & ~1u;
        if (v & 1) {            // start: channel in bits 5-3
            int ch = (v >> 3) & 7;
            m_adcdat0 = ch == 0 ? 0x3ff : 0x200;
            setSubIrq(SUB_ADC);
        }
        return;
    case 0x53000000:            // watchdog: never lets it reset the machine
        break;
    default:
        if (dbg() > 1) fprintf(stderr, "IO W%d %08x <- %08x (pc %08x)\n", size, pa, v, m_cpu->r[15]);
        break;
    }
    m_regs[a] = v;
}

// ---- frame ----

void GizmondoCore::runFrame()
{
    const int SLICES = 50;
    uint32_t f = fclk();
    const int64_t slice = std::max<int64_t>(int64_t(f / 60 / SLICES / CPI), 1000);   // instructions
    for (int i = 0; i < SLICES; i++) {
        // CLKCON bit 2: IDLE mode - the CPU sleeps until an interrupt
        if (m_regs[0x4c00000c] & 4) {
            m_regs[0x4c00000c] &= ~4u;
            m_cpu->halt();
        }
        uint64_t i0 = m_cpu->insn_count;
        m_cpu->run(slice);
        uint64_t ran = m_cpu->insn_count - i0;
        if (ran < uint64_t(slice)) m_idle += uint64_t(slice) - ran;
        syncTimers();
    }
    m_frame++;
    renderScreen();
    m_audioPending = 735;
    if (dbg() && (m_frame % 60) == 0)
        fprintf(stderr, "[frame %llu pc %08x cpsr %08x intmsk %08x srcpnd %08x]\n",
                (unsigned long long)m_frame, m_cpu->r[15], m_cpu->cpsr, m_intmsk, m_srcpnd);
}

size_t GizmondoCore::readAudio(int16_t* out, size_t maxSamples)
{
    size_t n = std::min(maxSamples, m_audioPending);
    std::memset(out, 0, n * sizeof(int16_t));
    m_audioPending -= n;
    return n;
}
