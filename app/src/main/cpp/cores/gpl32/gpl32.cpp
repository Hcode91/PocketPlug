#include <string>
// PocketPlug - GeneralPlus GPL32600 games (work in progress). See gpl32.h.
// SPDX-License-Identifier: GPL-3.0-or-later

#include "gpl32.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
constexpr int PAGE = 2048, SPARE = 64, PAGES_PER_BLOCK = 64;
bool dbg() { static int v = getenv("GPLDBG") ? 1 : 0; return v; }
}

GplCore::GplCore() : m_ram(RAM_SIZE, 0), m_fb(W * H, 0xff000000u) {}
GplCore::~GplCore()
{
    delete m_cpu;
}

bool GplCore::load(const std::vector<RomFile>& roms, std::string& error)
{
    for (auto& r : roms) if (r.data.size() == 0x8400000) m_nand = r.data;
    if (m_nand.empty()) { error = "Missing tc58nvg0s3eta00.u3 (the 132 MB NAND dump)"; return false; }
    // find the APP header: the second block starting with "GPNandTag2"; the image follows in the next block
    const size_t blockBytes = size_t(PAGE + SPARE) * PAGES_PER_BLOCK;
    int tags = 0; size_t appBlock = 0; uint32_t appPages = 0;
    for (size_t b = 0; b * blockBytes < m_nand.size() && b < 64; b++) {
        const uint8_t* p = &m_nand[b * blockBytes];
        if (!std::memcmp(p, "GPNandTag2", 10)) {
            if (++tags == 2) { appBlock = b + 1; appPages = uint32_t(p[16] | p[17] << 8); break; }
            b++;   // the boot area's tag repeats in its next page; skip on
        }
    }
    if (!appBlock || !appPages) { error = "No application found in the NAND"; return false; }
    m_app.clear();
    for (uint32_t i = 0; i < appPages; i++) {
        size_t off = (appBlock * PAGES_PER_BLOCK + i) * size_t(PAGE + SPARE);
        if (off + PAGE > m_nand.size()) break;
        m_app.insert(m_app.end(), m_nand.begin() + long(off), m_nand.begin() + long(off + PAGE));
    }
    if (dbg()) fprintf(stderr, "app at block %zu, %u pages (%zu bytes)\n", appBlock, appPages, m_app.size());
    delete m_cpu;
    m_cpu = new didj::ARM926(this);
    reset();
    return true;
}

void GplCore::reset()
{
    std::fill(m_ram.begin(), m_ram.end(), 0);
    std::memcpy(m_ram.data(), m_app.data(), std::min(m_app.size(), m_ram.size()));
    m_regs.clear();
    std::fill(m_ppuRam.begin(), m_ppuRam.end(), 0u);
    // The start-up check at 0x9a730 probes a part on the keypad/camera lines with a handshake
    // that isn't emulated; on the real board it passes, so make it return "present".
    static const uint32_t probe[4] = { 0xe92d4010u, 0xeb0230d5u, 0xe1a04000u, 0xebfffd76u };
    if (m_ram.size() > 0x9a740 && std::memcmp(&m_ram[0x9a730], probe, sizeof probe) == 0) {
        static const uint32_t ret1[2] = { 0xe3a00001u, 0xe12fff1eu };   // mov r0, #1; bx lr
        std::memcpy(&m_ram[0x9a730], ret1, sizeof ret1);
    }
    if (const char* pt = getenv("GPLPATCH")) {      // debugging: "addr=word,addr=word"
        std::string spec(pt);
        size_t i = 0;
        while (i < spec.size()) {
            size_t e = spec.find(',', i); if (e == std::string::npos) e = spec.size();
            std::string item = spec.substr(i, e - i);
            size_t q = item.find('=');
            if (q != std::string::npos) {
                uint32_t a = uint32_t(strtoul(item.substr(0, q).c_str(), nullptr, 16));
                uint32_t w = uint32_t(strtoul(item.substr(q + 1).c_str(), nullptr, 16));
                if (a + 4 <= RAM_SIZE) std::memcpy(&m_ram[a], &w, 4);
            }
            i = e + 1;
        }
    }
    m_cpu->reset();
    if (getenv("GPLBRK")) {
        m_cpu->hle_watch_pc = uint32_t(strtoul(getenv("GPLBRK"), nullptr, 16));
        m_cpu->hle_user = this;
        m_cpu->hle_hook = [](didj::ARM926* c, uint32_t pc, void* u) -> bool {
            auto* self = static_cast<GplCore*>(u);
            fprintf(stderr, "BRK %08x r0 %08x r1 %08x r2 %08x r3 %08x lr %08x :", pc, c->r[0], c->r[1], c->r[2], c->r[3], c->r[14]);
            uint32_t m = getenv("GPLBRKMEM") ? uint32_t(strtoul(getenv("GPLBRKMEM"), nullptr, 16)) : c->r[0];
            for (int i = 0; i < 32; i++) fprintf(stderr, " %02x", self->read8(m + uint32_t(i)));
            fprintf(stderr, "\n");
            return false;
        };
    }
    m_lastSync = m_cpu->insn_count;
    m_cpu->r[15] = 0;
}

