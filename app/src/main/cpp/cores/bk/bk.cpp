// PocketPlug - Elektronika BK-0010 / BK-0011 family
// SPDX-License-Identifier: BSD-3-Clause
//
// Port of MAME's ussr/bk.cpp, bk_m.cpp, 1801vp014.cpp and vm1timer.cpp (BSD-3-Clause).

#include "bk.h"
#include <cstdio>
#include <cstring>

#define BIT(x, n) (((x) >> (n)) & 1)

namespace {
enum : uint8_t { SEL1_UPDATED = 0004, SEL1_RX_SER = 0020, SEL1_RX_CAS = 0040, SEL1_KEYDOWN = 0100, SEL1_RDY_SER = 0200, SEL1_MOTOR = 0200 };
enum : uint16_t { CSR_IE = 0100, CSR_DONE = 0200, CSR_ERR = 0100000 };
enum : uint16_t { TMRCSR_SP = 0001, TMRCSR_CAP = 0002, TMRCSR_MON = 0004, TMRCSR_OS = 0010, TMRCSR_RUN = 0020, TMRCSR_FL = 0200, TMRCSR_WR = 0377 };
// BK0011 view index -> RAM page (bk0011_mem)
const int kViewShare[8] = { 1, 5, 2, 3, 4, 7, 0, 6 };

struct RomPlace { const char* name; uint32_t offset; };
}

BkCore::BkCore(Model m) : m_model(m)
{
    m_eleven = (m == BK0011 || m == BK0011M);
    m_clock = m_eleven ? 4000000 : 3000000;
    m_cpu.set_bus(this);
    m_pages.assign(m_eleven ? 8 * 0x2000 : 0x2000, 0);
    if (m == BK0010FD) m_fdram.assign(0x2000, 0);
    m_frame.assign(512 * 256, 0xff000000u);
}

bool BkCore::load(const std::vector<RomFile>& roms, std::string& error)
{
    static const RomPlace p10[] = { { "monit10.rom", 0 }, { "focal.rom", 0x2000 }, { "tests.rom", 0x6000 }, { nullptr, 0 } };
    static const RomPlace p1001[] = { { "monit10.rom", 0 }, { "basic10-1.rom", 0x2000 }, { "basic10-2.rom", 0x4000 }, { "basic10-3.rom", 0x6000 }, { nullptr, 0 } };
    static const RomPlace pfd[] = { { "monit10.rom", 0 }, { "focal.rom", 0x2000 }, { nullptr, 0 } };
    static const RomPlace p11[] = { { "bk11_199_basic2.rom", 0 }, { "bk11_200_basic3.rom", 0x2000 }, { "bk11_198_basic1.rom", 0x4000 },
                                    { "bk11_202_ext.rom", 0x6000 }, { "bk11_201_bos.rom", 0x8000 }, { nullptr, 0 } };
    static const RomPlace p11m[] = { { "bk11m_328_basic2.rom", 0 }, { "bk11m_329_basic3.rom", 0x2000 }, { "bk11m_327_basic1.rom", 0x4000 },
                                     { "bk11m_325_ext.rom", 0x6000 }, { "bk11m_324_bos.rom", 0x8000 }, { nullptr, 0 } };
    const RomPlace* pl = m_model == BK0010 ? p10 : m_model == BK001001 ? p1001 : m_model == BK0010FD ? pfd : m_model == BK0011 ? p11 : p11m;
    m_rom.assign(m_eleven ? 0xb000 : 0x8000, 0xff);
    for (const RomPlace* p = pl; p->name; p++) {
        const RomFile* f = nullptr;
        for (auto& r : roms) if (r.name == p->name) f = &r;
        if (!f) { error = std::string("Missing ") + p->name; return false; }
        memcpy(&m_rom[p->offset], f->data.data(), std::min(f->data.size(), m_rom.size() - p->offset));
    }
    // floppy controller ROM (the sets with the BY / KMD card)
    const char* disk = m_model == BK0010FD ? "disk_327.rom" : m_model == BK0011 ? "disk_253.rom" : m_model == BK0011M ? "disk_326.rom" : nullptr;
    if (disk) {
        for (auto& r : roms) if (r.name == disk) m_diskRom = r.data;
        if (m_diskRom.empty()) for (auto& r : roms) if (r.name.rfind("disk_", 0) == 0) m_diskRom = r.data;
        if (m_diskRom.empty()) { error = std::string("Missing ") + disk; return false; }
        m_diskRom.resize(0x1000, 0xff);
    }
    reset();
    return true;
}

