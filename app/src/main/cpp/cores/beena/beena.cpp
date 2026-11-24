// PocketPlug - Sega Toys Advanced Pico BEENA (see beena.h)
// SPDX-License-Identifier: BSD-3-Clause
// Based on MAME's sega_beena.cpp / ap2010pcm.cpp (BSD-3-Clause, R. Belmont, QUFB).

#include "beena.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace {
constexpr uint32_t CYCLES_PER_LINE = 2000;    // 81 MHz / 60 / 480 = 2812 cycles; ~1.4 cycles per instruction
inline uint32_t pal5(uint32_t x) { return (x << 3) | (x >> 2); }
inline uint32_t bgr555(uint16_t c) { return pal5(c & 0x1f) << 16 | pal5((c >> 5) & 0x1f) << 8 | pal5((c >> 10) & 0x1f); }
inline int signed10(uint32_t n) { return int(int32_t(n << 22) >> 22); }
inline int rescale(int x, int minx, int maxx, int a, int b) { return a + (((x - minx) * (b - a)) / (maxx - minx)); }
inline int alphaStep(int step) { return step < 0x20 ? rescale(step, 0, 0x1f, 0xff, 0) : rescale(step, 0x20, 0x3f, 0, 0xff); }

enum : uint32_t {
    BITS_EMPTY = 0, BITS_0 = 0x0000ff00, BITS_0_1 = 0x00ff0001, BITS_0_1_7 = 0xff000107,
    BITS_6 = 0x0000ff06, BITS_6_7 = 0x00ff0607, BITS_7 = 0x0000ff07, BITS_7_1 = 0x00ff0701, BITS_ERR = 0xffffffff,
};
enum : uint8_t { IDLE = 0, READ_COMMAND, READ_ADDRESS_BYTE, READ_ACK, READ_RAM_BYTE, WRITE_RAM_BYTE, CONSUME_UNTIL_IDLE };

const RomFile* findRom(const std::vector<RomFile>& roms, const char* name) {
    for (auto& r : roms) if (r.name == name) return &r;
    return nullptr;
}
}

BeenaCore::BeenaCore()
{
    m_iram.assign(0x4000, 0);
    m_vid.assign(0x30000, 0);
    m_ram60.assign(0x30000, 0);
    m_ram70.assign(0x30000, 0);
    m_vram.assign(0x200000, 0);
    m_palCache.assign(256 * H, 0);
    m_cacheLayer.assign(1024 * 512, 0);
    m_fb.assign(size_t(W) * H, 0xff000000u);
}

BeenaCore::~BeenaCore() { delete m_cpu; }

bool BeenaCore::load(const std::vector<RomFile>& roms, std::string& error)
{
    const RomFile* bios = findRom(roms, "9h0-0008.bios.ic1");
    if (!bios || bios->data.size() < 0x20000) { error = "Missing 9h0-0008.bios.ic1"; return false; }
    // big-endian words -> host-order words
    m_bios.assign(0x20000, 0);
    for (size_t i = 0; i < 0x20000; i += 4) {
        const uint8_t* p = &bios->data[i];
        uint32_t w = uint32_t(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3];
        memcpy(&m_bios[i], &w, 4);
    }
    // the cartridge: anything else (the largest file); its 16-bit words are byte-swapped
    const RomFile* cart = nullptr;
    for (auto& r : roms)
        if (r.name != "9h0-0008.bios.ic1" && r.name != "9h0-0008.midipcm.ic1" && r.data.size() >= 0x1000 &&
            (!cart || r.data.size() > cart->data.size())) cart = &r;
    m_cart.clear();
    if (cart) {
        size_t sz = 0x1000;
        while (sz < cart->data.size() && sz < 0x800000) sz <<= 1;
        m_cart.assign(sz, 0xff);
        size_t n = std::min(cart->data.size(), sz) & ~size_t(3);
        for (size_t i = 0; i < n; i += 4) {
            const uint8_t* p = &cart->data[i];
            uint32_t w = uint32_t(p[1]) << 24 | p[0] << 16 | p[3] << 8 | p[2];
            memcpy(&m_cart[i], &w, 4);
        }
        for (size_t i = n; i < sz; i += 4) { uint32_t w = 0xffffffffu; memcpy(&m_cart[i], &w, 4); }
        uint32_t base; memcpy(&base, &m_cart[0xdc], 4);
        m_cartBase = (base == 0xa0000000u) ? 0xa0000000u : 0x80000000u;
    }
    delete m_cpu;
    m_cpu = new didj::ARM926(this);
    m_cpu->set_big_endian(true);
    reset();
    return true;
}