static uint32_t watchAddr()
{
    static uint32_t a = getenv("GPLWATCH") ? uint32_t(strtoul(getenv("GPLWATCH"), nullptr, 16)) : 0xffffffffu;
    return a;
}

uint8_t* GplCore::page_ptr(uint32_t pa)
{
    if ((pa >> 12) == (watchAddr() >> 12)) return nullptr;
    if (pa < RAM_SIZE) return m_ram.data() + (pa & ~0xfffu);
    if ((pa & 0xfff00000u) == 0xf8000000u) return m_sram.data() + (pa & 0xff000u);
    return nullptr;
}

uint32_t GplCore::read32(uint32_t pa)
{
    if (pa < RAM_SIZE) { uint32_t v; std::memcpy(&v, &m_ram[pa & ~3u], 4); return v; }
    if ((pa & 0xfff00000u) == 0xf8000000u) { uint32_t v; std::memcpy(&v, &m_sram[pa & 0xffffcu], 4); return v; }
    return ioRead(pa & ~3u);
}

void GplCore::write32(uint32_t pa, uint32_t v)
{
    if ((pa & ~3u) == (watchAddr() & ~3u) && v != m_lastWatch && (m_lastWatch = v, true)) fprintf(stderr, "WATCH w32 %08x = %08x pc %08x lr %08x\n", pa, v, m_cpu->r[15] - 8, m_cpu->r[14]);
    if (pa < RAM_SIZE) { std::memcpy(&m_ram[pa & ~3u], &v, 4); return; }
    if ((pa & 0xfff00000u) == 0xf8000000u) { std::memcpy(&m_sram[pa & 0xffffcu], &v, 4); return; }
    ioWrite(pa & ~3u, v, 0xffffffffu);
}
void GplCore::write16(uint32_t pa, uint16_t v)
{
    if (pa < RAM_SIZE) { std::memcpy(&m_ram[pa & ~1u], &v, 2); return; }
    if ((pa & 0xfff00000u) == 0xf8000000u) { std::memcpy(&m_sram[pa & 0xffffeu], &v, 2); return; }
    int sh = (pa & 2) * 8;
    ioWrite(pa & ~3u, uint32_t(v) << sh, 0xffffu << sh);
}
void GplCore::write8(uint32_t pa, uint8_t v)
{
    if (pa < RAM_SIZE) { m_ram[pa] = v; return; }
    if ((pa & 0xfff00000u) == 0xf8000000u) { m_sram[pa & 0xfffffu] = v; return; }
    int sh = (pa & 3) * 8;
    ioWrite(pa & ~3u, uint32_t(v) << sh, 0xffu << sh);
}

static int traceLeft()
{
    static int n = getenv("GPLTRACE") ? atoi(getenv("GPLTRACE")) : 0;
    return n > 0 ? n-- : 0;
}
static uint32_t s_lastTraceR = 0;