void BkCore::reset()
{
    m_sel1 = SEL1_KEYDOWN | SEL1_MOTOR;
    m_scroll = 01330;
    m_misc = 0100;
    m_videoPage = 0;
    m_stopDisabled = 0;
    m_view1 = m_view2 = -1;
    m_kbdState = CSR_IE; m_keyCode = 0; m_keyVector = 0; m_rxrdy = false;
    m_tCsr = 0; m_tDivisor = 0; m_tReloadIn = -1;
    m_keyQueue.clear(); m_keyHold = 0; m_keysDown = 0;
    updateVirq();
    m_cpu.reset();
}

// ---------------------------------------------------------------------------------------------
// memory map

bool BkCore::readMem(uint16_t a, uint16_t& v)
{
    if (!m_eleven) {
        if (a < 040000) { v = m_lowram[a >> 1]; return true; }
        if (a < 0100000) { v = m_pages[(a - 040000) >> 1]; return true; }
        if (m_model == BK0010FD && a >= 0120000 && a < 0160000) { v = m_fdram[(a - 0120000) >> 1]; return true; }
        if (!m_diskRom.empty() && a >= 0160000 && a < 0170000) { size_t o = a - 0160000; v = uint16_t(m_diskRom[o] | m_diskRom[o + 1] << 8); return true; }
        if (a < 0177600) { size_t o = a - 0100000; v = uint16_t(m_rom[o] | m_rom[o + 1] << 8); return true; }
        return ioRead(a, v);
    }
    if (a < 040000) { v = ramPtr(0)[a >> 1]; return true; }
    if (a < 0100000) {
        if (m_view1 < 0) return false;
        v = ramPtr(kViewShare[m_view1])[(a - 040000) >> 1];
        return true;
    }
    if (a < 0140000) {
        if (m_view2 < 0) return false;
        if (m_view2 < 8) { v = ramPtr(kViewShare[m_view2])[(a - 0100000) >> 1]; return true; }
        if (m_view2 == 8 || m_view2 == 9) { size_t o = (m_view2 == 8 ? 0 : 0x4000) + (a - 0100000); v = uint16_t(m_rom[o] | m_rom[o + 1] << 8); return true; }
        return false;   // ROM3 / ROM4 sockets: empty
    }
    if (a < 0160000) { size_t o = 0x8000 + (a - 0140000); v = uint16_t(m_rom[o] | m_rom[o + 1] << 8); return true; }
    if (!m_diskRom.empty() && a < 0170000) { size_t o = a - 0160000; v = uint16_t(m_diskRom[o] | m_diskRom[o + 1] << 8); return true; }
    if (a >= 0177600) return ioRead(a, v);
    return false;
}

bool BkCore::writeMem(uint16_t a, uint16_t d, uint16_t mask)
{
    auto put = [&](uint16_t& w) { w = uint16_t((w & ~mask) | (d & mask)); };
    if (!m_eleven) {
        if (a < 040000) { put(m_lowram[a >> 1]); return true; }
        if (a < 0100000) { put(m_pages[(a - 040000) >> 1]); return true; }
        if (m_model == BK0010FD && a >= 0120000 && a < 0160000) { put(m_fdram[(a - 0120000) >> 1]); return true; }
        if (a >= 0177600) return ioWrite(a, d, mask);
        return false;
    }
    if (a < 040000) { put(ramPtr(0)[a >> 1]); return true; }
    if (a < 0100000) {
        if (m_view1 < 0) return false;
        put(ramPtr(kViewShare[m_view1])[(a - 040000) >> 1]);
        return true;
    }
    if (a < 0140000) {
        if (m_view2 >= 0 && m_view2 < 8) { put(ramPtr(kViewShare[m_view2])[(a - 0100000) >> 1]); return true; }
        return false;
    }
    if (a >= 0177600) return ioWrite(a, d, mask);
    return false;
}