void BeenaCore::reset()
{
    std::fill(m_iram.begin(), m_iram.end(), 0);
    std::fill(m_vid.begin(), m_vid.end(), 0);
    std::fill(m_ram60.begin(), m_ram60.end(), 0);
    std::fill(m_ram70.begin(), m_ram70.end(), 0);
    std::fill(m_vram.begin(), m_vram.end(), 0);
    std::fill(m_palCache.begin(), m_palCache.end(), 0);
    memset(m_palette, 0, sizeof(m_palette));
    memset(m_io50, 0, sizeof(m_io50));
    memset(m_videoRegs, 0, sizeof(m_videoRegs));
    memset(m_sensor, 0, sizeof(m_sensor));
    memset(m_aux, 0, sizeof(m_aux));
    memset(m_rtc, 0, sizeof(m_rtc));
    memset(m_midiRegs, 0, sizeof(m_midiRegs));
    memset(m_mc, 0, sizeof(m_mc));
    m_midiBusy = 0;
    m_videoRegs[0x10 / 4] = 0x200;
    m_scale = 1;
    m_scanline = 0;
    m_reqFiq = false;
    m_idleAddr = 0xffffffffu;
    m_sensor[0] = 7;
    m_sensor[0x2c / 4] = 0xffffffffu;
    m_sensor[0x30 / 4] = 0x00ffffffu;
    m_effPage = 0;
    m_mcSeq = BITS_EMPTY; m_mcAddr = 0; m_mcData = 0; m_mcI = 0; m_mcState = IDLE;
    stClear();
    m_evSize = m_evHead = m_evTail = 0;
    memset(m_pcmRegs, 0, sizeof(m_pcmRegs));
    m_pcmVol = 1.0f; m_pcmRate = 8000;
    m_fifoSize = m_fifoHead = m_fifoTail = 0;
    m_fastSize = m_fastHead = m_fastTail = 0;
    m_pcmAcc = m_outAcc = 0; m_pcmCur = 0;
    m_audio.clear();
    updateRtc();
    if (m_cpu) m_cpu->reset();
}

void BeenaCore::setAnalog(int stick, float x, float y)
{
    if (stick != 2 && stick != 3) return;
    if (x < 0) return;   // lifted: the pen keeps its last position
    m_penBook = stick == 3;
    m_penX = std::min(1.0f, std::max(0.0f, x));
    m_penY = std::min(1.0f, std::max(0.0f, y));
}

void BeenaCore::setOption(int id, int value)
{
    if (id == 0) m_page = std::max(0, std::min(6, value));
}

// ---------------------------------------------------------------------------------------------
// bus

uint8_t* BeenaCore::page_ptr(uint32_t pa)
{
    if (pa < 0x20000) return &m_bios[pa];
    if (pa - 0x20000000u < 0x4000) return &m_iram[pa - 0x20000000u];
    if (pa - 0x40010000u < 0x30000) return &m_vid[pa - 0x40010000u];
    if (pa - 0x60010000u < 0x30000) return &m_ram60[pa - 0x60010000u];
    if (pa - 0x70010000u < 0x30000) return &m_ram70[pa - 0x70010000u];
    if (pa - 0xc0000000u < 0x200000) return &m_vram[pa - 0xc0000000u];
    if (!m_cart.empty() && pa - m_cartBase < 0x1000000) return &m_cart[(pa - m_cartBase) & (m_cart.size() - 1)];
    return nullptr;
}

bool BeenaCore::page_readonly(uint32_t pa)
{
    return pa < 0x20000 || (pa & 0xfffff000u) == 0x40020000u || (!m_cart.empty() && pa - m_cartBase < 0x1000000);
}

uint32_t BeenaCore::read32(uint32_t pa)
{
    pa &= ~3u;
    if (uint8_t* p = page_ptr(pa & 0xfffff000u)) { uint32_t v; memcpy(&v, p + (pa & 0xfff), 4); return v; }
    return ioRead(pa);
}
uint16_t BeenaCore::read16(uint32_t pa) { return uint16_t(read32(pa) >> ((pa & 2) ? 0 : 16)); }
uint8_t BeenaCore::read8(uint32_t pa) { return uint8_t(read32(pa) >> ((3 - (pa & 3)) * 8)); }

void BeenaCore::write32(uint32_t pa, uint32_t v)
{
    pa &= ~3u;
    if ((pa & 0xfffff000u) == 0x40020000u) {
        if (pa < 0x40020200u) { palWrite((pa & 0x1ff) >> 1, uint16_t(v >> 16), 0xffff); palWrite(((pa & 0x1ff) >> 1) + 1, uint16_t(v), 0xffff); }
        else memcpy(&m_vid[pa - 0x40010000u], &v, 4);
        return;
    }
    if (page_readonly(pa)) return;
    if (uint8_t* p = page_ptr(pa & 0xfffff000u)) { memcpy(p + (pa & 0xfff), &v, 4); return; }
    ioWrite(pa, v, 0xffffffffu);
}
void BeenaCore::write16(uint32_t pa, uint16_t v)
{
    if ((pa & 0xfffff000u) == 0x40020000u) {
        if (pa < 0x40020200u) palWrite((pa & 0x1ff) >> 1, v, 0xffff);
        else memcpy(&m_vid[((pa - 0x40010000u) & ~1u) ^ 2], &v, 2);
        return;
    }
    if (page_readonly(pa)) return;
    int sh = (pa & 2) ? 0 : 16;
    ioWrite(pa & ~3u, uint32_t(v) << sh, 0xffffu << sh);
}
void BeenaCore::write8(uint32_t pa, uint8_t v)
{
    if ((pa & 0xfffff000u) == 0x40020000u) {
        if (pa < 0x40020200u) palWrite((pa & 0x1ff) >> 1, uint16_t(v) << ((pa & 1) ? 0 : 8), (pa & 1) ? 0x00ff : 0xff00);
        else m_vid[(pa - 0x40010000u) ^ 3] = v;
        return;
    }
    if (page_readonly(pa)) return;
    int sh = (3 - (pa & 3)) * 8;
    ioWrite(pa & ~3u, uint32_t(v) << sh, 0xffu << sh);
}