uint32_t GplCore::ioRead(uint32_t pa)
{
    if (pa >= 0xd0501000u && pa < 0xd0510000u) return m_ppuRam[(pa & 0xffff) >> 2];
    if (ioReads.size() < 20000) ioReads[pa]++;
    if (pa != s_lastTraceR && traceLeft()) {
        fprintf(stderr, "T R %08x @%08x = %08x lr %08x\n", pa, m_cpu->r[15] - 8, m_regs.count(pa) ? m_regs[pa] : 0, m_cpu->r[14]);
    }
    s_lastTraceR = pa;
    if ((pa & 0xffffff1fu) == 0xc0020010u && pa < 0xc00200e0u) syncPeripherals();   // timer counters
    switch (pa) {
    case 0xd000003c: return 1;          // PLL / clock change done (polled after setting the clock)
    case 0xd0900140: return (m_regs.count(pa) ? m_regs[pa] : 0) | 0x8000;    // controller ready
    case 0xc0000000: case 0xc0000020: case 0xc0000040: case 0xc0000060:
    case 0xc0000080: case 0xc00000a0: case 0xc00000c0: case 0xc00000e0:
    {
        uint32_t v = gpioRead(int((pa >> 5) & 7));
        static const bool logGpio = getenv("GPLGPIO") != nullptr;
        if (logGpio) fprintf(stderr, "GPIO %08x = %08x pc %08x lr %08x\n", pa, v, m_cpu->r[15] - 8, m_cpu->r[14]);
        return v;
    }
    case 0xc00e01cc: return (m_regs.count(pa) ? m_regs[pa] : 0) & ~1u;       // USB PHY access done
    case 0xd0900000: return (m_regs.count(pa) ? m_regs[pa] : 0) & ~1u;       // start bit self-clears
    case 0xd0900150: {                  // NAND data port (one byte per read)
        if (m_nstatus) return 0xe0;     // ready, not write-protected
        if (!m_nid.empty()) { uint8_t b = m_nid.front(); m_nid.erase(m_nid.begin()); return b; }
        return m_nptr < m_nbuf.size() ? m_nbuf[m_nptr++] : 0xff;
    }
    case 0xd0900160: return 0;          // ECC: no errors
    case 0xd0100028: return pendingVector();
    case 0xd010002c: return 0;          // no FIQ
    default: break;
    }
    auto it = m_regs.find(pa);
    uint32_t v = it == m_regs.end() ? 0 : it->second;
    return v;
}

