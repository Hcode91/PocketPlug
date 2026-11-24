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

enum { INT_GPIOA = 11, INT_GPIOB = 12, INT_GPIOC = 13, INT_RTC = 17, INT_TIM2 = 58, INT_TIM1 = 59, INT_DMA = 61, INT_GPIOD = 62, INT_SDHC = 35 };

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
    m_rtcSeconds = 0; m_rtcRtcctl = 0; m_rtcAlrmHM = m_rtcAlrmSec = m_rtcDayAlarm = 0; m_rtcStpwch = 0x3f;
    warmReset();
    reg(0x0021b800) = 0;            // RSR: power-on reset
}

// Reset with the SDRAM kept, as a soft reset leaves it: Palm OS finds its storage heap (the
// databases, preferences and calibration) and carries on with it.
void ZodiacCore::warmReset()
{
    std::fill(m_esram.begin(), m_esram.end(), 0);
    std::fill(m_vram.begin(), m_vram.end(), 0);
    m_regs.clear();
    m_intSrc = m_intEnable = m_intType = m_intForce = 0;
    m_intCntl = 0; m_niMask = 0x1f;
    std::memset(m_niPrio, 0, sizeof m_niPrio);
    for (auto& t : m_tim) t = Timer();
    for (auto& p : m_gpio) p = Port();
    for (auto& d : m_dma) d = DmaCh();
    m_dmaDcr = 0; m_dmaDisr = 0; m_dmaDimr = 0x7ff; m_audio.clear();
    m_w42IntStat = 0;
    m_sinceReset = 0; m_sdOut = SD_BOOT_FRAMES;
    m_sd.reset(); m_mmcClk = false; m_mmcStat = 0; m_mmcResp.clear(); m_mmcRespPos = 0; m_mmcRead = m_mmcWrite = 0;
    for (int i = 0; i < 4; i++) m_gpio[i].last = gpioIn(i);
    // clocks at reset (i.MX1 manual defaults)
    reg(0x0021b000) = 0x2f00ac03;   // CSCR
    reg(0x0021b004) = 0x003f1437;   // MPCTL0
    reg(0x0021b008) = 0;            // MPCTL1
    reg(0x0021b00c) = 0x07b32da5;   // SPCTL0
    reg(0x0021b010) = 0x00008000;   // SPCTL1 (locked)
    reg(0x0021b020) = 0x00000015;   // PCDR
    reg(0x0021b800) = 0x00000001;   // RSR: external (reset button) reset
    reg(0x0021b804) = 0x0005d01d;   // SIDR: MC9328MX1 silicon id
    m_rtcIsr = 0; m_rtcIenr = 0; m_rtcAcc = 0;   // the RTC itself runs on through a reset
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
    tickDma(sec);
    // RTC: seconds counter, 1 Hz and sampling interrupts
    m_rtcAcc += sec;
    while (m_rtcAcc >= 1.0) {
        m_rtcAcc -= 1.0;
        if (m_rtcRtcctl & 0x80) {
            m_rtcSeconds++;
            uint32_t s = m_rtcSeconds % 60, m = (m_rtcSeconds / 60) % 60, h = (m_rtcSeconds / 3600) % 24, d = (m_rtcSeconds / 86400) & 0x1ff;
            m_rtcIsr |= 0x10;                                  // 1 Hz
            if (s == 0) {
                m_rtcIsr |= 0x02;                              // minute
                if (m_rtcStpwch != 0x3f) {                     // stopwatch: counts minutes down
                    if (m_rtcStpwch == 0) { m_rtcIsr |= 0x01; m_rtcStpwch = 0x3f; }
                    else m_rtcStpwch--;
                }
                if (m == 0) { m_rtcIsr |= 0x20; if (h == 0) m_rtcIsr |= 0x08; }   // hour, day
            }
            if (s == (m_rtcAlrmSec & 0x3f) && m == (m_rtcAlrmHM & 0x3f) && h == ((m_rtcAlrmHM >> 8) & 0x1f) && d == (m_rtcDayAlarm & 0x1ff))
                m_rtcIsr |= 0x04;                              // alarm
        }
    }
    setIrq(INT_RTC, (m_rtcIsr & m_rtcIenr) != 0);
    updateIrq();
}

// Input pins (1 = high). Port B bit 14 is the power circuit's low-battery signal (low =
// battery fine).
uint32_t ZodiacCore::gpioIn(int port)
{
    uint32_t v = 0xffffffffu;
    if (port == 1) v &= ~(1u << 14);
    if (port == 1 && sdPresent()) v &= ~(1u << 20);   // SD slot card detect (low = card in)
    if (port == 0 && (m_w42IntStat & reg(W42_REG + 0x200) & 0xffff)) v &= ~(1u << 13);   // W4200 interrupt
    // Buttons pull their pins low. Pins found in the ROM's key scan:
    // A10 power, A7 home (launch), A8/A9 triggers, D12 function, D7 nav select,
    // D9 action A, D8 action B, D14 action C, D10 action D.
    static const struct { uint32_t bit; int port, pin; } keyPins[] = {
        {0x1000, 0, 10}, {0x800, 0, 7}, {0x100, 0, 8}, {0x200, 0, 9}, {0x400, 3, 12},
        {0x10, 3, 9}, {0x20, 3, 8}, {0x40, 3, 14}, {0x80, 3, 10}, {0x2000, 3, 7},
    };
    for (auto& k : keyPins)
        if (port == k.port && (m_keys & k.bit)) v &= ~(1u << k.pin);
    return v;
}

