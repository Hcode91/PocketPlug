// PocketPlug - Mattel HyperScan (2006)
// SPDX-License-Identifier: BSD-3-Clause
//
// Device models follow MAME's spg29x driver and SPG290 devices (spg290_timer, spg290_i2c,
// spg290_ppu, spg290_cdservo, hyperscan_ctrl; license BSD-3-Clause, copyright-holders:
// Sandro Ronco), rewritten around a cycle-counting scheduler instead of MAME's timers.
#include "hyperscan.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static const int LEADIN_LEN = 7500;
static const int LEADOUT_LEN = 6750;

static inline uint32_t le32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | uint32_t(p[3]) << 24; }
static inline uint8_t dec2bcd(uint32_t v) { return uint8_t((v / 10) << 4 | (v % 10)); }
static inline uint32_t bcd2dec(uint32_t v) { return (v >> 4) * 10 + (v & 15); }
static inline uint32_t lbaToMsf(uint32_t lba) { return uint32_t(dec2bcd(lba / (60 * 75))) << 16 | uint32_t(dec2bcd(lba / 75 % 60)) << 8 | dec2bcd(lba % 75); }
#define COMBINE(reg) ((reg) = ((reg) & ~mask) | (v & mask))

HyperScanCore::HyperScanCore()
{
    m_ram.assign(16 << 20, 0);
    m_sram.assign(16 << 10, 0);
    m_irom.assign(32 << 10, 0);
    m_dspMem.assign(0x10000, 0);
    m_out.assign(640 * 480, 0xff000000u);
    m_bmp.assign(640 * 480, 0xff000000u);
    m_cpu = new Score7(this);
    m_cpu->user = this;
}

HyperScanCore::~HyperScanCore() { delete m_cpu; }

static bool readWhole(const RomFile& r, std::vector<uint8_t>& out)
{
    if (!r.data.empty()) { out = r.data; return true; }
    FILE* f = fopen(r.path.c_str(), "rb");
    if (!f) return false;
    fseeko(f, 0, SEEK_END);
    long n = long(ftello(f));
    fseeko(f, 0, SEEK_SET);
    out.resize(size_t(std::max(0L, n)));
    bool ok = out.empty() || fread(out.data(), 1, out.size(), f) == out.size();
    fclose(f);
    return ok;
}

bool HyperScanCore::load(const std::vector<RomFile>& roms, std::string& error)
{
    std::string disc;
    for (auto& r : roms) {
        std::string n = r.name;
        for (auto& c : n) c = char(tolower((unsigned char)c));
        if (n == "hyperscan.bin") { if (!readWhole(r, m_bios)) { error = "Can't read hyperscan.bin"; return false; } }
        else if (n == "spg290.bin") { std::vector<uint8_t> d; if (readWhole(r, d)) { d.resize(m_irom.size()); m_irom = d; } }
        else disc = r.path;
    }
    if (m_bios.size() < 0x100000) { error = "hyperscan.bin (the console's BIOS, 1 MB) is missing"; return false; }
    m_bios.resize(0x100000);
    if (!disc.empty() && !m_cd.open(disc, error)) return false;
    reset();
    return true;
}

void HyperScanCore::reset()
{
    std::fill(m_ram.begin(), m_ram.end(), 0);
    std::fill(m_sram.begin(), m_sram.end(), 0);
    m_cycle = 0;
    m_tveControl = 0; m_tveFade = 0; m_tveBufCtrl = 3;
    m_tvStart[0] = m_tvStart[1] = m_tvStart[2] = 0;
    m_timersClkSel = 0; m_gpioOut = 0;
    for (auto& t : m_timer) t = Timer();
    m_i2cConfig = m_i2cIrq = m_i2cClock = m_i2cId = m_i2cPort = m_i2cW = m_i2cR = 0;
    m_i2cAt = UINT64_MAX; m_i2cPeriod = 0;
    std::memset(m_ctrlData, 0, sizeof m_ctrlData);
    std::memset(m_ppuRegs, 0, sizeof m_ppuRegs);
    m_ppuIrqStatus = 0;
    m_visW = 640; m_visH = 480;
    m_dacBufStart = m_dacClockCfg = m_dacMode1 = m_dacMode2 = m_dacOutL = m_dacOutR = 0;
    m_dacDivider = m_dacBaseLo = m_dacBaseHi = m_dacIrq = m_dacPos = 0;
    m_dacPhase = 0; m_dacLast = 0; m_dacOut.clear();
    cdReset();
    m_cpu->reset();
    m_cpu->cr(29) = 0x20000000;          // JTAG off
    m_frame = 0;
    m_failed = false;
}

// ---------------------------------------------------------------------------
// time
// ---------------------------------------------------------------------------
uint64_t HyperScanCore::now() const
{
    return m_cycle + uint64_t(std::max(0, m_sliceLen - m_cpu->cyclesLeft()));
}

void HyperScanCore::schedule() { m_cpu->stop(); }

void HyperScanCore::timerUpdate(int i, uint64_t t)
{
    Timer& tm = m_timer[i];
    if (t <= tm.last) return;
    uint64_t dt = t - tm.last;
    tm.last = t;
    if (!tm.enabled || !(tm.control & 0x80000000u)) return;
    double ticks = double(dt) * tm.hz / CPU_HZ + tm.frac;
    double whole = std::floor(ticks);
    tm.frac = ticks - whole;
    uint64_t n = uint64_t(whole);
    uint32_t period = 0x10000 - (tm.preload & 0xffff);
    while (n) {
        if (tm.counter == 0xffff) {
            tm.counter = tm.preload & 0xffff;
            n--;
            if (tm.control & (1u << 27)) { tm.control |= 0x04000000; m_cpu->set_input(56); }
            if (n > period * 4) n %= period;       // the interrupt is latched once anyway
        } else {
            uint64_t step = std::min<uint64_t>(n, 0xffff - tm.counter);
            tm.counter += uint32_t(step);
            n -= step;
        }
    }
}