void GplCore::ioWrite(uint32_t pa, uint32_t v, uint32_t mask)
{
    if (pa >= 0xd0501000u && pa < 0xd0510000u) { uint32_t& r = m_ppuRam[(pa & 0xffff) >> 2]; r = (r & ~mask) | (v & mask); return; }
    if (ioWrites.size() < 20000) ioWrites[pa]++;
    s_lastTraceR = 0;
    if (traceLeft()) fprintf(stderr, "T W %08x @%08x = %08x/%08x\n", pa, m_cpu->r[15] - 8, v, mask);
    if ((pa & 0xffffff00u) == 0xc0030000u && pa < 0xc003000cu) {     // time bases: bit 15 is write-1-to-clear
        uint32_t& r = m_regs[pa];
        uint32_t nv = (r & ~mask) | (v & mask);
        uint32_t flag = (r & 0x8000) && !(v & mask & 0x8000) ? 0x8000 : 0;
        r = (nv & ~0x8000u) | flag;
        bool any = false;
        for (int n = 0; n < 3; n++) { uint32_t q = m_regs[0xc0030000u + uint32_t(n) * 4]; if ((q & 0xc000) == 0xc000) any = true; }
        setIrq(2, any);
        return;
    }
    if (pa == 0xc0040054 || pa == 0xc0040058) {
        uint32_t& r = m_regs[pa];
        if (pa == 0xc0040054) r &= ~(v & mask); else r = (r & ~mask) | (v & mask);
        setIrq(1, (m_regs[0xc0040054] & m_regs[0xc0040058]) != 0);
        return;
    }
    if (pa == 0xd03001fc) {                                             // DMA status: write 1 to clear
        m_regs[pa] &= ~(v & mask);
        setIrq(24, (m_regs[pa] & 0xff) != 0);
        return;
    }
    if (pa >= 0xd0300000 && pa < 0xd0300100 && (pa & 0x3f) == 0) {    // DMA channel control
        uint32_t& r = m_regs[pa];
        r = (r & ~mask) | (v & mask);
        if (r & 1) dmaRun(int((pa >> 6) & 3));
        return;
    }
    if (pa == 0xd050018c) { m_regs[pa] &= ~(v & mask); ppuIrq(); return; }   // PPU IRQ status: write 1 to clear
    if (pa == 0xd0500188) { m_regs[pa] = (m_regs[pa] & ~mask) | (v & mask); ppuIrq(); return; }
    if (pa == 0xd05001c8) {                                             // PPU video DMA into PPU RAM:
        m_regs[pa] = (m_regs[pa] & ~mask) | (v & mask);                 // 16-bit units, one per 32-bit slot
        uint32_t src = m_regs[0xd05001c0], dst = m_regs[0xd05001c4] & 0xfffc, count = m_regs[pa] & 0xffff;
        uint32_t n = count >= 0x800 ? count : count + 1;
        if (getenv("GPLDBG")) fprintf(stderr, "PPUDMA %08x -> %04x x%u\n", src, dst, n);
        for (uint32_t i = 0; i < n; i++) m_ppuRam[((dst + i * 4) & 0xffff) >> 2] = read16(src + i * 2);
        m_regs[0xd050018c] |= 4;
        ppuIrq();
        return;
    }
    if (pa == 0xd05001f0 && (v & mask & 1)) {                           // PPU_GO: render a frame into the frame buffer
        if (getenv("GPLDBG")) fprintf(stderr, "PPU_GO fb %08x\n", m_regs[0xd05001e0]);
        ppuRender();
        m_regs[0xd050018c] |= 0x4000;
        ppuIrq();
        return;
    }
    if (pa == 0xc00c0004) {                                             // SAR ADC manual conversion
        uint32_t& r = m_regs[pa];
        r = (r & ~mask) | (v & mask);
        if ((v & mask & 0x40) && (r & 0x4000)) {
            int ch = int(r & 7);
            m_regs[0xc00c0008] = m_adc[ch];
            r = (r & ~0x70u) | 0x8000 | 0x80;
            setIrq(29, (m_regs[0xc00c0000] & 0x4000) != 0);
            if (getenv("GPLADC")) fprintf(stderr, "ADC ch%d -> %03x\n", ch, m_adc[ch]);
        }
        return;
    }
    if (pa == 0xc00c0000) {
        uint32_t& r = m_regs[pa];
        r = (r & ~mask) | (v & mask);
        if (v & mask & 0x8000) { m_regs[0xc00c0004] &= ~0x8000u; r &= ~0x8000u; }
        setIrq(29, (r & 0x4000) && (m_regs[0xc00c0004] & 0x8000));
        return;
    }
    if (pa == 0xc00a0014) { m_regs[pa] &= ~(v & mask); return; }       // SD status: write 1 to clear
    if (pa == 0xc00a0008 && (v & mask & 0x80)) {                        // SD command start: no card, so
        m_regs[pa] = (m_regs[pa] & ~mask) | (v & mask);                 // it completes with a timeout
        m_regs[0xc00a0014] = (m_regs[0xc00a0014] & ~1u) | 0x40 | 0x10;
        return;
    }
    if ((pa & 0xffffff1fu) == 0xc0020000u && pa < 0xc00200e0u) {      // timer control: bit 15 write-1-to-clear
        uint32_t& r = m_regs[pa];
        uint32_t nv = (r & ~mask) | (v & mask);
        uint32_t flag = (r & 0x8000) && !(v & mask & 0x8000) ? 0x8000 : 0;
        r = (nv & ~0x8000u) | flag;
        updateTimerIrqs();
        return;
    }
    uint32_t& r = m_regs[pa];
    r = (r & ~mask) | (v & mask);
    if (pa == 0xd0100030 || pa == 0xd0100038) updateIrq();
    switch (pa) {
    case 0xd0900000: if (r & 1) nandDma(); break;
    case 0xd0900144: nandCommand(uint8_t(r)); break;
    case 0xd0900148: m_ncol = r; m_nptr = r < 2112 ? r : 0; break;
    case 0xd090014c: m_nrow = r; break;
    default: break;
    }
}