bool BkCore::ioRead(uint16_t a, uint16_t& v)
{
    switch (a) {
    case 0177660: v = m_kbdState; return true;
    case 0177662:
        v = m_keyCode;
        m_kbdState &= ~CSR_DONE;
        updateVirq();
        return true;
    case 0177664: v = m_scroll; return true;
    case 0177706: v = m_tLimit; return true;
    case 0177710: v = m_tCounter; return true;
    case 0177712: v = uint16_t(m_tCsr | 0177400); return true;
    case 0177714: v = 0; return true;   // parallel port, nothing plugged in
    case 0177716:
        v = uint16_t((m_eleven ? 0140000 : 0100000) | SEL1_RDY_SER | m_sel1);
        m_sel1 &= ~SEL1_UPDATED;
        return true;
    case 0177130: if (!m_diskRom.empty()) { v = 0; return true; } return false;   // floppy: no drive ready
    case 0177132: if (!m_diskRom.empty()) { v = 0; return true; } return false;
    }
    return false;
}

bool BkCore::ioWrite(uint16_t a, uint16_t d, uint16_t mask)
{
    switch (a) {
    case 0177660: m_kbdState = uint16_t((m_kbdState & ~CSR_IE) | (d & CSR_IE)); updateVirq(); return true;
    case 0177662:
        if (!m_eleven) return false;
        if (mask & 0xff00) {
            m_misc = uint8_t(d >> 8);
            if (m_model == BK0011) m_stopDisabled = BIT(d, 15); else m_videoPage = BIT(d, 15);
        }
        return true;
    case 0177664: m_scroll = uint16_t(((m_scroll & ~mask) | (d & mask)) & 01377); return true;
    case 0177706: m_tLimit = uint16_t((m_tLimit & ~mask) | (d & mask)); return true;
    case 0177710: return true;
    case 0177712:
        if (BIT(d, 4)) {
            int div = 128;
            if (BIT(d, 5)) div *= 16;
            if (BIT(d, 6)) div *= 4;
            m_tDivisor = div; m_tAcc = 0;
        } else m_tDivisor = 0;
        m_tCsr = uint16_t((m_tCsr & ~TMRCSR_WR) | (d & TMRCSR_WR));
        m_tCounter = m_tLimit;
        return true;
    case 0177714: return true;
    case 0177716:
        if (!m_eleven) sel1_w(d);
        else if (m_model == BK0011) bk11_sel1_w(d, mask);
        else bk11m_sel1_w(d, mask);
        return true;
    case 0177130: case 0177132: return !m_diskRom.empty();
    }
    return false;
}

uint16_t BkCore::read_word(uint16_t a)
{
    uint16_t v;
    if (readMem(a, v)) return v;
    busError();
    return 0xffff;
}

void BkCore::write_word(uint16_t a, uint16_t d)
{
    if (!writeMem(a, d, 0xffff)) busError();
}

uint8_t BkCore::read_byte(uint16_t a)
{
    uint16_t v;
    if (!readMem(uint16_t(a & ~1), v)) { busError(); return 0xff; }
    return uint8_t((a & 1) ? v >> 8 : v);
}

void BkCore::write_byte(uint16_t a, uint8_t d)
{
    uint16_t mask = (a & 1) ? 0xff00 : 0x00ff;
    uint16_t data = (a & 1) ? uint16_t(d << 8) : d;
    if (!writeMem(uint16_t(a & ~1), data, mask)) busError();
}

// ---------------------------------------------------------------------------------------------
// system registers

void BkCore::sel1_w(uint16_t data)
{
    m_sel1 |= SEL1_UPDATED;
    m_dac = BIT(data, 6);
}

void BkCore::bk11_sel1_w(uint16_t data, uint16_t mask)
{
    if (mask != 0xffff) return;
    if (!BIT(data, 11)) return;
    m_videoPage = BIT(data, 2);
    m_dac = BIT(data, 6);
    bk11m_sel1_w(data, mask);
}

void BkCore::bk11m_sel1_w(uint16_t data, uint16_t mask)
{
    if (mask != 0xffff) return;
    m_sel1 |= SEL1_UPDATED;
    if (BIT(data, 11)) {
        m_view1 = (data >> 12) & 7;
        switch (data & 033) {
        case 000: m_view2 = (data >> 8) & 7; break;
        case 001: m_view2 = 8; break;
        case 002: m_view2 = 9; break;
        case 010: m_view2 = 10; break;
        case 020: m_view2 = 11; break;
        }
    } else {
        m_stopDisabled = BIT(data, 12);
        m_dac = BIT(data, 6);
    }
}

void BkCore::reset_out()
{
    // RESET instruction: INIT to the keyboard and timer
    m_kbdState = CSR_IE; m_keyCode = 0; m_rxrdy = false;
    updateVirq();
    m_tCsr = 0;
}

