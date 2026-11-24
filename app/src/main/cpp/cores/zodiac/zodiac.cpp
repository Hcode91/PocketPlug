// PocketPlug - Tapwave Zodiac (2003)
// SPDX-License-Identifier: GPL-3.0-or-later
#include "zodiac.h"
#include <zlib.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace {
int dbg() { static int v = getenv("ZODDBG") ? atoi(getenv("ZODDBG")) : 0; return v; }
inline uint32_t le32(const uint8_t* p) { uint32_t v; std::memcpy(&v, p, 4); return v; }
inline uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
inline uint16_t be16(const uint8_t* p) { return uint16_t(p[0] << 8 | p[1]); }

enum { INT_GPIOA = 11, INT_GPIOB = 12, INT_GPIOC = 13, INT_RTC = 17, INT_TIM2 = 58, INT_TIM1 = 59, INT_GPIOD = 62 };

bool gunzip(const uint8_t* src, size_t len, std::vector<uint8_t>& out)
{
    z_stream z{};
    if (inflateInit2(&z, 16 + MAX_WBITS) != Z_OK) return false;
    z.next_in = const_cast<Bytef*>(src);
    z.avail_in = uInt(len);
    out.clear();
    uint8_t buf[65536];
    int rc;
    do {
        z.next_out = buf; z.avail_out = sizeof buf;
        rc = inflate(&z, Z_NO_FLUSH);
        if (rc != Z_OK && rc != Z_STREAM_END) { inflateEnd(&z); return false; }
        out.insert(out.end(), buf, buf + (sizeof buf - z.avail_out));
    } while (rc != Z_STREAM_END);
    inflateEnd(&z);
    return true;
}
}

std::vector<uint8_t> ZodiacCore::romFromUpdate(const std::vector<uint8_t>& prc)
{
    std::vector<uint8_t> rom;
    if (prc.size() < 78) return rom;
    uint16_t n = be16(&prc[76]);
    for (uint16_t i = 0; i < n; i++) {
        size_t e = 78 + size_t(i) * 10;
        if (e + 10 > prc.size()) break;
        if (std::memcmp(&prc[e], "ROMZ", 4)) continue;
        uint32_t off = be32(&prc[e + 6]);
        uint32_t end = (i + 1 < n && e + 20 <= prc.size()) ? be32(&prc[e + 16]) : uint32_t(prc.size());
        if (off >= end || end > prc.size()) break;
        if (!gunzip(&prc[off], end - off, rom)) rom.clear();
        break;
    }
    return rom;
}

ZodiacCore::ZodiacCore() {}
ZodiacCore::~ZodiacCore() { delete m_cpu; }

bool ZodiacCore::load(const std::vector<RomFile>& roms, std::string& error)
{
    std::vector<uint8_t> rom;
    for (auto& r : roms) {
        if (r.data.size() >= 0x400000 && r.data.size() <= FLASH_SIZE && !std::memcmp(&r.data[0x10], "PalmCard", 8)) rom = r.data;
        else if (r.data.size() > 78 && !std::memcmp(&r.data[60], "applfins", 8)) rom = romFromUpdate(r.data);
        if (!rom.empty()) break;
    }
    if (rom.size() < 0x400000) {
        error = "Missing the Zodiac ROM: add zodiac_1.1_update.prc (Tapwave's official 1.1 ROM update)";
        return false;
    }
    std::fill(m_flash.begin(), m_flash.end(), 0xff);
    std::memcpy(m_flash.data(), rom.data(), rom.size());
    // The board's EEPROM: bytes 0x50-0xcf must hold the two 64-byte halves the 1.1 ROM keeps
    // at 0x7e75c and 0x7e79d (it checks them at start-up: "EEPROM/ROM mismatch" otherwise).
    std::fill(m_eeprom.begin(), m_eeprom.end(), 0xff);
    if (rom.size() > 0x7e79d + 0x40) {
        std::memcpy(&m_eeprom[0x50], &rom[0x7e75c], 0x40);
        std::memcpy(&m_eeprom[0x90], &rom[0x7e79d], 0x40);
    }
    delete m_cpu;
    m_cpu = new didj::ARM926(this);
    m_cpu->mmu_on_prefetch = true;     // ARM920T pipeline (see arm926.h)
    reset();
    return true;
}

void ZodiacCore::reset()
{
    std::fill(m_ram0.begin(), m_ram0.end(), 0);
    std::fill(m_ram1.begin(), m_ram1.end(), 0);
    std::fill(m_esram.begin(), m_esram.end(), 0);
    std::fill(m_vram.begin(), m_vram.end(), 0);
    m_regs.clear();
    m_intSrc = m_intEnable = m_intType = m_intForce = 0;
    m_intCntl = 0; m_niMask = 0x1f;
    std::memset(m_niPrio, 0, sizeof m_niPrio);
    for (auto& t : m_tim) t = Timer();
    for (auto& p : m_gpio) p = Port();
    // clocks at reset (i.MX1 manual defaults)
    reg(0x0021b000) = 0x2f00ac03;   // CSCR
    reg(0x0021b004) = 0x003f1437;   // MPCTL0
    reg(0x0021b008) = 0;            // MPCTL1
    reg(0x0021b00c) = 0x07b32da5;   // SPCTL0
    reg(0x0021b010) = 0x00008000;   // SPCTL1 (locked)
    reg(0x0021b020) = 0x00000015;   // PCDR
    reg(0x0021b800) = 0x00000000;   // RSR: power-on reset
    reg(0x0021b804) = 0x0005d01d;   // SIDR: MC9328MX1 silicon id
    m_rtcSeconds = 0; m_rtcRtcctl = 0; m_rtcIsr = 0; m_rtcIenr = 0; m_rtcAcc = 0;
    reg(W42_CIF + 0x10) = 1;           // W4200 CIF: chip present
    reg(W42_REG + 0x00) = 0x57441002;  // W4200 register 0: ATI vendor 0x1002, device 0x5744 (the ROM tells it from the W100, 0x5644, by this)
    serialLog.clear();
    m_seen.clear();
    m_cpu->reset();
    m_idle = 0; m_lastSync = m_cpu->insn_count;
    m_cpu->set_irq(false);
    m_cpu->set_fiq(false);
    m_cpu->r[15] = 0;
    if (getenv("ZODTRACE")) {
        m_cpu->trace_user = this;
        m_cpu->trace_hook = [](didj::ARM926* c, uint32_t pc, void*) {
            static long n = atol(getenv("ZODTRACE"));
            if (n-- > 0) fprintf(stderr, "T %08x r0 %08x r1 %08x r2 %08x r3 %08x sp %08x lr %08x\n", pc, c->r[0], c->r[1], c->r[2], c->r[3], c->r[13], c->r[14]);
        };
    }
}