uint64_t HyperScanCore::timerNext(int i) const
{
    const Timer& tm = m_timer[i];
    if (!tm.enabled || !(tm.control & 0x80000000u) || !(tm.control & (1u << 27))) return UINT64_MAX;
    double ticks = double(0xffff - tm.counter) + 1.0 - tm.frac;
    return tm.last + uint64_t(std::ceil(ticks * CPU_HZ / tm.hz));
}

void HyperScanCore::runFrame()
{
    if (m_failed) return;
    uint64_t frameStart = m_frame * FRAME_CYCLES;
    uint64_t vblankAt = frameStart + 480 * LINE_CYCLES, frameEnd = frameStart + FRAME_CYCLES;
    ppuVblank(false);
    bool vblankDone = false;
    while (m_cycle < frameEnd && !m_cpu->halted) {
        if (!vblankDone && m_cycle >= vblankAt) { vblankDone = true; ppuVblank(true); render(); }
        uint64_t next = vblankDone ? frameEnd : vblankAt;
        for (int i = 0; i < 6; i++) next = std::min(next, timerNext(i));
        next = std::min(next, std::min(m_i2cAt, m_cdAt));
        int slice = int(std::max<int64_t>(6, int64_t(next) - int64_t(m_cycle)));
        slice = std::min(slice, int(LINE_CYCLES * 8));
        uint64_t before = m_cpu->insns;
        m_sliceLen = slice;
        m_cpu->run(slice);
        m_cycle += (m_cpu->insns - before) * 6;
        m_sliceLen = 0;
        for (int i = 0; i < 6; i++) timerUpdate(i, m_cycle);
        dacRun(m_cycle);
        if (m_i2cAt <= m_cycle) i2cEvent();
        while (m_cdAt <= m_cycle) cdEvent();
    }
    if (!vblankDone) render();
    if (m_cpu->halted && !m_failed) {
        m_failed = true;
        debugLog += m_cpu->failMsg;
        fprintf(stderr, "hyperscan: CPU stopped: %s", m_cpu->failMsg);
    }
    m_frame++;
}

size_t HyperScanCore::readAudio(int16_t* out, size_t maxSamples)
{
    // one frame of 48 kHz from what the DAC played (at its own rate) during the frame
    size_t n = std::min<size_t>(maxSamples, 801);
    if (m_dacOut.empty()) { std::memset(out, 0, n * sizeof(int16_t)); return n; }
    double step = double(m_dacOut.size()) / double(n);
    for (size_t i = 0; i < n; i++) {
        size_t k = std::min(m_dacOut.size() - 1, size_t(double(i) * step));
        out[i] = m_dacOut[k];
    }
    m_dacOut.clear();
    return n;
}

void HyperScanCore::dacRun(uint64_t t)
{
    if (t <= m_dacLast) return;
    uint64_t dt = t - m_dacLast;
    m_dacLast = t;
    if (!dacOn()) return;
    m_dacPhase += double(dt) * 54000000.0 / CPU_HZ / double(m_dacDivider + 1);
    bool stereo = m_dacIrq & 4;
    uint32_t base = (m_dacBaseHi & 0xffff) << 16 | (m_dacBaseLo & 0xffff);
    while (m_dacPhase >= 1.0) {
        m_dacPhase -= 1.0;
        uint32_t la = (base + m_dacPos) & 0x1fffffff, ra = stereo ? la + 2 : la;
        auto rd = [&](uint32_t a) { return a < 0x08000000 ? uint32_t(m_ram[a & 0xffffff] | m_ram[(a + 1) & 0xffffff] << 8) : 0x8000u; };
        int l = int(int16_t(rd(la) ^ 0x8000)), r = int(int16_t(rd(ra) ^ 0x8000));
        m_dacOutL = rd(la); m_dacOutR = rd(ra);
        if (m_dacOut.size() < 48000) m_dacOut.push_back(int16_t((l + r) / 2));
        uint32_t prev = m_dacPos, size = dacSize();
        m_dacPos = (m_dacPos + (stereo ? 4 : 2)) % size;
        if ((prev < size / 2 && m_dacPos >= size / 2) || m_dacPos < prev) {
            m_dacIrq |= 0x8000;
            if (m_dacIrq & 0x4000) m_cpu->set_input(63);
        }
    }
}

bool HyperScanCore::dacReg(uint32_t a, uint32_t* r, uint32_t v, bool write)
{
    uint32_t* reg = nullptr;
    switch (a) {
    case 0x08070054: reg = &m_dacBufStart; break;
    case 0x0821003c: reg = &m_dacClockCfg; break;
    case 0x08051034: reg = &m_dacMode2; break;
    case 0x08051040: reg = &m_dacOutL; break;
    case 0x08051044: reg = &m_dacOutR; break;
    case 0x08051064: reg = &m_dacDivider; break;
    case 0x08051080: reg = &m_dacBaseLo; break;
    case 0x08051084: reg = &m_dacBaseHi; break;
    case 0x08051088: reg = &m_dacIrq; break;
    case 0x08051474: reg = &m_dacMode1; break;
    default: return false;
    }
    if (!write) { *r = *reg; return true; }
    if (debugLevel > 1) fprintf(stderr, "dac W %08x = %08x frame %llu\n", a, v, (unsigned long long)m_frame);
    dacRun(now());
    if (a == 0x08051088) {
        uint32_t pending = m_dacIrq & 0x8000;
        m_dacIrq = (v & 0x7fff) | pending;
        if (v & 0x8000) m_dacIrq &= ~0x8000u;
        m_dacPos %= dacSize();
    } else {
        *reg = v;
        if (a == 0x08070054) m_dacPos = v % dacSize();
    }
    return true;
}

// ---------------------------------------------------------------------------
// bus
// ---------------------------------------------------------------------------
uint32_t HyperScanCore::read32(uint32_t a)
{
    a &= 0x1fffffff;
    if (a < 0x08000000) return le32(&m_ram[a & 0xfffffc]);
    if (a >= 0x10000000) return le32(&m_bios[a & 0xffffc]);
    if ((a >> 24) == 0x0a) return le32(&m_sram[a & 0x3ffc]);
    if ((a >> 24) == 0x0b) return le32(&m_irom[a & 0x7ffc]);
    return ioRead(a & ~3u, 0xffffffffu);
}