void BeenaCore::palWrite(int idx, uint16_t data, uint16_t mask)
{
    uint32_t a = 0x10000 + uint32_t(idx) * 2;        // offset in m_vid
    uint16_t old; memcpy(&old, &m_vid[a ^ 2], 2);
    uint16_t e = uint16_t((old & ~mask) | (data & mask));
    memcpy(&m_vid[a ^ 2], &e, 2);
    uint32_t c = bgr555(e);
    m_palette[idx] = c;
    if (m_scanline >= 0 && m_scanline < H) m_palCache[size_t(m_scanline) * 256 + idx] = c;
    m_reqFiq = true;
}

uint32_t BeenaCore::ioRead(uint32_t pa)
{
    uint32_t hi = pa >> 16;
    if (hi == 0x4000) return m_videoRegs[(pa & 0xff) >> 2];
    if (hi == 0x5000) return m_io50[(pa & 0xf) >> 2];
    if (hi == 0x5001) return pcmRead((pa & 0x3f) >> 2);
    if (hi == 0x5002) {
        int off = (pa & 0x7f) >> 2;
        updateSensor(off);
        return m_sensor[off];
    }
    if (hi == 0x5003) {
        int off = (pa & 0xff) >> 2;
        if (off == 0x0c / 4) return 0;           // expansion port: nothing connected
        if (off == 0xbc / 4) return memcacheRead();
        return m_aux[off];
    }
    if (hi == 0x6000) return m_rtc[(pa & 0xff) >> 2];
    if (hi == 0x7000) {
        int off = (pa & 0x1f) >> 2;
        if (off == 0 && m_midiBusy > 0) { m_midiBusy--; return 2; }
        return m_midiRegs[off];
    }
    if ((pa & 0xe0000000u) == 0x80000000u || (pa & 0xe0000000u) == 0xa0000000u) return 0xffffffffu;
    return 0;
}

void BeenaCore::ioWrite(uint32_t pa, uint32_t data, uint32_t mask)
{
    auto combine = [&](uint32_t& r) { r = (r & ~mask) | (data & mask); };
    uint32_t hi = pa >> 16;
    if (hi == 0x4000) {
        int off = (pa & 0xff) >> 2;
        if (off == 0x10 / 4) m_scale = (data & 0x200) == 0 ? 2 : 1;
        combine(m_videoRegs[off]);
    } else if (hi == 0x5000) combine(m_io50[(pa & 0xf) >> 2]);
    else if (hi == 0x5001) {
        int off = (pa & 0x3f) >> 2;
        if (off == 0x10 / 4 && data != 0) m_reqFiq = true;
        pcmWrite(off, data, mask);
    } else if (hi == 0x5003) {
        int off = (pa & 0xff) >> 2;
        combine(m_aux[off]);
        if (off == 0xc0 / 4) evPush(uint8_t(data));
    } else if (hi == 0x6000) {
        uint32_t r = m_rtc[(pa & 0xff) >> 2];
        combine(r);
        m_rtc[(pa & 0xff) >> 2] = uint16_t(r);
    } else if (hi == 0x7000) {
        int off = (pa & 0x1f) >> 2;
        if (off == 0x8 / 4) m_midiBusy = 1;
        combine(m_midiRegs[off]);
    }
}

// ---------------------------------------------------------------------------------------------
// sensors (pen, pages, pads, power)