// Input pins that changed raise their port's interrupt per ICR (00 rising, 01 falling,
// 10 high, 11 low) when unmasked in IMR.
void ZodiacCore::gpioInputsChanged()
{
    for (int p = 0; p < 4; p++) {
        uint32_t now = gpioIn(p), was = m_gpio[p].last;
        m_gpio[p].last = now;
        for (int b = 0; b < 32; b++) {
            uint32_t m = 1u << b;
            uint32_t mode = ((b < 16 ? m_gpio[p].icr1 >> (b * 2) : m_gpio[p].icr2 >> ((b - 16) * 2))) & 3;
            bool hit = false;
            switch (mode) {
            case 0: hit = (now & m) && !(was & m); break;
            case 1: hit = !(now & m) && (was & m); break;
            case 2: hit = (now & m) != 0; break;
            case 3: hit = !(now & m); break;
            }
            if (hit) m_gpio[p].isr |= m;
        }
    }
    gpioUpdate();
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
            case 0x00: return ((m_rtcSeconds / 3600) % 24) << 8 | ((m_rtcSeconds / 60) % 60);   // HOURMIN
            case 0x08: return m_rtcAlrmHM;
            case 0x0c: return m_rtcAlrmSec;
            case 0x1c: return m_rtcStpwch;
            case 0x24: return m_rtcDayAlarm;
            case 0x04: return m_rtcSeconds % 60;                                         // SECONDS
            case 0x10: return m_rtcRtcctl;
            case 0x14: return m_rtcIsr;
            case 0x18: return m_rtcIenr;
            case 0x20: return (m_rtcSeconds / 86400) & 0x1ff;                           // DAYR
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
                if (dbg() > 1) { int& n = m_seen[0x60000000u | uint32_t(port) << 24 | (m_cpu->r[14] & 0xffffff)]; if (n++ < 2) fprintf(stderr, "gpio%c SSR = %08x (ddir %08x gius %08x) pc %08x lr %08x\n", 'A' + port, v, p.ddir, p.gius, m_cpu->current_pc(), m_cpu->r[14]); }
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
        case 0x00209000:
            return dmaRead(off);
        case 0x00214000:
            return mmcRead(off);
        case 0x00215000:
            return aspRead(off);
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
//   0x15 polyline:  points (x<<16|y) joined in DP_BRUSH_FRGD_CLR
//   0x1a fill:      pos (x<<16|y), size (w<<16|h); colour DP_BRUSH_FRGD_CLR (0x107c) into DST
//   0x19 host data: pos (y<<16|x), size (h<<16|w), then pixels into DST - RGB565, two per
//                   word, or (DP_GUI_MASTER_CNTL bit 31) 4-bit coverage of DP_SRC_FRGD_CLR
//   0x1b copy:      src pos, dst pos (x<<16|y), size (w<<16|h) from SRC to DST
//   0x1c keyed copy: flags, key colour, -, src pos, dst pos, size (w<<16|h); source pixels equal
//                   to the key are skipped
// Stretch blit from registers: source surface 0x1060 (pitch 0x1064) from its corner,
// stepping 0x1088 (x<<16|y, 1/256 pixel) per destination pixel; destination DST_OFFSET /
// DST_PITCH at 0x1194 (x<<16|y), 0x1080 x 0x1084 pixels, kept inside 0x1190..0x1198.
void ZodiacCore::w42ScaledBlit()
{
    uint32_t gmc = reg(W42_REG + 0x106c);
    if (((gmc >> 16) & 0xff) != 0x98) return;
    uint32_t dOff = reg(W42_REG + 0x1004), dPitch = reg(W42_REG + 0x1008) & 0x3fff;
    uint32_t so = reg(W42_REG + 0x1060) & 0xffffff, sp = reg(W42_REG + 0x1064) & 0x3fff;
    int sx = 0, sy = 0;           // (0x1058 holds 4 here; what it means isn't known)
    uint32_t step = reg(W42_REG + 0x1088);
    int stx = int(step >> 16), sty = int(step & 0xffff);
    int dx0 = int(reg(W42_REG + 0x1194) >> 16), dy0 = int(reg(W42_REG + 0x1194) & 0xffff);
    int w = int(reg(W42_REG + 0x1080) & 0xffff), h = int(reg(W42_REG + 0x1084) & 0xffff);
    int cl = int(reg(W42_REG + 0x1190) >> 16), ct = int(reg(W42_REG + 0x1190) & 0xffff);
    int cr = int(reg(W42_REG + 0x1198) >> 16), cb = int(reg(W42_REG + 0x1198) & 0xffff);
    if (!stx) stx = 256;
    if (!sty) sty = 256;
    if (dbg() > 1) fprintf(stderr, "w42 scaled blit %x/%x (%d,%d) step %x -> %x/%x (%d,%d) %dx%d\n", so, sp, sx, sy, step, dOff, dPitch, dx0, dy0, w, h);
    for (int y = 0; y < h && y < 2048; y++) {
        int Y = dy0 + y;
        if (Y < ct || Y >= cb) continue;
        for (int x = 0; x < w && x < 2048; x++) {
            int X = dx0 + x;
            if (X < cl || X >= cr) continue;
            uint16_t* s = w42Pixel(so, sp, sx + x * stx / 256, sy + y * sty / 256);
            if (!s) continue;
            if (uint16_t* d = w42Pixel(dOff, dPitch, X, Y)) *d = *s;
        }
    }
}

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
        if (n++ < (dbg() > 1 ? 400 : 3)) {
            fprintf(stderr, "cp type3 op %02x len %zu:", op, body.size());
            for (size_t i = 0; i < body.size() && i < 12; i++) fprintf(stderr, " %08x", body[i]);
            fprintf(stderr, "  | dst %x/%x src %x/%x gmc %08x brush %08x srcfg %08x\n",
                    reg(W42_REG + 0x1004), reg(W42_REG + 0x1008), reg(W42_REG + 0x11ac), reg(W42_REG + 0x11b0),
                    reg(W42_REG + 0x106c), reg(W42_REG + 0x107c), reg(W42_REG + 0x11d8));
        }
    }
    if (dbg() > 0) { static std::map<uint32_t, int> cnt; if (cnt[op]++ == 0) fprintf(stderr, "cp new op %02x\n", op); }
    uint32_t dOff = reg(W42_REG + 0x1004), dPitch = reg(W42_REG + 0x1008) & 0x3fff;
    uint32_t sOff = reg(W42_REG + 0x11ac), sPitch = reg(W42_REG + 0x11b0) & 0x3fff;
    switch (op) {
    case 0x15: {
        // polyline: points (x<<16|y), joined in order in DP_BRUSH_FRGD_CLR
        uint16_t c = uint16_t(reg(W42_REG + 0x107c));
        for (size_t i = 0; i + 1 < body.size(); i++) {
            int x0 = int(body[i] >> 16), y0 = int(body[i] & 0xffff), x1 = int(body[i + 1] >> 16), y1 = int(body[i + 1] & 0xffff);
            int dx = std::abs(x1 - x0), dy = -std::abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
            for (int guard = 0; guard < 4096; guard++) {
                if (uint16_t* p = w42Pixel(dOff, dPitch, x0, y0)) *p = c;
                if (x0 == x1 && y0 == y1) break;
                int e2 = 2 * err;
                if (e2 >= dy) { err += dy; x0 += sx; }
                if (e2 <= dx) { err += dx; y0 += sy; }
            }
        }
        break;
    }
    case 0x1a: {
        if (body.size() < 2) break;
        int x0 = int(body[0] >> 16), y0 = int(body[0] & 0xffff), w = int(body[1] >> 16), h = int(body[1] & 0xffff);
        uint32_t gmc = reg(W42_REG + 0x106c);
        if (((gmc >> 16) & 0xff) == 0x98) {
            // with a source: a (scaled) blit from the rectangle at 0x1058 (x<<16|y), 0x1080 x
            // 0x1084, of the surface at 0x1060 / 0x1064
            uint32_t so = reg(W42_REG + 0x1060) & 0xffffff, sp = reg(W42_REG + 0x1064) & 0x3fff;
            int sx = int(reg(W42_REG + 0x1058) >> 16), sy = int(reg(W42_REG + 0x1058) & 0xffff);
            int sw = int(reg(W42_REG + 0x1080) & 0xffff), sh = int(reg(W42_REG + 0x1084) & 0xffff);
            if (sw <= 0 || sh <= 0 || w <= 0 || h <= 0) break;
            std::vector<uint16_t> tmp(size_t(w) * size_t(h));
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++) {
                    uint16_t* p = w42Pixel(so, sp, sx + x * sw / w, sy + y * sh / h);
                    tmp[size_t(y) * size_t(w) + size_t(x)] = p ? *p : 0;
                }
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++)
                    if (uint16_t* p = w42Pixel(dOff, dPitch, x0 + x, y0 + y)) *p = tmp[size_t(y) * size_t(w) + size_t(x)];
            break;
        }
        uint16_t c = uint16_t(reg(W42_REG + 0x107c));
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++)
                if (uint16_t* p = w42Pixel(dOff, dPitch, x0 + x, y0 + y)) *p = c;
        break;
    }
    case 0x19: {
        if (body.size() < 2) break;
        int y0 = int(body[0] >> 16), x0 = int(body[0] & 0xffff), h = int(body[1] >> 16), w = int(body[1] & 0xffff);
        uint32_t gmc = reg(W42_REG + 0x106c);
        bool coverage = (gmc & 0x80000000u) != 0;
        uint32_t dtype = (gmc >> 12) & 3;           // 3 colour, 0 mono with background, 1 mono transparent
        size_t n = 0, total = size_t(w) * size_t(h);
        if (!coverage && dtype != 3) {
            // 1 bit a pixel, most significant bit of each byte first, rows padded to 32 bits
            uint16_t fg = uint16_t(reg(W42_REG + 0x1240)), bg = uint16_t(reg(W42_REG + 0x123c));   // source foreground / background colours
            size_t rowWords = (size_t(w) + 31) / 32;
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++) {
                    size_t wi = 2 + size_t(y) * rowWords + size_t(x) / 32;
                    if (wi >= body.size()) break;
                    int bit = x % 32, byte = bit / 8;
                    bool on = (body[wi] >> (byte * 8 + 7 - bit % 8)) & 1;
                    if (!on && dtype == 1) continue;
                    if (uint16_t* p = w42Pixel(dOff, dPitch, x0 + x, y0 + y)) *p = on ? fg : bg;
                }
        } else if (coverage) {
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
            sx = int(body[3] >> 16); sy = int(body[3] & 0xffff);
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
    case 0x204: v = m_w42IntStat; break;       // GEN_INT_STATUS
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
    case 0x220: {
        // command ring write pointer (dwords): run the packets up to it at once. Ring base is
        // CP_RB_BASE (0x214, internal address), size 8 << (CP_RB_CNTL & 0x3f) bytes; 0x21c is
        // the read pointer.
        reg(W42_REG + off) = v;
        uint32_t base = reg(W42_REG + 0x214) & 0xffffff, words = (8u << (reg(W42_REG + 0x210) & 0x3f)) / 4;
        uint32_t rp = reg(W42_REG + 0x21c);
        if (dbg() > 1) { static int n = 0; if (n++ < 30) { uint32_t a = base - 0x100000 + rp * 4; fprintf(stderr, "ring wptr %x rptr %x base %x words %x first %08x %08x %08x\n", v, rp, base, words, a + 12 <= m_vram.size() ? le32(&m_vram[a]) : 0, a + 12 <= m_vram.size() ? le32(&m_vram[a + 4]) : 0, a + 12 <= m_vram.size() ? le32(&m_vram[a + 8]) : 0); } }
        if (words < 4 || words > 0x100000 || base < 0x100000) { reg(W42_REG + 0x21c) = v; return; }
        for (uint32_t guard = 0; rp != v % words && guard < words; guard++) {
            uint32_t a = base - 0x100000 + rp * 4;
            if (a + 4 <= m_vram.size()) cpWord(le32(&m_vram[a]));
            rp = (rp + 1) % words;
        }
        reg(W42_REG + 0x21c) = rp;
        return;
    }
    case 0xd0c:
        if ((v & 0x400) && !(reg(W42_REG + off) & 0x400)) {
            m_adcFifo.clear();
            if (dbg()) { int& n = m_seen[0x70000000u | (reg(W42_REG + 0xd08) & 7)]; if (n++ < 3) fprintf(stderr, "adc start sel %u pc %08x lr %08x\n", reg(W42_REG + 0xd08) & 7, m_cpu->current_pc(), m_cpu->r[14]); }
            switch (reg(W42_REG + 0xd08) & 7) {
            case 0:
                m_adcFifo.push_back(1u << 24 | uint32_t(m_stickX & 0x3ff));
                m_adcFifo.push_back(2u << 24 | uint32_t(m_stickY & 0x3ff));
                break;
            case 1: m_adcFifo.push_back(9u << 24 | 900); break;      // battery voltage: well charged (the OS refuses card launches below about 600)
            case 2: m_adcFifo.push_back(10u << 24 | 1000); break;
            default: m_adcFifo.push_back((8u + (reg(W42_REG + 0xd08) & 7)) << 24 | 512); break;
            }
        }
        break;
    case 0x1198:
        // Written last when a scaled blit is set up through the registers (not the ring):
        // 0x1190 / 0x1194 / 0x1198 frame it, the rest is as for the ring's source blit.
        reg(W42_REG + off) = v;
        w42ScaledBlit();
        return;
    case 0x204:                                // GEN_INT_STATUS: write 1 to clear
        m_w42IntStat &= ~v;
        gpioInputsChanged();
        return;
    case 0x200:
        reg(W42_REG + off) = v;
        gpioInputsChanged();
        if (dbg() > 1) fprintf(stderr, "w42 int cntl %08x pc %08x\n", v, m_cpu->current_pc());
        return;
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
            case 0x08: m_rtcAlrmHM = v & 0x1f3f; if (dbg() > 1) fprintf(stderr, "rtc alarm hm %x now %u frame %u\n", v, m_rtcSeconds, unsigned(m_frame)); break;
            case 0x0c: m_rtcAlrmSec = v & 0x3f; break;
            case 0x1c: m_rtcStpwch = v & 0x3f; break;
            case 0x24: m_rtcDayAlarm = v & 0x1ff; break;
            case 0x10: m_rtcRtcctl = v; break;
            case 0x14: m_rtcIsr &= ~v; break;
            case 0x18: m_rtcIenr = v; if (dbg() > 1) fprintf(stderr, "rtc ienr %x frame %u\n", v, unsigned(m_frame)); break;
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
        case 0x00209000:
            dmaWrite(off, v);
            return;
        case 0x00214000:
            mmcWrite(off, v);
            return;
        case 0x00217000:
            i2cWrite(off, v);
            return;
        case 0x00215000:
            aspWrite(off, v);
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
    if (pa - W42_WRAP < 0x80000) {
        // the CIF wrap window: writes land in the command ring (wrapping at its size); they
        // run when the driver moves the write pointer (0x220)
        uint32_t base = reg(W42_REG + 0x214) & 0xffffff, bytes = 8u << (reg(W42_REG + 0x210) & 0x3f);
        if (base >= 0x100000 && bytes >= 16 && bytes <= 0x400000) {
            uint32_t a = base - 0x100000 + ((pa - W42_WRAP) & (bytes - 1) & ~3u);
            if (a + 4 <= m_vram.size()) std::memcpy(&m_vram[a], &v, 4);
        } else if (size == 4) cpWord(v);
        return;
    }
    m_regs[pa] = v;
    unknown("W", pa, v, size);
}