uint16_t HyperScanCore::read16(uint32_t a)
{
    a &= 0x1fffffff;
    if (a < 0x08000000) { const uint8_t* p = &m_ram[a & 0xfffffe]; return uint16_t(p[0] | p[1] << 8); }
    int sh = (a & 2) * 8;
    return uint16_t(read32ForSub(a, 0xffffu << sh) >> sh);
}

uint8_t HyperScanCore::read8(uint32_t a)
{
    a &= 0x1fffffff;
    if (a < 0x08000000) return m_ram[a & 0xffffff];
    int sh = (a & 3) * 8;
    return uint8_t(read32ForSub(a, 0xffu << sh) >> sh);
}

uint32_t HyperScanCore::read32ForSub(uint32_t a, uint32_t mask)
{
    if (a >= 0x10000000) return le32(&m_bios[a & 0xffffc]);
    if ((a >> 24) == 0x0a) return le32(&m_sram[a & 0x3ffc]);
    if ((a >> 24) == 0x0b) return le32(&m_irom[a & 0x7ffc]);
    return ioRead(a & ~3u, mask);
}

void HyperScanCore::write32(uint32_t a, uint32_t v) { writeMasked(a & ~3u, v, 0xffffffffu); }
void HyperScanCore::write16(uint32_t a, uint16_t v) { int sh = (a & 2) * 8; writeMasked(a & ~3u, uint32_t(v) << sh, 0xffffu << sh); }
void HyperScanCore::write8(uint32_t a, uint8_t v) { int sh = (a & 3) * 8; writeMasked(a & ~3u, uint32_t(v) << sh, 0xffu << sh); }

void HyperScanCore::writeMasked(uint32_t a, uint32_t v, uint32_t mask)
{
    a &= 0x1fffffff;
    uint8_t* p = nullptr;
    if (a < 0x08000000) p = &m_ram[a & 0xfffffc];
    else if ((a >> 24) == 0x0a) p = &m_sram[a & 0x3ffc];
    else if (a >= 0x08000000 && a < 0x0a000000) { ioWrite(a, v, mask); return; }
    else return;                                   // ROM
    for (int i = 0; i < 4; i++) if ((mask >> (i * 8)) & 0xff) p[i] = uint8_t(v >> (i * 8));
}

void HyperScanCore::unknown(const char* what, uint32_t a, uint32_t v)
{
    for (auto& s : m_seen) if (s.first == a) { if (++s.second > 3) return; goto log; }
    m_seen.push_back({ a, 1 });
log:
    if (debugLevel) fprintf(stderr, "hs: %s %08x %08x pc %08x\n", what, a, v, m_cpu->m_ppc);
}

uint32_t HyperScanCore::ioRead(uint32_t a, uint32_t mask)
{
    (void)mask;
    if (a >= 0x08010000 && a < 0x08020000) return ppuRead(a & 0xffff);
    if (a >= 0x08060000 && a < 0x08070000) {
        switch (a & 0xffff) {
        case 0x08: return m_cdData;
        case 0x0c: return 0;                      // servo ready
        case 0x40: return m_cdControl0;
        case 0x44: return m_cdControl1;
        case 0x48: return m_seekMin;
        case 0x4c: return m_seekSec;
        case 0x50: return m_seekFrm;
        case 0x54: return 0;
        case 0x60: return m_cdBufStart;
        case 0x64: return m_cdBufEnd;
        case 0x68: return m_cdBufPtr;
        case 0x6c: return m_sectorSize;
        }
        unknown("cd R", a, 0);
        return 0;
    }
    if (a >= 0x08130000 && a < 0x08140000) {
        switch (a & 0xffff) {
        case 0x20: return m_i2cConfig;
        case 0x24: return m_i2cIrq;
        case 0x28: return m_i2cClock;
        case 0x2c: return m_i2cId;
        case 0x30: return m_i2cPort;
        case 0x34: return m_i2cW;
        case 0x38: return m_i2cR;
        }
        unknown("i2c R", a, 0);
        return 0;
    }
    if (a >= 0x08160000 && a < 0x08166000) {
        int i = int((a >> 12) & 0xf);
        Timer& t = m_timer[i];
        switch (a & 0xfff) {
        case 0x00: return t.control;
        case 0x04: return t.control2;
        case 0x08: return t.preload;
        case 0x0c: return t.ccp;
        case 0x10: return t.upcount;
        }
        unknown("timer R", a, 0);
        return 0;
    }
    { uint32_t r; if (dacReg(a, &r, 0, false)) return r; }
    switch (a) {
    case 0x08030000: return m_tveControl;
    case 0x08070000: case 0x08070004: case 0x08070008: return m_tvStart[(a >> 2) & 3];
    case 0x0807006c: return 1;                    // SDRAM in self-refresh
    case 0x082100e4: return m_timersClkSel;
    case 0x08200024: return m_gpioOut;
    case 0x08200068: return uint32_t(cardRead());
    }
    if (a >= 0x08240000 && a < 0x08240010) return 0;
    unknown("R", a, 0);
    return 0;
}