void BeenaCore::updateSensor(int offset)
{
    if (offset == 0x38 / 4) { m_sensor[offset] = 3; return; }   // power ok
    switch (offset) {
    case 0x8 / 4: case 0xc / 4: case 0x10 / 4: {
        int px = int(m_penX * 255.0f + 0.5f), py = int(m_penY * 255.0f + 0.5f);
        int x, y;
        if (!m_penBook) {
            x = rescale(px, 0, 255, 0x1b8, 0x58);
            y = rescale(py, 0, 255, 0x338, 0x248);
        } else {
            px = 8 + px * (255 - 16) / 255; py = 8 + py * (255 - 8) / 255;
            x = rescale(px, 8, 255 - 8, 0x1b6, 0x109);
            y = rescale(py, 8, 255, 0x3b8, 0x342);
        }
        uint32_t pressed = (m_keys & (1u << 10)) ? 0x80000u : 0;
        m_sensor[offset] = uint32_t(y) << 9 | uint32_t(x) | pressed;
        break;
    }
    case 0x2c / 4:
        if (m_effPage != m_page) {
            m_effPage = m_page;
            m_sensor[offset] = m_effPage >= 4 ? 0 : uint32_t(0xffffffffull >> (m_effPage * 8));
        }
        break;
    case 0x30 / 4:
        m_sensor[offset] = m_effPage == 5 ? 0x0000ff00u : m_effPage == 6 ? 0 : 0x00ffff00u;
        break;
    case 0x34 / 4: {
        uint32_t l = ~m_keys & 0x1f, r = ~(m_keys >> 5) & 0x1f;
        m_sensor[offset] = ((l << 5) | r) & 0x3ff;
        break;
    }
    }
}

void BeenaCore::updateRtc()
{
    time_t t = m_wallClock ? time_t(m_wallClock) : time(nullptr);
    struct tm tmv;
    if (m_wallClock) gmtime_r(&t, &tmv); else localtime_r(&t, &tmv);
    m_rtc[0] = uint16_t(tmv.tm_sec);
    m_rtc[1] = uint16_t(tmv.tm_min);
    m_rtc[2] = uint16_t(tmv.tm_hour);
    m_rtc[3] = uint16_t(tmv.tm_mday);
    m_rtc[4] = uint16_t(tmv.tm_wday);
    m_rtc[5] = uint16_t(tmv.tm_mon);   // MAME's system_time month is 0-based
    m_rtc[6] = uint16_t(tmv.tm_year + 1900 - 2000);
}

// ---------------------------------------------------------------------------------------------
// memcache

uint8_t BeenaCore::stPop() { uint8_t s = m_st[m_stHead]; m_stHead = (m_stHead + 1) & 0xff; m_stSize--; return s; }
uint8_t BeenaCore::evPop() { uint8_t e = m_ev[m_evHead]; m_evHead = (m_evHead + 1) & 0xff; m_evSize--; return e; }
void BeenaCore::stPush(uint8_t s)
{
    if (m_stSize > 0xff) stPop();
    m_st[m_stTail] = s; m_stTail = (m_stTail + 1) & 0xff; m_stSize++;
}
void BeenaCore::evPush(uint8_t e)
{
    if (m_evSize > 0xff) evPop();
    m_ev[m_evTail] = e; m_evTail = (m_evTail + 1) & 0xff; m_evSize++;
}

void BeenaCore::memcacheAdvance(uint32_t& status)
{
    uint8_t ev = evPop();
    status = ev == 6 ? 1 : 0;
    switch (m_mcSeq) {
    case BITS_0: m_mcSeq = ev == 1 ? BITS_0_1 : BITS_ERR; break;
    case BITS_0_1: m_mcSeq = ev == 7 ? BITS_0_1_7 : (ev == 6 ? BITS_6 : BITS_ERR); break;
    case BITS_6: m_mcSeq = ev == 7 ? BITS_6_7 : (ev == 0 ? BITS_0 : BITS_ERR); break;
    case BITS_7: m_mcSeq = ev == 1 ? BITS_7_1 : BITS_ERR; break;
    case BITS_EMPTY: m_mcSeq = ev == 7 ? BITS_7 : (ev == 6 ? BITS_6 : (ev == 0 ? BITS_0 : BITS_ERR)); break;
    default: m_mcSeq = BITS_ERR;
    }
}

void BeenaCore::memcacheParseBit(uint32_t& status)
{
    if (m_mcSeq == BITS_0_1) {
        if (m_evSize && m_ev[m_evHead] == 7) return;
        m_mcSeq = BITS_EMPTY; m_mcData = uint8_t(m_mcData << 1); m_mcI++;
    } else if (m_mcSeq == BITS_6_7) {
        m_mcSeq = BITS_EMPTY; m_mcData = uint8_t(m_mcData << 1 | 1); m_mcI++;
    } else if (m_mcSeq == BITS_0_1_7) {
        status = 1; m_mcSeq = BITS_EMPTY; m_mcState = IDLE; stClear();
    }
}