// ---------------------------------------------------------------------------
// touch panel (i.MX1 ASP)
// ---------------------------------------------------------------------------
void ZodiacCore::aspUpdate()
{
    if (m_penDown != m_penWasDown) m_aspStat |= 0x400;      // pen interrupt on touch and on lift
    m_penWasDown = m_penDown;
    setIrq(46, (m_aspStat & 0x400) && (reg(0x00215018) & 0x10));
    updateIrq();
}

uint32_t ZodiacCore::aspRead(uint32_t off)
{
    switch (off) {
    case 0x00: {
        uint32_t v;
        int k = m_aspIdx++ % 4;                    // conversions come in fours: reference, x, y, spare
        if (!m_penDown) v = (k == 1 || k == 2) ? 0xff80 : 0;   // lifted: no contact
        else if (k == 0) v = 0;
        // The panel is mounted portrait (its x reads up the landscape screen); the ROM's factory
        // limits are x 36-366 (down the landscape screen), y 32-382 (across it).
        else if (k == 1) v = uint32_t(36 + int((1 - m_penY) * 330)) << 7;
        else if (k == 2) v = uint32_t(32 + int(m_penX * 350)) << 7;
        else v = 0;
        if (dbg() > 2) fprintf(stderr, "asp sample %d = %04x pc %08x\n", k, v, m_cpu->current_pc());
        return v;
    }
    case 0x1c: return m_aspStat | (m_penDown ? 0 : 0x400);   // while lifted the pen bit stays up
    case 0x34: return m_penDown ? 12u : 0u;
    }
    return reg(0x00215000 + off);
}