// ---------------------------------------------------------------------------------------------
// keyboard

void BkCore::updateVirq()
{
    // the interrupt enable bit is inverted in this device: interrupts when IE is 0
    bool want = (m_kbdState & CSR_DONE) && !(m_kbdState & CSR_IE);
    if (!(m_kbdState & CSR_DONE)) m_rxrdy = false;
    if (want && !m_rxrdy) m_rxrdy = true;
    m_cpu.set_input_line(t11_device::VEC_LINE, m_rxrdy ? 1 : 0);
}

int BkCore::iack()
{
    if (!m_rxrdy) return -1;
    m_rxrdy = false;
    m_cpu.set_input_line(t11_device::VEC_LINE, 0);
    return m_keyVector;
}

void BkCore::keyMake(int code)
{
    m_kbdState |= CSR_DONE;
    m_keyCode = uint16_t(code & 0177);
    m_keyVector = (code & 0x200) ? 0274 : 060;
    code &= 0177;
    if ((code <= 7 && code != 3) || code == 011 || code == 013 || code == 021) m_keyVector = 0274;
    m_rxrdy = false;
    if (!(m_kbdState & CSR_IE)) m_rxrdy = true;
    m_cpu.set_input_line(t11_device::VEC_LINE, m_rxrdy ? 1 : 0);
    m_sel1 &= ~SEL1_KEYDOWN;
}

void BkCore::keyEvent(int code, bool down)
{
    if (code == 0x81) { m_ar2 = down; return; }
    if (code == 13) code = 012;         // Return -> ВВОД
    if (code == 0x80) {
        if (m_eleven && m_stopDisabled) return;
        // held for a couple of frames whatever the key-up timing, so the CPU always sees it
        if (down) { m_cpu.set_input_line(t11_device::HLT_LINE, 1); m_haltFrames = 2; }
        return;
    }
    m_keyQueue.push_back({ code | (m_ar2 ? 0x200 : 0), down });
}

// ---------------------------------------------------------------------------------------------
// timer