void HyperScanCore::ioWrite(uint32_t a, uint32_t v, uint32_t mask)
{
    if (a >= 0x08010000 && a < 0x08020000) { ppuWrite(a & 0xffff, v, mask); return; }
    if (a >= 0x08060000 && a < 0x08070000) {
        switch (a & 0xffff) {
        case 0x04: COMBINE(m_cdAddr); return;
        case 0x08: COMBINE(m_cdData); return;
        case 0x0c: if (mask & 0xff) { if (v & 1) servoCmdR(); else servoCmdW(); } return;
        case 0x40: COMBINE(m_cdControl0); return;
        case 0x44:
            COMBINE(m_cdControl1);
            m_seekLba = (bcd2dec(m_seekMin) * 60 + bcd2dec(m_seekSec)) * 75 + bcd2dec(m_seekFrm);
            m_curSector = int(LEADIN_LEN + m_seekLba);
            if (debugLevel > 1) fprintf(stderr, "cd ctl1 %x seek %u frame %llu pc %08x\n", m_cdControl1, m_seekLba, (unsigned long long)m_frame, m_cpu->m_ppc);
            return;
        case 0x48: if (mask & 0xff) m_seekMin = uint8_t(v); return;
        case 0x4c: if (mask & 0xff) m_seekSec = uint8_t(v); return;
        case 0x50: if (mask & 0xff) m_seekFrm = uint8_t(v); return;
        case 0x54: return;
        case 0x60: COMBINE(m_cdBufStart); return;
        case 0x64: COMBINE(m_cdBufEnd); return;
        case 0x68: COMBINE(m_cdBufPtr); return;
        case 0x6c: COMBINE(m_sectorSize); return;
        }
        unknown("cd W", a, v);
        return;
    }
    if (a >= 0x08130000 && a < 0x08140000) {
        switch (a & 0xffff) {
        case 0x20: {
            COMBINE(m_i2cConfig);
            double clk = 27000000.0 / 4 / double((m_i2cClock & 0x3ff) + 1);
            auto bits = [&](int n) { return uint64_t(double(n) * CPU_HZ / clk); };
            uint64_t t = now();
            if (m_i2cConfig & 1) { m_i2cAt = t + bits(2 + 9 * 4); m_i2cPeriod = 0; }
            else if (m_i2cConfig & 2) { m_i2cAt = t + bits(2 + 9 * 5); m_i2cPeriod = 0; }
            else if (m_i2cConfig & 4) { m_i2cPeriod = bits(2 + 9 * 4); m_i2cAt = t + m_i2cPeriod; }
            else if (m_i2cConfig & 0x100) m_i2cAt = UINT64_MAX;
            schedule();
            return;
        }
        case 0x24:
            COMBINE(m_i2cIrq);
            if (mask & 0xff) m_i2cIrq &= ~(v & 1);
            return;
        case 0x28: COMBINE(m_i2cClock); return;
        case 0x2c: COMBINE(m_i2cId); return;
        case 0x30: COMBINE(m_i2cPort); return;
        case 0x34: COMBINE(m_i2cW); return;
        case 0x38: COMBINE(m_i2cR); return;
        }
        unknown("i2c W", a, v);
        return;
    }
    if (a >= 0x08160000 && a < 0x08166000) {
        int i = int((a >> 12) & 0xf);
        timerUpdate(i, now());
        Timer& t = m_timer[i];
        switch (a & 0xfff) {
        case 0x00:
            COMBINE(t.control);
            if (mask & 0xff000000u) t.control &= ~(v & 0x04000000);   // interrupt acknowledge
            break;
        case 0x04: COMBINE(t.control2); break;
        case 0x08: COMBINE(t.preload); break;
        case 0x0c: COMBINE(t.ccp); break;
        case 0x10: COMBINE(t.upcount); break;
        default: unknown("timer W", a, v);
        }
        schedule();
        return;
    }
    if (a >= 0x0821006c && a <= 0x08210080) {                // timer start / reload
        int i = int((a - 0x0821006c) >> 2);
        timerUpdate(i, now());
        Timer& t = m_timer[i];
        t.enabled = v & 1;
        if (v & 2) t.counter = t.preload & 0xffff;
        t.last = now();
        schedule();
        return;
    }
    if (dacReg(a, nullptr, v, true)) return;
    switch (a) {
    case 0x08030000: COMBINE(m_tveControl); {
        switch (m_tveControl & 0xc) {
        case 0x0: m_visW = 320; m_visH = 240; break;     // QVGA
        case 0x4: m_visW = 640; m_visH = 480; break;     // VGA
        case 0x8: m_visW = 640; m_visH = 240; break;     // HVGA
        default: break;
        }
    } return;
    case 0x0803000c: m_tveFade = v & 0xff; return;
    case 0x08070000: case 0x08070004: case 0x08070008: COMBINE(m_tvStart[(a >> 2) & 3]); return;
    case 0x08090020: m_tveBufCtrl = v & 3; return;
    case 0x082100e4: {
        COMBINE(m_timersClkSel);
        double hz = 27000000.0 / double((v & 0xff) + 1);
        for (int i = 0; i < 6; i++) {
            timerUpdate(i, now());
            m_timer[i].hz = (v & (0x100u << i)) ? 32768.0 : hz;
        }
        schedule();
        return;
    }
    case 0x08200024: COMBINE(m_gpioOut); return;
    }
    if (a >= 0x08240000 && a < 0x08240010) return;
    unknown("W", a, v);
}

// ---------------------------------------------------------------------------
// I2C and the controllers
// ---------------------------------------------------------------------------
void HyperScanCore::i2cEvent()
{
    if (m_i2cConfig & 0x40) {
        uint32_t off = m_i2cPort;
        int port = int((off >> 4) & 0x0f);
        m_i2cR = port < 2 ? ctrlRead(port, off) : 0xffff;
    }
    m_i2cConfig |= (m_i2cConfig & 7) << 3;           // ack
    if (m_i2cIrq & 0x02) { m_i2cIrq |= 0x01; m_cpu->set_input(39); }
    m_i2cAt = m_i2cPeriod ? m_i2cAt + m_i2cPeriod : UINT64_MAX;
}