uint32_t BeenaCore::memcacheRead()
{
    uint32_t status = 0;
    while (m_evSize) {
        memcacheAdvance(status);
        switch (m_mcState) {
        case IDLE:
            if (m_mcSeq == BITS_6) { m_mcSeq = BITS_EMPTY; m_mcState = CONSUME_UNTIL_IDLE; }
            else if (m_mcSeq == BITS_7_1) { m_mcSeq = BITS_EMPTY; m_mcState = READ_COMMAND; }
            break;
        case READ_COMMAND:
            memcacheParseBit(status);
            if (m_mcI == 8) {
                m_mcState = READ_ACK;
                m_mcAddr = (((m_mcData & 0xe) >> 1) << 8) | (m_mcAddr & 0xff);
                if ((m_mcData & 0xa1) == 0xa0) { stPush(READ_ADDRESS_BYTE); stPush(READ_ACK); stPush(READ_RAM_BYTE); }
                else if ((m_mcData & 0xa1) == 0xa1) stPush(WRITE_RAM_BYTE);
                else { m_mcState = CONSUME_UNTIL_IDLE; stClear(); }
                m_mcData = 0; m_mcI = 0;
            }
            break;
        case READ_ACK:
            if (m_mcSeq == BITS_6_7) { m_mcSeq = BITS_EMPTY; m_mcState = stPop(); }
            break;
        case READ_ADDRESS_BYTE:
            memcacheParseBit(status);
            if (m_mcI == 8) { m_mcAddr |= m_mcData; m_mcData = 0; m_mcI = 0; m_mcState = stPop(); }
            break;
        case READ_RAM_BYTE:
            memcacheParseBit(status);
            if (m_mcSeq == BITS_7_1) { m_mcSeq = BITS_EMPTY; m_mcState = READ_COMMAND; m_mcData = 0; m_mcI = 0; }
            if (m_mcI == 8) {
                m_mc[m_mcAddr & 0x7ff] = m_mcData;
                m_mcData = 0; m_mcI = 0;
                m_mcState = READ_ACK; stClear(); stPush(READ_RAM_BYTE);
                m_mcAddr++;
            }
            break;
        case WRITE_RAM_BYTE:
            if (m_mcI == 8 && m_mcSeq == BITS_6_7) { m_mcSeq = BITS_EMPTY; m_mcI = 0; }
            if (m_mcSeq == BITS_0_1) {
                if (m_evSize && m_ev[m_evHead] == 7) continue;
                m_mcSeq = BITS_EMPTY; m_mcAddr++; m_mcI = 0;
            } else if (m_mcSeq == BITS_6_7) {
                status = (m_mc[m_mcAddr & 0x7ff] >> (7 - m_mcI)) & 1;
                m_mcSeq = BITS_EMPTY; m_mcI++;
            } else if (m_mcSeq == BITS_0_1_7) {
                status = 1; m_mcAddr = 0; m_mcData = 0; m_mcI = 0; m_mcSeq = BITS_EMPTY; m_mcState = IDLE; stClear();
            }
            break;
        case CONSUME_UNTIL_IDLE:
            if (m_mcSeq == BITS_0_1_7) { status = 1; m_mcAddr = 0; m_mcData = 0; m_mcI = 0; m_mcSeq = BITS_EMPTY; m_mcState = IDLE; }
            break;
        }
        if (m_mcSeq == BITS_ERR) {
            m_mcAddr = 0; m_mcData = 0; m_mcI = 0; m_mcSeq = BITS_EMPTY; m_mcState = CONSUME_UNTIL_IDLE; stClear();
        }
    }
    return status;
}

// ---------------------------------------------------------------------------------------------
// PCM (AP2010)

uint32_t BeenaCore::pcmRead(int off)
{
    if (off == 0) return m_pcmRegs[1] != 0 ? 0x0f : 0;
    if (off == 1 && m_fifoSize > 0x1ff) return m_pcmRegs[1] & 0xfffffffeu;
    if (off == 0x1c / 4) {
        int n = m_fifoSize;
        if (n > 0x1ff) n = 0x1ff; else if (n > 1) n -= 2;
        return uint32_t(n);
    }
    return m_pcmRegs[off];
}

void BeenaCore::pcmWrite(int off, uint32_t data, uint32_t mask)
{
    m_pcmRegs[off] = (m_pcmRegs[off] & ~mask) | (data & mask);
    auto push = [](uint16_t* f, int& size, int& head, int& tail, uint16_t s) {
        if (s == 0) return;
        if (size > FIFO - 1) { head = (head + 1) & (FIFO - 1); size--; }
        f[tail] = s; tail = (tail + 1) & (FIFO - 1); size++;
    };
    switch (off) {
    case 0x4 / 4:
        if ((data & 0x78) == 0x78) {
            m_pcmRate = 8000 * (1 + ((data >> 1) & 1));
            m_fifoSize = m_fifoHead = m_fifoTail = 0;
        }
        break;
    case 0xc / 4:
        if (mask & 0xffff0000u) push(m_fifo, m_fifoSize, m_fifoHead, m_fifoTail, uint16_t(data >> 16));
        if (mask & 0x0000ffffu) push(m_fifo, m_fifoSize, m_fifoHead, m_fifoTail, uint16_t(data));
        break;
    case 0x10 / 4:
        if (mask & 0xffff0000u) push(m_fast, m_fastSize, m_fastHead, m_fastTail, uint16_t(data >> 16));
        if (mask & 0x0000ffffu) push(m_fast, m_fastSize, m_fastHead, m_fastTail, uint16_t(data));
        break;
    case 0x18 / 4:
        m_pcmVol = std::min(float((data & 0x1ff00000u) >> 20) / 345.0f, 1.0f);
        break;
    }
}