void BkCore::timerAdvance(int cycles)
{
    if (m_tReloadIn >= 0) {
        m_tReloadIn -= cycles;
        if (m_tReloadIn < 0) { m_tCounter = m_tLimit; m_tReloadIn = -1; }
    }
    if (!m_tDivisor) return;
    m_tAcc += cycles;
    while (m_tAcc >= m_tDivisor) {
        m_tAcc -= m_tDivisor;
        if (m_tCsr & TMRCSR_SP) continue;
        if (--m_tCounter == 0) {
            if (m_tCsr & TMRCSR_MON) {
                if (m_tCsr & CSR_ERR) m_tCsr |= TMRCSR_FL;
                m_tCsr |= CSR_ERR;
            }
            if (!(m_tCsr & TMRCSR_CAP)) {
                if (m_tCsr & TMRCSR_OS) { m_tCsr &= ~TMRCSR_RUN; m_tDivisor = 0; }
                m_tReloadIn = 4;
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------
// frame

void BkCore::runFrame()
{
    // feed the key queue: one change per few frames so the ROM sees every key
    if (m_keyHold > 0) m_keyHold--;
    else if (!m_keyQueue.empty()) {
        auto k = m_keyQueue.front();
        m_keyQueue.erase(m_keyQueue.begin());
        if (k.second) { keyMake(k.first); m_keysDown++; m_keyHold = 2; }
        else { if (m_keysDown > 0) m_keysDown--; if (!m_keysDown) m_sel1 |= SEL1_KEYDOWN; m_keyHold = 1; }
    }

    const int frameCycles = int(m_clock / refreshRate());
    const int vblankCycles = m_eleven ? int(m_clock * 0.004010) : int(m_clock * 0.0025);
    const int samples = int(44100 / refreshRate() + 0.5);
    int done = 0;
    bool inVblank = false;
    for (int s = 0; s < samples; s++) {
        int target = int(int64_t(frameCycles) * (s + 1) / samples);
        // vblank starts at the end of the visible frame
        if (!inVblank && target >= frameCycles - vblankCycles) {
            inVblank = true;
            render();
            if (m_eleven && !BIT(m_misc, 6)) m_cpu.set_input_line(t11_device::CP2_LINE, 1);
        }
        int n = target - done;
        if (n > 0) {
            m_cpu.run(n);
            int used = n - m_cpu.m_icount;   // m_icount went negative by the overrun
            (void)used;
            timerAdvance(n);
            done = target;
        }
        m_samples.push_back(int16_t(m_dac ? 6000 : -6000));
    }
    if (m_eleven) m_cpu.set_input_line(t11_device::CP2_LINE, 0);
    if (m_haltFrames > 0 && --m_haltFrames == 0) m_cpu.set_input_line(t11_device::HLT_LINE, 0);
    if (m_samples.size() > 44100) m_samples.erase(m_samples.begin(), m_samples.end() - 4096);
}

size_t BkCore::readAudio(int16_t* out, size_t maxSamples)
{
    size_t n = std::min(maxSamples, m_samples.size());
    memcpy(out, m_samples.data(), n * 2);
    m_samples.erase(m_samples.begin(), m_samples.begin() + long(n));
    return n;
}

void BkCore::render()
{
    static const uint32_t pal10[5] = { 0xff000000, 0xff0000ff, 0xff00ff00, 0xffff0000, 0xffffffff };
    static const uint32_t pal11[16][3] = {
        { 0x0000ff, 0x00ff00, 0xff0000 }, { 0xffff00, 0xff00ff, 0xff0000 }, { 0x00ffff, 0x0000ff, 0xff00ff }, { 0x00ff00, 0x00ffff, 0xffff00 },
        { 0xff00ff, 0x00ffff, 0xffffff }, { 0xffffff, 0xffffff, 0xffffff }, { 0xc00000, 0x8e0000, 0xff0000 }, { 0xc0ff00, 0x8eff00, 0xffff00 },
        { 0xc000ff, 0x8e00ff, 0xff00ff }, { 0x8eff00, 0x8e00ff, 0x8e0000 }, { 0xc0ff00, 0xc000ff, 0xc00000 }, { 0x00ffff, 0xffff00, 0xff0000 },
        { 0xff0000, 0x00ff00, 0x00ffff }, { 0x00ffff, 0xffff00, 0xffffff }, { 0xffff00, 0x00ff00, 0xffffff }, { 0x00ffff, 0x00ff00, 0xffffff } };
    const uint16_t mini = !BIT(m_scroll, 9);
    const uint16_t ofs = uint16_t((m_scroll & 255) + (mini ? 40 : -216));
    const uint16_t* vram = m_eleven ? ramPtr(5 + m_videoPage) : m_pages.data();
    uint32_t cols[4];
    if (m_eleven) {
        cols[0] = 0xff000000;
        for (int i = 0; i < 3; i++) cols[i + 1] = 0xff000000 | pal11[m_misc & 15][i];
    } else {
        cols[0] = pal10[0]; cols[1] = pal10[1]; cols[2] = pal10[2]; cols[3] = pal10[3];
    }
    for (int y = 0; y < 256; y++) {
        uint32_t* row = &m_frame[size_t(y) * 512];
        for (int x = 0; x < 32; x++) {
            uint16_t code = (y > 63 && mini) ? 0 : vram[((y + ofs) % 256) * 32 + x];
            if (m_colour) {
                for (int b = 0; b < 16; b += 2) { uint32_t c = cols[(code >> b) & 3]; row[x * 16 + b] = c; row[x * 16 + b + 1] = c; }
            } else {
                for (int b = 0; b < 16; b++) row[x * 16 + b] = BIT(code, b) ? 0xffffffff : 0xff000000;
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------
// .bin programs (MAME's quickload)

bool BkCore::mediaInsert(int drive, const std::string& path, std::string& error)
{
    if (drive != 2) { error = "No such drive"; return false; }
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) { error = "Could not open the file"; return false; }
    std::vector<uint8_t> d;
    uint8_t buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) d.insert(d.end(), buf, buf + n);
    fclose(f);
    if (d.size() < 4) { error = "Not a BK .bin file"; return false; }
    uint16_t addr = uint16_t(d[0] | d[1] << 8), len = uint16_t(d[2] | d[3] << 8);
    if (d.size() < size_t(len) + 4) len = uint16_t(d.size() - 4);
    if (uint32_t(addr) + len > 0100000) { error = "File too large"; return false; }
    for (uint32_t i = 0; i < len; i++) write_byte(uint16_t(addr + i), d[4 + i]);
    write_word(0264, addr);
    write_word(0266, len);
    // start it as the monitor's S command would (programs loaded below 1000 octal start at 1000)
    m_cpu.start_at(addr < 01000 ? 01000 : addr);
    return true;
}