// ---------------------------------------------------------------------------
// memory
// ---------------------------------------------------------------------------
uint8_t* ZodiacCore::page_ptr(uint32_t pa)
{
    if (pa - SDRAM0 < SDRAM_SIZE) return m_ram0.data() + ((pa - SDRAM0) & ~0xfffu);
    if (pa - SDRAM1 < SDRAM_SIZE) return m_ram1.data() + ((pa - SDRAM1) & ~0xfffu);
    static uint32_t watchPage = getenv("ZODWATCHPAGE") ? strtoul(getenv("ZODWATCHPAGE"), nullptr, 16) : 0;
    if (watchPage && (pa & ~0xfffu) == watchPage) return nullptr;   // (debug: route this page's reads through ioRead)
    if (pa - FLASH_BASE < FLASH_SIZE) return m_flash.data() + ((pa - FLASH_BASE) & ~0xfffu);
    if (pa < 0x100000) return m_flash.data() + (pa & ~0xfffu);     // "double map" of CS0
    if (pa - ESRAM < ESRAM_SIZE) return m_esram.data() + ((pa - ESRAM) & ~0xfffu);
    if (pa - W42_MEM < W42_MEM_SIZE && dbg() < 4) return m_vram.data() + ((pa - W42_MEM) & ~0xfffu);
    return nullptr;
}

bool ZodiacCore::page_readonly(uint32_t pa)
{
    return pa - FLASH_BASE < FLASH_SIZE || pa < 0x100000;
}

uint32_t ZodiacCore::read32(uint32_t pa)
{
    if (uint8_t* p = page_ptr(pa)) return le32(p + (pa & 0xffc));
    return ioRead(pa & ~3u, 4);
}
uint16_t ZodiacCore::read16(uint32_t pa)
{
    if (uint8_t* p = page_ptr(pa)) { uint16_t v; std::memcpy(&v, p + (pa & 0xffe), 2); return v; }
    uint32_t v = ioRead(pa & ~3u, 2);
    return uint16_t(v >> ((pa & 2) * 8));
}
uint8_t ZodiacCore::read8(uint32_t pa)
{
    if (uint8_t* p = page_ptr(pa)) return p[pa & 0xfff];
    uint32_t v = ioRead(pa & ~3u, 1);
    return uint8_t(v >> ((pa & 3) * 8));
}
void ZodiacCore::write32(uint32_t pa, uint32_t v)
{
    if (!page_readonly(pa)) if (uint8_t* p = page_ptr(pa)) { std::memcpy(p + (pa & 0xffc), &v, 4); return; }
    ioWrite(pa & ~3u, v, 4);
}
void ZodiacCore::write16(uint32_t pa, uint16_t v)
{
    if (!page_readonly(pa)) if (uint8_t* p = page_ptr(pa)) { std::memcpy(p + (pa & 0xffe), &v, 2); return; }
    ioWrite(pa, v, 2);
}
void ZodiacCore::write8(uint32_t pa, uint8_t v)
{
    if (!page_readonly(pa)) if (uint8_t* p = page_ptr(pa)) { p[pa & 0xfff] = v; return; }
    ioWrite(pa, v, 1);
}

void ZodiacCore::unknown(const char* what, uint32_t pa, uint32_t v, int size)
{
    if (!dbg()) return;
    int& n = m_seen[pa ^ (what[0] == 'W' ? 0x80000000u : 0)];
    if (n++ < (dbg() > 1 ? 50 : 3))
        fprintf(stderr, "%s%d %08x = %08x  pc %08x lr %08x\n", what, size * 8, pa, v, m_cpu->current_pc(), m_cpu->r[14]);
}

// ---------------------------------------------------------------------------
// interrupts (AITC)
// ---------------------------------------------------------------------------
uint32_t ZodiacCore::niVector() const
{
    uint64_t p = (m_intSrc | m_intForce) & m_intEnable & ~m_intType;
    int best = -1, bestPrio = -1;
    for (int n = 63; n >= 0; n--)
        if ((p >> n) & 1) { int pr = prio(n); if (pr > bestPrio) { best = n; bestPrio = pr; } }
    if (best < 0) return 0xffffffffu;
    return uint32_t(best) << 16 | uint32_t(bestPrio);
}