void ZodiacCore::aspWrite(uint32_t off, uint32_t v)
{
    switch (off) {
    case 0x1c: m_aspStat &= ~v; break;
    case 0x10: m_aspIdx = 0; reg(0x00215000 + off) = v; break;
    default: reg(0x00215000 + off) = v;
    }
    if (dbg() > 1) fprintf(stderr, "asp W %02x = %08x pc %08x\n", off, v, m_cpu->current_pc());
    aspUpdate();
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
        if (m_penDown != m_penWasDown) aspUpdate();
        gpioInputsChanged();
    }
    m_frame++;
    m_sinceReset++;
    m_w42IntStat |= 1;                         // W4200: vertical blank
    gpioInputsChanged();
    if (m_sdOut && --m_sdOut == 0) gpioInputsChanged();
    static const char* seenReset = getenv("ZODSEENRESET");   // (debug)
    if (seenReset && m_frame == uint64_t(atoi(seenReset))) { m_seen.clear(); fprintf(stderr, "---- seen reset ----\n"); }
    renderScreen();
    m_audioPending = 735;
    if (dbg() && m_frame == 1500)
        for (int i = 0; i < 4; i++)
            fprintf(stderr, "gpio%c ddir %08x gius %08x imr %08x icr1 %08x icr2 %08x puen %08x dr %08x ocr1 %08x ocr2 %08x\n", 'A' + i,
                    m_gpio[i].ddir, m_gpio[i].gius, m_gpio[i].imr, m_gpio[i].icr1, m_gpio[i].icr2, m_gpio[i].puen, m_gpio[i].dr, m_gpio[i].ocr1, m_gpio[i].ocr2);
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
    // In the rotated scan (GRAPHIC_PITCH 0x3c0) GRAPHIC_OFFSET points at the far end of the
    // first landscape line.
    uint32_t off = reg(W42_REG + 0x418), pitch = reg(W42_REG + 0x41c) & 0xffff;
    if (pitch == 0x3c0) off -= 0x3bc;
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
    // One frame's worth: what the SSI played, padded with silence when it played nothing.
    size_t n = std::min(maxSamples, m_audioPending);
    size_t i = 0;
    for (; i < n && !m_audio.empty(); i++) { out[i] = m_audio.front(); m_audio.pop_front(); }
    for (; i < n; i++) out[i] = 0;
    m_audioPending -= n;
    if (m_audio.size() > 44100 / 4) m_audio.erase(m_audio.begin(), m_audio.end() - 44100 / 8);   // drift
    return n;
}