// The controller has a command latch (0x144), column/row address latches (0x148/0x14c),
// a byte data port (0x150) and a DMA engine (0x000 start, 0x004 SDRAM address, 0x008 length).
void GplCore::nandCommand(uint8_t cmd)
{
    static constexpr uint32_t PAGE = 2112;
    m_nstatus = false;
    switch (cmd) {
    case 0xff: m_nid.clear(); break;                                   // reset
    case 0x70: m_nstatus = true; break;                                // read status
    case 0x90: m_nid = { 0x98, 0xd1, 0x90, 0x15, 0x76 }; break;        // TC58NVG0S3E id
    case 0x00: m_nid.clear(); break;                                   // read, 1st cycle
    case 0x30: {                                                        // read, 2nd cycle
        size_t off = size_t(m_nrow) * PAGE;
        if (off + PAGE <= m_nand.size()) std::memcpy(m_nbuf.data(), &m_nand[off], PAGE);
        else std::fill(m_nbuf.begin(), m_nbuf.end(), 0xff);
        m_nptr = m_ncol < PAGE ? m_ncol : 0;
        break;
    }
    case 0x60: break;                                                   // erase setup
    case 0xd0: {                                                        // erase confirm (row in 0x148)
        size_t off = size_t(m_ncol & ~63u) * PAGE;
        if (off + PAGE * 64 <= m_nand.size()) std::fill(m_nand.begin() + long(off), m_nand.begin() + long(off + PAGE * 64), 0xff);
        break;
    }
    case 0x80: std::fill(m_nbuf.begin(), m_nbuf.end(), 0xff); m_nptr = 0; break;
    case 0x10: {                                                        // program confirm
        size_t off = size_t(m_nrow) * PAGE;
        if (off + PAGE <= m_nand.size()) for (uint32_t i = 0; i < PAGE; i++) m_nand[off + i] &= m_nbuf[i];
        break;
    }
    default: break;
    }
    m_ncmd = cmd;
}

// System DMA, 4 channels at 0xd0300000 + n*0x40: +0 control, +4 source, +8 destination,
// +0xc count. Control bit 0 starts; bit 8 selects 32-bit units; bit 7 holds the source
// address (fills). Completion sets status bit n at 0xd03001fc and raises vector 24.
// Buttons. The direction pad, A, B, L and R go through an encoder that puts a 6-bit code on
// pins 0x21-0x23 (low bits) and 0x56-0x58 (high bits); the application turns the code into
// key bits with a 64-entry table (code 1 = up, 2 = down, 3 = left, 4 = right, 5 = B, 6 = R,
// 7 = A, 8 = L, higher codes = pairs). MENU, VOL+ and VOL- have their own pins, active low.
uint32_t GplCore::keypadLines(int port) const
{
    // application key bits for each PocketPlug input bit (see the MachineDef)
    static const uint32_t keyBit[8] = { 0x001, 0x020, 0x002, 0x040, 0x400, 0x200, 0x800, 0x080 };
    static const uint16_t codeTable[53] = {
        0x000, 0x001, 0x020, 0x002, 0x040, 0x200, 0x080, 0x400, 0x800, 0x003, 0x041, 0x201,
        0x081, 0x401, 0x801, 0x022, 0x060, 0x220, 0x0a0, 0x420, 0x820, 0x202, 0x082, 0x402,
        0x802, 0x240, 0x0c0, 0x440, 0x840, 0x600, 0xa00, 0x480, 0x880, 0x203, 0x083, 0x403,
        0x803, 0x241, 0x0c1, 0x441, 0x841, 0x222, 0x0a2, 0x422, 0x822, 0x260, 0x0e0, 0x460,
        0x860, 0x603, 0xa03, 0x622, 0xa22 };
    uint32_t want = 0;
    for (int i = 0; i < 8; i++) if (m_keys & (1u << i)) want |= keyBit[i];
    int code = 0;
    if (want) {
        int best = 0, bestBits = 0;
        for (int c = 1; c < 53; c++) {
            if ((codeTable[c] & ~want) != 0) continue;
            int bits = __builtin_popcount(codeTable[c]);
            if (bits > bestBits) { best = c; bestBits = bits; }
        }
        code = best;
    }
    if (port == 2) return uint32_t(code & 7) << 1;
    if (port == 5) return uint32_t((code >> 3) & 7) << 6;
    return 0;
}

// GPIO ports at 0xc0000000 + n*0x20: +0 input, +4 output data, +8 direction (1 = output).
// Input pins idle high (pull-ups); m_gpioLow[n] holds pins pulled low from outside.
uint32_t GplCore::gpioRead(int port)
{
    uint32_t base = 0xc0000000u + uint32_t(port) * 0x20;
    uint32_t dir = m_regs[base + 8], out = m_regs[base + 4];
    uint32_t low = m_gpioLow[port];
    if (port == 7 && (m_keys & 0x100)) low |= 1u << 5;     // MENU
    if (port == 4 && (m_keys & 0x200)) low |= 1u << 2;     // VOL+
    if (port == 4 && (m_keys & 0x400)) low |= 1u << 3;     // VOL-
    uint32_t ext = (~low & m_gpioPull[port]) | keypadLines(port);
    return (out & dir) | (ext & ~dir);
}