uint16_t HyperScanCore::ctrlRead(int pad, uint32_t offset)
{
    int addr = int(offset & 0x0f);
    uint8_t* d = m_ctrlData[pad];
    if (addr < 4) {
        uint8_t in[4] = { 0, 0, 0x7f, 0x7f };
        if (pad == 0) {
            uint32_t k = m_keys;
            in[0] = uint8_t(((k & BLUE) ? 0x01 : 0) | ((k & START) ? 0x02 : 0) | ((k & SELECT) ? 0x04 : 0) |
                            ((k & L_SHOULDER) ? 0x08 : 0) | ((k & R_SHOULDER) ? 0x10 : 0) |
                            ((k & L_TRIGGER) ? 0x20 : 0) | ((k & R_TRIGGER) ? 0x40 : 0));
            in[1] = uint8_t(((k & YELLOW) ? 0x20 : 0) | ((k & RED) ? 0x40 : 0) | ((k & GREEN) ? 0x80 : 0));
            float x = m_anaX, y = m_anaY;
            if (k & LEFT) x = -1; else if (k & RIGHT) x = 1;
            if (k & UP) y = -1; else if (k & DOWN) y = 1;
            // both axes run backwards: left and up read high
            in[2] = uint8_t(std::clamp(int(0x7f - y * 0x7f), 0, 0xff));
            in[3] = uint8_t(std::clamp(int(0x7f - x * 0x7f), 0, 0xff));
        }
        d[addr] = in[addr];
        return d[addr];
    }
    return uint16_t(((d[0] + d[1] + d[2] + d[3]) << 2) | pad);   // checksum the BIOS checks
}

// ---------------------------------------------------------------------------
// PPU
// ---------------------------------------------------------------------------
uint32_t HyperScanCore::ppuRead(uint32_t off)
{
    if (off < 0x100) {
        switch (off) {
        case 0x84: return m_ppuIrqStatus;
        case 0x94: {
            uint64_t inFrame = now() - m_frame * FRAME_CYCLES;
            return m_ppuRegs[0x90 >> 2] + uint32_t(inFrame / LINE_CYCLES);
        }
        }
        return m_ppuRegs[off >> 2];
    }
    if (off >= 0x1000 && off < 0x1800) return m_charPal[(off - 0x1000) >> 2];
    if (off >= 0x1800 && off < 0x2000) return m_spritePal[(off - 0x1800) >> 2];
    if (off >= 0x2000 && off < 0x2800) return m_hoff[(off - 0x2000) >> 2];
    if (off >= 0x3000 && off < 0x3800) return m_voff[(off - 0x3000) >> 2];
    if (off >= 0x4000 && off < 0x5000) return m_spriteRam[(off - 0x4000) >> 2];
    unknown("ppu R", 0x08010000 + off, 0);
    return 0;
}

void HyperScanCore::ppuWrite(uint32_t off, uint32_t v, uint32_t mask)
{
    if (off < 0x100) {
        uint32_t& r = m_ppuRegs[off >> 2];
        switch (off) {
        case 0x80: COMBINE(r); m_ppuIrqStatus &= r; return;
        case 0x84: if (mask & 0xff) m_ppuIrqStatus &= ~v; return;
        }
        COMBINE(r);
        return;
    }
    if (off >= 0x1000 && off < 0x1800) { COMBINE(m_charPal[(off - 0x1000) >> 2]); return; }
    if (off >= 0x1800 && off < 0x2000) { COMBINE(m_spritePal[(off - 0x1800) >> 2]); return; }
    if (off >= 0x2000 && off < 0x2800) { COMBINE(m_hoff[(off - 0x2000) >> 2]); return; }
    if (off >= 0x3000 && off < 0x3800) { COMBINE(m_voff[(off - 0x3000) >> 2]); return; }
    if (off >= 0x4000 && off < 0x5000) { COMBINE(m_spriteRam[(off - 0x4000) >> 2]); return; }
    unknown("ppu W", 0x08010000 + off, v);
}

void HyperScanCore::ppuVblank(bool start)
{
    uint32_t ctl = m_ppuRegs[0x80 >> 2];
    if (start && (ctl & 0x01)) { m_ppuIrqStatus |= 0x01; m_cpu->set_input(53); }
    else if (!start && (ctl & 0x02)) { m_ppuIrqStatus |= 0x02; m_cpu->set_input(53); }
}

uint32_t HyperScanCore::spaceRead32(uint32_t a)
{
    a &= 0x1fffffff;
    if (a < 0x08000000) return le32(&m_ram[a & 0xfffffc]);
    return read32(a);
}

static inline uint32_t rgb(int r, int g, int b) { return 0xff000000u | uint32_t(std::clamp(r, 0, 255)) << 16 | uint32_t(std::clamp(g, 0, 255)) << 8 | uint32_t(std::clamp(b, 0, 255)); }
static inline int pal5(uint32_t v) { v &= 31; return int(v << 3 | v >> 2); }
static inline int pal6(uint32_t v) { v &= 63; return int(v << 2 | v >> 4); }

uint32_t HyperScanCore::blend(uint32_t c0, uint32_t c1, uint8_t level) const
{
    int r0 = int(c0 >> 16 & 255), g0 = int(c0 >> 8 & 255), b0 = int(c0 & 255);
    int r1 = int(c1 >> 16 & 255), g1 = int(c1 >> 8 & 255), b1 = int(c1 & 255);
    if (m_ppuRegs[0x0c >> 2] & 1)
        return rgb(r0 * level / 63 - r1 * (63 - level) / 63, g0 * level / 63 - g1 * (63 - level) / 63, b0 * level / 63 - b1 * (63 - level) / 63);
    return rgb(r0 * level / 63 + r1 * (63 - level) / 63, g0 * level / 63 + g1 * (63 - level) / 63, b0 * level / 63 + b1 * (63 - level) / 63);
}

void HyperScanCore::putArgb1555(int y, int x, uint16_t argb, uint8_t bl)
{
    if ((argb & 0x8000) || x < 0 || y < 0 || x >= m_visW || y >= m_visH) return;
    uint32_t c = rgb(pal5(argb >> 10), pal5(argb >> 5), pal5(argb));
    uint32_t& p = m_bmp[size_t(y) * size_t(m_visW) + size_t(x)];
    p = bl ? blend(p, c, bl) : c;
}

void HyperScanCore::putRgb565(int y, int x, uint16_t c565, uint8_t bl)
{
    uint32_t trans = m_ppuRegs[0x10 >> 2];
    if (((trans & 0x10000) && (trans & 0xffff) == c565) || x < 0 || y < 0 || x >= m_visW || y >= m_visH) return;
    uint32_t c = rgb(pal5(c565 >> 11), pal6(c565 >> 5), pal5(c565));
    uint32_t& p = m_bmp[size_t(y) * size_t(m_visW) + size_t(x)];
    p = bl ? blend(p, c, bl) : c;
}