// ---------------------------------------------------------------------------
// DMA (i.MX1: DCR 0x00, DISR 0x04, DIMR 0x08, ...; channel n at 0x80 + n * 0x40 with
// SAR, DAR, CNTR, CCR, RSSR, BLR, RTOR/BUCR). Only transfers into the SSI transmit FIFO are
// carried out: they're the sound output, paced at the sample rate.
// ---------------------------------------------------------------------------
static const uint32_t SSI_STX = 0x00218000;
static const uint32_t MMC_BUF = 0x00214038;
static const double SOUND_RATE = 44100.0;

uint32_t ZodiacCore::dmaRead(uint32_t off)
{
    switch (off) {
    case 0x00: return m_dmaDcr;
    case 0x04: return m_dmaDisr;
    case 0x08: return m_dmaDimr;
    }
    if (off >= 0x80 && off < 0x80 + 11 * 0x40) {
        DmaCh& d = m_dma[(off - 0x80) / 0x40];
        switch ((off - 0x80) % 0x40) {
        case 0x00: return d.sar;
        case 0x04: return d.dar;
        case 0x08: return d.cntr;
        case 0x0c: return d.ccr;
        case 0x10: return d.rssr;
        case 0x14: return d.blr;
        case 0x18: return d.bucr;
        case 0x1c: return d.active ? d.pos : d.curCnt;        // CCNR: bytes moved
        }
    }
    return reg(0x00209000 + off);
}

void ZodiacCore::dmaWrite(uint32_t off, uint32_t v)
{
    if (dbg() > 2) fprintf(stderr, "dma W %03x = %08x pc %08x\n", off, v, m_cpu->current_pc());
    switch (off) {
    case 0x00: m_dmaDcr = v; if (v & 2) { for (auto& d : m_dma) d = DmaCh(); m_dmaDisr = 0; m_dmaDimr = 0x7ff; } dmaIrq(); return;
    case 0x04: m_dmaDisr &= ~v; dmaIrq(); return;
    case 0x08: m_dmaDimr = v; dmaIrq(); return;
    }
    if (off >= 0x80 && off < 0x80 + 11 * 0x40) {
        int n = (off - 0x80) / 0x40;
        DmaCh& d = m_dma[n];
        switch ((off - 0x80) % 0x40) {
        case 0x00: d.sar = v; return;
        case 0x04: d.dar = v; return;
        case 0x08: d.cntr = v & 0xffffff; return;
        case 0x0c: {
            sync();
            bool was = d.ccr & 1;
            d.ccr = v;
            if ((v & 1) && !was) dmaStart(n);
            else if (!(v & 1)) d.active = false;
            return;
        }
        case 0x10: d.rssr = v; return;
        case 0x14: d.blr = v; return;
        case 0x18: d.bucr = v; return;
        }
    }
    reg(0x00209000 + off) = v;
}