int16_t BeenaCore::pcmPop()
{
    if (m_fastSize) { int16_t s = int16_t(m_fast[m_fastHead]); m_fastHead = (m_fastHead + 1) & (FIFO - 1); m_fastSize--; return s; }
    if (m_fifoSize) { int16_t s = int16_t(m_fifo[m_fifoHead]); m_fifoHead = (m_fifoHead + 1) & (FIFO - 1); m_fifoSize--; return s; }
    return 0;
}

size_t BeenaCore::readAudio(int16_t* out, size_t maxSamples)
{
    size_t n = std::min(maxSamples, m_audio.size());
    std::copy(m_audio.begin(), m_audio.begin() + long(n), out);
    m_audio.erase(m_audio.begin(), m_audio.begin() + long(n));
    return n;
}

// ---------------------------------------------------------------------------------------------
// timing

void BeenaCore::pulseIrq(bool fiq)
{
    if (fiq) m_cpu->set_fiq(true); else m_cpu->set_irq(true);
    m_cpu->run(1);
    if (fiq) m_cpu->set_fiq(false); else m_cpu->set_irq(false);
}

void BeenaCore::findIdleLoop()
{
    // the games' wait-for-IRQ loop, copied into work RAM:
    // mov r3,#1 / strb r3,[r0] / ldrb r3,[r0] / cmp r3,#0 / bne -4
    static const uint32_t sig[5] = { 0xe3a03001, 0xe5c03000, 0xe5d03000, 0xe3530000, 0x1afffffc };
    uint32_t pc = m_cpu->r[15];
    if (pc <= 0xc00cc000u || pc >= 0xc00fff80u) return;
    for (uint32_t s = (pc & ~3u) - 16; s <= (pc & ~3u); s += 4) {
        bool ok = true;
        for (int i = 0; i < 5 && ok; i++) {
            uint32_t a = s + uint32_t(i) * 4 - 0xc0000000u, w;
            if (a + 4 > m_vram.size()) { ok = false; break; }
            memcpy(&w, &m_vram[a], 4);
            ok = w == sig[i];
        }
        if (ok) { m_idleAddr = s; return; }
    }
}

void BeenaCore::scanline(int param)
{
    m_scanline = param / m_scale;
    m_videoRegs[1] = 0x10000u * uint32_t(20 + m_scanline);
    if (m_scanline % H == 0) {
        m_videoRegs[0] ^= 0x10;
        updateRtc();
        pulseIrq(false);
        m_reqFiq = true;
    }
    if (m_reqFiq) { m_reqFiq = false; pulseIrq(true); }
}

void BeenaCore::runFrame()
{
    if (!m_cpu) return;
    for (int line = 0; line < H; line++) {
        scanline(line);
        uint32_t left = CYCLES_PER_LINE;
        while (left) {
            uint32_t n = std::min<uint32_t>(left, 250);
            m_cpu->run(n);
            left -= n;
            uint32_t pc = m_cpu->r[15];
            if (m_idleAddr == 0xffffffffu) findIdleLoop();
            if (pc - m_idleAddr < 20) break;    // waiting for the next interrupt
        }
        // audio: 16 kHz out, the PCM FIFO drains at its own rate
        m_pcmAcc += m_pcmRate;
        m_outAcc += 16000;
        while (m_outAcc >= 60 * H) {
            m_outAcc -= 60 * H;
            while (m_pcmAcc >= 16000) { m_pcmAcc -= 16000; m_pcmCur = pcmPop(); }
            m_audio.push_back(int16_t(float(m_pcmCur) * m_pcmVol));
        }
    }
    if (m_audio.size() > 16000) m_audio.erase(m_audio.begin(), m_audio.begin() + long(m_audio.size() - 16000));
    render();
    m_frame++;
}

// ---------------------------------------------------------------------------------------------
// video

int BeenaCore::scrollX(int x, int i)
{
    x += -signed10(vb16(0xc00c6000u + uint32_t(i) * 2));
    if (x > 0x2c0 + 0xa0) x -= 0x2c0 + 0xa0 * 2;
    if (x < -0xa0) x += 0x2c0 + 0xa0 * 2;
    return x;
}

int BeenaCore::scrollY(int y, int i)
{
    uint32_t e = vid16(0x40030000u + uint32_t(i) * 2);
    if ((e & 0xfc00) == 0xfc00 && y <= 224) return y;
    y += signed10(e);
    if (y > (0x400 - 0x20) - 0x10) y -= 0x400;
    if (y < -0x20) y += 0x400;
    return y;
}