void ZodiacCore::updateIrq()
{
    uint64_t p = (m_intSrc | m_intForce) & m_intEnable;
    bool irq = false;
    {   // (INTCNTL bits 19/20 are the i.MX1's FIAD/NIAD arbiter settings, not disables)
        // NIMASK: normal interrupts at or below this priority are held off (0x1f: none)
        uint32_t v = niVector(), mask = m_niMask & 0x1f;
        irq = v != 0xffffffffu && (mask >= 16 || int(v & 0xffff) > int(mask));
    }
    bool fiq = (p & m_intType) != 0;
    m_cpu->set_irq(irq);
    m_cpu->set_fiq(fiq);
}

// ---------------------------------------------------------------------------
// timers, GPIO, clocks
// ---------------------------------------------------------------------------
uint32_t ZodiacCore::perclk1() const
{
    // system PLL (from the 32.768 kHz crystal x 512 x MF/PD) divided by PCDR's PCLKDIV1
    uint32_t sp = m_regs.count(0x0021b00c) ? m_regs.at(0x0021b00c) : 0x07b32da5;
    double mfi = (sp >> 10) & 0xf, mfn = sp & 0x3ff, mfd = ((sp >> 16) & 0x3ff) + 1, pd = ((sp >> 26) & 0xf) + 1;
    if (mfi < 5) mfi = 5;
    double f = 2.0 * 32768.0 * 512.0 * (mfi + mfn / mfd) / pd;
    uint32_t pcdr = m_regs.count(0x0021b020) ? m_regs.at(0x0021b020) : 0x15;
    return uint32_t(f / ((pcdr & 0xf) + 1));
}

void ZodiacCore::tickTimers(uint64_t cycles)
{
    double sec = double(cycles) / CPU_HZ;
    for (int i = 0; i < 2; i++) {
        Timer& t = m_tim[i];
        if (!(t.tctl & 1)) continue;
        double clk;
        switch ((t.tctl >> 1) & 7) {
        case 1: clk = perclk1(); break;
        case 2: clk = perclk1() / 16.0; break;
        case 4: case 5: case 6: case 7: clk = 32768.0; break;
        default: continue;
        }
        t.acc += sec * clk / ((t.tprer & 0xff) + 1);
        uint64_t ticks = uint64_t(t.acc);
        t.acc -= double(ticks);
        while (ticks) {
            uint64_t toCmp = uint64_t(uint32_t(t.tcmp - t.tcn));
            if (toCmp == 0) toCmp = 0x100000000ull;
            if (ticks < toCmp) { t.tcn += uint32_t(ticks); break; }
            ticks -= toCmp;
            t.tcn = (t.tctl & (1u << 8)) ? t.tcmp : 0;   // free-run or restart
            t.tstat |= 1;
            if (!(t.tctl & (1u << 8))) t.tcn = 0;
        }
        setIrq(i ? INT_TIM2 : INT_TIM1, (t.tstat & 1) && (t.tctl & (1u << 4)));
    }
    // RTC: seconds counter, 1 Hz and sampling interrupts
    m_rtcAcc += sec;
    while (m_rtcAcc >= 1.0) {
        m_rtcAcc -= 1.0;
        if (m_rtcRtcctl & 0x80) {
            m_rtcSeconds++;
            m_rtcIsr |= 0x10;          // 1 Hz
        }
    }
    setIrq(INT_RTC, (m_rtcIsr & m_rtcIenr) != 0);
    updateIrq();
}

// Input pins (1 = high). Port B bit 14 is the power circuit's low-battery signal (low =
// battery fine).
uint32_t ZodiacCore::gpioIn(int port)
{
    static const char* e[4] = { "ZODGPA", "ZODGPB", "ZODGPC", "ZODGPD" };
    if (const char* v = getenv(e[port])) return strtoul(v, nullptr, 16);   // (debug)
    uint32_t v = 0xffffffffu;
    if (port == 1) v &= ~(1u << 14);
    return v;
}

void ZodiacCore::gpioUpdate()
{
    static const int irqs[4] = { INT_GPIOA, INT_GPIOB, INT_GPIOC, INT_GPIOD };
    for (int i = 0; i < 4; i++) setIrq(irqs[i], (m_gpio[i].isr & m_gpio[i].imr) != 0);
    updateIrq();
}