void HyperScanCore::blitSprite(uint32_t control, uint32_t attribute, uint32_t bufStart)
{
    uint32_t spriteNum = control & 0xffff;
    int sx = int((control >> 16) & 0x3ff), sy = int((attribute >> 16) & 0x3ff);
    int hsize = 8 << ((attribute >> 4) & 3), vsize = 8 << ((attribute >> 6) & 3);
    int bitPixel = int(((attribute & 3) + 1) << 1);
    int flip = int((attribute >> 2) & 3);
    uint32_t penBank = ((attribute >> 8) & 0x1f) * 0x10;
    uint8_t bl = (attribute & 0x8000) ? uint8_t(0x3f - ((attribute >> 26) & 0x3f)) : 0;
    int pixelWord = 32 / bitPixel, pixelByte = 8 / bitPixel, wordLine = hsize / pixelWord;
    uint32_t penMask = (1u << bitPixel) - 1;
    if (spriteNum == 0) return;
    uint32_t base = bufStart + spriteNum * uint32_t(hsize * vsize * bitPixel / 8);
    if (sx > m_visW - 1) sx = (sx & 0x3ff) - 0x400;
    if (sy > m_visH - 1) sy = (sy & 0x3ff) - 0x400;
    for (int y = 0; y < vsize; y++)
        for (int x = 0; x < wordLine; x++) {
            uint32_t data = spaceRead32(base + uint32_t(y * wordLine + x) * 4);
            for (int i = 0; i < 4; i++) {
                for (int b = 0; b < pixelByte; b++) {
                    int bitpos = i * pixelByte + b;
                    uint32_t pen = (data >> (8 - (b + 1) * bitPixel)) & penMask;
                    int px = (flip & 1) ? sx + (hsize - (x * pixelWord + bitpos)) : sx + x * pixelWord + bitpos;
                    int py = (flip & 2) ? sy + (vsize - y) : sy + y;
                    putArgb1555(py, px, uint16_t(m_spritePal[(penBank + pen) & 0x1ff]), bl);
                }
                data >>= 8;
            }
        }
}

void HyperScanCore::blitBitmap(uint32_t control, uint32_t, int posy, int posx, uint32_t nptr, uint32_t bufStart, uint8_t bl)
{
    int maxX = m_visW - 1 + posx, maxY = m_visH - 1 + posy;
    for (int y = 0; y < maxY; y++) {
        if (y - posy < 0 || y - posy >= m_visH) continue;
        int line = (control & 0x04) ? 0 : y;
        uint32_t txStart = spaceRead32(nptr + uint32_t(line) * 4) * 2;
        for (int x = 0; x < maxX; x += 2) {
            uint32_t data = spaceRead32(bufStart + txStart + uint32_t(x) * 2);
            for (int b = 0; b < 2; b++) {
                if (control & 0x1000) putRgb565(y - posy, x + b - posx, uint16_t(data), bl);
                else putArgb1555(y - posy, x + b - posx, uint16_t(data), bl);
                data >>= 16;
            }
        }
    }
}

void HyperScanCore::blitCharacter(uint32_t control, uint32_t attribute, int posy, int posx, uint32_t nptr, uint32_t bufStart, uint8_t bl)
{
    int hsize = 8 << ((attribute >> 4) & 3), vsize = 8 << ((attribute >> 6) & 3);
    int bitPixel = int(((attribute & 3) + 1) << 1);
    uint32_t penBank = ((attribute >> 8) & 0x1f) * 0x10;
    int pixelWord = 32 / bitPixel, charsLine = 1024 / hsize;
    uint32_t penMask = (1u << bitPixel) - 1;
    int maxX = (m_visW - 1 + posx) / vsize + 1, maxY = (m_visH - 1 + posy) / hsize + 1;
    for (int y = 0; y <= maxY; y++)
        for (int x = 0; x <= maxX; x++) {
            int line = (control & 0x04) ? 0 : y;
            uint32_t addr = uint32_t(line * charsLine + x);
            uint32_t charId = spaceRead32(nptr + addr * 2);
            if (addr & 1) charId >>= 16;
            uint32_t charAddr = bufStart + (charId & 0xffff) * uint32_t(hsize * vsize);
            for (int cy = 0; cy < vsize; cy++) {
                int py = y * vsize + cy - posy;
                if (py < 0 || py >= m_visH) continue;
                uint32_t lineAddr = charAddr + uint32_t(cy * hsize);
                for (int cx = 0; cx < hsize; cx++) {
                    uint32_t data = spaceRead32(lineAddr + uint32_t(cx * bitPixel) / 8);
                    uint32_t pen = (data >> ((cx % pixelWord) * bitPixel)) & penMask;
                    uint16_t pix = uint16_t(m_charPal[(penBank + pen) & 0x1ff]);
                    int px = x * hsize + cx - posx;
                    if (control & 0x1000) putRgb565(py, px, pix, bl);
                    else putArgb1555(py, px, pix, bl);
                }
            }
        }
}