void GplCore::ppuIrq()
{
    setIrq(27, (m_regs[0xd050018c] & m_regs[0xd0500188]) != 0);
}

void GplCore::dmaRun(int ch)
{
    uint32_t base = 0xd0300000u + uint32_t(ch) * 0x40;
    uint32_t ctrl = m_regs[base], src = m_regs[base + 4], dst = m_regs[base + 8], cnt = m_regs[base + 0xc];
    int unit = (ctrl & 0x100) ? 4 : (ctrl & 0x200) ? 2 : 1;
    bool srcFixed = ctrl & 0x80, dstFixed = ctrl & 0x40;
    if (getenv("GPLDBG")) fprintf(stderr, "DMA%d ctrl %08x %08x -> %08x x%u\n", ch, ctrl, src, dst, cnt);
    for (uint32_t i = 0; i < cnt; i++) {
        uint32_t val = unit == 4 ? read32(src) : unit == 2 ? read16(src) : read8(src);
        if (unit == 4) write32(dst, val); else if (unit == 2) write16(dst, uint16_t(val)); else write8(dst, uint8_t(val));
        if (!srcFixed) src += uint32_t(unit);
        if (!dstFixed) dst += uint32_t(unit);
    }
    m_regs[base] &= ~1u;
    m_regs[0xd03001fc] |= 1u << ch;
    setIrq(24, true);
}

void GplCore::nandDma()
{
    uint32_t addr = m_regs[0xd0900004], len = m_regs[0xd0900008] & 0xffff;
    bool toNand = (m_regs[0xd0900000] & 2) != 0;
    for (uint32_t i = 0; i < len && m_nptr < m_nbuf.size(); i++, addr++, m_nptr++) {
        if (addr >= RAM_SIZE) break;
        if (toNand) m_nbuf[m_nptr] = m_ram[addr]; else m_ram[addr] = m_nbuf[m_nptr];
    }
    m_regs[0xd0900000] &= ~1u;
}

void GplCore::debugDump()
{
    if (const char* f = getenv("GPLRAM")) { FILE* fp = fopen(f, "wb"); if (fp) { fwrite(m_ram.data(), 1, 0x1000000, fp); fclose(fp); } }
    if (dbg()) {
        fprintf(stderr, "pc %08x lr %08x sp %08x cpsr %08x insns %llu\n", m_cpu ? m_cpu->r[15] : 0, m_cpu ? m_cpu->r[14] : 0, m_cpu ? m_cpu->r[13] : 0, m_cpu ? m_cpu->cpsr : 0, m_cpu ? (unsigned long long)m_cpu->insn_count : 0ull);
        { uint32_t v; std::memcpy(&v, &m_sram[0x20c], 4); fprintf(stderr, "irqvec %08x\n", v);
          for (int i = 0; i < 32; i++) { std::memcpy(&v, &m_ram[0x17eadc + i * 4], 4); if (v) fprintf(stderr, "irq%d -> %08x\n", i, v); } }
        for (auto& kv : ioReads) fprintf(stderr, "R %08x x%u = %08x\n", kv.first, kv.second, m_regs.count(kv.first) ? m_regs[kv.first] : 0);
        for (auto& kv : ioWrites) fprintf(stderr, "W %08x x%u = %08x\n", kv.first, kv.second, m_regs[kv.first]);
    }
}

void GplCore::setIrq(int vector, bool on)
{
    uint32_t bit = 1u << (32 - vector);
    m_irqRaw = on ? (m_irqRaw | bit) : (m_irqRaw & ~bit);
    updateIrq();
}

uint32_t GplCore::pendingVector()
{
    uint32_t p = m_irqRaw & ~m_regs[0xd0100030];
    if (!p) return 0;
    int b = 31;
    while (!(p & (1u << b))) b--;
    return uint32_t(32 - b);
}

// Timer 0 is the OS tick on vector 25; timer 3's interrupt is vector 9.
void GplCore::updateTimerIrqs()
{
    static const int vec[7] = { 25, 0, 0, 9, 0, 0, 0 };
    for (int t = 0; t < 7; t++) {
        if (!vec[t]) continue;
        uint32_t c = m_regs[0xc0020000u + uint32_t(t) * 0x20];
        setIrq(vec[t], (c & 0xc000) == 0xc000);
    }
}