// ---------------------------------------------------------------------------
// peripheral registers
// ---------------------------------------------------------------------------
uint32_t ZodiacCore::ioRead(uint32_t pa, int size)
{
    if (pa >= 0x00200000 && pa < 0x00225000) {
        uint32_t base = pa & 0xfffff000, off = pa & 0xfff;
        switch (base) {
        case 0x00202000: case 0x00203000: {            // timers
            sync();
            Timer& t = m_tim[base == 0x00203000];
            switch (off) {
            case 0x00: return t.tctl;
            case 0x04: return t.tprer;
            case 0x08: return t.tcmp;
            case 0x0c: return t.tcr;
            case 0x10: if (dbg() > 2) fprintf(stderr, "tcn%d %u tctl %x prer %x clk %u cmp %x acc %f now %llu last %llu\n", base == 0x00203000, t.tcn, t.tctl, t.tprer, perclk1(), t.tcmp, t.acc, (unsigned long long)now(), (unsigned long long)m_lastSync); return t.tcn;
            case 0x14: return t.tstat;
            }
            break;
        }
        case 0x00204000:                                 // RTC
            sync();
            switch (off) {
            case 0x00: return (m_rtcSeconds / 3600) << 8 | ((m_rtcSeconds / 60) % 60);   // HOURMIN
            case 0x04: return m_rtcSeconds % 60;                                         // SECONDS
            case 0x10: return m_rtcRtcctl;
            case 0x14: return m_rtcIsr;
            case 0x18: return m_rtcIenr;
            case 0x20: return m_rtcSeconds / 86400;                                      // DAYR
            }
            return reg(pa);
        case 0x00206000: case 0x00207000:                // UARTs
            switch (off) {
            case 0x00: return 0;                         // URXD: nothing received
            case 0x94: return 0x2000;                    // USR1: TRDY
            case 0x98: return 0x4008;                    // USR2: TXFE, TXDC
            case 0xb4: return reg(pa) | 0x40;            // UTS: TXEMPTY
            }
            return reg(pa);
        case 0x00223000:                                 // AITC
            switch (off) {
            case 0x00: return m_intCntl;
            case 0x04: return m_niMask;
            case 0x10: return uint32_t(m_intEnable >> 32);
            case 0x14: return uint32_t(m_intEnable);
            case 0x18: return uint32_t(m_intType >> 32);
            case 0x1c: return uint32_t(m_intType);
            case 0x40: return niVector();
            case 0x44: {
                uint64_t p = (m_intSrc | m_intForce) & m_intEnable & m_intType;
                for (int n = 63; n >= 0; n--) if ((p >> n) & 1) return uint32_t(n);
                return 0xffffffffu;
            }
            case 0x48: return uint32_t(m_intSrc >> 32);
            case 0x4c: return uint32_t(m_intSrc);
            case 0x50: return uint32_t(m_intForce >> 32);
            case 0x54: return uint32_t(m_intForce);
            case 0x58: return uint32_t(((m_intSrc | m_intForce) & m_intEnable & ~m_intType) >> 32);
            case 0x5c: return uint32_t((m_intSrc | m_intForce) & m_intEnable & ~m_intType);
            case 0x60: return uint32_t(((m_intSrc | m_intForce) & m_intEnable & m_intType) >> 32);
            case 0x64: return uint32_t((m_intSrc | m_intForce) & m_intEnable & m_intType);
            }
            if (off >= 0x20 && off < 0x40) return m_niPrio[(off - 0x20) / 4];
            break;
        case 0x0021c000: {                               // GPIO
            int port = (off >> 8) & 3;
            Port& p = m_gpio[port];
            switch (off & 0xff) {
            case 0x00: return p.ddir;
            case 0x04: return p.ocr1;
            case 0x08: return p.ocr2;
            case 0x0c: case 0x10: case 0x14: case 0x18: return p.iconf[((off & 0xff) - 0x0c) / 4];
            case 0x1c: return p.dr;
            case 0x20: return p.gius;
            case 0x24: {                                                     // SSR
                uint32_t v = (p.dr & p.ddir) | (gpioIn(port) & ~p.ddir);
                if (dbg() > 1) { int& n = m_seen[0x60000000u | uint32_t(port) << 24 | (m_cpu->r[15] & 0xffffff)]; if (n++ < 2) fprintf(stderr, "gpio%c SSR = %08x (ddir %08x gius %08x) pc %08x\n", 'A' + port, v, p.ddir, p.gius, m_cpu->r[15]); }
                return v;
            }
            case 0x28: return p.icr1;
            case 0x2c: return p.icr2;
            case 0x30: return p.imr;
            case 0x34: return p.isr;
            case 0x38: return p.gpr;
            case 0x3c: return p.swr;
            case 0x40: return p.puen;
            }
            break;
        }
        case 0x0021b000:                                 // PLL / reset module
            if (off == 0x000) return reg(pa) & ~((1u << 21) | (1u << 22));   // restarts complete
            if (off == 0x010) return reg(pa) | 0x8000;    // SPCTL1: locked
            if (off == 0x008) return reg(pa) | 0x8000;    // MPCTL1: locked
            return reg(pa);
        case 0x00201000:                                 // watchdog
        case 0x00220000: case 0x00221000:                // EIM, SDRAMC
            return reg(pa);
        case 0x00217000:
            return i2cRead(off);
        }
        uint32_t v = reg(pa);
        unknown("R", pa, v, size);
        return v;
    }
    if (pa - W42_REG < 0x10000) return w42Read(pa - W42_REG, size);
    if (pa - FLASH_BASE < FLASH_SIZE) {
        static int n = 0;
        if (n++ < 20) fprintf(stderr, "watched read %08x pc %08x lr %08x sp %08x\n", pa, m_cpu->current_pc(), m_cpu->r[14], m_cpu->r[13]);
        return le32(&m_flash[pa - FLASH_BASE]);
    }
    if (pa - W42_MEM < W42_MEM_SIZE) {         // (debug path: video memory through here at ZODDBG=4)
        uint32_t v = le32(&m_vram[pa - W42_MEM]);
        int& n = m_seen[0x40000000u | ((pa - W42_MEM) >> 12)];
        if (n++ == 0) fprintf(stderr, "vram R page %06x pc %08x\n", pa - W42_MEM + 0x100000, m_cpu->r[15]);
        return v;
    }
    uint32_t v = m_regs.count(pa) ? m_regs[pa] : 0;
    unknown("R", pa, v, size);
    return v;
}