void ZodiacCore::dmaStart(int n)
{
    DmaCh& d = m_dma[n];
    d.curSar = d.sar; d.curCnt = d.cntr; d.pos = 0; d.acc = 0;
    d.active = d.dar == SSI_STX && d.curCnt != 0;
    if (d.sar == MMC_BUF || d.dar == MMC_BUF) mmcDma();
    else if (d.dar != SSI_STX && d.curCnt && !(d.ccr & 0x800)) dmaMemCopy(n);   // no request line: memory to memory
}

uint8_t* ZodiacCore::hostPtr(uint32_t pa, uint32_t& avail)
{
    if (pa - W42_MEM < W42_MEM_SIZE) { avail = W42_MEM_SIZE - (pa - W42_MEM); return &m_vram[pa - W42_MEM]; }
    if (!page_readonly(pa)) if (uint8_t* p = page_ptr(pa)) { avail = 0x1000 - (pa & 0xfff); return p + (pa & 0xfff); }
    avail = 0;
    return nullptr;
}

void ZodiacCore::dmaMemCopy(int n)
{
    DmaCh& d = m_dma[n];
    if (dbg() > 1) fprintf(stderr, "dma ch%d copy %08x -> %08x %u bytes ccr %x\n", n, d.sar, d.dar, d.curCnt, d.ccr);
    uint32_t s = d.sar, t = d.dar, left = d.curCnt;
    while (left) {
        uint32_t as, at;
        uint8_t* ps = hostPtr(s, as);
        uint8_t* pt = hostPtr(t, at);
        uint32_t k = std::min(left, std::min(ps ? as : 1u, pt ? at : 1u));
        if (ps && pt) std::memcpy(pt, ps, k);
        else for (uint32_t i = 0; i < k; i++) write8(t + i, read8(s + i));
        s += k; t += k; left -= k;
    }
    d.curCnt = 0;
    d.ccr &= ~1u;
    m_dmaDisr |= 1u << n;
    dmaIrq();
}

void ZodiacCore::dmaComplete(int n)
{
    DmaCh& d = m_dma[n];
    // The buffer holds 16-bit stereo frames; the output is mono.
    for (uint32_t i = 0; i + 4 <= d.curCnt; i += 4) {
        int l = int16_t(read16(d.curSar + i)), r = int16_t(read16(d.curSar + i + 2));
        m_audio.push_back(int16_t((l + r) / 2));
    }
    m_dmaDisr |= 1u << n;
    if (d.ccr & 0x3000) dmaStart(n);                    // repeat: go again from the (new) registers
    else { d.active = false; d.ccr &= ~1u; }
    dmaIrq();
}

void ZodiacCore::tickDma(double sec)
{
    for (int n = 0; n < 11; n++) {
        DmaCh& d = m_dma[n];
        if (!d.active) continue;
        d.acc += sec * SOUND_RATE * 4;                // bytes at 16-bit stereo
        uint32_t step = uint32_t(d.acc);
        d.acc -= step;
        d.pos += step;
        if (d.pos >= d.curCnt) dmaComplete(n);
    }
}

void ZodiacCore::dmaIrq()
{
    setIrq(INT_DMA, (m_dmaDisr & ~m_dmaDimr & 0x7ff) != 0);
    updateIrq();
}

// ---------------------------------------------------------------------------
// saved state
// ---------------------------------------------------------------------------
static const char STATE_MAGIC[8] = { 'Z', 'O', 'D', 'R', 'A', 'M', '0', '1' };

bool ZodiacCore::saveState(std::vector<uint8_t>& out)
{
    sync();
    out.assign(STATE_MAGIC, STATE_MAGIC + 8);
    auto put64 = [&](uint64_t v) { for (int i = 0; i < 8; i++) out.push_back(uint8_t(v >> (i * 8))); };
    put64(uint64_t(m_rtcSeconds) | uint64_t(m_rtcRtcctl) << 32);
    put64(uint64_t(m_wallClock));
    z_stream z{};
    if (deflateInit(&z, 1) != Z_OK) return false;
    const std::vector<uint8_t>* parts[] = { &m_ram0, &m_ram1, &m_eeprom };
    std::vector<uint8_t> buf(1 << 20);
    for (int i = 0; i < 3; i++) {
        z.next_in = const_cast<uint8_t*>(parts[i]->data());
        z.avail_in = uInt(parts[i]->size());
        int flush = i == 2 ? Z_FINISH : Z_NO_FLUSH;
        int r;
        do {
            z.next_out = buf.data(); z.avail_out = uInt(buf.size());
            r = deflate(&z, flush);
            if (r == Z_STREAM_ERROR) { deflateEnd(&z); return false; }
            out.insert(out.end(), buf.data(), buf.data() + (buf.size() - z.avail_out));
        } while (z.avail_out == 0 || (flush == Z_FINISH && r != Z_STREAM_END));
    }
    deflateEnd(&z);
    return true;
}

bool ZodiacCore::loadState(const std::vector<uint8_t>& in)
{
    if (in.size() < 24 || std::memcmp(in.data(), STATE_MAGIC, 8)) return false;
    auto get64 = [&](size_t at) { uint64_t v = 0; for (int i = 0; i < 8; i++) v |= uint64_t(in[at + i]) << (i * 8); return v; };
    uint64_t rtc = get64(8);
    int64_t wall = int64_t(get64(16));
    z_stream z{};
    if (inflateInit(&z) != Z_OK) return false;
    z.next_in = const_cast<uint8_t*>(in.data() + 24);
    z.avail_in = uInt(in.size() - 24);
    std::vector<uint8_t>* parts[] = { &m_ram0, &m_ram1, &m_eeprom };
    for (int i = 0; i < 3; i++) {
        z.next_out = parts[i]->data();
        z.avail_out = uInt(parts[i]->size());
        int r = inflate(&z, Z_SYNC_FLUSH);
        if ((r != Z_OK && r != Z_STREAM_END) || z.avail_out) { inflateEnd(&z); reset(); return false; }
    }
    inflateEnd(&z);
    warmReset();
    // the clock carries on from where it was left, plus the time spent away
    m_rtcRtcctl = uint32_t(rtc >> 32);
    m_rtcSeconds = uint32_t(rtc) + uint32_t(m_wallClock > wall && wall ? m_wallClock - wall : 0);
    return true;
}