void GplCore::updateIrq()
{
    bool on = !(m_regs[0xd0100038] & 1) && pendingVector() != 0;
    m_irqOut = on;
    m_cpu->set_irq(on);
}

// CPU clock 200 MHz. Timers count at 375 kHz (the app's 1 ms timeouts are 375 counts of
// timer 3). Time bases tick at 100 Hz and raise vector 2.
void GplCore::tickPeripherals(uint64_t cycles)
{
    static constexpr uint64_t CPU = 200000000, TIMER = 375000, TB = 100;
    m_tmAcc += cycles * TIMER;
    uint64_t ticks = m_tmAcc / CPU;
    m_tmAcc %= CPU;
    for (int t = 0; t < 7 && ticks; t++) {
        uint32_t base = 0xc0020000u + uint32_t(t) * 0x20;
        uint32_t& ctrl = m_regs[base];
        if (!(ctrl & 0x2000)) continue;
        uint32_t& cnt = m_regs[base + 0x10];
        uint64_t c = cnt + ticks;
        while (c > 0xffff) {
            c = c - 0x10000 + (m_regs[base + 8] & 0xffff);
            ctrl |= 0x8000;
        }
        cnt = uint32_t(c);
    }
    updateTimerIrqs();
    // RTC periodic interrupt (0xc0040050 bits 0-2 select the rate, bit 8 enables it): status bit 8
    {
        uint32_t ctl = m_regs[0xc0040050];
        if (ctl & 0x100) {
            uint32_t sel = ctl & 7;
            uint64_t hz = sel ? (32768u >> (sel - 1)) : 32768u;
            m_rtcAcc += cycles * hz;
            if (m_rtcAcc >= CPU) {
                m_rtcAcc %= CPU;
                m_regs[0xc0040054] |= 0x100;
                setIrq(1, (m_regs[0xc0040054] & m_regs[0xc0040058]) != 0);
            }
        }
    }
    m_tbAcc += cycles * TB;
    if (m_tbAcc >= CPU) {
        m_tbAcc %= CPU;
        bool any = false;
        for (int n = 0; n < 3; n++) {
            uint32_t& r = m_regs[0xc0030000u + uint32_t(n) * 4];
            if ((r & 0x2000) == 0) continue;
            r |= 0x8000;
            if (r & 0x4000) any = true;
        }
        if (any) setIrq(2, true);
    }
}

void GplCore::syncPeripherals()
{
    uint64_t now = m_cpu->insn_count;
    if (now > m_lastSync) tickPeripherals(now - m_lastSync);
    m_lastSync = now;
}