// ---------------------------------------------------------------------------
// W4200 command processor (PM4 packets: type 0 = register run, type 1 = two registers,
// type 2 = filler, type 3 = operation)
// ---------------------------------------------------------------------------
void ZodiacCore::cpWord(uint32_t w)
{
    if (m_cpLeft == 0) {
        m_cpHeader = w;
        switch (w >> 30) {
        case 0: m_cpReg = (w & 0x7fff) << 2; m_cpLeft = ((w >> 16) & 0x3fff) + 1; m_cpIndex = 0; break;
        case 1: m_cpReg = (w & 0x7ff) << 2; m_cpReg2 = ((w >> 11) & 0x7ff) << 2; m_cpLeft = 2; m_cpIndex = 0; break;
        case 2: break;
        case 3: m_cpLeft = ((w >> 16) & 0x3fff) + 1; m_cpBody.clear(); break;
        }
        return;
    }
    switch (m_cpHeader >> 30) {
    case 0:
        w42Write(m_cpReg + ((m_cpHeader & 0x8000) ? 0 : m_cpIndex * 4), w, 4);   // bit 15: one register
        m_cpIndex++;
        break;
    case 1:
        w42Write(m_cpIndex++ == 0 ? m_cpReg : m_cpReg2, w, 4);
        break;
    case 3:
        m_cpBody.push_back(w);
        if (m_cpLeft == 1) { m_cpLeft = 0; cpPacket3((m_cpHeader >> 8) & 0xff, m_cpBody); return; }
        break;
    }
    m_cpLeft--;
}

// 2D operations (worked out from what the ROM sends; surfaces are RGB565, offsets are W4200
// internal addresses, pitches in pixels):
//   0x1a fill:      pos (x<<16|y), size (w<<16|h); colour DP_BRUSH_FRGD_CLR (0x107c) into DST
//   0x19 host data: pos (y<<16|x), size (h<<16|w), then pixels into DST - RGB565, two per
//                   word, or (DP_GUI_MASTER_CNTL bit 31) 4-bit coverage of DP_SRC_FRGD_CLR
//   0x1b copy:      src pos, dst pos (x<<16|y), size (w<<16|h) from SRC to DST
//   0x1c keyed copy: flags, key colour, -, -, dst pos, size (w<<16|h); source pixels equal
//                   to the key are skipped
uint16_t* ZodiacCore::w42Pixel(uint32_t offset, uint32_t pitch, int x, int y)
{
    if (x < 0 || y < 0) return nullptr;
    uint64_t a = uint64_t(offset) + (uint64_t(uint32_t(y)) * pitch + uint32_t(x)) * 2;
    if (a < 0x100000 || a + 2 > 0x100000 + uint64_t(m_vram.size())) return nullptr;
    return reinterpret_cast<uint16_t*>(&m_vram[size_t(a - 0x100000)]);
}

void ZodiacCore::cpPacket3(uint32_t op, const std::vector<uint32_t>& body)
{
    if (dbg()) {
        int& n = m_seen[0xc0000000u | op];
        if (n++ < (dbg() > 1 ? 12 : 3)) {
            fprintf(stderr, "cp type3 op %02x len %zu:", op, body.size());
            for (size_t i = 0; i < body.size() && i < 12; i++) fprintf(stderr, " %08x", body[i]);
            fprintf(stderr, "  | dst %x/%x src %x/%x gmc %08x brush %08x srcfg %08x\n",
                    reg(W42_REG + 0x1004), reg(W42_REG + 0x1008), reg(W42_REG + 0x11ac), reg(W42_REG + 0x11b0),
                    reg(W42_REG + 0x106c), reg(W42_REG + 0x107c), reg(W42_REG + 0x11d8));
        }
    }
    uint32_t dOff = reg(W42_REG + 0x1004), dPitch = reg(W42_REG + 0x1008) & 0x3fff;
    uint32_t sOff = reg(W42_REG + 0x11ac), sPitch = reg(W42_REG + 0x11b0) & 0x3fff;
    switch (op) {
    case 0x1a: {
        if (body.size() < 2) break;
        int x0 = int(body[0] >> 16), y0 = int(body[0] & 0xffff), w = int(body[1] >> 16), h = int(body[1] & 0xffff);
        uint16_t c = uint16_t(reg(W42_REG + 0x107c));
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
                if (uint16_t* p = w42Pixel(dOff, dPitch, x0 + x, y0 + y)) *p = c;
        break;
    }
    case 0x19: {
        if (body.size() < 2) break;
        int y0 = int(body[0] >> 16), x0 = int(body[0] & 0xffff), h = int(body[1] >> 16), w = int(body[1] & 0xffff);
        bool coverage = (reg(W42_REG + 0x106c) & 0x80000000u) != 0;
        size_t n = 0, total = size_t(w) * size_t(h);
        if (coverage) {
            uint16_t fg = uint16_t(reg(W42_REG + 0x11d8));
            int fr = fg >> 11, fgc = (fg >> 5) & 63, fb = fg & 31;
            for (size_t i = 2; i < body.size() && n < total; i++)
                for (int k = 0; k < 8 && n < total; k++, n++) {
                    int a = int((body[i] >> (k * 4)) & 15);
                    if (!a) continue;
                    uint16_t* p = w42Pixel(dOff, dPitch, x0 + int(n % size_t(w)), y0 + int(n / size_t(w)));
                    if (!p) continue;
                    int r = *p >> 11, g = (*p >> 5) & 63, b = *p & 31;
                    r += (fr - r) * a / 15; g += (fgc - g) * a / 15; b += (fb - b) * a / 15;
                    *p = uint16_t(r << 11 | g << 5 | b);
                }
        } else {
            for (size_t i = 2; i < body.size() && n < total; i++)
                for (int k = 0; k < 2 && n < total; k++, n++)
                    if (uint16_t* p = w42Pixel(dOff, dPitch, x0 + int(n % size_t(w)), y0 + int(n / size_t(w))))
                        *p = uint16_t(body[i] >> (k * 16));
        }
        break;
    }
    case 0x1b: case 0x1c: {
        bool keyed = op == 0x1c;
        if (body.size() < (keyed ? 6u : 3u)) break;
        int sx = 0, sy = 0, dx, dy, w, h;
        uint16_t key = 0;
        if (keyed) {
            key = uint16_t(body[1]);
            dx = int(body[4] >> 16); dy = int(body[4] & 0xffff); w = int(body[5] >> 16); h = int(body[5] & 0xffff);
            sx = int(body[2] >> 16); sy = int(body[2] & 0xffff);
        } else {
            sx = int(body[0] >> 16); sy = int(body[0] & 0xffff);
            dx = int(body[1] >> 16); dy = int(body[1] & 0xffff); w = int(body[2] >> 16); h = int(body[2] & 0xffff);
        }
        std::vector<uint16_t> tmp(size_t(w) * size_t(h));
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                uint16_t* p = w42Pixel(sOff, sPitch, sx + x, sy + y);
                tmp[size_t(y) * size_t(w) + size_t(x)] = p ? *p : 0;
            }
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                uint16_t c = tmp[size_t(y) * size_t(w) + size_t(x)];
                if (keyed && c == key) continue;
                if (uint16_t* p = w42Pixel(dOff, dPitch, dx + x, dy + y)) *p = c;
            }
        break;
    }
    }
}