void HyperScanCore::render()
{
    size_t n = size_t(m_visW) * size_t(m_visH);
    if (m_bmp.size() < n) m_bmp.resize(n);
    std::fill(m_bmp.begin(), m_bmp.begin() + long(n), 0xff000000u);
    if (m_ppuRegs[0] & 0x1000) {
        for (int depth = 0; depth < 4; depth++) {
            for (int l = 0; l < 3; l++) {
                uint32_t base = 0x20 + uint32_t(l) * 0x1c;
                uint32_t posxR = m_ppuRegs[base >> 2], posyR = m_ppuRegs[(base + 4) >> 2];
                uint32_t attr = m_ppuRegs[(base + 8) >> 2], ctl = m_ppuRegs[(base + 0xc) >> 2];
                uint32_t nptr = m_ppuRegs[(base + 0x10) >> 2], blendR = m_ppuRegs[(base + 0x18) >> 2];
                uint32_t bufStart = m_ppuRegs[(0xa0 + l * 0xc) >> 2];
                if (!(ctl & 0x08) || ((attr >> 13) & 3) != uint32_t(depth)) continue;
                uint8_t bl = (ctl & 0x0100) ? uint8_t(blendR) : 0;
                int posx = int(posxR & 0x3ff); if (posx & 0x200) posx -= 0x400;
                int posy = int(posyR & 0x1ff); if (posy & 0x100) posy -= 0x200;
                if (ctl & 0x01) blitBitmap(ctl, attr, posy, posx, nptr, bufStart, bl);
                else blitCharacter(ctl, attr, posy, posx, nptr, bufStart, bl);
            }
            if (m_ppuRegs[0x04 >> 2] & 1) {
                uint32_t maxS = m_ppuRegs[0x08 >> 2] & 0x1ff;
                for (uint32_t i = 0; i <= maxS && i < 0x200; i++)
                    if (((m_spriteRam[i * 2 + 1] >> 13) & 3) == uint32_t(depth))
                        blitSprite(m_spriteRam[i * 2], m_spriteRam[i * 2 + 1], m_ppuRegs[0xd0 >> 2]);
            }
        }
    }
    // TV encoder frame buffer (RGB565 in SDRAM)
    if (m_tveBufCtrl < 3) {
        bool interlaced = m_tveControl & 1;
        uint32_t base = m_tvStart[m_tveBufCtrl];
        for (int y = 0; y < m_visH; y += interlaced ? 1 : 2)
            for (int x = 0; x < m_visW; x++) {
                uint32_t a = (base + uint32_t(y * m_visW + x) * 2) & 0x1fffffff;
                uint16_t c = a < 0x08000000 ? uint16_t(m_ram[a & 0xffffff] | m_ram[(a + 1) & 0xffffff] << 8) : 0;
                uint32_t px = rgb(pal5(c >> 11), pal6(c >> 5), pal5(c));
                m_bmp[size_t(y) * size_t(m_visW) + size_t(x)] = px;
                if (!interlaced && y + 1 < m_visH) m_bmp[size_t(y + 1) * size_t(m_visW) + size_t(x)] = px;
            }
    }
    if (m_tveFade) {
        int k = 255 - int(m_tveFade);
        for (size_t i = 0; i < n; i++) {
            uint32_t p = m_bmp[i];
            m_bmp[i] = rgb(int(p >> 16 & 255) * k / 255, int(p >> 8 & 255) * k / 255, int(p & 255) * k / 255);
        }
    }
    // scale to 640 x 480
    for (int y = 0; y < 480; y++) {
        const uint32_t* src = &m_bmp[size_t(y * m_visH / 480) * size_t(m_visW)];
        uint32_t* dst = &m_out[size_t(y) * 640];
        if (m_visW == 640) std::memcpy(dst, src, 640 * 4);
        else for (int x = 0; x < 640; x++) dst[x] = src[x * m_visW / 640];
    }
}

// ---------------------------------------------------------------------------
// CD servo
// ---------------------------------------------------------------------------
void HyperScanCore::cdReset()
{
    m_cdAddr = m_cdData = m_cdBufStart = m_cdBufEnd = m_cdBufPtr = 0;
    m_cdSpeed = 1; m_seekMin = m_seekSec = m_seekFrm = 0;
    m_seekLba = m_sectorSize = m_dspData = 0;
    m_frameFound = false;
    m_cdControl0 = m_cdControl1 = 0;
    m_cdSkip = 0;
    std::memset(m_dspRegs, 0, sizeof m_dspRegs);
    std::fill(m_dspMem.begin(), m_dspMem.end(), 0);
    m_totSectors = 0; m_curSector = 0;
    m_qsub.clear();
    if (m_cd.loaded()) generateQsub();
    m_cdAt = UINT64_MAX;
    cdTiming();
}

void HyperScanCore::cdTiming()
{
    if (m_cdSpeed == 0 || !m_cd.loaded()) { m_cdAt = UINT64_MAX; return; }
    m_cdAt = now() + uint64_t(CPU_HZ / (75.0 * m_cdSpeed * cdBoost()));
    schedule();
}

void HyperScanCore::cdEvent()
{
    if (!(m_cdControl1 & 0x04) && uint32_t(m_curSector) == m_seekLba + LEADIN_LEN) {
        uint8_t buf[2448] = {};
        m_cd.readRaw(uint32_t(m_curSector - 150 - LEADIN_LEN), buf);
        // the slot is the sector size the BIOS set (0x940: the raw sector and 16 spare bytes)
        uint32_t n = (m_cdControl0 & 0x8000) ? 2352 : std::min<uint32_t>(m_sectorSize, 2448);
        for (uint32_t i = 0; i < n; i++) {
            uint32_t a = m_cdBufPtr & 0x1fffffff;
            if (a < 0x08000000) m_ram[a & 0xffffff] = buf[i];
            else write8(m_cdBufPtr, buf[i]);
            if (++m_cdBufPtr > m_cdBufEnd) m_cdBufPtr = m_cdBufStart;
        }
        if (debugLevel > 1) fprintf(stderr, "cd sector %u -> %08x (size %u ctl0 %x) cyc %llu\n", m_seekLba, m_cdBufPtr, n, m_cdControl0, (unsigned long long)m_cycle);
        m_seekLba++;
        m_frameFound = true;
        m_cpu->set_input(60);
    }
    if (m_curSector < int(m_seekLba + LEADIN_LEN) && uint32_t(m_curSector) < m_totSectors) m_curSector++;
    m_cdAt += uint64_t(CPU_HZ / (75.0 * m_cdSpeed * cdBoost()));
}