// ---------------------------------------------------------------------------
// MMC/SD host (i.MX1 SDHC: STR_STP_CLK 0x00, STATUS 0x04, CLK_RATE 0x08, CMD_DAT_CONT 0x0c,
// RES_TO 0x10, READ_TO 0x14, BLK_LEN 0x18, NOB 0x1c, REV_NO 0x20, INT_MASK 0x24, CMD 0x28,
// ARGH 0x2c, ARGL 0x30, RES_FIFO 0x34, BUFFER_ACCESS 0x38). Transfers complete at once.
// ---------------------------------------------------------------------------
enum {
    MMC_TIME_OUT_RESP = 1u << 1, MMC_APPL_BUFF_FE = 1u << 6, MMC_APPL_BUFF_FF = 1u << 7,
    MMC_CLK_RUN = 1u << 8, MMC_DATA_TRANS_DONE = 1u << 11, MMC_WRITE_OP_DONE = 1u << 12,
    MMC_END_CMD_RESP = 1u << 13, MMC_CARD_PRESENCE = 1u << 15,
};

uint32_t ZodiacCore::mmcRead(uint32_t off)
{
    switch (off) {
    case 0x04: {
        uint32_t st = m_mmcStat;
        if (m_mmcClk) st |= MMC_CLK_RUN;
        if (sdPresent()) st |= MMC_CARD_PRESENCE;
        if (m_mmcRead) st |= MMC_APPL_BUFF_FF;
        if (m_mmcWrite) st |= MMC_APPL_BUFF_FE;
        if (dbg() > 2) fprintf(stderr, "mmc status %08x pc %08x\n", st, m_cpu->current_pc());
        return st;
    }
    case 0x20: return 0x0400;                 // REV_NO
    case 0x34: {
        uint32_t v = m_mmcRespPos < m_mmcResp.size() ? m_mmcResp[m_mmcRespPos++] : 0;
        return v;
    }
    case 0x38: {
        if (!m_mmcRead) return 0;
        uint32_t v = 0;
        for (int i = 0; i < 2 && m_mmcRead; i++) {
            if (m_sd.dataOut.empty()) m_sd.moreRead();
            uint8_t b = 0;
            if (!m_sd.dataOut.empty()) { b = m_sd.dataOut.front(); m_sd.dataOut.erase(m_sd.dataOut.begin()); }
            v |= uint32_t(b) << (8 * i);
            m_mmcRead--;
        }
        if (!m_mmcRead) { m_mmcStat |= MMC_DATA_TRANS_DONE; mmcIrq(); }
        return v;
    }
    }
    return reg(0x00214000 + off);
}

void ZodiacCore::mmcWrite(uint32_t off, uint32_t v)
{
    switch (off) {
    case 0x00:
        if (v & 8) { m_mmcStat = 0; m_mmcResp.clear(); m_mmcRead = m_mmcWrite = 0; }
        if (v & 2) m_mmcClk = true;
        if (v & 1) m_mmcClk = false;
        break;
    case 0x04:
        m_mmcStat &= ~v;
        mmcIrq();
        return;
    case 0x0c:
        reg(0x00214000 + off) = v;
        mmcCommand(v);
        return;
    case 0x24:
        reg(0x00214000 + off) = v;
        mmcIrq();
        return;
    case 0x38:
        if (m_mmcWrite) {
            for (int i = 0; i < 2 && m_mmcWrite; i++) {
                uint8_t b = uint8_t(v >> (8 * i));
                m_sd.writeData(&b, 1);
                m_mmcWrite--;
            }
            if (!m_mmcWrite) { m_mmcStat |= MMC_WRITE_OP_DONE | MMC_DATA_TRANS_DONE; mmcIrq(); }
        }
        return;
    }
    reg(0x00214000 + off) = v;
}

void ZodiacCore::mmcCommand(uint32_t cont)
{
    int cmd = int(reg(0x00214028) & 0x3f);
    uint32_t arg = (reg(0x0021402c) & 0xffff) << 16 | (reg(0x00214030) & 0xffff);
    int fmt = int(cont & 7);
    std::vector<uint8_t> r;
    bool ok = sdPresent() && m_sd.command(cmd, arg, r);
    if (dbg() > 1) fprintf(stderr, "mmc cmd %d arg %08x cont %04x -> %s\n", cmd, arg, cont, ok ? "ok" : "timeout");
    m_sdActivity = 1;
    m_mmcResp.clear(); m_mmcRespPos = 0;
    m_mmcRead = m_mmcWrite = 0;
    m_mmcStat &= ~(MMC_DATA_TRANS_DONE | MMC_WRITE_OP_DONE | MMC_TIME_OUT_RESP);
    m_mmcStat |= MMC_END_CMD_RESP;
    if (!ok) {
        if (fmt) m_mmcStat |= MMC_TIME_OUT_RESP;
        mmcIrq();
        return;
    }
    if (fmt == 2 && r.size() == 16) {
        for (int i = 0; i < 16; i += 2) m_mmcResp.push_back(uint16_t(r[i] << 8 | r[i + 1]));
    } else if (fmt && r.size() == 4) {
        uint8_t b[6] = { uint8_t(fmt == 3 ? 0x3f : cmd), r[0], r[1], r[2], r[3], 0x01 };
        for (int i = 0; i < 6; i += 2) m_mmcResp.push_back(uint16_t(b[i] << 8 | b[i + 1]));
    }
    if (cont & 8) {                                    // data
        uint32_t len = (reg(0x00214018) & 0xfff) * std::max<uint32_t>(1, reg(0x0021401c) & 0xffff);
        if (cont & 0x10) m_mmcWrite = len;
        else {
            m_mmcRead = len;
            if (m_sd.dataOut.empty() && !m_sd.multiRead) m_mmcRead = 0;
        }
        mmcDma();
    }
    mmcIrq();
}