// ---------------------------------------------------------------------------
// ATI Imageon W4200 register file
// ---------------------------------------------------------------------------
uint32_t ZodiacCore::w42Read(uint32_t off, int size)
{
    uint32_t v = reg(W42_REG + off);
    if (off == 0x54c) { if (dbg() > 2) unknown("Rw42", W42_REG + off, v, size); }
    switch (off) {
    case 0x1b8: v |= 1; break;                 // PLL locked
    case 0x140: v &= ~0x80000000u; break;      // RBBM_STATUS: engine idle
    case 0xd10: v = (v & ~(0x1fu << 20)) | uint32_t(std::min<size_t>(m_adcFifo.size(), 31)) << 20; break;
    case 0xd14:
        if (m_adcFifo.empty()) v = 0;
        else { v = m_adcFifo.front(); m_adcFifo.erase(m_adcFifo.begin()); }
        break;
    case 0x220: v = reg(W42_REG + 0x21c); break;   // command read pointer caught up
    case 0x54c: {                              // display status: bit 10 = vertical blank
        uint64_t period = CPU_HZ / 60, ph = now() % period;
        v = (v & ~0x400u) | (ph >= period - period / 16 ? 0x400u : 0);
        break;
    }
    }
    if (off != 0x54c) unknown("Rw42", W42_REG + off, v, size);
    return v;
}

void ZodiacCore::w42Write(uint32_t off, uint32_t v, int size)
{
    switch (off) {
    case 0xd0c:
        if ((v & 0x400) && !(reg(W42_REG + off) & 0x400)) {
            m_adcFifo.clear();
            if (dbg()) { int& n = m_seen[0x70000000u | (reg(W42_REG + 0xd08) & 7)]; if (n++ < 3) fprintf(stderr, "adc start sel %u pc %08x lr %08x\n", reg(W42_REG + 0xd08) & 7, m_cpu->current_pc(), m_cpu->r[14]); }
            switch (reg(W42_REG + 0xd08) & 7) {
            case 0:
                m_adcFifo.push_back(1u << 26 | uint32_t(m_stickX & 0x3ff));
                m_adcFifo.push_back(2u << 26 | uint32_t(m_stickY & 0x3ff));
                break;
            case 1: m_adcFifo.push_back(9u << 26 | 512); break;
            case 2: m_adcFifo.push_back(10u << 26 | 1000); break;     // battery: full
            default: m_adcFifo.push_back((8u + (reg(W42_REG + 0xd08) & 7)) << 26 | 512); break;
            }
        }
        break;
    case 0x4d8:
        // bit 1: start; the operation is treated as finished at once (semantics still unknown)
        if (dbg()) fprintf(stderr, "w42 op 4d8=%x 414=%x 418=%x 41c=%x\n", v, reg(W42_REG + 0x414), reg(W42_REG + 0x418), reg(W42_REG + 0x41c));
        v &= ~2u;
        break;
    }
    reg(W42_REG + off) = v;
    unknown("Ww42", W42_REG + off, v, size);
}