void BeenaCore::drawTile(uint32_t* dst, int dw, int dh, const uint8_t* tiles, int code, bool fx, bool fy,
                         int x, int y, int sc, bool linePal)
{
    int size = 16 * sc;
    if (x >= dw || y >= dh || x + size <= 0 || y + size <= 0) return;
    uint32_t base = uint32_t(code & 0x7ff) * 256;
    for (int py = 0; py < size; py++) {
        int dy = y + py;
        if (dy < 0 || dy >= dh) continue;
        int sy = py / sc; if (fy) sy = 15 - sy;
        uint32_t* row = dst + size_t(dy) * dw;
        const uint32_t* lc = linePal ? &m_palCache[size_t(dy / m_scale) * 256] : nullptr;
        for (int px = 0; px < size; px++) {
            int dx = x + px;
            if (dx < 0 || dx >= dw) continue;
            int sx = px / sc; if (fx) sx = 15 - sx;
            uint8_t c = tiles[(base + uint32_t(sy) * 16 + uint32_t(sx)) ^ 3];
            if (!c) continue;
            uint32_t col = lc ? lc[c] : 0;
            if (!col) col = m_palette[c];
            row[dx] = col;
        }
    }
}

void BeenaCore::drawLayer(uint32_t mapAddr, int scrollIdx, bool active, bool overlay)
{
    if (!active) return;
    if ((m_videoRegs[0x10 / 4] & 0x30) == 0x30) drawLayerScanlines(mapAddr, scrollIdx, overlay);
    else drawLayerTiles(mapAddr, scrollIdx, overlay);
}

void BeenaCore::drawLayerTiles(uint32_t mapAddr, int scrollIdx, bool overlay)
{
    const uint8_t* tiles = &m_vram[0x100000];
    uint32_t vr = m_videoRegs[0x10 / 4];
    for (int off = 0; off < 0x1000; off++) {
        int y = ((off / 64) - 2) * 16;
        int x = ((off % 64) - 10) * 16;
        int sxi = scrollIdx, syi = scrollIdx;
        if (y >= 0 && y < (0x90 / 4) * 16 && (vr & 0x20)) sxi += y / 16 * 2;
        if (x >= 0 && x < (0xb4 / 4) * 16 && (vr & 0x40)) syi += x / 16 * 2;
        x = scrollX(x, sxi);
        y = scrollY(y, syi);
        uint16_t tile = vb16(mapAddr + uint32_t(off) * 2);
        int tr = tile >> 12;
        if (bool(tr & 4) != overlay) continue;
        const uint8_t* t = (tile & 0x800) ? tiles + 0x80000 : tiles;
        drawTile(m_fb.data(), W, H, t, tile & 0x7ff, tr & 1, tr & 2, x * m_scale, y * m_scale, m_scale, true);
    }
}

void BeenaCore::drawLayerScanlines(uint32_t mapAddr, int scrollIdx, bool overlay)
{
    std::fill(m_cacheLayer.begin(), m_cacheLayer.end(), 0);
    const uint8_t* tiles = &m_vram[0x100000];
    uint32_t vr = m_videoRegs[0x10 / 4];
    for (int off = 0; off < 0x1000; off++) {
        int y = ((off / 64) - 2) * 16;
        int x = ((off % 64) - 10) * 16;
        uint16_t tile = vb16(mapAddr + uint32_t(off) * 2);
        int tr = tile >> 12;
        if (bool(tr & 4) != overlay) continue;
        int syi = scrollIdx;
        if (x >= 0 && x < (0xb4 / 4) * 16 && (vr & 0x40)) syi += x / 16 * 2;
        y = scrollY(y, syi);
        x += 10 * 16;
        const uint8_t* t = (tile & 0x800) ? tiles + 0x80000 : tiles;
        drawTile(m_cacheLayer.data(), 1024, 512, t, tile & 0x7ff, tr & 1, tr & 2, x, y, 1, false);
    }
    for (int dy = 0; dy < 512 / m_scale && dy < 512; dy++) {
        int sxi = scrollIdx;
        if (dy < (0x90 / 4) * 16) sxi += dy * 2;
        for (int sx = 0; sx < 1024; sx++) {
            uint32_t c = m_cacheLayer[size_t(dy) * 1024 + sx];
            if (!c) continue;
            int dx = scrollX(sx, sxi) - 10 * 16;
            int ox = dx * m_scale, oy = dy * m_scale;
            if (ox < 0 || oy < 0 || ox >= W || oy >= H) continue;
            for (int a = 0; a < m_scale; a++)
                for (int b = 0; b < m_scale; b++)
                    if (oy + a < H && ox + b < W) m_fb[size_t(oy + a) * W + ox + b] = c;
        }
    }
}

void BeenaCore::drawBitmap()
{
    uint32_t r20 = m_videoRegs[0x20 / 4];
    if (!(r20 & 1)) return;
    int fullW = int((r20 >> 8) & 0xff) << 4, fullH = int((r20 >> 16) & 0xff) << 4;
    int offX = int(m_videoRegs[0x24 / 4] & 0x3ff), offY = int((m_videoRegs[0x24 / 4] >> 16) & 0x3ff);
    int scX = int(m_videoRegs[0x28 / 4] & 0x3ff), scY = int((m_videoRegs[0x28 / 4] >> 16) & 0x3ff);
    int clW = int(m_videoRegs[0x2c / 4] & 0x3ff), clH = int((m_videoRegs[0x2c / 4] >> 16) & 0x3ff);
    uint32_t i = uint32_t(offY * fullW + offX);
    const int s = m_scale;
    for (int y = 0; y < fullH * s; y += s) {
        for (int x = 0; x < fullW * s; x += s) {
            if (i >= 0xc6000 / 2) return;
            uint16_t c = vb16(0xc0000000u + i * 2);
            i++;
            if (c == 0 || y >= clH * s || x >= clW * s || y + scY * s > H - 1 || x + scX * s > W - 1) continue;
            uint32_t rgb = bgr555(c);
            for (int a = 0; a < s; a++)
                for (int b = 0; b < s; b++) {
                    int oy = y + a + scY * s, ox = x + b + scX * s;
                    if (oy < H && ox < W) m_fb[size_t(oy) * W + ox] = rgb;
                }
        }
    }
}