// Busy-wait loops that only poll RAM variables advanced by interrupt handlers (the frame
// limiters wait on the RTC tick count). Such a loop is a short block ending in a backward
// branch whose instructions are only literal loads, loads from RAM through those literals,
// arithmetic and compares. While the CPU sits in one, nothing changes until an interrupt, so
// the slice can be skipped. Results are cached per loop address.
bool GplCore::inWaitLoop(uint32_t pc)
{
    auto it = m_waitLoops.find(pc);
    if (it != m_waitLoops.end() && it->second.first == m_cpu->debug_read32(pc)) return it->second.second;
    bool result = false;
    // find a backward branch at or after pc that jumps to at or before pc
    for (uint32_t b = pc; b < pc + 48 && !result; b += 4) {
        uint32_t w = m_cpu->debug_read32(b);
        if ((w & 0x0f000000u) != 0x0a000000u || (w >> 28) == 0xf) continue;     // B / Bcc only
        int32_t off = int32_t(w << 8) >> 6;
        uint32_t target = b + 8 + uint32_t(off);
        if (target > pc || b - target > 48) break;
        bool ok = true;
        uint32_t known[16] = {}; bool isKnown[16] = {};
        // registers written anywhere in the loop; reading one before this iteration wrote it
        // means state carries between iterations, which a pure poll does not have
        uint32_t writtenAnywhere = 0;
        for (uint32_t a = target; a < b; a += 4) {
            uint32_t i = m_cpu->debug_read32(a);
            if ((i & 0x0c000000u) == 0x04000000u || (i & 0x0c000000u) == 0) writtenAnywhere |= 1u << ((i >> 12) & 15);
        }
        uint32_t written = 0;
        auto readable = [&](uint32_t r) { return r == 15 || (written & (1u << r)) || !(writtenAnywhere & (1u << r)); };
        for (uint32_t a = target; a < b && ok; a += 4) {
            uint32_t i = m_cpu->debug_read32(a);
            uint32_t rd = (i >> 12) & 15, rn = (i >> 16) & 15, rm = i & 15;
            if ((i & 0x0f000000u) == 0x0a000000u) {                                 // branch inside the loop
                int32_t o = int32_t(i << 8) >> 6;
                uint32_t t = a + 8 + uint32_t(o);
                bool conditional = (i >> 28) != 0xe;
                if (!conditional && (t < target || t > b)) ok = false;              // only exits may leave
                continue;
            }
            if ((i & 0x0e500000u) == 0x04100000u && !(i & (1u << 25))) {           // LDR/LDRB imm
                if (rd == 15 || (i & (1u << 21)) || !(i & (1u << 24))) { ok = false; break; }
                uint32_t imm = i & 0xfff;
                bool up = i & (1u << 23);
                if (rn == 15) {
                    uint32_t lit = a + 8 + (up ? imm : uint32_t(-int32_t(imm)));
                    known[rd] = m_cpu->debug_read32(lit); isKnown[rd] = true;
                } else if (isKnown[rn] && known[rn] + imm < RAM_SIZE) {
                    isKnown[rd] = false;
                } else { ok = false; break; }
                written |= 1u << rd;
                continue;
            }
            if ((i & 0x0e5000f0u) == 0x005000b0u) {                                 // LDRH imm
                if (!isKnown[rn] || known[rn] >= RAM_SIZE || (i & (1u << 21)) || !(i & (1u << 24))) { ok = false; break; }
                isKnown[rd] = false;
                written |= 1u << rd;
                continue;
            }
            if ((i & 0x0c000000u) == 0) {                                           // data processing
                uint32_t op = (i >> 21) & 15;
                bool isCompare = op >= 8 && op <= 11;
                if ((i & 0x90) == 0x90 && !(i & (1u << 25))) { ok = false; break; }  // multiply / extra loads
                if (!isCompare && (rd == 15 || (i & (1u << 20)))) { ok = false; break; }
                bool usesRn = op != 13 && op != 15;                                 // MOV/MVN ignore Rn
                if (usesRn && !readable(rn)) { ok = false; break; }
                if (!(i & (1u << 25)) && !readable(rm)) { ok = false; break; }
                if (!(i & (1u << 25)) && (i & 0x10) && !readable((i >> 8) & 15)) { ok = false; break; }
                if (!isCompare) { isKnown[rd] = false; written |= 1u << rd; }
                continue;
            }
            ok = false;
        }
        result = ok;
    }
    m_waitLoops[pc] = { m_cpu->debug_read32(pc), result };
    return result;
}

void GplCore::runFrame()
{
    static const bool sample = getenv("GPLSAMPLE") != nullptr;
    const int SLICES = sample ? 5000 : 100;
    const int sliceCycles = 200000000 / 60 / SLICES;   // ~200 MHz
    for (int i = 0; i < SLICES; i++) {
        // The RTOS idle task spins on "b ."; while it does and no interrupt is due, skip the
        // CPU and only advance the peripherals.
        bool irqTaken = m_irqOut && !(m_cpu->cpsr & 0x80);
        if (!irqTaken && (m_cpu->debug_read32(m_cpu->r[15]) == 0xeafffffeu || inWaitLoop(m_cpu->r[15]))) {
            if (sample) fprintf(stderr, "SKIP %08x\n", m_cpu->r[15]);
            tickPeripherals(uint64_t(sliceCycles));
            m_lastSync = m_cpu->insn_count;
            continue;
        }
        m_cpu->run(sliceCycles);
        syncPeripherals();
        if (sample && m_cpu->r[15] != 0xd1fd0) fprintf(stderr, "S %llu %08x %08x\n", (unsigned long long)m_frame, m_cpu->r[15], m_cpu->r[14]);
    }
    m_frame++;
    m_regs[0xd050018c] |= 0x0801;                 // vertical blank
    ppuIrq();
    m_audioPending = 735;
    if (getenv("GPLDUMP") && m_frame == (uint64_t)atoi(getenv("GPLDUMP"))) debugDump();
}

size_t GplCore::readAudio(int16_t* out, size_t maxSamples)
{
    size_t n = std::min<size_t>(maxSamples, m_audioPending);
    std::memset(out, 0, n * 2);
    m_audioPending -= n;
    return n;
}