void ZodiacCore::ioWrite(uint32_t pa, uint32_t v, int size)
{
    if (pa - W42_MEM < W42_MEM_SIZE) {
        int& n = m_seen[0x50000000u | ((pa - W42_MEM) >> 12)];
        if (n++ == 0) fprintf(stderr, "vram W page %06x pc %08x\n", pa - W42_MEM + 0x100000, m_cpu->r[15]);
        uint32_t o = pa - W42_MEM;
        if (size == 4) std::memcpy(&m_vram[o], &v, 4);
        else if (size == 2) { uint16_t h = uint16_t(v); std::memcpy(&m_vram[o & ~1u], &h, 2); }
        else m_vram[o] = uint8_t(v);
        return;
    }
    uint32_t a4 = pa & ~3u;
    if (size < 4) {                                       // merge partial writes into the register
        uint32_t sh = (pa & 3) * 8, mask = (size == 1 ? 0xffu : 0xffffu) << sh;
        v = (reg(a4) & ~mask) | ((v << sh) & mask);
    }
    pa = a4;
    if (pa >= 0x00200000 && pa < 0x00225000) {
        uint32_t base = pa & 0xfffff000, off = pa & 0xfff;
        switch (base) {
        case 0x00202000: case 0x00203000: {
            sync();
            Timer& t = m_tim[base == 0x00203000];
            switch (off) {
            case 0x00:
                if (v & 0x8000) { t = Timer(); break; }  // software reset
                if ((v & 1) && !(t.tctl & 1)) { t.tcn = 0; t.acc = 0; }
                t.tctl = v; break;
            case 0x04: t.tprer = v; break;
            case 0x08: t.tcmp = v; break;
            case 0x14: t.tstat = 0; break;            // any write clears the status
            }
            setIrq(base == 0x00203000 ? INT_TIM2 : INT_TIM1, (t.tstat & 1) && (t.tctl & (1u << 4)));
            updateIrq();
            return;
        }
        case 0x00204000:
            sync();
            switch (off) {
            case 0x00: m_rtcSeconds = (m_rtcSeconds / 86400) * 86400 + ((v >> 8) & 0x1f) * 3600 + (v & 0x3f) * 60 + m_rtcSeconds % 60; break;
            case 0x04: m_rtcSeconds = m_rtcSeconds - m_rtcSeconds % 60 + (v & 0x3f); break;
            case 0x10: m_rtcRtcctl = v; break;
            case 0x14: m_rtcIsr &= ~v; break;
            case 0x18: m_rtcIenr = v; break;
            case 0x20: m_rtcSeconds = m_rtcSeconds % 86400 + (v & 0x1ff) * 86400; break;
            default: reg(pa) = v;
            }
            setIrq(INT_RTC, (m_rtcIsr & m_rtcIenr) != 0);
            updateIrq();
            return;
        case 0x00206000: case 0x00207000:
            if (off == 0x40) {
                char c = char(v & 0xff);
                serialLog += c;
                if (dbg()) fputc(c, stderr);
                return;
            }
            reg(pa) = v;
            return;
        case 0x00223000:
            switch (off) {
            case 0x00: m_intCntl = v; break;
            case 0x04: m_niMask = v; break;
            case 0x08: m_intEnable |= 1ull << (v & 63); break;
            case 0x0c: m_intEnable &= ~(1ull << (v & 63)); break;
            case 0x10: m_intEnable = (m_intEnable & 0xffffffffull) | uint64_t(v) << 32; break;
            case 0x14: m_intEnable = (m_intEnable & ~0xffffffffull) | v; break;
            case 0x18: m_intType = (m_intType & 0xffffffffull) | uint64_t(v) << 32; break;
            case 0x1c: m_intType = (m_intType & ~0xffffffffull) | v; break;
            case 0x50: m_intForce = (m_intForce & 0xffffffffull) | uint64_t(v) << 32; break;
            case 0x54: m_intForce = (m_intForce & ~0xffffffffull) | v; break;
            default:
                if (off >= 0x20 && off < 0x40) m_niPrio[(off - 0x20) / 4] = v;
                else unknown("W", pa, v, size);
            }
            updateIrq();
            return;
        case 0x0021c000: {
            int port = (off >> 8) & 3;
            Port& p = m_gpio[port];
            switch (off & 0xff) {
            case 0x00: p.ddir = v; break;
            case 0x04: p.ocr1 = v; break;
            case 0x08: p.ocr2 = v; break;
            case 0x0c: case 0x10: case 0x14: case 0x18: p.iconf[((off & 0xff) - 0x0c) / 4] = v; break;
            case 0x1c: p.dr = v; break;
            case 0x20: p.gius = v; break;
            case 0x28: p.icr1 = v; break;
            case 0x2c: p.icr2 = v; break;
            case 0x30: p.imr = v; break;
            case 0x34: p.isr &= ~v; break;
            case 0x38: p.gpr = v; break;
            case 0x3c: p.swr = v; break;
            case 0x40: p.puen = v; break;
            }
            gpioUpdate();
            return;
        }
        case 0x0021b000: case 0x00201000: case 0x00220000: case 0x00221000:
            reg(pa) = v;
            return;
        case 0x00217000:
            i2cWrite(off, v);
            return;
        }
        reg(pa) = v;
        unknown("W", pa, v, size);
        return;
    }
    if (pa - FLASH_BASE < FLASH_SIZE || pa < 0x100000) {
        unknown("Wflash", pa, v, size);
        return;
    }
    if (pa - W42_REG < 0x10000) { w42Write(pa - W42_REG, v, size); return; }
    if (pa - W42_WRAP < 0x80000) { if (size == 4) cpWord(v); else unknown("Wwrap", pa, v, size); return; }
    m_regs[pa] = v;
    unknown("W", pa, v, size);
}

// ---------------------------------------------------------------------------
// I2C (i.MX1: IADR 0x00, IFDR 0x04, I2CR 0x08, I2SR 0x0c, I2DR 0x10)
// ---------------------------------------------------------------------------
uint32_t ZodiacCore::i2cRead(uint32_t off)
{
    switch (off) {
    case 0x08: return m_i2cr;
    case 0x0c: return m_i2sr;
    case 0x10: {
        uint32_t v = m_i2dr;
        if ((m_i2cr & 0x20) && !(m_i2cr & 0x10)) {         // master receive: reading starts the next byte
            m_i2dr = i2cDevRead();
            m_i2sr |= 0x82;                                  // ICF, IIF
            setIrq(39, (m_i2cr & 0x40) != 0); updateIrq();
        }
        return v;
    }
    }
    return reg(0x00217000 + off);
}