void HyperScanCore::servoCmdR()
{
    uint32_t a = m_cdAddr;
    struct L { HyperScanCore* c; ~L() { if (c->debugLevel > 2) fprintf(stderr, "servo R %03x -> %02x\n", c->m_cdAddr, c->m_cdData); } } l{ this };
    if (a == 0x079) m_cdData = 0xe3;                         // servo status
    else if (a == 0x07b) m_cdData = 0x06;
    else if (a == 0x07c) m_cdData = m_dspData >> 8;
    else if (a == 0x07d) m_cdData = m_dspData & 0xff;
    else if (a == 0x082) m_cdData = 0x04;
    else if (a == 0x083) m_cdData = uint32_t(rand() & 0xff);
    else if (a == 0x307) m_cdData = m_frameFound;
    else if (a >= 0x340 && a <= 0x349)                       // Q subchannel
        m_cdData = m_qsub.empty() ? 0 : m_qsub[size_t(m_curSector) * 12 + (a & 0x0f)];
    else if (a == 0x34c) m_cdData = 0x40;
    else if (a == 0x506) m_cdData = 0x04;                    // DSP ready
    else if (a >= 0x500 && a < 0x50a) m_cdData = m_dspRegs[a & 0x0f];
    else if (a == 0x41f) m_cdData = 0xff;
    else m_cdData = 0;
}

void HyperScanCore::servoCmdW()
{
    uint32_t a = m_cdAddr, d = m_cdData;
    if (debugLevel > 2) fprintf(stderr, "servo W %03x = %02x cyc %llu\n", a, d, (unsigned long long)now());
    if (a == 0x013) {                                         // skip sectors
        if (d & 0x01) m_curSector += (d & 0x10) ? -int(m_cdSkip) : int(m_cdSkip);
        if (m_curSector < 0) m_curSector = 0;
        if (m_totSectors && m_curSector >= int(m_totSectors)) m_curSector = int(m_totSectors) - 1;
    } else if (a == 0x020) {                                  // speed
        uint8_t s = uint8_t(1 << (d & 3));
        if (m_cdSpeed != s) { m_cdSpeed = s; cdTiming(); }
    } else if (a == 0x030) {                                  // disc id
        if (d == 0x13) m_dspData = 8;
        else m_dspData = m_qsub.empty() ? 0 : 100;
    } else if (a == 0x032) m_dspData = 0x0102;
    else if (a == 0x19a) m_cdSkip = uint16_t((m_cdSkip & 0x00ff) | (d << 8));
    else if (a == 0x29a) m_cdSkip = uint16_t((m_cdSkip & 0xff00) | (d & 0xff));
    else if (a == 0x307) m_frameFound = false;
    else if (a == 0x505) {                                    // DSP memory
        uint16_t addr = uint16_t(m_dspRegs[0] << 8 | m_dspRegs[1]);
        if ((d & 0x0f) == 0x02) m_dspMem[addr] = m_dspRegs[2] << 16 | m_dspRegs[3] << 8 | m_dspRegs[4];
        else if ((d & 0x0f) == 0x03) {
            m_dspRegs[7] = (m_dspMem[addr] >> 16) & 0xff;
            m_dspRegs[8] = (m_dspMem[addr] >> 8) & 0xff;
            m_dspRegs[9] = m_dspMem[addr] & 0xff;
        }
    } else if (a >= 0x500 && a < 0x50a) m_dspRegs[a & 0x0f] = d;
}

void HyperScanCore::addQsub(int sector, uint8_t addrctrl, uint8_t track, uint8_t index, uint32_t relMsf, uint32_t absMsf)
{
    uint8_t* d = &m_qsub[size_t(sector) * 12];
    d[0] = uint8_t(((addrctrl & 0x0f) << 4) | ((addrctrl & 0xf0) >> 4));
    d[1] = track; d[2] = index;
    d[3] = uint8_t(relMsf >> 16); d[4] = uint8_t(relMsf >> 8); d[5] = uint8_t(relMsf);
    d[6] = 0;
    d[7] = uint8_t(absMsf >> 16); d[8] = uint8_t(absMsf >> 8); d[9] = uint8_t(absMsf);
    d[10] = d[11] = 0;
}

void HyperScanCore::generateQsub()
{
    int numtracks = m_cd.numTracks();
    const CdImage::Track& last = m_cd.track(numtracks - 1);
    uint32_t totalSectors = last.start + last.frames + 150;
    m_totSectors = LEADIN_LEN + totalSectors + LEADOUT_LEN;
    m_qsub.assign(size_t(m_totSectors) * 12, 0);
    int lba = 0;
    for (int s = 0; s < LEADIN_LEN; s += numtracks + 3) {
        if (lba < LEADIN_LEN) addQsub(lba++, 0x14, 0, 0xa0, lbaToMsf(uint32_t(s)), 1u << 16);
        if (lba < LEADIN_LEN) addQsub(lba++, 0x14, 0, 0xa1, lbaToMsf(uint32_t(s + 1)), uint32_t(numtracks) << 16);
        if (lba < LEADIN_LEN) addQsub(lba++, 0x14, 0, 0xa2, lbaToMsf(uint32_t(s + 2)), lbaToMsf(totalSectors));
        for (int t = 0; t < numtracks; t++)
            if (lba < LEADIN_LEN)
                addQsub(lba++, m_cd.adrControl(t), 0, dec2bcd(uint32_t(t + 1)), lbaToMsf(uint32_t(s + 3 + t)), lbaToMsf(m_cd.track(t).start + 150));
    }
    for (int t = 0; t < numtracks; t++) {
        uint8_t control = m_cd.adrControl(t);
        uint32_t trackStart = m_cd.track(t).start;
        uint32_t pregap = t == 0 ? 150 : 0;
        if (t != 0) trackStart += 150;
        for (uint32_t s = 0; s < pregap; s++)
            addQsub(lba++, control, dec2bcd(uint32_t(t + 1)), 0, lbaToMsf(s), lbaToMsf(trackStart + s));
        trackStart += pregap;
        for (uint32_t s = 0; s < m_cd.track(t).frames; s++)
            addQsub(lba++, control, dec2bcd(uint32_t(t + 1)), 1, lbaToMsf(s), lbaToMsf(trackStart + s));
    }
    for (int s = 0; s < LEADOUT_LEN && lba < int(m_totSectors); s++)
        addQsub(lba++, 0x14, 0xaa, 1, lbaToMsf(uint32_t(s)), lbaToMsf(totalSectors + uint32_t(s)));
}