void BeenaCore::drawSprites(bool overlay)
{
    const uint8_t* tiles = &m_vram[0x180000];
    for (int off = 0x400 / 2 - 4; off >= 0; off -= 4) {
        uint32_t base = 0x40010000u + uint32_t(off) * 2;
        uint16_t w0 = vid16(base), w1 = vid16(base + 2), w2 = vid16(base + 4);
        int prio = w1 & 0xff;
        if ((prio > 0 && !overlay) || (prio == 0 && overlay)) continue;
        uint32_t pos = uint32_t(w0) << 16 | w1;
        int y = int((pos >> 20) & 0x7ff), x = int((pos >> 8) & 0x7ff);
        bool fy = (pos >> 20) & 0x800, fx = (pos >> 8) & 0x800;
        int df = int(m_videoRegs[0x38 / 4] & 0xff), dx = 0, dy = 0;
        if (df == 0x2c) { dx = 14 * 16; dy = 7 * 16; }
        else if (df == 0x20) { dx = 6 * 16; dy = -16; }
        int idx = w2 & 0x7ff, len = (w2 & 0xf000) >> 12;
        if (idx == 0 && x == 0 && y == 0) continue;
        int nx = (len % 4) + 1, ny = (len / 4) + 1, n = 0;
        for (int j = 0; j < ny; j++) {
            int py = fy ? ny - 1 - j : j;
            for (int k = 0; k < nx; k++) {
                int px = fx ? nx - 1 - k : k;
                drawTile(m_fb.data(), W, H, tiles, idx + n++, fx, fy,
                         (x + px * 16) * m_scale - 0x2c0 / 2 + dx, (y + py * 16) * m_scale - 0x1e0 / 2 + dy, m_scale, false);
            }
        }
    }
}

void BeenaCore::screenBlend()
{
    uint32_t r30 = m_videoRegs[0x30 / 4];
    int sr = r30 & 0x3f, sg = (r30 >> 8) & 0x3f, sb = (r30 >> 16) & 0x3f;
    int ar = alphaStep(sr), ag = alphaStep(sg), ab = alphaStep(sb);
    if (ar == 0xff && ag == 0xff && ab == 0xff && sr < 0x20 && sg < 0x20 && sb < 0x20) return;   // (near) identity
    uint32_t blend = (sb < 0x20 ? 0xffu : 0) | (sg < 0x20 ? 0xff00u : 0) | (sr < 0x20 ? 0xff0000u : 0);
    // as MAME: the channel levels end up as (bits 0-7: g, 8-15: r, 16-23: b)
    int l0 = ag, l1 = ar, l2 = ab;
    for (auto& p : m_fb) {
        uint32_t s = p, d = blend;
        p = (((s & 0xff) * l0 + (d & 0xff) * (256 - l0)) >> 8) |
            ((((s & 0xff00) * l1 + (d & 0xff00) * (256 - l1)) >> 8) & 0xff00) |
            ((((s & 0xff0000) * uint64_t(l2) + (d & 0xff0000) * uint64_t(256 - l2)) >> 8) & 0xff0000);
    }
}

void BeenaCore::render()
{
    std::fill(m_fb.begin(), m_fb.end(), m_palette[0]);
    uint32_t vr = m_videoRegs[0x10 / 4];
    if (vr & 1) {
        uint32_t prio = vr & 0x3000;
        bool after = (m_videoRegs[0x20 / 4] & 0x10) != 0;
        if (prio == 0x3000) drawBitmap();
        drawLayer(0xc00ca000u, 1, vr & 4, false);
        if (prio == 0x2000) drawBitmap();
        drawLayer(0xc00c8000u, 0, vr & 2, false);
        if (prio == 0x1000 && !after) drawBitmap();
        drawSprites(false);
        drawLayer(0xc00ca000u, 1, vr & 4, true);
        if (prio == 0 && !after) drawBitmap();
        drawLayer(0xc00c8000u, 0, vr & 2, true);
        if (prio == 0x1000 && after) drawBitmap();
        drawSprites(true);
        if (prio == 0 && after) drawBitmap();
    }
    screenBlend();
    for (auto& p : m_fb) p |= 0xff000000u;
    std::fill(m_palCache.begin(), m_palCache.end(), 0);
}