void ZodiacCore::i2cWrite(uint32_t off, uint32_t v)
{
    switch (off) {
    case 0x08: {
        uint32_t old = m_i2cr;
        m_i2cr = v & 0xfc;
        if (!(v & 0x80)) { m_i2sr = 0x81; m_i2cPhase = 0; break; }
        if ((v & 0x20) && !(old & 0x20)) { m_i2sr |= 0x20; m_i2cPhase = 1; }         // START
        else if (!(v & 0x20) && (old & 0x20)) { m_i2sr &= ~0x20u; m_i2cPhase = 0; }   // STOP
        if (v & 0x04) { m_i2cPhase = 1; m_i2cr &= ~0x04u; }                           // repeated START
        break;
    }
    case 0x0c:
        m_i2sr = (m_i2sr & ~0x12u) | (v & 0x12);                                      // IIF, IAL clear
        if (!(m_i2sr & 2)) { setIrq(39, false); updateIrq(); }
        break;
    case 0x10:
        if (m_i2cPhase == 1) {
            m_i2cAddr = (v >> 1) & 0x7f;
            m_i2cRead = v & 1;
            m_i2cPhase = 2;
            if (!m_i2cRead) m_i2cWa = 0;
            bool ack = m_i2cAddr == 0x50 || (m_i2cAddr >= 0x50 && m_i2cAddr <= 0x57);
            if (dbg()) fprintf(stderr, "i2c start %02x %s%s\n", m_i2cAddr, m_i2cRead ? "read" : "write", ack ? "" : " (no ack)");
            m_i2sr = (m_i2sr & ~1u) | (ack ? 0 : 1);
            // (in receive mode the driver's first, dummy read of I2DR starts the first byte)
        } else if (m_i2cPhase == 2) {
            i2cDevWrite(uint8_t(v));
            m_i2sr &= ~1u;
        }
        m_i2sr |= 0x82;
        setIrq(39, (m_i2cr & 0x40) != 0); updateIrq();
        break;
    default:
        reg(0x00217000 + off) = v;
    }
}

uint8_t ZodiacCore::i2cDevRead()
{
    if (m_i2cAddr >= 0x50 && m_i2cAddr <= 0x57) {
        uint8_t b = m_eeprom[m_i2cPtr % m_eeprom.size()];
        if (dbg() > 1) fprintf(stderr, "i2c eeprom[%03x] -> %02x\n", m_i2cPtr, b);
        m_i2cPtr++;
        return b;
    }
    return 0xff;
}

void ZodiacCore::i2cDevWrite(uint8_t v)
{
    if (dbg() > 1) fprintf(stderr, "i2c %02x <- %02x\n", m_i2cAddr, v);
    if (m_i2cAddr >= 0x50 && m_i2cAddr <= 0x57) {
        // one word-address byte (24C02/04/08/16 style: the device address picks the 256-byte block)
        if (m_i2cWa < 1) { m_i2cPtr = (m_i2cAddr & 7) << 8 | v; m_i2cWa++; return; }
        m_eeprom[m_i2cPtr++ % m_eeprom.size()] = v;
    }
}

// ---------------------------------------------------------------------------
void ZodiacCore::runFrame()
{
    const int SLICES = 50;
    const int64_t slice = int64_t(CPU_HZ / 60 / SLICES / CPI);
    for (int i = 0; i < SLICES; i++) {
        uint64_t i0 = m_cpu->insn_count;
        m_cpu->run(slice);
        uint64_t ran = m_cpu->insn_count - i0;
        if (ran < uint64_t(slice)) m_idle += uint64_t(slice) - ran;
        sync();
    }
    m_frame++;
    if (getenv("ZODSEENRESET") && m_frame == uint64_t(atoi(getenv("ZODSEENRESET")))) { m_seen.clear(); fprintf(stderr, "---- seen reset ----\n"); }
    renderScreen();
    m_audioPending = 735;
    if (dbg() && (m_frame % 60) == 0)
        fprintf(stderr, "[frame %llu pc %08x cpsr %08x src %016llx en %016llx type %016llx cntl %x nimask %x vec %08x]\n",
                (unsigned long long)m_frame, m_cpu->r[15], m_cpu->cpsr,
                (unsigned long long)m_intSrc, (unsigned long long)m_intEnable, (unsigned long long)m_intType,
                m_intCntl, m_niMask, niVector());
}

// Scan-out: the frame is 480 x 320 RGB565, 960 bytes a line, from GRAPHIC_OFFSET (a W4200
// internal address). The CRTC itself is set up for the panel's native 320 x 480 portrait
// scan; how GRAPHIC_PITCH (0x280) relates to the landscape layout isn't known yet.
void ZodiacCore::renderScreen()
{
    uint32_t off = reg(W42_REG + 0x418);
    if (off < 0x100000) return;
    uint32_t base = off - 0x100000;
    if (base + 960 * 320 > m_vram.size()) return;
    for (int y = 0; y < 320; y++) {
        const uint8_t* p = &m_vram[base + uint32_t(y) * 960];
        uint32_t* o = &m_fb[y * 480];
        for (int x = 0; x < 480; x++) {
            uint16_t c = uint16_t(p[x * 2] | p[x * 2 + 1] << 8);
            uint32_t r = (c >> 11) & 0x1f, g = (c >> 5) & 0x3f, b = c & 0x1f;
            o[x] = 0xff000000u | (r << 19 | (r >> 2) << 16) | (g << 10 | (g >> 4) << 8) | (b << 3 | b >> 2);
        }
    }
}

size_t ZodiacCore::readAudio(int16_t* out, size_t maxSamples)
{
    size_t n = std::min(maxSamples, m_audioPending);
    std::memset(out, 0, n * sizeof(int16_t));
    m_audioPending -= n;
    return n;
}