// DMA between memory and the SD data buffer: an enabled channel moves its count at once
// when the host has data waiting (reads) or wants data (writes).
void ZodiacCore::mmcDma()
{
    for (int n = 0; n < 11; n++) {
        DmaCh& d = m_dma[n];
        if (!(d.ccr & 1) || d.active || !d.curCnt) continue;
        bool rd = d.sar == MMC_BUF, wr = d.dar == MMC_BUF;
        if (!(rd && m_mmcRead) && !(wr && m_mmcWrite)) continue;
        uint32_t cnt = d.curCnt;
        if (dbg() > 1) fprintf(stderr, "mmc dma ch%d %s %u bytes %08x\n", n, rd ? "read" : "write", cnt, rd ? d.dar : d.sar);
        for (uint32_t i = 0; i < cnt; i += 2) {
            if (rd) {
                uint32_t v = mmcRead(0x38);
                write8(d.dar + i, uint8_t(v));
                if (i + 1 < cnt) write8(d.dar + i + 1, uint8_t(v >> 8));
            } else {
                uint32_t v = read8(d.sar + i) | (i + 1 < cnt ? uint32_t(read8(d.sar + i + 1)) << 8 : 0);
                mmcWrite(0x38, v);
            }
        }
        d.curCnt = 0;
        m_dmaDisr |= 1u << n;
        d.ccr &= ~1u;
        dmaIrq();
    }
}

void ZodiacCore::mmcIrq()
{
    uint32_t mask = reg(0x00214024);
    bool on = ((m_mmcStat & MMC_END_CMD_RESP) && !(mask & 4)) ||
              ((m_mmcStat & MMC_DATA_TRANS_DONE) && !(mask & 1)) ||
              ((m_mmcStat & MMC_WRITE_OP_DONE) && !(mask & 2)) ||
              ((m_mmcRead || m_mmcWrite) && !(mask & 8));
    setIrq(INT_SDHC, on);
    updateIrq();
}

// ---------------------------------------------------------------------------
// SD card media
// ---------------------------------------------------------------------------
static const uint64_t NEW_CARD_BYTES = 128ull << 20;

bool ZodiacCore::mediaInsert(int drive, const std::string& path, std::string& error)
{
    if (drive == 0) {
        std::vector<uint8_t> img;
        if (FILE* f = fopen(path.c_str(), "rb")) {
            fseek(f, 0, SEEK_END);
            long n = ftell(f);
            fseek(f, 0, SEEK_SET);
            if (n > 0) { img.resize(size_t(n)); if (fread(img.data(), 1, img.size(), f) != img.size()) img.clear(); }
            fclose(f);
        }
        if (img.empty()) img = fat::format(NEW_CARD_BYTES);
        img.resize((img.size() + 511) & ~size_t(511));
        m_sd.image.swap(img);
        m_sd.dirty = false;
        m_sd.reset();
        m_sdOut = std::max(0, SD_BOOT_FRAMES - int(m_sinceReset));
        gpioInputsChanged();
        return true;
    }
    if (drive == 1) {
        std::string src = path, dest;
        size_t bar = path.find('|');
        if (bar != std::string::npos) { src = path.substr(0, bar); dest = path.substr(bar + 1); }
        std::vector<uint8_t> data;
        FILE* f = fopen(src.c_str(), "rb");
        if (!f) { error = "Can't read " + src; return false; }
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        data.resize(size_t(std::max(0L, n)));
        bool ok = data.empty() || fread(data.data(), 1, data.size(), f) == data.size();
        fclose(f);
        if (!ok) { error = "Can't read " + src; return false; }
        if (dest.empty()) {
            std::string name = src.substr(src.find_last_of("/\\") + 1), ext;
            if (name.rfind('.') != std::string::npos) ext = name.substr(name.rfind('.') + 1);
            for (auto& ch : ext) ch = char(tolower((unsigned char)ch));
            dest = (ext == "prc" || ext == "pdb" || ext == "pqa") ? "/PALM/Launcher/" + name : "/" + name;
        }
        if (!m_sd.present()) { m_sd.image = fat::format(NEW_CARD_BYTES); m_sd.reset(); }
        if (!fat::addFile(m_sd.image, dest, data, error)) return false;
        m_sd.dirty = true;
        // out and back in, so Palm OS looks at the card again
        m_sd.reset();
        m_sdOut = std::max(30, SD_BOOT_FRAMES - int(m_sinceReset));
        gpioInputsChanged();
        return true;
    }
    error = "No such drive";
    return false;
}

bool ZodiacCore::mediaSave(int drive, const std::string& path)
{
    if (drive != 0 || !m_sd.present()) return false;
    if (!m_sd.dirty) if (FILE* e = fopen(path.c_str(), "rb")) { fclose(e); return true; }   // unchanged
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = fwrite(m_sd.image.data(), 1, m_sd.image.size(), f) == m_sd.image.size();
    fclose(f);
    if (ok) m_sd.dirty = false;
    return ok;
}

void ZodiacCore::mediaEject(int drive)
{
    if (drive != 0) return;
    m_sd.image.clear();
    m_sd.reset();
    gpioInputsChanged();
}
