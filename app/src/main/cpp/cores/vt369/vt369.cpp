#include <set>
#include <cstdio>
#include <cstdlib>
// PocketPlug - VT369 "NES-on-a-chip"
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Ported from MAME (see vt369.h for the original files and authors). Function
// names follow MAME's so the two can be compared side by side.

#include "vt369.h"
#include <cstring>
#include <cmath>
#include <algorithm>
#ifdef VT_DEBUG
#include <cstdio>
#include <cstdlib>
extern int g_vtFrame;
#endif

#define BIT(x, n) (((x) >> (n)) & 1)
static inline uint32_t rgb(int r, int g, int b) { return 0xff000000u | (r << 16) | (g << 8) | b; }

enum { PPU_CONTROL0 = 0, PPU_CONTROL1, PPU_STATUS, PPU_SPRITE_ADDRESS, PPU_SPRITE_DATA, PPU_SCROLL, PPU_ADDRESS, PPU_DATA };
enum {
    PPU_CONTROL0_INC = 0x04, PPU_CONTROL0_SPR_SELECT = 0x08, PPU_CONTROL0_CHR_SELECT = 0x10, PPU_CONTROL0_SPRITE_SIZE = 0x20, PPU_CONTROL0_NMI = 0x80,
    PPU_CONTROL1_DISPLAY_MONO = 0x01, PPU_CONTROL1_BACKGROUND_L8 = 0x02, PPU_CONTROL1_SPRITES_L8 = 0x04, PPU_CONTROL1_BACKGROUND = 0x08,
    PPU_CONTROL1_SPRITES = 0x10, PPU_CONTROL1_COLOR_EMPHASIS = 0xe0,
    PPU_STATUS_8SPRITES = 0x20, PPU_STATUS_SPRITE0_HIT = 0x40, PPU_STATUS_VBLANK = 0x80
};
static const int BOTTOM_VISIBLE = 239, W = 256;   // scanlines / first vblank line: m_lines / m_vblankFirst (NTSC 262 / 241, PAL 312 / 291)
static const int DOTS_PER_LINE = 341;
static const int64_t DOTS_PER_FRAME = 341 * 262;          // one 60.0988 Hz screen frame (the VT369 LCD timer: NTSC only)
static const int64_t OAM_DMA_DELAY_DOTS = 10739;          // 2 ms at 21.477 MHz / 4
static const unsigned YUV444_COLOR = 0x40 * 8;
static const int DOTS_PER_APU_SAMPLE = 12;      // APU stream runs at clock / 4

static uint8_t cpu_rd(void* c, uint16_t a) { return ((Vt369Core*)c)->cpuRead(a); }
static void cpu_wr(void* c, uint16_t a, uint8_t d) { ((Vt369Core*)c)->cpuWrite(a, d); }
static uint8_t spu_rd(void* c, uint16_t a) { return ((Vt369Core*)c)->spuRead(a); }
static void spu_wr(void* c, uint16_t a, uint8_t d) { ((Vt369Core*)c)->spuWrite(a, d); }
static uint8_t spu_ext(void* c, uint32_t a) { return ((Vt369Core*)c)->spuExtRead(a); }
static const double MASTER_CLOCK = 21477272.7;  // RP2A03_NTSC_XTAL, sound CPU clock
static const int SPU_CYCLES_PER_DOT = 4;

/*************************************************************************
 Setup
*************************************************************************/

Vt369Core::Vt369Core(const Vt369Config& c) : m_cfg(c)
{
    if (m_cfg.pal) { m_lines = 312; m_vblankFirst = 291; }
    std::memset(&m_s, 0, sizeof(m_s));
    std::memset(m_out, 0, sizeof(m_out));
    std::memset(m_bitmapBuf, 0, sizeof(m_bitmapBuf));
    std::memset(&m_sd, 0, sizeof(m_sd));
    m_cpu.setBus(this, cpu_rd, cpu_wr);
    m_cpu.decimalEnabled = c.soc == 3;   // RP2A03 core; the SH6578 has a plain 6502
    m_cpu.interruptDelay = true;
    m_spu.setBus(this, spu_rd, spu_wr);
    m_spu.extRead = spu_ext;
    m_spu.interruptDelay = true;
    m_spu.irqHold = true;
    // MAME m6502_swap_op_d5_d6.cpp: rp2a03_core_swap_op_d5_d6 descramble()
    for (int op = 0; op < 256; op++) {
        uint8_t v = op;
        switch (c.crypt) {
        case 0: v = (op & 0x9f) | ((op >> 1) & 0x20) | ((op << 1) & 0x40); break;   // d5/d6
        case 1: v = (op & 0xed) | ((op >> 3) & 0x02) | ((op << 3) & 0x10); break;   // d1/d4
        case 3: v = (op & 0x39) | ((op >> 1) & 0x40) | ((op << 1) & 0x80) | ((op >> 1) & 0x02) | ((op << 1) & 0x04); break;   // gbox2020: d6/d7 + d1/d2
        }
        m_cryptTable[op] = v;
    }
    initPalettes();
    apuInit();
}

bool Vt369Core::load(const std::vector<RomFile>& roms, std::string& error)
{
    const RomFile* r = nullptr;
    for (auto& f : roms) if (f.name == m_cfg.romName) r = &f;
    if (!r && roms.size() == 1) r = &roms[0];
    if (!r) { error = "Missing " + m_cfg.romName; return false; }
    if (r->data.size() == m_cfg.romSize) {
        // big sets (128 MB): take the loader's buffer instead of holding two copies
        m_rom = std::move(const_cast<std::vector<uint8_t>&>(r->data));
    } else {
        m_rom.assign(m_cfg.romSize, 0xff);
        std::memcpy(m_rom.data(), r->data.data(), std::min(r->data.size(), (size_t)m_cfg.romSize));
    }
    for (auto& f : roms) {
        if (!m_cfg.internalRomName.empty() && f.name == m_cfg.internalRomName) m_internal = f.data;
        if (!m_cfg.protRomName.empty() && f.name == m_cfg.protRomName) m_protRom = f.data;
    }
    if (!m_cfg.protRomName.empty() && m_protRom.size() < 0x100) { error = "Missing " + m_cfg.protRomName; return false; }
    if (!m_cfg.internalRomName.empty() && m_internal.size() < 0x1000) { error = "Missing " + m_cfg.internalRomName; return false; }
    if (!m_cfg.sdName.empty()) {
        const RomFile* sd = nullptr;
        for (auto& f : roms) if (f.name == m_cfg.sdName) sd = &f;
        if (!sd) { error = "Missing the SD card image (" + m_cfg.sdName + ")"; return false; }
        m_sdFile = fopen(sd->path.c_str(), "rb");
        if (!m_sdFile) { error = "Can't open " + sd->path; return false; }
        if (ChdFile::isChd(m_sdFile)) {
            // MAME's compressed card image: read hunk by hunk, never unpacked
            fclose(m_sdFile); m_sdFile = nullptr;
            m_sdChd.reset(new ChdFile());
            if (!m_sdChd->open(sd->path, error)) { error = "SD card image: " + error; return false; }
            m_sdSize = m_sdChd->size();
        } else {
            fseek(m_sdFile, 0, SEEK_END);
            m_sdSize = (uint64_t)ftello(m_sdFile);
        }
    }
    reset();
    return true;
}

Vt369Core::~Vt369Core()
{
    if (m_sdFile) fclose(m_sdFile);
}

// ---------------------------------------------------------------------------
// The A6Plus's security chip: an I2C device at address E0 / E1 on port 414b
// (SCL bit 7, SDA bit 6). The games read register C1 (60 bytes) into RAM at
// 0106: four-byte stubs that stand in for hardware accesses they have been
// patched to call (JSR $0126 for LDA $4016, $0132 for STA $2006, ...). The
// chip isn't dumped; these stubs are worked out from how the games use them.
// ---------------------------------------------------------------------------

static const uint8_t kSecurityC1[60] = {
    // 0106-0125: not used by the games seen: plain returns
    0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60,
    0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60,
    0xad, 0x16, 0x40, 0x60,     // 0126: LDA $4016  (pad 1, read bit by bit)
    0xad, 0x17, 0x40, 0x60,     // 012a: LDA $4017  (pad 2)
    0x8d, 0x03, 0x20, 0x60,     // 012e: STA $2003  (sprite address, before sprite DMA)
    0x8d, 0x06, 0x20, 0x60,     // 0132: STA $2006  (PPU address)
    0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60,
};

void Vt369Core::i2cLines()
{
    uint8_t dir = m_s.dir414x, out = m_s.data414b;
    bool scl = (dir & 0x80) ? (out & 0x80) != 0 : true;
    bool sdaM = (dir & 0x40) ? (out & 0x40) != 0 : true;
    bool sda = sdaM && m_i2c.sdaOut;
    auto& s = m_i2c;
    if (scl && s.scl && s.sda && !sda) {               // start
        s.state = 1; s.bits = 0; s.shift = 0; s.sdaOut = true;
    } else if (scl && s.scl && !s.sda && sda) {        // stop
        s.state = 0; s.sdaOut = true;
    } else if (scl && !s.scl) {                        // clock rising: sample
        if (s.state == 1 || s.state == 2) {            // receiving address / register
            if (s.bits < 8) { s.shift = uint8_t((s.shift << 1) | (sdaM ? 1 : 0)); s.bits++; }
        } else if (s.state == 3) {                     // sending: master acks after 8 bits
            if (s.bits == 8) { s.nack = sdaM; }
        }
    } else if (!scl && s.scl) {                        // clock falling: drive
        if (s.state == 1 || s.state == 2) {
            if (s.bits == 8) {
                if (s.state == 1) {
                    if ((s.shift & 0xfe) == 0xe0) {
                        s.sdaOut = false;              // ack
                        s.readMode = s.shift & 1;
                        s.bits = 9;
                    } else { s.state = 0; }
                } else {
                    s.reg = s.shift; s.pos = 0;
                    s.sdaOut = false; s.bits = 9;
                }
            } else if (s.bits == 9) {                  // ack clock done
                s.sdaOut = true;
                if (s.state == 1 && s.readMode) {
                    s.state = 3; s.bits = 0;
                    s.cur = i2cByte(); s.sdaOut = (s.cur & 0x80) != 0;
                } else {
                    s.state = 2; s.bits = 0; s.shift = 0;
                }
            }
        } else if (s.state == 3) {
            s.bits++;
            if (s.bits < 8) s.sdaOut = ((s.cur << s.bits) & 0x80) != 0;
            else if (s.bits == 8) s.sdaOut = true;     // release for the master's ack
            else {                                     // next byte
                s.bits = 0; s.pos++;
                s.cur = i2cByte(); s.sdaOut = (s.cur & 0x80) != 0;
            }
        }
    }
    s.scl = scl; s.sda = sdaM && s.sdaOut;
}

uint8_t Vt369Core::i2cByte()
{
    if (m_i2c.reg == 0xc1 && m_i2c.pos < sizeof(kSecurityC1)) return kSecurityC1[m_i2c.pos];
    return 0xff;
}

// ---------------------------------------------------------------------------
// SPI flash controller (4350-435f), as the A6Plus loader uses it to copy a
// game from the card into the program flash before starting it:
//   4350 chip select (bit 7 set: deselect), 4352 command / address byte out,
//   4353 byte exchange (write a dummy, read the reply), 4354 bit 0 busy,
//   4356-4358 flash address, 435c-435d length, 435e-435f CPU source,
//   4355 bit 0 starts a page-program DMA. Writes only change the copy of the
//   flash in memory: the ROM file itself is never touched.
// ---------------------------------------------------------------------------

uint8_t Vt369Core::spiRead(uint16_t a)
{
    auto& s = m_spi;
    switch (a) {
    case 0x4353: {
        uint8_t r = 0xff;
        if (s.len && s.cmd[0] == 0x05) r = 0x00;                       // status: not busy
        else if (s.len && s.cmd[0] == 0x35) r = 0x02;                  // status 2: quad enabled
        else if (s.len && s.cmd[0] == 0x9f) { static const uint8_t id[3] = { 0xef, 0x40, 0x17 }; r = id[s.readPos++ % 3]; }
        else if (s.len >= 4 && (s.cmd[0] == 0x03 || s.cmd[0] == 0x0b)) {
            uint32_t addr = (uint32_t(s.cmd[1]) << 16 | s.cmd[2] << 8 | s.cmd[3]) + s.readPos++;
            r = m_rom[addr & (m_rom.size() - 1)];
        }
        return r;
    }
    case 0x4354: return 0x00;                                          // never busy
    default: return s.reg[a & 0x0f];
    }
}

void Vt369Core::spiWrite(uint16_t a, uint8_t d)
{
    auto& s = m_spi;
    s.reg[a & 0x0f] = d;
    switch (a) {
    case 0x4350:
        if (d & 0x80) spiEnd();
        s.len = 0; s.readPos = 0;
        break;
    case 0x4352:
        if (s.len < sizeof(s.cmd)) s.cmd[s.len++] = d;
        if (s.len == 1 && s.cmd[0] == 0x06) s.wel = true;
        if (s.len == 1 && s.cmd[0] == 0x04) s.wel = false;
        break;
    case 0x4355:
        if ((d & 0x01) && s.len && (s.cmd[0] == 0x32 || s.cmd[0] == 0x02) && s.wel) {
            uint32_t addr = uint32_t(s.reg[8]) << 16 | s.reg[7] << 8 | s.reg[6];
            uint16_t src = uint16_t(s.reg[0xf] << 8 | s.reg[0xe]);
            uint32_t n = uint32_t(s.reg[0xd] << 8 | s.reg[0xc]);
            if (n == 0 || n > 256) n = 256;
            for (uint32_t i = 0; i < n; i++) {
                uint32_t at = (addr & ~0xffu) | ((addr + i) & 0xff);       // wraps inside the page
                m_rom[at & (m_rom.size() - 1)] &= cpuRead(uint16_t(src + i));
                flashDirty(at & (m_rom.size() - 1));
            }
            s.wel = false;
        }
        break;
    }
}

void Vt369Core::spiEnd()
{
    auto& s = m_spi;
    if (!s.len || !s.wel) return;
    uint32_t addr = s.len >= 4 ? (uint32_t(s.cmd[1]) << 16 | s.cmd[2] << 8 | s.cmd[3]) : 0;
    uint32_t size = 0;
    switch (s.cmd[0]) {
    case 0x20: size = 0x1000; break;
    case 0x52: size = 0x8000; break;
    case 0xd8: size = 0x10000; break;
    case 0x60: case 0xc7: addr = 0; size = (uint32_t)m_rom.size(); break;
    case 0x02: case 0x32:                                              // page program by bytes
        for (int i = 4; i < s.len; i++) {
            uint32_t at = ((addr & ~0xffu) | ((addr + i - 4) & 0xff)) & (m_rom.size() - 1);
            m_rom[at] &= s.cmd[i];
            flashDirty(at);
        }
        s.wel = false;
        return;
    default: return;
    }
    if (s.len < 4 && size < m_rom.size()) return;
    addr &= ~(size - 1);
    if (addr < m_rom.size()) {
        size_t n = std::min<size_t>(size, m_rom.size() - addr);
        memset(&m_rom[addr], 0xff, n);
        for (size_t o = 0; o < n; o += 0x1000) flashDirty(uint32_t(addr + o));
    }
    s.wel = false;
}

// ---------------------------------------------------------------------------
// SD card controller (VT369 4310-4328), worked out from the A6Plus code:
// 4314 = response flags, 4315 = command index, 4310-4313 = argument (LE);
// writing 4313 sends the command. 4326 bit 0 = done, 4327 bit 7 = error
// (4328 read acknowledges it). The response is at 4318-431b (MSB at 431b).
// ---------------------------------------------------------------------------

bool Vt369Core::sdReadBlock(uint64_t addr)
{
    if (!sdPresent() || addr + 512 > m_sdSize) return false;
    m_sd.bufPos = 0;
    if (m_sdChd) { m_sd.bufLen = m_sdChd->read(addr, m_sd.buf, 512) ? 512 : 0; return m_sd.bufLen == 512; }
    fseeko(m_sdFile, (off_t)addr, SEEK_SET);
    m_sd.bufLen = (uint32_t)fread(m_sd.buf, 1, 512, m_sdFile);
    return m_sd.bufLen == 512;
}

void Vt369Core::sdCommand()
{
    uint32_t arg = m_sd.arg[0] | (m_sd.arg[1] << 8) | (m_sd.arg[2] << 16) | ((uint32_t)m_sd.arg[3] << 24);
    uint32_t r = 0x900;                    // R1: transfer state, ready for data
    m_sd.err = 0;
    bool app = m_sd.app;
    m_sd.app = false;
    memset(m_sd.resp, 0, sizeof m_sd.resp);
    switch (m_sd.cmd) {
    case 0: r = 0; m_sd.multi = false; break;
    case 8: r = arg & 0xfff; break;                                   // echo the check pattern
    case 55: m_sd.app = true; r = 0x120; break;
    case 41: r = app ? 0x80ff8000u : 0x900; break;                    // OCR: powered up, standard capacity
    case 2: r = 0x12345678; break;
    case 3: m_sd.rca = 0x1234; r = (m_sd.rca << 16) | 0x0500; break;
    case 7: r = 0x700; break;
    case 12: m_sd.multi = false; r = 0xb00; break;
    case 16: m_sd.blockLen = arg; break;
    case 17: case 18:
        m_sd.multi = m_sd.cmd == 18;
        m_sd.nextBlock = arg / 512 + 1;
        if (!sdReadBlock(arg)) { m_sd.err = 1; }
        break;
    default: break;
    }
    for (int i = 0; i < 4; i++) m_sd.resp[i] = uint8_t(r >> (8 * i));
    m_sd.done = 1;
}

// 4340-4341: CPU destination, 4342-4343: blocks - 1 (?), 4344-4347: card address;
// writing 4347 copies the blocks into memory and sets 431c bit 0.
void Vt369Core::sdDma()
{
    uint16_t dst = m_sd.dma[0] | (m_sd.dma[1] << 8);
    uint32_t count = (m_sd.dma[2] | (m_sd.dma[3] << 8)) + 1;
    uint32_t addr = m_sd.dma[4] | (m_sd.dma[5] << 8) | (m_sd.dma[6] << 16) | ((uint32_t)m_sd.dma[7] << 24);
    for (uint32_t b = 0; b < count; b++) {
        if (!sdReadBlock((uint64_t)addr + b * 512ull)) { m_sd.err = 1; break; }
        for (int i = 0; i < 512; i++) cpuWrite(uint16_t(dst + b * 512 + i), m_sd.buf[i]);
    }
    m_sd.dmaDone = 1;
}

uint8_t Vt369Core::sdRead(uint16_t a)
{
    uint8_t v = 0;
    if (a >= 0x4318 && a < 0x431c) v = m_sd.resp[a - 0x4318];
    else if (a == 0x4326) v = m_sd.done;
    else if (a == 0x4327) v = m_sd.err ? 0x80 : 0x00;
    else if (a == 0x4328) { m_sd.err = 0; v = 0; }
    else if (a == 0x431c) v = m_sd.dmaDone;
    else v = a < 0x4330 ? m_sd.ctl[a - 0x4310] : m_sd.dma[a - 0x4340];
    return v;
}

void Vt369Core::sdWrite(uint16_t a, uint8_t d)
{
    if (a >= 0x4310 && a < 0x4314) m_sd.arg[a - 0x4310] = d;
    else if (a == 0x4314) m_sd.flags = d;
    else if (a == 0x4315) m_sd.cmd = d & 0x3f;
    else if (a >= 0x4340) { m_sd.dma[a - 0x4340] = d; if (a == 0x4347) sdDma(); }
    else { m_sd.ctl[a - 0x4310] = d; }
    if (a == 0x4313) { m_sd.done = 0; sdCommand(); }
}

void Vt369Core::reset()
{
    // power-on: RAM cleared (MAME default fill)
    std::memset(&m_s, 0, sizeof(m_s));
    m_spi = Spi(); m_i2c = I2c();
    std::memset(&m_sd, 0, sizeof(m_sd));
    m_s.initialE000 = 0xff;
    m_s.clockMul = 1;
    m_s.nmiAt = -1;
    m_s.spuTimerNext = -1;
    m_s.lcdcNmiAt = m_s.oamDmaAt = -1;
    m_s.gpioWriteHistory[0] = m_s.gpioWriteHistory[1] = 0xff;
    socReset();
    if (m_cfg.soc == 3) {
        std::memset(&m_sh, 0, sizeof(m_sh));
        for (int i = 0; i < 8; i++) m_sh.bank[i] = uint8_t(i);
        m_sh.irqMask = 0xff;
        m_sh.timerLines = -1;
        std::memset(m_sh.nvram, 0xff, sizeof(m_sh.nvram));   // NVRAM default fill (all 1s)
        m_sh.rng = 0x6578;
    }
    ppuReset();
    apuReset();
    setEncryption(m_cfg.crypt >= 0);
    m_cpu.reset();
    m_audio.clear();
}

/*************************************************************************
 SoC: nes_vt02_vt03_soc_device / vt3xx_soc_base_device
*************************************************************************/

void Vt369Core::socReset()
{
    std::memset(m_s.r410x, 0, sizeof(m_s.r410x));
    m_s.r410x[0x8] = 0x01;
    m_s.r410x[0x9] = 0x02;
    m_s.r411c = m_s.r411d = m_s.r4242 = 0;
    m_s.relative[0] = m_s.relative[1] = 0;
    m_s.newdma4024 = 0; m_s.vdmaCtrl = 0;
    m_s.timerIrqEnabled = m_s.timerRunning = m_s.timerVal = 0;
    updateBanks();
    m_s.bank6000 = 0; m_s.bank6000Enable = 0; m_s.r411f = 0;
    std::memset(m_s.alu, 0, sizeof(m_s.alu));
}

uint32_t Vt369Core::getBanks(uint8_t bnk) const
{
    const uint8_t* r = m_s.r410x;
    switch (r[0xb] & 0x07) {
    case 0: return ((r[0x0] & 0xF0) << 4) + ((r[0xa] & 0xC0) | (bnk & 0x3F));
    case 1: return ((r[0x0] & 0xF0) << 4) + ((r[0xa] & 0xE0) | (bnk & 0x1F));
    case 2: return ((r[0x0] & 0xF0) << 4) + ((r[0xa] & 0xF0) | (bnk & 0x0F));
    case 3: return ((r[0x0] & 0xF0) << 4) + ((r[0xa] & 0xF8) | (bnk & 0x07));
    case 4: return ((r[0x0] & 0xF0) << 4) + ((r[0xa] & 0xFC) | (bnk & 0x03));
    case 5: return ((r[0x0] & 0xF0) << 4) + ((r[0xa] & 0xFE) | (bnk & 0x01));
    case 6: return ((r[0x0] & 0xF0) << 4) + (r[0xa]);
    case 7: return ((r[0x0] & 0xF0) << 4) + bnk;
    }
    return 0;
}

void Vt369Core::updateBanks()
{
    uint8_t* r = m_s.r410x;
    uint8_t bank;
    if ((r[0xb] & 0x40) != 0 || (r[0x5] & 0x40) == 0) bank = (r[0x5] & 0x40) == 0 ? r[0x7] : r[0x9];
    else bank = 0xfe;
    m_s.bankaddr[0] = getBanks(bank);
    m_s.bankaddr[1] = getBanks(r[0x8]);
    if ((r[0xb] & 0x40) != 0 || (r[0x5] & 0x40) != 0) bank = (r[0x5] & 0x40) == 0 ? r[0x9] : r[0x7];
    else bank = 0xfe;
    m_s.bankaddr[2] = getBanks(bank);
    m_s.bankaddr[3] = getBanks(m_s.initialE000);
}

uint16_t Vt369Core::decodeNtAddr(uint16_t addr, bool singlePage)
{
    if (!(m_s.r410x[0x6] & 0x02) || !singlePage) {
        bool vert = !(m_s.r410x[0x6] & 0x01);
        int a11 = (addr >> 11) & 1, a10 = (addr >> 10) & 1;
        return ((vert ? a10 : a11) << 10) | (addr & 0x3ff);
    }
    uint16_t base = addr & 0x3ff;
    if (m_s.r410x[0x6] & 0x01) base |= 0x400;
    return base;
}

void Vt369Core::w410x(int offset, uint8_t data)
{
    uint8_t* r = m_s.r410x;
    switch (offset) {
    case 0x0: r[0] = data; updateBanks(); break;
    case 0x1: r[1] = data; m_s.timerRunning = 0; break;
    case 0x2: r[2] = data; m_s.timerVal = r[1]; m_s.timerRunning = 1; break;
    case 0x3: m_cpu.setIrq(false); r[3] = data; m_s.timerIrqEnabled = 0; break;
    case 0x4: r[4] = data; m_s.timerIrqEnabled = 1; break;
    case 0x5: r[5] = data; updateBanks(); break;
    case 0x6: r[6] = data; break;
    case 0x7: r[7] = data; updateBanks(); break;       // 'no scramble' configuration
    case 0x8: r[8] = data; updateBanks(); break;
    case 0x9: r[9] = data; updateBanks(); break;
    case 0xa: r[0xa] = data; updateBanks(); break;
    case 0xb: r[0xb] = data; updateBanks(); break;
    }
}

// MMC3 compatibility (no scrambling)
void Vt369Core::scrambled8000w(uint16_t offset, uint8_t data)
{
    const uint8_t* s8000 = m_cfg.scramble8000;
    offset &= 0x7fff;
    uint16_t addr = offset + 0x8000;
    if (addr < 0xa000 && !(addr & 1)) {
        m_s.addrLatch8000 = data & 7;
        m_s.r410x[5] = data & ~(1 << 5);
        updateBanks();
    } else if (addr < 0xa000) {
        int sel = m_s.r410x[5] & 7;
        if (sel < 6) m_s.videobank0[s8000[sel]] = data;
        else if (sel == 6) { m_s.r410x[7] = data; updateBanks(); }
        else { m_s.r410x[8] = data; updateBanks(); }
    } else if (addr < 0xc000 && !(addr & 1)) {
        m_s.r410x[6] = (m_s.r410x[6] & 0xfe) | (data & 1);
    } else if (addr < 0xc000) {
        // PRG RAM control
    } else if (addr < 0xe000 && !(addr & 1)) w410x(1, data);
    else if (addr < 0xe000) w410x(2, data);
    else if (!(addr & 1)) w410x(3, data);
    else w410x(4, data);
}

int Vt369Core::va17va10(int addr)
{
    int sel = (addr & 0x1c00) | ((m_s.r410x[0x5] & 0x80) ? 0x2000 : 0x000);
    int t = 0;
    switch ((sel >> 10) & 0xf) {
    case 0x0: case 0x1: case 0xc: case 0xd: t = (m_s.videobank0[4] & 0xfe) | ((addr & 0x400) ? 1 : 0); break;
    case 0x2: case 0x3: case 0xe: case 0xf: t = (m_s.videobank0[5] & 0xfe) | ((addr & 0x400) ? 1 : 0); break;
    case 0x4: case 0x8: t = m_s.videobank0[0]; break;
    case 0x5: case 0x9: t = m_s.videobank0[1]; break;
    case 0x6: case 0xa: t = m_s.videobank0[2]; break;
    case 0x7: case 0xb: t = m_s.videobank0[3]; break;
    }
    uint8_t ex = m_s.videobank0Extra;
    switch (ex & 7) {
    case 0x0: return t;
    case 0x1: return (t & 0x7f) | (ex & 0x80);
    case 0x2: return (t & 0x3f) | (ex & 0xc0);
    case 0x4: return (t & 0x1f) | (ex & 0xe0);
    case 0x5: return (t & 0x0f) | (ex & 0xf0);
    case 0x6: return (t & 0x07) | (ex & 0xf8);
    default: return -1;
    }
}

int Vt369Core::realVideoAddress(int addr, int readtype)
{
    int va34 = (addr & 0x6000) >> 13;
    addr &= 0x1fff;
    int v = va17va10(addr);
    int alt_order = m_s.extModes & 0x40;
    int extended = (readtype == 0) ? (m_s.extModes & 0x10) != 0 : (m_s.extModes & 0x08) != 0;
    int finaladdr;
    if (!extended) {
        int is4bpp = (readtype == 0) ? (m_s.extModes & 0x02) : (m_s.extModes & 0x04);
        int va20_va18 = (m_s.videobank1 & 0x70) >> 4;
        finaladdr = ((m_s.r410x[0] & 0x0F) << 21) | (va20_va18 << 18) | (v << 10) | (addr & 0x03ff);
        if (is4bpp) {
            if (!alt_order) finaladdr = ((finaladdr & ~0xf) << 1) | ((va34 & 1) << 4) | (finaladdr & 0xf);
            else finaladdr = (finaladdr << 1) | (va34 & 1);
        }
    } else {
        int eva = 0, is4bpp = 0;
        if (readtype == 0) {
            is4bpp = m_s.extModes & 0x02;
            eva |= m_s.readBg4Bg3;
            if (m_s.extModes2 & 0x02) { if (m_s.r410x[0x6] & 1) eva |= 4; }
            else if (m_s.videobank1 & 0x08) eva |= 4;
        } else if (readtype == 1) {
            is4bpp = m_s.extModes & 0x04;
            eva |= m_s.extraSpriteBits;
        }
        finaladdr = ((m_s.r410x[0] & 0x0f) << 21) | (v << 13) | (eva << 10) | (addr & 0x03ff);
        if (is4bpp) {
            if (!alt_order) finaladdr = ((finaladdr & ~0xf) << 1) | ((va34 & 1) << 4) | (finaladdr & 0xf);
            else finaladdr = (finaladdr << 1) | (va34 & 1);
        }
    }
    return finaladdr;
}

uint8_t Vt369Core::chrR(int offset)
{
    if (m_cfg.board == 4) return m_s.afbVram[afbChrAddr(offset)];
    return progRead(relative() + realVideoAddress(offset, 0));
}
uint8_t Vt369Core::sprR(int offset)
{
    if (m_cfg.board == 4) return m_s.afbVram[afbChrAddr(offset)];
    return progRead(relative() + realVideoAddress(offset, 1));
}

// ---- Atari Flashback mini (nes_clone_afbm7800_state) ----
int Vt369Core::afbPrgBank(int i) const
{
    const uint8_t* x = m_s.afbExtra;
    const uint8_t* b = m_s.afbBankregs;
    uint8_t mask, outer;
    if (x[0] & 0x40) { mask = 0x0f; outer = uint8_t((x[0] & 0x07) << 4); }
    else { mask = 0x1f; outer = uint8_t((x[0] & 0x06) << 4); }
    int sel[4];
    if ((x[3] & 3) == 0) {          // MMC3 style
        sel[1] = outer | (b[7] & mask);
        sel[3] = outer | (0x1f & mask);
        if (m_s.afbBanksel & 0x40) { sel[0] = outer | (0x1e & mask); sel[2] = outer | (b[6] & mask); }
        else { sel[0] = outer | (b[6] & mask); sel[2] = outer | (0x1e & mask); }
    } else {
        int base = b[6] & mask;
        sel[0] = outer | (base + 0);
        sel[1] = outer | (base + 1);
        if ((x[3] & 3) == 3) { sel[2] = outer | (base + 2); sel[3] = outer | (base + 3); }
        else { sel[2] = outer | (base + 0); sel[3] = outer | (base + 1); }
    }
    return sel[i] & int(m_rom.size() / 0x2000 - 1);
}

uint32_t Vt369Core::afbChrAddr(int offset) const
{
    offset &= 0x1fff;
    uint16_t sel[6];
    const uint8_t* b = m_s.afbBankregs;
    bool invert = false;
    if (m_s.afbExtra[3] & 0x10) {
        int o = (m_s.afbExtra[2] & 0x0f) << 3;
        static const uint8_t lo[6] = { 0, 2, 4, 5, 6, 7 };
        for (int i = 0; i < 6; i++) sel[i] = uint16_t(o | lo[i]);
    } else {
        int o = (m_s.afbExtra[0] & 0x30) << 1;
        for (int i = 0; i < 6; i++) sel[i] = uint16_t(o | (b[i] & 0x1f));
        invert = (m_s.afbBanksel & 0x80) != 0;
    }
    // 2 KB banks 0-1 and 1 KB banks 2-5 (MAME masks with the 31 1 KB pages of the 32 KB RAM)
    int a = invert ? (offset ^ 0x1000) : offset;
    if (a < 0x1000) {
        int bank = a >> 11;
        return uint32_t(((sel[bank] & 31) >> 1) * 0x800 + (a & 0x7ff));
    }
    int bank = 2 + ((a - 0x1000) >> 10);
    return uint32_t((sel[bank] & 31) * 0x400 + (a & 0x3ff));
}

void Vt369Core::afbWrite(uint16_t a, uint8_t d)
{
    if (a < 0x8000) {               // $6000-$7fff: outer bank registers (mirrored every 4)
        if (!(m_s.afbRamprot & 0x80)) return;
        int r = a & 3;
        if ((r == 0 || r == 3) && (m_s.afbExtra[3] & 0x80)) return;
        m_s.afbExtra[r] = d;
        return;
    }
    switch (a & 0xe001) {
    case 0x8000: m_s.afbBanksel = d; break;
    case 0x8001: m_s.afbBankregs[m_s.afbBanksel & 7] = d; break;
    case 0xa000: m_s.afbNtMirror = d; break;
    case 0xa001: m_s.afbRamprot = d; break;
    case 0xc000: m_s.afbIrqCounter = int16_t(d ^ 0xff); break;
    case 0xc001: m_s.afbIrqLatch = d; break;
    case 0xe000: m_cpu.setIrq(false); m_s.afbIrqEnable = 0; break;
    case 0xe001: m_s.afbIrqEnable = 1; break;
    }
}

void Vt369Core::chrW(int offset, uint8_t data)
{
    if (m_cfg.board == 4) { m_s.afbVram[afbChrAddr(offset)] = data; return; }
    if (m_s.r4242 & 1 || m_s.r411d & 4) m_s.chrram[offset & 0x1fff] = data;
    // otherwise a write to the ROM: ignored
}

void Vt369Core::videoIrq(bool hblank, int scanline, bool vblank, bool blanked)
{
    if (m_cfg.board == 4) {
        // MMC3-style counter on MAME's PPU scanline callback
        if (!hblank && m_s.afbIrqEnable && !vblank && !blanked) {
            if (--m_s.afbIrqCounter == -1) { m_s.afbIrqCounter = m_s.afbIrqLatch; m_cpu.setIrq(true); }
        }
        return;
    }
    if (((m_s.r410x[0xb] >> 7) & 1) == (hblank ? 1 : 0)) {
        if (m_s.timerRunning && scanline < 0xe0) {
            m_s.timerVal--;
            if (m_s.timerVal < 0 && m_s.timerIrqEnabled && !blanked) m_cpu.setIrq(true);
        }
    }
}

void Vt369Core::doDma(uint8_t data, bool ntscBug)
{
    uint8_t mode = m_s.vdmaCtrl & 1;
    uint8_t lenSel = (m_s.vdmaCtrl >> 1) & 7;
    uint8_t nib = (m_s.vdmaCtrl >> 4) & 0x0f;
    int length = 256;
    switch (lenSel) { case 4: length = 16; break; case 5: length = 32; break; case 6: length = 64; break; case 7: length = 128; break; }
    uint16_t src = (data << 8) | (nib << 4);
    if (ntscBug && mode == 1 && (m_s.videomemAddr & 0xff00) == 0x3f00 && !(m_s.extModes2 & 0x80)) { length -= 1; src += 1; }
    for (int i = 0; i < length; i++) {
        uint8_t v = cpuRead((uint16_t)(src + i));
        cpuWrite(mode ? 0x2007 : 0x2004, v);
    }
}

void Vt369Core::vtDmaW(uint8_t data)
{
    // MAME's DMA takes no CPU time: don't let its bus accesses advance the clock
    bool inInst = m_inInst;
    m_inInst = false;
    vtDmaWInner(data);
    m_inInst = inInst;
}

void Vt369Core::vtDmaWInner(uint8_t data)
{
    if (m_s.bank6000Enable & 0x80) {
        uint16_t src = m_s.newdma4024 | data << 8;
        int length = (m_s.vdmaCtrl >> 1) & 7;
        if (length == 0) length = 8;
        length = 1 << length;
        if (m_cfg.case36 && (m_s.vdmaCtrl & 1)) {
            uint16_t dest = m_s.dmaVramTarget;
            for (int i = 0; i < length; i++) {
                ppuWritebyte(dest & 0x3fff, cpuRead((uint16_t)(src + i)));
                dest = (dest + 1) & 0x3fff;
            }
            m_s.dmaVramTarget = dest;
        } else if (m_cfg.case36) {
            // OAM DMA is queued and copied 2 ms after the last request
            if (m_s.oamDmaCount < 4) {
                int slot = m_s.oamDmaCount++;
                m_s.oamDmaSrc[slot] = src;
                m_s.oamDmaDst[slot] = m_s.oamDmaNext & 0x1ff;
                m_s.oamDmaLen[slot] = length & 0xff;
                m_s.oamDmaNext = (m_s.oamDmaNext + length) & 0x1ff;
                m_s.oamDmaAt = m_s.dot + OAM_DMA_DELAY_DOTS;
            }
        } else {
            for (int i = 0; i < length; i++) {
                uint8_t v = cpuRead((uint16_t)(src + i));
                cpuWrite((m_s.vdmaCtrl & 1) ? 0x2007 : 0x2004, v);
            }
        }
    } else {
        doDma(data, m_cfg.ntscDmaBug);     // force_bad_dma() unless the VT02/03 NTSC bug is on
    }
}

void Vt369Core::flushOamDma()
{
    bool inInst = m_inInst;
    m_inInst = false;
    for (int r = 0; r < m_s.oamDmaCount; r++) {
        int len = m_s.oamDmaLen[r] ? m_s.oamDmaLen[r] : 0x100;
        for (int i = 0; i < len; i++)
            m_s.spriteram[(m_s.oamDmaDst[r] + i) & 0x1ff] = cpuRead((uint16_t)(m_s.oamDmaSrc[r] + i));
    }
    m_s.oamDmaCount = 0;
    m_s.oamDmaNext = 0;
    m_inInst = inInst;
}

void Vt369Core::gpioBusW()
{
    m_s.gpioWriteHistory[0] = m_s.gpioWriteHistory[1];
    m_s.gpioWriteHistory[1] = gpioBusLatch();
    if (m_s.gpioWriteHistory[0] == 0x00 && m_s.gpioWriteHistory[1] == 0x00) {
        // the menu waits for this two-byte answer before applying its LCD/GPIO table
        m_s.gpioResponse[0] = 0x93;
        m_s.gpioResponse[1] = 0x35;
        m_s.gpioResponsePos = 0;
        m_s.gpioResponseLen = 2;
    }
}

// vt_menu_protection_device (David Haywood, BSD-3-Clause)
void Vt369Core::protWriteEnable(int state)
{
    if ((bool)state != (bool)m_s.protInEnable) {
        if (!state) { m_s.protState = 1; m_s.protCommandBits = 8; }
        else m_s.protState = 0;
    }
    m_s.protInEnable = state;
}

void Vt369Core::protWriteClock(int state)
{
    if ((bool)state != (bool)m_s.protInClock && state) {
        if (m_s.protState == 1) {
            m_s.protCommand = (m_s.protCommand << 1) | (m_s.protInData ? 1 : 0);
            if (--m_s.protCommandBits == 0) {
                if (m_s.protCommand == 0x30 || m_s.protCommand == 0x20) {
                    m_s.protState = 2;
                    m_s.protReadPos = m_cfg.protStartByte * 8;
                } else if (m_cfg.case36 && m_s.protCommand == 0x10) {
                    m_s.protState = 3; m_s.protCommand = 0; m_s.protCommandBits = 8;
                } else m_s.protState = 0;
            }
        } else if (m_s.protState == 2) {
            uint8_t b = m_protRom[(m_s.protReadPos >> 3) & 0xff];
            m_s.protLatch = (b >> (~m_s.protReadPos & 7)) & 1;
            m_s.protReadPos++;
        } else if (m_s.protState == 3) {
            m_s.protCommand = (m_s.protCommand << 1) | (m_s.protInData ? 1 : 0);
            if (--m_s.protCommandBits == 0) {
                uint8_t r = 0;
                switch (m_s.protCommand) { case 0x02: r = 0x01; break; case 0x4a: r = 0x00; break; case 0x6d: r = 0x1e; break; case 0xb2: r = 0x02; break; }
                m_s.protLateResponse = r;
                m_s.protLateResponsePos = 0;
                m_s.protState = 4;
            }
        } else if (m_s.protState == 4) {
            m_s.protLatch = (m_s.protLateResponse >> (~m_s.protLateResponsePos & 7)) & 1;
            if (++m_s.protLateResponsePos == 8) m_s.protState = 0;
        }
    }
    m_s.protInClock = state;
}

void Vt369Core::w4034(uint8_t data)
{
    m_s.vdmaCtrl = data;
    if ((m_s.newvid1d & 0x01) == 0) m_s.newdma4024 = data & 0xf0;
}

void Vt369Core::aluW(int offset, uint8_t data)
{
    uint8_t* a = m_s.alu;
    a[offset] = data;
    if (offset == 5) {
        uint32_t p1 = a[0] | (a[1] << 8), p2 = a[4] | (a[5] << 8);
        uint32_t r = p1 * p2;
        a[0] = r; a[1] = r >> 8; a[2] = r >> 16; a[3] = r >> 24;
    } else if (offset == 7) {
        uint32_t p1 = a[0] | (a[1] << 8) | (a[2] << 16) | ((uint32_t)a[3] << 24);
        uint32_t p2 = a[6] | (a[7] << 8);
        if (p2) {
            uint32_t q = p1 / p2, m = p1 % p2;
            a[0] = q; a[1] = q >> 8; a[2] = q >> 16; a[3] = q >> 24;
            a[4] = m; a[5] = m >> 8;
        }
    }
}

/*************************************************************************
 CPU address map (vt369_soc_introm_noswap_device::vt369_introm_map)
*************************************************************************/

#include <cstdio>
uint8_t Vt369Core::cpuRead(uint16_t a)
{
    catchUp();
    if (m_cfg.soc == 3) return shCpuRead(a);
    if (m_cfg.soc != 0) {
        // nes_vt02_vt03_soc::nes_vt_map / vt4ffx_soc::vt369_map
        if (m_cfg.soc == 2) {
            if (a < 0x2000) return m_s.ram[a & 0x7ff];
            if (a < 0x2100) {
                int r = a & 0x1f;
                if (r < 8) { apuSync(); return ppuRead(r); }
                if (r < 0x10) return 0;
                if (r >= 0x1c) return 0;                       // light gun
                a = uint16_t(0x2000 | r);
            } else if (a < 0x4000) return 0;
        } else {
            if (a < 0x2000) return a < 0x800 ? m_s.ram[a] : 0;
            if (a >= 0x3000 && a < 0x4000) return 0;
            if (a == 0x4138) return m_s.dir414x;
            if (a == 0x4139) return m_s.data414a & m_s.dir414x;
        }
        if (m_cfg.board == 4 && a >= 0x6000) {
            if (a < 0x8000) return 0;       // write-only registers
            return progRead(uint32_t(afbPrgBank((a >> 13) & 3)) * 0x2000 + (a & 0x1fff));
        }
        if (a >= 0x6000 && a < 0x8000 && m_cfg.soc == 2) return m_s.ram6000[a & 0x1fff];
        if (a >= 0x4020 && a < 0x6000 && !(a >= 0x4100 && a <= 0x410f) && a != 0x4119) return 0;
    }
    if (a < 0x1000) return m_s.ram[a];
    if (a < 0x2000) return m_internal.empty() ? 0x00 : m_internal[a & 0xfff];   // internal ROM
    if (a < 0x2008) { apuSync(); return ppuRead(a & 7); }
    if (a >= 0x3000 && a < 0x4000) return m_s.ntram[decodeNtAddr(a - 0x3000, false)];
    if (a >= 0x8000) {
        int bank = (a & 0x6000) >> 13;
        return progRead(relative() + m_s.bankaddr[bank] * 0x2000 + (a & 0x1fff));
    }
    if (a >= 0x6000) {
        if (m_s.bank6000Enable & 0x40) return progRead(relative() + getBanks(m_s.bank6000) * 0x2000 + (a & 0x1fff));
        return m_s.ram6000[a & 0x1fff];
    }
    if (a >= 0x4800 && a < 0x5000) return m_s.soundram[a & 0x7ff];
    switch (a) {
    case 0x2008: return m_s.spritehigh2008;
    case 0x2010: return m_s.extModes;
    case 0x2011: return m_s.extModes2;
    case 0x2012: case 0x2013: case 0x2014: case 0x2015: case 0x2016: case 0x2017: return m_s.videobank0[m_cfg.order2012[a - 0x2012]];
    case 0x2018: return m_s.videobank1;
    case 0x2019: return 0;
    case 0x201a: return m_s.videobank0Extra;
    case 0x201b: return 0;
    case 0x201c: return m_s.newvid1c;
    case 0x201d: return m_s.newvid1d;
    case 0x201e: return m_s.newvid1e;
    case 0x201f: return 0;
    case 0x2020: case 0x2021: case 0x2022: case 0x2023: return m_s.tilebases[a & 3];
    case 0x2024: return m_s.lightgun2024;
    case 0x4015: apuSync(); return apuStatus();
    case 0x4016:
        if (m_cfg.board == 1) {
            // ABL Pinball: bit 0 is the plunger, which has to keep toggling (low for
            // as many reads as it's pulled back, then high for 5), bit 1 Select
            m_s.plungerCount++;
            if (m_s.plungerOff) {
                if (m_s.plungerCount == 5) { m_s.plungerOff = 0; m_s.plungerCount = 0; }
            } else if (m_s.plungerCount >= m_s.plungerHold || m_s.plungerCount >= 0x80) {
                m_s.plungerOff = 1; m_s.plungerCount = 0;
            }
            return uint8_t(m_s.plungerOff | ((m_inputs & 0x04) ? 0x02 : 0));
        } else { uint8_t r = 0x40 | (m_s.latch0 & 1); m_s.latch0 >>= 1; return r; }
    case 0x4017:
        if (m_cfg.board == 1) {
            // Start, left / right flippers; bit 3 echoes $4016 bit 2, bit 4 has to change
            uint8_t r = uint8_t(rand() & 0x10);
            r |= (m_s.pinIn0 & 0x04) ? 0x00 : 0x08;
            if (m_inputs & 0x08) r |= 0x01;
            if (m_inputs & (0x40 | 0x02)) r |= 0x02;
            if (m_inputs & (0x80 | 0x01)) r |= 0x04;
            return r;
        } else { uint8_t r = 0x40 | (m_s.latch1 & 1); m_s.latch1 >>= 1; return r; }
    case 0x4100: case 0x4101: case 0x4102: case 0x4103: case 0x4104: case 0x4105:
    case 0x4106: case 0x4107: case 0x4108: case 0x4109: case 0x410a: case 0x410b: return m_s.r410x[a - 0x4100];
    case 0x410e: return 0;
    case 0x410f:
        // ABL Ping Pong's speech chip: bit 4 acknowledges the strobe, bit 5 says its buffer is full
        if (m_cfg.board == 2) return uint8_t((m_s.ppStrobe ? 0x10 : 0) | (m_s.ppCount >= PP_FIFO_FULL ? 0x20 : 0));
        if (m_cfg.board == 1) return (m_inputs & 0x10) ? 0x80 : 0;                      // Pinball: nudge
        return 0;
    case 0x4119: return m_cfg.pal ? 0x18 : 0;      // PAL / 50 Hz flags
    case 0x411f: return m_s.r411f;
    case 0x4144: case 0x4145: case 0x4146: case 0x4147: return m_s.gpio414x[4 + (a - 0x4144)];
    case 0x4148: return m_s.dir414x;
    case 0x414a: return (0 & ~m_s.dir414x) | (m_s.data414a & m_s.dir414x);
    case 0x414b:
        if (m_cfg.i2cSecurity) {
            // the security chip pulls SDA (bit 6) low; the lines idle high
            uint8_t in = 0xff;
            if (!m_i2c.sdaOut) in &= ~0x40;
            return (in & ~m_s.dir414x) | (m_s.data414b & m_s.dir414x);
        }
        return (0 & ~m_s.dir414x) | (m_s.data414b & m_s.dir414x);
    case 0x414f:
        if (m_cfg.case36) return (m_s.gpio414x[0x0f] & 0xe0) | (gpioBusR() & 0x1f);
        return m_s.gpio414x[0x0f];
    case 0x414c: case 0x414d: case 0x414e: return m_s.gpio414x[0xc + (a - 0x414c)];
    case 0x4150: return m_s.dir415x;
    case 0x4152: return (0xff & ~m_s.dir415x) | (m_s.data4152 & m_s.dir415x);
    case 0x4153: {
        // gtct885_prot_r: the serial protection chip answers on bit 5
        uint8_t in = m_protRom.empty() ? 0xff : (m_s.protLatch ? 0x20 : 0x00);
        return (in & ~m_s.dir415x) | (m_s.data4153 & m_s.dir415x);
    }
    case 0x4155: case 0x4156: case 0x4157: return m_s.gpio415x[5 + (a - 0x4155)];
    case 0x415a: case 0x415b: return m_s.gpio415x[0xa + (a - 0x415a)];
    case 0x415c: return 0xf0;
    case 0x418a:
#ifdef VT_DEBUG
#endif
        return (uint8_t)rand();
    case 0x4199: return 0x02;
    case 0x41b7: return 0x04;
    case 0x41e4: case 0x41e5: return m_s.gpio41ex[4 + (a - 0x41e4)];
    case 0x41e6: return m_s.gpio41ex[6];
    case 0x41e7:
        if (m_cfg.case36) return (m_s.gpio41ex[7] & 0xe3) | ((gpioBusR() >> 3) & 0x1c);
        return m_s.gpio41ex[7];
    case 0x41e8: case 0x41e9: case 0x41ea: case 0x41eb: return m_s.gpio41ex[7 + (a - 0x41e7)];
    }
    if (sdPresent() && a >= 0x4310 && a < 0x4350) return sdRead(a);
    if (m_cfg.spiFlashWrites && a >= 0x4350 && a < 0x4360) return spiRead(a);
    if (a >= 0x4130 && a < 0x413e) {
        int off = (a < 0x4138) ? a - 0x4130 : a - 0x4138;
        if (off < 6) return m_s.alu[off];
        return 0;
    }
    return 0;
}

#ifdef VT_DEBUG
#include <cstdio>
int g_vtLogFrame = -1, g_vtFrame = 0;
#endif
void Vt369Core::cpuWrite(uint16_t a, uint8_t d)
{
    catchUp();
    if (m_cfg.soc == 3) { shCpuWrite(a, d); return; }
    if (sdPresent() && a >= 0x4310 && a < 0x4350) { sdWrite(a, d); return; }
    if (m_cfg.spiFlashWrites && a >= 0x4350 && a < 0x4360) { spiWrite(a, d); return; }
#ifdef VT_DEBUG
    if (g_vtFrame == g_vtLogFrame && ((a >= 0x2000 && a < 0x2030 && a != 0x2004 && a != 0x2007) || (a >= 0x4100 && a < 0x4120)))
        printf("W %04x %02x scan=%d dot=%d\n", a, d, m_s.scanline, (int)(m_s.dot % 341));
#endif
    if (m_cfg.soc != 0) {
        if (m_cfg.soc == 2) {
            if (a < 0x2000) { m_s.ram[a & 0x7ff] = d; return; }
            if (a < 0x2100) {
                int r = a & 0x1f;
                if (r < 8) { apuSync(); ppuWrite(r, d); return; }
                if (r < 0x10 || r >= 0x1b) return;
                a = uint16_t(0x2000 | r);
            } else if (a < 0x4000) return;
            if (m_cfg.board == 4 && a >= 0x6000) { afbWrite(a, d); return; }
            if (a >= 0x6000 && a < 0x8000) { m_s.ram6000[a & 0x1fff] = d; return; }
        } else {
            if (a < 0x2000) { if (a < 0x800) m_s.ram[a] = d; return; }
            if (a >= 0x3000 && a < 0x4000) return;
            if (a == 0x411c) {                              // gbox_411c_w: opcode scrambling only
                if (m_cfg.crypt >= 0) { if (d == 0x05) setEncryption(false); else if (d == 0xc5) setEncryption(true); }
                return;
            }
            if (a == 0x4138) { m_s.dir414x = d; return; }
            if (a == 0x4139) { m_s.data414a = d; return; }
        }
        if (a >= 0x4020 && a < 0x6000 && !(a >= 0x4100 && a <= 0x410f) && a != 0x4112 && a != 0x411d && a != 0x4024 && a != 0x4034) return;
        if (m_cfg.soc == 2 && (a == 0x4112 || a == 0x411d || a == 0x4024)) return;
        if (m_cfg.soc == 2 && a >= 0x2020 && a < 0x2100) return;
    }
    if (a < 0x1000) { m_s.ram[a] = d; return; }
    if (a < 0x2000) return;
    if (a < 0x2008) { apuSync(); ppuWrite(a & 7, d); return; }
    if (a >= 0x3000 && a < 0x4000) { m_s.ntram[decodeNtAddr(a - 0x3000, false)] = d; return; }
    if (a >= 0x8000) {
        if (m_s.r410x[0xb] & 0x08) return;           // write to flash: ignored
        scrambled8000w(a - 0x8000, d);
        return;
    }
    if (a >= 0x6000) { if (!(m_s.bank6000Enable & 0x40)) m_s.ram6000[a & 0x1fff] = d; return; }
    if (a >= 0x4800 && a < 0x5000) { m_s.soundram[a & 0x7ff] = d; return; }
    if (a >= 0x4000 && a <= 0x4013) { apuSync(); apuWrite(a - 0x4000, d); return; }
    switch (a) {
    case 0x2008: m_s.spritehigh2008 = d; return;
    case 0x2010: m_s.extModes = d; return;
    case 0x2011: m_s.extModes2 = d; return;
    case 0x2012: case 0x2013: case 0x2014: case 0x2015: case 0x2016: case 0x2017: m_s.videobank0[m_cfg.order2012[a - 0x2012]] = d; return;
    case 0x2018: m_s.videobank1 = d; return;
    case 0x201a: m_s.videobank0Extra = d; return;
    case 0x201c: m_s.newvid1c = d; return;
    case 0x201d: m_s.newvid1d = d; return;
    case 0x201e: m_s.newvid1e = m_cfg.i2cSecurity ? (d & 0x0f) : d; return;   // A6Plus: bit 4 alone keeps the NES palette
    case 0x201f: m_s.ext201f = d; return;
    case 0x2020: case 0x2021: case 0x2022: case 0x2023: m_s.tilebases[a & 3] = d; return;
    case 0x2024: m_s.lightgun2024 = d; return;
    case 0x2050:
        m_s.lcdc2050 = d;
        if (m_cfg.case36) {
            if (d & 1) {
                // frame_period() + time_until_pos(240, 120) on the 256-wide MAME screen
                const int64_t frameStart = m_s.lineStart - (int64_t)m_s.scanline * DOTS_PER_LINE;
                int64_t t = frameStart + 240 * DOTS_PER_LINE + 120 * DOTS_PER_LINE / 256;
                while (t <= m_s.dot) t += DOTS_PER_FRAME;
                m_s.lcdcNmiAt = t + DOTS_PER_FRAME;
            } else m_s.lcdcNmiAt = -1;
        }
        return;
    case 0x4014: vtDmaW(d); return;
    case 0x4015: apuSync(); apuWrite(0x15, d); return;
    case 0x410f:
        if (m_cfg.board == 2) { m_s.ppNibble = d & 0x0f; return; }
        break;
    case 0x4016:
        if (m_cfg.board == 1) m_s.pinIn0 = d;
        if (m_cfg.board == 2 && ((d >> 2) & 1) != m_s.ppStrobe) {
            // a nibble is taken on each edge of bit 2: the high one rising, the low one falling
            m_s.ppStrobe = (d >> 2) & 1;
            // a full buffer refuses the byte (the game sees bit 5 and backs off)
            if (m_s.ppStrobe) { m_s.ppHi = uint8_t(m_s.ppNibble << 4); m_s.ppRefuse = m_s.ppMode == 3 && m_s.ppCount >= PP_FIFO_FULL; }
            else if (!m_s.ppRefuse) ppByte(uint8_t(m_s.ppHi | m_s.ppNibble));
        }
        if ((d & 1) != (m_s.prevPort0 & 1) && (d & 1)) { m_s.latch0 = (uint8_t)m_inputs; m_s.latch1 = m_cfg.twoPads ? uint8_t(m_inputs >> 8) : 0; }
        m_s.prevPort0 = d;
        return;
    case 0x4017: apuSync(); apuWrite(0x17, d); return;
    case 0x4024: m_s.newdma4024 = d; return;
    case 0x4034: w4034(d); return;
    case 0x4112: m_s.bank6000 = d; return;
    case 0x411c: m_s.bank6000Enable = d; m_s.clockMul = (d & 0x80) ? 3 : 1; return;
    case 0x411d: m_s.r411d = d; updateBanks(); return;
    case 0x411f: m_s.r411f = d; return;
    case 0x4147:
        if (m_cfg.case36 && !(m_s.gpio414x[7] & 0x20) && (d & 0x20)) { m_s.gpio414x[7] = d; gpioBusW(); return; }
        m_s.gpio414x[7] = d; return;
    case 0x4144: case 0x4145: case 0x4146: m_s.gpio414x[4 + (a - 0x4144)] = d; return;
    case 0x4148: m_s.dir414x = d; if (m_cfg.i2cSecurity) i2cLines(); return;
    case 0x414a: m_s.data414a = d; return;
    case 0x414b: m_s.data414b = d; if (m_cfg.i2cSecurity) i2cLines(); return;
    case 0x414c: case 0x414d: case 0x414e: case 0x414f: m_s.gpio414x[0xc + (a - 0x414c)] = d; return;
    case 0x4150: m_s.dir415x = d; return;
    case 0x4152:
        m_s.data4152 = d;
        if (!m_protRom.empty()) {   // gtct885_prot_w
            m_s.protInData = (d >> 5) & 1;
            protWriteEnable((d >> 4) & 1);
            protWriteClock((d >> 3) & 1);
        }
        return;
    case 0x4153: m_s.data4153 = d; return;
    case 0x4155: case 0x4156: case 0x4157: m_s.gpio415x[5 + (a - 0x4155)] = d; return;
    case 0x415a: case 0x415b: m_s.gpio415x[0xa + (a - 0x415a)] = d; return;
    case 0x4160: case 0x4161: m_s.relative[a - 0x4160] = d; return;
    case 0x41e4: case 0x41e5: m_s.gpio41ex[4 + (a - 0x41e4)] = d; return;
    case 0x41e6:
        m_s.gpio41ex[6] = d;
        if (m_cfg.bank41e6) m_s.ahigh = (uint32_t)(d & 0x03) << 25;   // extbank_red5mam_w
        return;
    case 0x41eb:
        if (m_cfg.case36 && !(m_s.gpio41ex[0xb] & 0x10) && (d & 0x10) && m_s.gpioResponsePos < m_s.gpioResponseLen)
            m_s.gpioResponsePos++;
        m_s.gpio41ex[0xb] = d; return;
    case 0x41e7: case 0x41e8: case 0x41e9: case 0x41ea: m_s.gpio41ex[7 + (a - 0x41e7)] = d; return;
    case 0x4162: spuControl(d); return;
    case 0x4169:                                    // encryption_4169_w
        if (m_cfg.crypt >= 0) { if (d == 0x01) setEncryption(false); else if (d == 0x00) setEncryption(true); }
        return;
    case 0x4201: {
        bool inInst = m_inInst;
        m_inInst = false;
        for (int i = 0; i < 0x200; i++) m_s.spriteram[i] = cpuRead((uint16_t)((d << 8) + i));
        m_inInst = inInst;
        return;
    }
    }
    if (a >= 0x4100 && a <= 0x410b) { w410x(a - 0x4100, d); return; }
    if (a >= 0x2040 && a <= 0x2049) { lcdcRegsW(a - 0x2040, d); return; }
    if (a >= 0x4130 && a < 0x413e) { aluW((a < 0x4138) ? a - 0x4130 : a - 0x4138, d); return; }
}

/*************************************************************************
 Sound CPU (vt3xx_soc_base_device::vt369_sound_map)
*************************************************************************/

void Vt369Core::spuControl(uint8_t data)
{
    bool run = data == 0x0d;
    if (run && !m_s.spuRunning) {
        m_s.spuCycle = m_s.dot * SPU_CYCLES_PER_DOT;
        m_spu.databank = 0;
        m_spu.m_irqLine = false;
        m_spu.reset();
    }
    m_s.spuRunning = run;
}

void Vt369Core::spuTimerUpdate()
{
    if ((m_s.spuTimerCtrl & 0x01) && m_s.spuTimerPeriod)
        m_s.spuTimerNext = (double)m_s.spuCycle + MASTER_CLOCK * 8.0 / m_s.spuTimerPeriod;
    else
        m_s.spuTimerNext = -1;
}

uint8_t Vt369Core::spuRead(uint16_t a)
{
    if (a < 0x1800) return m_s.spuRam[a];
    if (a < 0x2000) return m_s.soundram[a & 0x7ff];
    switch (a) {
    case 0x2210: case 0x2211: return m_s.adderResult[a & 1];
    case 0x2402: case 0x2403: return m_s.adpcmResult[a & 1];
    case 0x2404: return 0x00;
    }
    if (a >= 0xfffa) {            // vt369_soundcpu_vectors_r
        int off = a - 0xfffa;
        if (off == 4 || off == 5) return m_s.soundram[0x7f8 + (off & 1)];
        return m_s.soundram[0x7fa + off];
    }
    if (a >= 0x4000 && a < 0x5000 && !m_internal.empty()) return m_internal[a & 0xfff];
    return 0;                     // 4000-4fff internal ROM
}

static const int16_t s_adpcmSteps[16][16] = {   // vt369_adpcm.h (NewRisingSun, GPL-2.0+)
    {0, 14, 28, 42, 56, 70, 84, 97, -111, -97, -84, -70, -56, -42, -28, -14 },
    {0, 13, 26, 39, 52, 65, 78, 91, -104, -91, -78, -65, -52, -39, -26, -13 },
    {0, 11, 21, 32, 43, 54, 64, 75,  -86, -75, -64, -54, -43, -32, -21, -11 },
    {0,  9, 18, 27, 35, 44, 53, 62,  -71, -62, -53, -44, -35, -27, -18,  -9 },
    {0,  7, 13, 20, 27, 34, 40, 47,  -54, -47, -40, -34, -27, -20, -13,  -7 },
    {0,  6, 11, 17, 22, 28, 33, 39,  -44, -39, -33, -28, -22, -17, -11,  -6 },
    {0,  5,  9, 14, 18, 23, 27, 32,  -36, -32, -27, -23, -18, -14,  -9,  -5 },
    {0,  4,  8, 11, 15, 19, 23, 26,  -30, -26, -23, -19, -15, -11,  -8,  -4 },
    {0,  3,  6,  9, 12, 15, 17, 20,  -23, -20, -17, -15, -12,  -9,  -6,  -3 },
    {0,  2,  5,  7, 10, 12, 14, 17,  -19, -17, -14, -12, -10,  -7,  -5,  -2 },
    {0,  2,  4,  6,  8, 10, 12, 14,  -16, -14, -12, -10,  -8,  -6,  -4,  -2 },
    {0,  2,  3,  5,  6,  8, 10, 11,  -13, -11, -10,  -8,  -6,  -5,  -3,  -2 },
    {0,  1,  2,  4,  5,  6,  7,  9,  -10,  -9,  -7,  -6,  -5,  -4,  -2,  -1 },
    {0,  1,  2,  3,  4,  5,  6,  7,   -8,  -7,  -6,  -5,  -4,  -3,  -2,  -1 },
    {0,  1,  2,  3,  3,  4,  5,  6,   -7,  -6,  -5,  -4,  -3,  -3,  -2,  -1 },
    {0,  1,  1,  2,  3,  4,  4,  5,   -6,  -5,  -4,  -4,  -3,  -2,  -1,  -1 } };

void Vt369Core::spuWrite(uint16_t a, uint8_t d)
{
    if (a < 0x1800) { m_s.spuRam[a] = d; return; }
    if (a < 0x2000) { m_s.soundram[a & 0x7ff] = d; return; }
    switch (a) {
    case 0x2100: m_s.spuTimerPeriod = (m_s.spuTimerPeriod & 0xff00) | d; return;
    case 0x2101: m_s.spuTimerPeriod = (m_s.spuTimerPeriod & 0x00ff) | (d << 8); return;
    case 0x2102: m_s.spuTimerCtrl = d; spuTimerUpdate(); return;
    case 0x2205: case 0x2206: {
        m_s.adderAddr[a - 0x2205] = d;
        if (a == 0x2206) {        // do_sound_adder
            uint16_t ad = (m_s.adderAddr[0] << 8) | m_s.adderAddr[1];
            int32_t r = 0;
            for (int i = 0; i < 4; i++)
                r += (int16_t)(spuRead((ad + i * 2) & 0x1fff) | (spuRead((ad + i * 2 + 1) & 0x1fff) << 8));
            m_s.adderResult[0] = r >> 8; m_s.adderResult[1] = r;
        }
        return; }
    case 0x2210: case 0x2211: m_s.adderResult[a & 1] = d; return;
    case 0x2400: case 0x2401: {
        m_s.adpcmAddr[a & 1] = d;
        if (a == 0x2401) {        // do_sound_adpcm_decode / vt369_adpcm_decoder_device::decode_packet
            uint16_t ad = ((m_s.adpcmAddr[0] << 8) | m_s.adpcmAddr[1]) & 0x1fff;
            uint8_t pk[6];
            for (int i = 0; i < 6; i++) pk[i] = m_s.soundram[(ad - 0x1800 + i) & 0x7ff];
            pk[5] %= 48;
            int nibble = pk[1] >> (pk[5] & 1 ? 4 : 0);
            int index = pk[0] - (pk[5] >= 24 && (pk[0] & 0x40) ? 1 : 0) + (pk[5] >= 24 && (pk[0] & 0x80) ? 2 : 0);
            int step = s_adpcmSteps[index & 0xf][nibble & 0xf];
            int8_t out = (int8_t)pk[4];
            switch ((pk[0] >> 4) & 3) {
            case 0: out = step; break;
            case 1: out = step + (int8_t)pk[4]; break;
            case 2: out = step + (int8_t)pk[4] * 2 - (int8_t)pk[3]; break;
            case 3: out = step + (int8_t)pk[4] - ((int8_t)pk[3] >> 1); break;
            }
            pk[3] = pk[4]; pk[4] = (uint8_t)out; pk[5]++;
            uint16_t fin = (uint16_t)(out * (pk[2] & 0x7f));
            for (int i = 0; i < 6; i++) m_s.soundram[(ad - 0x1800 + i) & 0x7ff] = pk[i];
            m_s.adpcmResult[0] = fin >> 8; m_s.adpcmResult[1] = fin;
        }
        return; }
    case 0x2800: case 0x2801: case 0x2802: case 0x2803: {
        int off = a & 3;
        apuSyncTo(m_s.spuCycle / SPU_CYCLES_PER_DOT);
        m_s.dac[off] = d;
#ifdef VT_DEBUG
#endif
        if (off & 1) {
            int ch = (off & 2) >> 1;
            m_s.dacOut[ch] = (int16_t)(m_s.dac[off] | (m_s.dac[off - 1] << 8));
        }
        return; }
    }
}

void Vt369Core::spuRun()
{
    if (!m_s.spuRunning) return;
    const int64_t target = m_s.dot * SPU_CYCLES_PER_DOT;
    while (m_s.spuCycle < target) {
        if (m_s.spuTimerNext >= 0 && m_s.spuCycle >= m_s.spuTimerNext) {   // sound_timer_expired
            if (m_s.spuTimerCtrl & 0x02) m_spu.setIrq(true);
            double next = m_s.spuTimerNext;
            spuTimerUpdate();
            if (m_s.spuTimerNext >= 0) m_s.spuTimerNext += next - (double)m_s.spuCycle;
        }
        m_s.spuCycle += m_spu.step();
    }
}

/*************************************************************************
 PPU (ppu2c0x + ppu_vt03 + ppu_vt3xx)
*************************************************************************/

void Vt369Core::initPalettes()
{
    // ppu2c0x_device::init_palette_tables (NTSC)
    const double tint = 0.22, hue = 287.0, Kr = 0.2989, Kb = 0.1145, Ku = 2.029, Kv = 1.140;
    static const double brightness[3][4] = { { 0.50, 0.75, 1.0, 1.0 }, { 0.29, 0.45, 0.73, 0.9 }, { 0, 0.24, 0.47, 0.77 } };
    static const double rgb_mod[8][3] = {
        { 1.0, 1.0, 1.0 }, { 1.24, 0.915, 0.743 }, { 0.794, 1.09, 0.882 }, { 0.905, 1.03, 1.28 },
        { 0.741, 0.987, 1.0 }, { 1.02, 0.908, 0.979 }, { 1.02, 0.98, 0.653 }, { 0.75, 0.75, 0.75 } };
    int entry = 0;
    for (int emp = 0; emp < 8; emp++)
        for (int inten = 0; inten < 4; inten++)
            for (int num = 0; num < 16; num++) {
                double sat, y, rad;
                switch (num) {
                case 0: sat = 0; rad = 0; y = brightness[0][inten]; break;
                case 13: sat = 0; rad = 0; y = brightness[2][inten]; break;
                case 14: case 15: sat = 0; rad = 0; y = 0; break;
                default: sat = tint; rad = (num * 30 + hue) * M_PI / 180.0; y = brightness[1][inten]; break;
                }
                double u = sat * cos(rad), v = sat * sin(rad);
                double R = (y + Kv * v) * 255.0;
                double G = (y - (Kb * Ku * u + Kr * Kv * v) / (1 - Kb - Kr)) * 255.0;
                double B = (y + Ku * u) * 255.0;
                R = std::clamp(R * rgb_mod[emp][0], 0.0, 255.0);
                G = std::clamp(G * rgb_mod[emp][1], 0.0, 255.0);
                B = std::clamp(B * rgb_mod[emp][2], 0.0, 255.0);
                m_pens[entry++] = rgb((int)floor(R + .5), (int)floor(G + .5), (int)floor(B + .5));
            }
    // ppu_vt03_device::init_vt03_palette_tables(0)
    entry = 0;
    for (int emp = 0; emp < 8; emp++)
        for (int palval = 0; palval < 0x1000; palval++) {
            int nPhase = palval & 0xF, nLuma = (palval >> 4) & 0xF, nChroma = (palval >> 8) & 0xF;
            float phaseOffset = -11.0;
            if (nLuma < (nChroma + 1) >> 1 || nLuma > 15 - (nChroma >> 1)) {
                static const unsigned char altPhases[16] = { 13, 7, 8, 9, 10, 11, 12, 1, 2, 3, 4, 5, 6, 0, 14, 15 };
                static const float altPhaseOffset[16] = { -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, -5, 0, -5, -5, -5 };
                phaseOffset += altPhaseOffset[nPhase];
                nPhase = altPhases[nPhase];
                nChroma = 16 - nChroma;
                nLuma = (nLuma - 8) & 0xF;
            }
            float fLuma = (nLuma - 4) / 9.625f;
            float fChroma = nChroma / 18.975f;
            const float fPhase = ((nPhase - 2) * 30.0f + phaseOffset) * (float)M_PI / 180.0f;
            float Y = fLuma, C = fChroma;
            if (nPhase == 0 || nPhase > 12) C = 0.0;
            if (nPhase == 0) Y += fChroma;
            if (nPhase == 13) Y -= fChroma;
            if (nPhase >= 14) Y = 0.0;
            const float V = sinf(fPhase) * C * 1.05f, U = cosf(fPhase) * C * 1.05f;
            float R = Y + 1.1400f * V, G = Y - 0.5807f * V - 0.3940f * U, B = Y + 2.0290f * U;
            R = std::clamp(R, 0.0f, 1.0f); G = std::clamp(G, 0.0f, 1.0f); B = std::clamp(B, 0.0f, 1.0f);
            m_pens[YUV444_COLOR + entry++] = rgb((int)(R * 255.0), (int)(G * 255.0), (int)(B * 255.0));
        }
    entry = 0;
    for (int emp = 0; emp < 8; emp++)
        for (int palval = 0; palval < 0x8000; palval++)
            m_rgb555[entry++] = rgb((palval & 0x7C00) >> 7, (palval & 0x3e0) >> 2, (palval & 0x001f) << 3);
    entry = 0;
    for (int emp = 0; emp < 8; emp++)
        for (int palval = 0; palval < 0x1000; palval++)
            m_rgb444[entry++] = rgb((palval & 0x000f) << 4, palval & 0x0f0, (palval & 0xf00) >> 4);
}

void Vt369Core::ppuReset()
{
    // most fields were zeroed by reset(); these are the non-zero ones
    m_s.add = 1;
    m_s.scanline = 0;
    m_s.visMinX = 0; m_s.visMaxX = 255; m_s.visMinY = 0; m_s.visMaxY = 239;
}

uint32_t Vt369Core::apply_grayscale_and_emphasis(uint8_t color)
{
    uint32_t palval = color;
    palval &= (m_s.regs[PPU_CONTROL1] & PPU_CONTROL1_DISPLAY_MONO) ? 0x30 : 0x3f;
    palval |= (m_s.regs[PPU_CONTROL1] & PPU_CONTROL1_COLOR_EMPHASIS) << 1;
    return palval;
}

// --- PPU address space ---
uint8_t Vt369Core::paletteRead(int offset)
{
    // ppu_vt03_device::palette_read
    if (offset < 0x20) {
        if (m_s.regs[PPU_CONTROL1] & PPU_CONTROL1_DISPLAY_MONO) return m_s.paletteRam[offset & 0x1f] & 0x30;
        return m_s.paletteRam[offset & 0x1f];
    }
    return m_s.paletteRam[offset & 0xff];
}

void Vt369Core::paletteWrite(int offset, uint8_t data)
{
    if (offset < 0x20) {
        data &= 0x3f;
        if (offset & 0x3) m_s.paletteRam[offset & 0x1f] = data;
        else {
            if (0 == (offset & 0xf)) m_s.backColor = data;
            m_s.paletteRam[offset & 0xf] = m_s.paletteRam[(offset & 0xf) + 0x10] = data;
        }
    } else m_s.paletteRam[offset & 0xff] = data;
}

uint8_t Vt369Core::vt3xxPaletteR(int offset)
{
    if (m_s.newvid1e) return m_s.vt3xxPalette[offset & 0x3ff];
    if (offset < 0x300) return ntR(offset + 0x3c00);
    return paletteRead(offset - 0x300);
}

void Vt369Core::vt3xxPaletteW(int offset, uint8_t data)
{
    if (m_s.newvid1e) { m_s.vt3xxPalette[offset & 0x3ff] = data; return; }
    if (offset < 0x300) ntW(offset + 0x3c00, data);
    else paletteWrite(offset - 0x300, data);
}

uint8_t Vt369Core::ppuReadbyte(uint32_t address)
{
    if (m_cfg.soc == 3) return shPpuRead(address);
    address &= 0x1ffff;
    if (address < 0x2000) return chrR(address);
    if (address < 0x3c00) return ntR(address - 0x2000);
    if (address < 0x4000) return vt3xxPaletteR(address - 0x3c00);
    return 0;
}

void Vt369Core::ppuWritebyte(uint32_t address, uint8_t data)
{
    if (m_cfg.soc == 3) { shPpuWrite(address, data); return; }
    address &= 0x1ffff;
    if (address < 0x2000) chrW(address, data);
    else if (address < 0x3c00) ntW(address - 0x2000, data);
    else if (address < 0x4000) vt3xxPaletteW(address - 0x3c00, data);
}

// --- registers ---
uint8_t Vt369Core::ppuRead(int offset)
{
    switch (offset & 7) {
    case PPU_STATUS:
        m_s.dataLatch = m_s.regs[PPU_STATUS] | (m_s.dataLatch & 0x1f);
        m_s.toggle = 0;
        if (m_s.dataLatch & PPU_STATUS_VBLANK) m_s.regs[PPU_STATUS] &= 0x60;
#ifdef XAVIX_DEBUG
        if (getenv("VTST")) { static int n = 0, v = 0; n++; if (m_s.dataLatch & 0x80) v++; if (n % 200000 == 0) fprintf(stderr, "2002 reads %d vbl %d line %d\n", n, v, m_s.scanline); }
#endif
        break;
    case PPU_SPRITE_DATA:
        m_s.dataLatch = m_s.spriteram[m_s.regs[PPU_SPRITE_ADDRESS]];
        break;
    case PPU_DATA:
        if (m_s.videomemAddr >= 0x3f00 && m_cfg.soc != 3) {
            m_s.dataLatch = ppuReadbyte(m_s.videomemAddr);
            m_s.bufferedData = ppuReadbyte(m_s.videomemAddr & 0x2fff);
        } else {
            m_s.dataLatch = m_s.bufferedData;
            m_s.bufferedData = ppuReadbyte(m_s.videomemAddr);
        }
        m_s.videomemAddr += m_s.add;
        break;
    }
    return m_s.dataLatch;
}

void Vt369Core::writeSpriteramWithIncrement(uint8_t data)
{
    if (!m_s.newvid1e) {
        if (m_s.scanline > BOTTOM_VISIBLE || !(m_s.regs[PPU_CONTROL1] & (PPU_CONTROL1_BACKGROUND | PPU_CONTROL1_SPRITES))) {
            m_s.spriteram[m_s.regs[PPU_SPRITE_ADDRESS]] = data;
            m_s.regs[PPU_SPRITE_ADDRESS] = (m_s.regs[PPU_SPRITE_ADDRESS] + 1) & 0xff;
        }
    } else {
        m_s.spriteram[m_s.regs[PPU_SPRITE_ADDRESS] | (m_s.spritehigh2008 & 1) << 8] = data;
        m_s.regs[PPU_SPRITE_ADDRESS] = (m_s.regs[PPU_SPRITE_ADDRESS] + 1) & 0xff;
        if (m_s.regs[PPU_SPRITE_ADDRESS] == 0) m_s.spritehigh2008 ^= 1;
    }
}

void Vt369Core::ppuWriteBase(int offset, uint8_t data)
{
    switch (offset & 7) {
    case PPU_CONTROL0:
        m_s.regs[PPU_CONTROL0] = data;
        m_s.refreshLatch &= 0x73ff;
        m_s.refreshLatch |= (data & 3) << 10;
        m_s.tilePage = (data & PPU_CONTROL0_CHR_SELECT) >> 2;
        m_s.spritePage = (data & PPU_CONTROL0_SPR_SELECT) >> 1;
        m_s.add = (data & PPU_CONTROL0_INC) ? 32 : 1;
        break;
    case PPU_CONTROL1: m_s.regs[PPU_CONTROL1] = data; break;
    case PPU_SPRITE_ADDRESS: m_s.regs[PPU_SPRITE_ADDRESS] = data; break;
    case PPU_SPRITE_DATA: writeSpriteramWithIncrement(data); break;
    case PPU_SCROLL:
        if (m_s.toggle) {
            m_s.refreshLatch &= 0x0c1f;
            m_s.refreshLatch |= (data & 0xf8) << 2;
            m_s.refreshLatch |= (data & 0x07) << 12;
        } else {
            m_s.refreshLatch &= (0xffe0 & (m_cfg.soc == 3 ? 0xffff : 0x7fff));
            m_s.refreshLatch |= (data & 0xf8) >> 3;
            m_s.xFine = data & 7;
        }
        m_s.toggle = !m_s.toggle;
        break;
    case PPU_ADDRESS:
        if (m_s.toggle) {
            m_s.refreshLatch &= (0xff00 & (m_cfg.soc == 3 ? 0xffff : 0x7fff));
            m_s.refreshLatch |= data;
            m_s.refreshData = m_s.refreshLatch;
            m_s.videomemAddr = m_s.refreshLatch;
        } else {
            m_s.refreshLatch &= 0x00ff;
            m_s.refreshLatch |= (data & ((m_cfg.soc == 3 ? 0xffff : 0x3fff) >> 8)) << 8;
        }
        m_s.toggle = !m_s.toggle;
        break;
    case PPU_DATA: {
        const int tempAddr = m_s.videomemAddr & (m_cfg.soc == 3 ? 0xffff : 0x3fff);
        ppuWritebyte(tempAddr, data);
        m_s.videomemAddr += m_s.add;
        break;
    }
    }
    m_s.dataLatch = data;
}

void Vt369Core::ppuWrite(int offset, uint8_t data)
{
    // ppu_vt3xx_device::write
    const bool mode12090f = (m_s.newvid1c == 0x12) && (m_s.newvid1d == 0x09) && (m_s.newvid1e == 0x0f);
    if (((offset & 7) == PPU_ADDRESS) && mode12090f) {
        if (m_s.toggle) m_s.dmaVramTarget = (m_s.dmaVramTarget & 0xff00) | data;
        else m_s.dmaVramTarget = (m_s.dmaVramTarget & 0x00ff) | ((data & 0x3f) << 8);
    }
    const bool wasRendering = (m_s.regs[PPU_CONTROL1] & (PPU_CONTROL1_BACKGROUND | PPU_CONTROL1_SPRITES)) != 0;
    ppuWriteBase(offset, data);
    if (((offset & 7) == PPU_CONTROL1) && mode12090f) {
        const bool isRendering = (m_s.regs[PPU_CONTROL1] & (PPU_CONTROL1_BACKGROUND | PPU_CONTROL1_SPRITES)) != 0;
        if (!wasRendering && isRendering) m_s.refreshData = m_s.refreshLatch;
    }
}

void Vt369Core::lcdcRegsW(int offset, uint8_t data)
{
    m_s.screenregs204x[offset] = data;
    struct vid_mode { int min_x, max_x, min_y, max_y; uint8_t regvals[0xa]; };
    static const vid_mode modes[] = {
        { 0, 159, 0, 127, { 0xa0, 0xff, 0x00, 0x40, 0xff, 0x04, 0x00, 0xa8, 0x04, 0x0f } },
        { 0, 199, 0, 199, { 0xdc, 0xff, 0x00, 0x58, 0xff, 0x04, 0x10, 0xa8, 0x04, 0x00 } },
        { 0, 127, 0, 159, { 0x80, 0x80, 0x00, 0x50, 0xff, 0x04, 0x00, 0xaa, 0x08, 0x00 } },
        { 0, 127, 0, 159, { 0x80, 0xff, 0x00, 0x50, 0xff, 0x04, 0x00, 0xa6, 0x04, 0x00 } },
        { 0, 127, 0, 159, { 0x80, 0x3f, 0x00, 0x50, 0xff, 0x69, 0x00, 0x54, 0x08, 0x00 } },
        { 0, 127, 0, 159, { 0x80, 0xfe, 0x00, 0x50, 0xff, 0x04, 0x00, 0xa8, 0x04, 0x00 } },
        { 0, 255, 0, 239, { 0x40, 0xa1, 0x00, 0x78, 0xff, 0x04, 0x0a, 0xd8, 0x0a, 0x00 } },
        { 0, 255, 0, 239, { 0xa0, 0x57, 0x09, 0x40, 0x93, 0x04, 0x00, 0x83, 0x08, 0x00 } },
        { 0, 255, 0, 239, { 0x40, 0xa1, 0x00, 0x78, 0xff, 0x69, 0x0a, 0x69, 0x26, 0x00 } },
        { 0, 255, 0, 239, { 0x40, 0xa1, 0x00, 0x78, 0xff, 0x04, 0x0a, 0xd4, 0x0a, 0x00 } },
        { 0, 255, 0, 239, { 0xdc, 0xe1, 0x00, 0x58, 0xbf, 0x04, 0x10, 0x93, 0x04, 0x00 } },
    };
    if (offset == 9) {
        int nx0 = 0, nx1 = 255, ny0 = 0, ny1 = 239;
        for (auto& m : modes)
            if (!memcmp(m.regvals, m_s.screenregs204x, 0xa)) { nx0 = m.min_x; nx1 = m.max_x; ny0 = m.min_y; ny1 = m.max_y; }
        m_s.visMinX = nx0; m_s.visMaxX = nx1; m_s.visMinY = ny0; m_s.visMaxY = ny1;
    }
}

// --- scanline events ---
void Vt369Core::hblankTick()
{
    const bool blanked = (m_s.regs[PPU_CONTROL1] & (PPU_CONTROL1_BACKGROUND | PPU_CONTROL1_SPRITES)) == 0;
    const bool vblank = (m_s.scanline >= m_vblankFirst - 1) && (m_s.scanline < m_lines - 1);
    if (!blanked && ((m_s.scanline <= BOTTOM_VISIBLE) || (m_s.scanline == m_lines - 1)))
        m_s.regs[PPU_SPRITE_ADDRESS] = 0;
    m_s.renderAtHblank = !blanked;
    videoIrq(true, m_s.scanline, vblank, blanked);
}

void Vt369Core::scanlineTick()
{
    if (m_cfg.soc == 3 && m_sh.timerLines >= 0) {
        if (--m_sh.timerLines < 0) {
            if (!(m_sh.irqMask & 0x80)) m_cpu.setIrq(true);
        }
    }
    const bool blanked = (m_s.regs[PPU_CONTROL1] & (PPU_CONTROL1_BACKGROUND | PPU_CONTROL1_SPRITES)) == 0;
    const bool vblank = (m_s.scanline >= m_vblankFirst - 1) && (m_s.scanline < m_lines - 1);
    videoIrq(false, m_s.scanline, vblank, blanked);
    updateScanline();
    m_s.scanline++;
    if (m_s.scanline == m_vblankFirst) {
        m_s.regs[PPU_STATUS] |= PPU_STATUS_VBLANK;
        if (m_s.regs[PPU_CONTROL0] & PPU_CONTROL0_NMI) {
            // fire the NMI a few CPU cycles into vblank (B-Wings), as MAME
            m_s.nmiAt = m_s.lineStart + 4 * (3 / m_s.clockMul);
        }
    }
    if (m_s.scanline == m_lines - 1) {
        m_s.regs[PPU_STATUS] &= ~(PPU_STATUS_VBLANK | PPU_STATUS_SPRITE0_HIT | PPU_STATUS_8SPRITES);
    } else if (m_s.scanline == m_lines) {
        if (!blanked) m_s.refreshData = m_s.refreshLatch;
        m_s.scanline = 0;
    }
}

void Vt369Core::updateScanline()
{
    if (m_s.scanline > BOTTOM_VISIBLE) return;
    if (m_s.renderAtHblank) {
        // update_visible_enabled_scanline
        m_s.refreshData &= ~0x041f;
        m_s.refreshData |= m_s.refreshLatch & 0x041f;
        renderScanline();
        scanlineIncrementFineY();
    } else {
        uint32_t backPen = m_s.backColor;
        if (m_s.videomemAddr >= 0x3f00 && m_cfg.soc != 3) backPen = m_s.paletteRam[m_s.videomemAddr & 0x1f];
        for (int i = 0; i < W; i++) drawBackPen(pix(m_s.scanline, i), backPen);
    }
}

void Vt369Core::scanlineIncrementFineY()
{
    m_s.refreshData += 0x1000;
    if (m_s.refreshData & 0x8000) {
        const uint16_t tmp = (m_s.refreshData & 0x03e0) + 0x20;
        m_s.refreshData &= 0x7c1f;
        if (tmp == (m_cfg.soc == 3 ? 0x0400 : 0x03c0)) m_s.refreshData ^= 0x0800;
        else m_s.refreshData |= tmp & 0x03e0;
    }
}

void Vt369Core::renderScanline()
{
    uint8_t lp[W + 32];
    memset(lp, 0, sizeof(lp));
    if (m_s.regs[PPU_CONTROL1] & PPU_CONTROL1_BACKGROUND) drawBackground(lp);
    else drawBackgroundPen();
    drawSprites(lp);
}

void Vt369Core::drawBackgroundPen()
{
    for (int i = 0; i < W; i++) drawBackPen(pix(m_s.scanline, i), m_s.backColor);
}

void Vt369Core::drawBackPen(uint32_t* dst, int backPen)
{
    if (m_s.extModes & 0x80) drawTilePixelInner(0, dst);
    else *dst = m_pens[backPen & 0x1ff];
}

// ppu_vt03 / ppu_vt3xx read_tile_plane_data
void Vt369Core::readTilePlaneDataVt03(int address, int color)
{
    const bool is4bpp = BIT(m_s.extModes, 1);
    m_s.whichpixel = 0;
    m_s.readBg4Bg3 = (m_s.extModes & 0x10) ? color : 0;
    if (is4bpp) {
        m_s.planebuf[0] = chrR((address + 0) & 0x1fff);
        m_s.planebuf[1] = chrR((address + 8) & 0x1fff);
        m_s.planebuf[2] = chrR(((address + 0) & 0x1fff) | 0x2000);
        m_s.planebuf[3] = chrR(((address + 8) & 0x1fff) | 0x2000);
    } else {
        m_s.planebuf[0] = chrR(address & 0x1fff);
        m_s.planebuf[1] = chrR((address + 8) & 0x1fff);
    }
}

uint32_t Vt369Core::recalc8x8x4(int address, int va34)
{
    int finaladdr = (m_s.tilebases[0] | (m_s.tilebases[1] << 8)) * 0x2000;
    int tileline = address & 7, tileplane = address & 8, tilenum = address & 0x0ff0;
    int finaloffset = (tilenum << 1) | (tileline << 2) | (tileplane >> 2) | va34;
    finaloffset += m_s.readBg4Bg3 * 0x2000;
    return finaladdr + finaloffset;
}

uint32_t Vt369Core::recalc8x8x8(int address, int va34)
{
    int finaladdr = (m_s.tilebases[0] | (m_s.tilebases[1] << 8)) * 0x2000;
    int tileline = address & 7, tileplane = address & 8;
    const bool m = (m_s.newvid1c == 0x12) && (m_s.newvid1d == 0x09) && (m_s.newvid1e == 0x0f);
    int tilenum = address & (m ? 0xfff0 : 0x0ff0);
    int finaloffset = (tilenum << 2) | (tileline << 3) | (tileplane >> 1) | va34;
    finaloffset += m_s.readBg4Bg3 * 0x4000;
    return finaladdr + finaloffset;
}

uint32_t Vt369Core::recalc16x16x8(int address, int va34)
{
    int finaladdr = (m_s.tilebases[0] | (m_s.tilebases[1] << 8)) * 0x2000;
    int tileline = address & 7, tileplane = address & 8, tilenum = (address & 0x0ff0) >> 4;
    int finaloffset = tilenum * 0x100 + tileline * 0x20 + va34 + tileplane;
    finaloffset += m_s.readBg4Bg3 * 0x10000;
    return finaladdr + finaloffset;
}

void Vt369Core::readTilePlaneData(int address, int color)
{
    if (!m_s.newvid1e) { readTilePlaneDataVt03(address, color); return; }
    m_s.readBg4Bg3 = color;
    m_s.whichpixel = 0;
    if (m_s.newvid1c & 0x04) {
        for (int i = 0; i < 8; i++) {
            m_s.planebuf[i * 2] = onespaceRel(recalc16x16x8((address + 0) & 0x1fff, i));
            m_s.planebuf[i * 2 + 1] = onespaceRel(recalc16x16x8((address + 8) & 0x1fff, i));
        }
    } else if ((m_s.newvid1c & 0x03) == 0x02) {
        const bool wide = (m_s.newvid1c == 0x12) && (m_s.newvid1d == 0x09) && (m_s.newvid1e == 0x0f);
        const int a0 = wide ? (address + 0) : ((address + 0) & 0x1fff);
        const int a8 = wide ? (address + 8) : ((address + 8) & 0x1fff);
        for (int i = 0; i < 4; i++) {
            m_s.planebuf[i * 2] = onespaceRel(recalc8x8x8(a0, i));
            m_s.planebuf[i * 2 + 1] = onespaceRel(recalc8x8x8(a8, i));
        }
    } else {
        m_s.planebuf[0] = onespaceRel(recalc8x8x4((address + 0) & 0x1fff, 0));
        m_s.planebuf[1] = onespaceRel(recalc8x8x4((address + 8) & 0x1fff, 0));
        m_s.planebuf[2] = onespaceRel(recalc8x8x4((address + 0) & 0x1fff, 1));
        m_s.planebuf[3] = onespaceRel(recalc8x8x4((address + 8) & 0x1fff, 1));
    }
}

void Vt369Core::shiftTilePlaneDataVt03(uint8_t& p)
{
    const bool is4bpp = BIT(m_s.extModes, 1);
    int b = ~m_s.whichpixel & 7;
    p = BIT(m_s.planebuf[0], b) | (BIT(m_s.planebuf[1], b) << 1);
    if (is4bpp) p |= (BIT(m_s.planebuf[2], b) << 5) | (BIT(m_s.planebuf[3], b) << 6);
    m_s.whichpixel++;
}

void Vt369Core::shiftTilePlaneData(uint8_t& p)
{
    if (!m_s.newvid1e) { shiftTilePlaneDataVt03(p); return; }
    int w = m_s.whichpixel;
    if (m_s.newvid1c & 0x04) p = m_s.planebuf[((w & 3) << 2) | BIT(w, 2)];
    else if ((m_s.newvid1c & 0x03) == 0x02) p = m_s.planebuf[(BIT(w, 1) << 2) | (BIT(w, 0) << 1) | BIT(w, 2)];
    else p = (m_s.planebuf[(BIT(w, 1) << 1) | BIT(w, 2)] >> (BIT(w, 0) << 2)) & 0x0f;
    m_s.whichpixel++;
}

void Vt369Core::drawTilePixelInner(uint8_t pen, uint32_t* dest)
{
    if (m_s.newvid1e) {
        uint16_t pal0 = ppuReadbyte(((pen & 0xff) * 2) + 0x3c00);
        pal0 |= ppuReadbyte(((pen & 0xff) * 2) + 0x3c01) << 8;
        *dest = rgb(((pal0 >> 10) & 0x1f) << 3, ((pal0 >> 5) & 0x1f) << 3, (pal0 & 0x1f) << 3);
    } else if (BIT(m_s.extModes, 7) && m_cfg.palMode == 0) {
        // VT03 mode: 12 bits of YUV over the two palette halves
        uint32_t palval = (m_s.paletteRam[pen & 0x7f] & 0x3f) | ((m_s.paletteRam[(pen & 0x7f) + 0x80] & 0x3f) << 6);
        palval |= ((m_s.regs[PPU_CONTROL1] & PPU_CONTROL1_COLOR_EMPHASIS) << 7);
        *dest = m_pens[YUV444_COLOR + (palval & 0x7fff)];
    } else if (BIT(m_s.extModes, 7)) {
        // PAL_MODE_NEW_RGB
        uint32_t palval = (m_s.paletteRam[pen & 0x7f] & 0xff) | ((m_s.paletteRam[(pen & 0x7f) + 0x80] & 0x7f) << 8);
        palval |= ((m_s.regs[PPU_CONTROL1] & PPU_CONTROL1_COLOR_EMPHASIS) << 10);
        *dest = m_rgb555[palval & 0x3ffff];
    } else {
        uint16_t palval = m_s.paletteRam[pen & 0x7f] & 0x3f;
        if (m_s.regs[PPU_CONTROL1] & PPU_CONTROL1_DISPLAY_MONO) palval &= 0x30;
        palval |= ((m_s.regs[PPU_CONTROL1] & PPU_CONTROL1_COLOR_EMPHASIS) << 1);
        *dest = m_pens[palval & 0x1ff];
    }
}

void Vt369Core::drawTilePixel(uint8_t p, int color, uint32_t backPen, uint32_t* dest)
{
    if (m_s.newvid1e) { drawTilePixelInner(p, dest); return; }
    if (!BIT(m_s.extModes, 1)) {
        const uint16_t palval = p ? m_s.paletteRam[((4 * color) + p) & 0x1f] : backPen;
        *dest = m_pens[apply_grayscale_and_emphasis(palval)];
    } else {
        int basepen = (m_s.extModes & 0x10) ? 0 : 4 * color;
        drawTilePixelInner(p ? p + basepen : 0, dest);
    }
}

void Vt369Core::drawTile(uint8_t* lp, int colorByte, int colorBits, int address, int startX, uint32_t backPen, uint32_t*& dest)
{
    const int color = (colorByte >> colorBits) & 0x03;
    readTilePlaneData(address, color);
    for (int i = 0; i < 8; i++) {
        uint8_t p;
        shiftTilePlaneData(p);
        if ((startX + i) >= 0 && (startX + i) < W) {
            drawTilePixel(p, color, backPen, dest);
            if (p) lp[startX + i] |= 0x02;
        }
        dest++;
    }
}

void Vt369Core::drawBackgroundBase(uint8_t* lp)
{
    const uint8_t sxc = m_s.refreshData & 0x001f;
    const uint8_t syc = (m_s.refreshData & 0x03e0) >> 5;
    const uint16_t nametable = m_s.refreshData & 0x0c00;
    const uint8_t syf = (m_s.refreshData & 0x7000) >> 12;
    int x = sxc;
    int tileIndex = (nametable | 0x2000) + syc * 32;
    int startX = (m_s.xFine ^ 0x07) - 7;
    uint32_t* dest = pix(m_s.scanline, startX);
    m_s.tilecount = 0;
    while (m_s.tilecount < 34) {
        const int index1 = tileIndex + x;
        const int page2 = ppuReadbyte(index1);
        const int pos = ((index1 & 0x380) >> 4) | ((index1 & 0x1f) >> 2);
        const int page = (index1 & 0x0c00) >> 10;
        int address = 0x3c0 + pos;
        const int colorByte = ppuReadbyte((((page * 0x400) + address) & 0xfff) + 0x2000);
        const int colorBits = ((index1 & 0x40) >> 4) + (index1 & 0x02);
        if (startX < W) {
            address = (m_s.tilePage ? 0x1000 : 0) + (page2 * 16) + syf;
            drawTile(lp, colorByte, colorBits, address, startX, m_s.backColor, dest);
            startX += 8;
            x++;
            if (x > 31) { x = 0; tileIndex ^= 0x400; }
        }
        m_s.tilecount++;
    }
    if (!(m_s.regs[PPU_CONTROL1] & PPU_CONTROL1_BACKGROUND_L8)) {
        dest = pix(m_s.scanline, 0);
        for (int i = 0; i < 8; i++) { drawBackPen(dest, m_s.backColor); dest++; lp[i] ^= 0x02; }
    }
}

void Vt369Core::drawBackground(uint8_t* lp)
{
    if (m_cfg.soc == 3) { shDrawBackground(lp); return; }
    const bool names64 = (m_s.newvid1c == 0x12) && (m_s.newvid1d == 0x09) && (m_s.newvid1e == 0x0f)
        && BIT(m_s.tilebases[0], 4) && (m_s.tilebases[2] == 0x08);
    if (!names64) { drawBackgroundBase(lp); return; }
    const uint8_t sxc = m_s.refreshData & 0x001f;
    const uint8_t syc = (m_s.refreshData & 0x03e0) >> 5;
    const uint16_t nametable = m_s.refreshData & 0x0c00;
    const uint8_t syf = (m_s.refreshData & 0x7000) >> 12;
    int x = sxc;
    const uint8_t srcY = (syc + (BIT(nametable, 11) ? 30 : 0)) & 0x1f;
    int tileIndex = 0x2000 + srcY * 64;
    int startX = (m_s.xFine ^ 0x07) - 7;
    uint32_t* dest = pix(m_s.scanline, startX);
    m_s.tilecount = 0;
    while (m_s.tilecount < 34) {
        const int index1 = tileIndex + x * 2;
        const int page2 = ppuReadbyte(index1) | (ppuReadbyte(index1 + 1) << 8);
        if (startX < W) {
            readTilePlaneData((m_s.tilePage ? 0x1000 : 0) + (page2 * 16) + syf, 0);
            for (int i = 0; i < 8; i++) {
                uint8_t p;
                shiftTilePlaneData(p);
                if ((startX + i) >= 0 && (startX + i) < W) {
                    drawTilePixel(p, 0, m_s.backColor, dest);
                    if (p) lp[startX + i] |= 0x02;
                }
                dest++;
            }
            startX += 8;
            x++;
            if (x > 31) x = 0;
        }
        m_s.tilecount++;
    }
    if (!(m_s.regs[PPU_CONTROL1] & PPU_CONTROL1_BACKGROUND_L8)) {
        dest = pix(m_s.scanline, 0);
        for (int i = 0; i < 8; i++) { drawBackPen(dest, m_s.backColor); dest++; lp[i] ^= 0x02; }
    }
}

// --- sprites ---
void Vt369Core::readSpritePlaneData(int address)
{
    if (m_cfg.soc == 3) {
        m_s.planebuf[0] = shPpuRead((address + 0) & 0x3fff);
        m_s.planebuf[1] = shPpuRead((address + 8) & 0x3fff);
        return;
    }
    m_s.planebuf[0] = sprR((address + 0) & 0x1fff);
    m_s.planebuf[1] = sprR((address + 8) & 0x1fff);
    if (BIT(m_s.extModes, 2)) {
        m_s.planebuf[2] = sprR(((address + 0) & 0x1fff) | 0x2000);
        m_s.planebuf[3] = sprR(((address + 8) & 0x1fff) | 0x2000);
    }
}

void Vt369Core::makeSpritePixelData(uint8_t& pd, bool flipx)
{
    if (flipx) {
        pd = (m_s.planebuf[0] & 1) | ((m_s.planebuf[1] & 1) << 1);
        m_s.planebuf[0] >>= 1; m_s.planebuf[1] >>= 1;
    } else {
        pd = BIT(m_s.planebuf[0], 7) | (BIT(m_s.planebuf[1], 7) << 1);
        m_s.planebuf[0] <<= 1; m_s.planebuf[1] <<= 1;
    }
    const bool is4bpp = BIT(m_s.extModes, 2), is16pix = BIT(m_s.extModes, 0);
    if (is4bpp) {
        if (flipx) {
            pd |= ((m_s.planebuf[2] & 1) << 5) | ((m_s.planebuf[3] & 1) << 6);
            m_s.planebuf[2] >>= 1; m_s.planebuf[3] >>= 1;
            if (is16pix) {
                const uint8_t p0 = pd & 3, p1 = (pd >> 5) & 3;
                pd = p1 | (p0 << 5);
            }
        } else {
            pd |= (((m_s.planebuf[2] >> 7) & 1) << 5) | (((m_s.planebuf[3] >> 7) & 1) << 6);
            m_s.planebuf[2] <<= 1; m_s.planebuf[3] <<= 1;
        }
    }
}

void Vt369Core::drawSpritePixel(int xpos, int color, int pixel, uint8_t pd)
{
    if (m_cfg.soc == 3) { *pix(m_s.scanline, xpos + pixel) = m_pens[m_sh.pal[(pd | color << 2) & 0x3f] & 0x3f]; return; }
    const bool is4bpp = BIT(m_s.extModes, 2), is16pix = BIT(m_s.extModes, 0);
    if (is4bpp) {
        if (!is16pix) drawTilePixelInner(pd + (4 * color), pix(m_s.scanline, xpos + pixel));
        else {
            if (pd & 3) drawTilePixelInner((pd & 3) + (4 * color), pix(m_s.scanline, xpos + pixel));
            if ((pd >> 5) & 3) drawTilePixelInner(((pd >> 5) & 3) + (4 * color), pix(m_s.scanline, xpos + pixel + 8));
        }
    } else {
        const uint16_t palval = m_s.paletteRam[((4 * color) | pd) & 0x1f];
        *pix(m_s.scanline, xpos + pixel) = m_pens[apply_grayscale_and_emphasis(palval)];
    }
}

void Vt369Core::drawSpritePixelLow(int pd, int pixel, int xpos, int color, int idx, uint8_t* lp)
{
    if (pd) {
        if ((xpos + pixel) < W) {
            if (!lp[xpos + pixel]) drawSpritePixel(xpos, color, pixel, pd);
            lp[xpos + pixel] |= 0x01;
        }
    }
    if (idx == 0 && (pd & 3) && ((xpos + pixel) < 255) && (lp[xpos + pixel] & 0x02)) m_s.regs[PPU_STATUS] |= PPU_STATUS_SPRITE0_HIT;
}

void Vt369Core::drawSpritePixelHigh(int pd, int pixel, int xpos, int color, int idx, uint8_t* lp)
{
    if (pd) {
        if ((xpos + pixel) < W) {
            if (BIT(~lp[xpos + pixel], 0)) {
                drawSpritePixel(xpos, color, pixel, pd);
                lp[xpos + pixel] |= 0x01;
            }
        }
    }
    if (idx == 0 && (pd & 3) && ((xpos + pixel) < 255) && (lp[xpos + pixel] & 0x02)) m_s.regs[PPU_STATUS] |= PPU_STATUS_SPRITE0_HIT;
}

void Vt369Core::drawSpritesBase(uint8_t* lp)
{
    int count = 0;
    int size = (m_s.regs[PPU_CONTROL0] & PPU_CONTROL0_SPRITE_SIZE) ? 16 : 8;
    int firstPixel = (m_s.regs[PPU_CONTROL1] & PPU_CONTROL1_SPRITES_L8) ? 0 : 8;
    for (int si = 0; si < 0x100; si += 4) {
        int ypos = m_s.spriteram[si] + 1;
        int xpos = m_s.spriteram[si + 3];
        if (si == 0 && xpos == 254) {
            ypos--;
            if (ypos <= m_s.scanline && m_s.scanline < ypos + size && (lp[xpos] & 0x02)) m_s.regs[PPU_STATUS] |= PPU_STATUS_SPRITE0_HIT;
        }
        if ((ypos + size <= m_s.scanline) || (ypos > m_s.scanline)) continue;
        int tile = m_s.spriteram[si + 1];
        const int color = (m_s.spriteram[si + 2] & 0x03) + 4;
        const bool pri = BIT(m_s.spriteram[si + 2], 5);
        const bool flipx = BIT(m_s.spriteram[si + 2], 6);
        const bool flipy = BIT(m_s.spriteram[si + 2], 7);
        m_s.extraSpriteBits = (m_s.spriteram[si + 2] & 0x1c) >> 2;
        if (size == 16 && BIT(tile, 0)) { tile &= ~1; tile |= 0x100; }
        int line = m_s.scanline - ypos;
        if (flipy) line = (size - 1) - line;
        if (size == 16 && line > 7) { tile++; line -= 8; }
        int index1 = tile * 16;
        if (m_cfg.soc == 3) index1 += ((m_sh.colsel >> 2) & 3) * 0x1000;      // apply_sprite_pattern_page
        else if (size == 8) index1 += m_s.spritePage == 0 ? 0 : 0x1000;
        readSpritePlaneData(index1 + line);
        if (count == 8) { m_s.regs[PPU_STATUS] |= PPU_STATUS_8SPRITES; break; }
        count++;
        if (!(m_s.regs[PPU_CONTROL1] & PPU_CONTROL1_SPRITES)) continue;
        for (int p = 0; p < 8; p++) {
            uint8_t pd;
            makeSpritePixelData(pd, flipx);
            if (xpos + p >= firstPixel) {
                if (pri) drawSpritePixelLow(pd, p, xpos, color, si, lp);
                else drawSpritePixelHigh(pd, p, xpos, color, si, lp);
            }
        }
    }
}

uint32_t Vt369Core::getPenValue(int pd, int bpp, int pal)
{
    uint8_t pen = (bpp == 4) ? (pd | pal << 4) : pd;
    uint16_t pal0 = ppuReadbyte(((pen & 0xff) * 2) + 0x3e00);
    pal0 |= ppuReadbyte(((pen & 0xff) * 2) + 0x3e01) << 8;
    return rgb(((pal0 >> 10) & 0x1f) << 3, ((pal0 >> 5) & 0x1f) << 3, (pal0 & 0x1f) << 3);
}

void Vt369Core::drawSpritesHighRes(uint8_t*)
{
    for (int n = 0; n < 0x40; n++) {
        const uint8_t* s = &m_s.spriteram[n * 8];
        int ypos = s[0];
        int tilenum = s[1] | (s[2] << 8);
        int bpp = (s[3] & 0x80) ? 8 : 4;
        int width = 4 << ((s[3] & 0x30) >> 4);
        int height = 4 << ((s[3] & 0x0c) >> 2);
        int pal = s[6] & 0x3f;
        int xpos = s[7];
        if ((ypos + height <= m_s.scanline) || (ypos > m_s.scanline)) continue;
        const int line = m_s.scanline - ypos;
        int off = (bpp == 4) ? tilenum * (2 * height * width) + line * (2 * width) : tilenum * (4 * height * width) + line * (4 * width);
        off += (m_s.tilebases[2] | (m_s.tilebases[3] << 8)) * 0x2000;
        for (int p = 0; p < width; p++) {
            uint8_t pd = (bpp == 4) ? (onespaceRel(off + p) & 0xf) : onespaceRel(off + p * 2);
            if (xpos + p >= 0 && pd && (xpos + p) < W) *pix(m_s.scanline, xpos + p) = getPenValue(pd, bpp, pal);
        }
    }
}

void Vt369Core::drawSpritesStandardRes(uint8_t* lp)
{
    for (int n = 0; n < 0x80; n++) {
        const bool newFormat = m_s.newvid1e & 0x04;
        int ypT = 0x000, xpT = 0x003, tnT = 0x001, exT = 0x002, step = 4;
        if (newFormat) { ypT = 0x000; xpT = 0x180; tnT = 0x080; exT = 0x100; step = 1; }
        const int o = n * step;
        int pri = 0;
        int ypos = m_s.spriteram[ypT + o];
        int xpos = m_s.spriteram[xpT + o];
        int tilenum = m_s.spriteram[tnT + o];
        uint8_t ex = m_s.spriteram[exT + o];
        tilenum |= (ex & 0x1c) << 6;
        int pal = ex & 0x03;
        if (m_s.newvid1d & 0x08) {
            pal |= (ex & 0x20) >> 3;
            if (ex & 0x40) xpos = -0x100 + xpos;
            if (ex & 0x80) ypos = -0x100 + ypos;
        } else pri = (ex & 0x20) >> 5;
        int height, width, bpp;
        bool alt16 = false;
        if (newFormat) {
            height = 16; width = 8; bpp = 8;
            if (m_s.newvid1d & 0x02) { width = 16; bpp = 4; }
            if ((!(m_s.newvid1d & 0x04)) && (m_s.newvid1c & 0x10)) { alt16 = true; bpp = 4; }
        } else {
            height = (m_s.regs[PPU_CONTROL0] & PPU_CONTROL0_SPRITE_SIZE) ? 16 : 8;
            width = 8; bpp = 8;
            if ((m_s.newvid1c == 0x12) && (m_s.newvid1d == 0x0f) && (m_s.newvid1e == 0x0b)) { bpp = 4; width = 16; height = 16; }
        }
        if ((ypos + height <= m_s.scanline) || (ypos > m_s.scanline)) continue;
        const int line = m_s.scanline - ypos;
        int index1 = (bpp == 4) ? (alt16 ? tilenum * 32 : tilenum * 128) : tilenum * 64;
        int off = alt16 ? index1 + line * 4 : index1 + line * 8;
        off += (m_s.tilebases[2] | (m_s.tilebases[3] << 8)) * 0x2000;
        uint8_t buf[8];
        for (int i = 0; i < 8; i++) buf[i] = onespaceRel(off + i);
        for (int p = 0; p < width; p++) {
            uint8_t pd;
            if (bpp == 4) { pd = buf[p >> 1]; pd = (p & 1) ? (pd >> 4) : (pd & 0xf); }
            else pd = buf[p];
            if (xpos + p < 0 || !pd || (xpos + p) >= W) continue;
            if (pri) {
                if (!lp[xpos + p]) { *pix(m_s.scanline, xpos + p) = getPenValue(pd, bpp, pal); lp[xpos + p] |= 0x01; }
            } else {
                if (BIT(~lp[xpos + p], 0)) { *pix(m_s.scanline, xpos + p) = getPenValue(pd, bpp, pal); lp[xpos + p] |= 0x01; }
            }
        }
    }
}

void Vt369Core::drawSprites(uint8_t* lp)
{
    if (!m_s.newvid1e) drawSpritesBase(lp);
    else if (m_s.newvid1c & 0x04) drawSpritesHighRes(lp);
    else drawSpritesStandardRes(lp);
}

/*************************************************************************
 APU (nes_apu.cpp)
*************************************************************************/

static const uint8_t vbl_length[32] = {
    10, 254, 20, 2, 40, 4, 80, 6, 160, 8, 60, 10, 14, 12, 26, 14,
    12, 16, 24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30 };
static const int freq_limit[8] = { 0x3FF, 0x555, 0x666, 0x71C, 0x787, 0x7C1, 0x7E0, 0x7F0 };
static const int noise_freq[16] = { 4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068 };
static const int dpcm_clocks[16] = { 428, 380, 340, 320, 286, 254, 226, 214, 190, 160, 142, 128, 106, 84, 72, 54 };
static const int duty_lut[4] = { 0b01000000, 0b01100000, 0b01111000, 0b10011111 };

void Vt369Core::apuInit()
{
    const uint32_t frameClocks = 29830;
    m_sampsPerSync = frameClocks / 4;
    for (int i = 0; i < 0x20; i++) {
        m_vblTimes[i] = vbl_length[i] * m_sampsPerSync / 2;
        m_syncTimes1[i] = m_sampsPerSync * (i + 1);
    }
    for (int i = 0; i < 0x80; i++) m_syncTimes2[i] = (m_sampsPerSync * i) >> 2;
    for (int i = 0; i < 31; i++) m_squareLut[i] = (i == 0) ? 0.0f : (float)(95.88 / ((8128.0 / i) + 100.0));
    for (int t = 0; t < 16; t++)
        for (int n = 0; n < 16; n++)
            for (int d = 0; d < 128; d++) {
                double o = (t / 8227.0) + (n / 12241.0) + (d / 22638.0);
                m_tndLut[t][n][d] = (float)((o == 0.0) ? 0.0 : 159.79 / ((1.0 / o) + 100.0));
            }
}

static const int64_t FRAME_TIMER_DOTS = 29830 * 3;   // m_frame_period (APU clock = CPU clock = 3 dots)

void Vt369Core::apuReset()
{
    m_s.stepMode = 4;
    m_s.frameIrqEnabled = 1;
    m_s.frameTimerAt = m_s.dot + FRAME_TIMER_DOTS;
    m_s.noi.lfsr = 1;
    apuWrite(0x15, 0x00);
    m_s.tri.adder = 0;
}

void Vt369Core::apuSquare(State::Square* chan)
{
    if (!chan->enabled) { chan->output = 0; return; }
    int env_delay = m_syncTimes1[chan->regs[0] & 0x0f];
    chan->env_phase -= 4;
    while (chan->env_phase < 0) {
        chan->env_phase += env_delay;
        if (chan->regs[0] & 0x20) chan->env_vol = (chan->env_vol + 1) & 15;
        else if (chan->env_vol < 15) chan->env_vol++;
    }
    if (chan->vbl_length > 0 && !(chan->regs[0] & 0x20)) chan->vbl_length--;
    if (!chan->vbl_length) { chan->output = 0; return; }
    if ((chan->regs[1] & 0x80) && (chan->regs[1] & 7)) {
        int sweep_delay = m_syncTimes1[(chan->regs[1] >> 4) & 7];
        chan->sweep_phase -= 2;
        while (chan->sweep_phase < 0) {
            chan->sweep_phase += sweep_delay;
            if (chan->regs[1] & 8) chan->freq -= chan->freq >> (chan->regs[1] & 7);
            else chan->freq += chan->freq >> (chan->regs[1] & 7);
        }
    }
    if ((!(chan->regs[1] & 8) && (chan->freq >> 16) > freq_limit[chan->regs[1] & 7]) || (chan->freq >> 16) < 4) { chan->output = 0; return; }
    chan->phaseacc -= 4;
    while (chan->phaseacc < 0) { chan->phaseacc += (chan->freq >> 16); chan->adder = (chan->adder + 1) & 0x0f; }
    chan->output = (chan->regs[0] & 0x10) ? (chan->regs[0] & 0x0f) : (0x0f - chan->env_vol);
    int bit = ((chan->adder >> 1) & 7);
    chan->output *= BIT(duty_lut[chan->regs[0] >> 6], 7 - bit);
}

void Vt369Core::apuTriangle(State::Triangle* chan)
{
    if (!chan->enabled) return;
    bool not_held = !BIT(chan->regs[0], 7);
    if (!chan->counter_started && not_held) {
        if (chan->write_latency) chan->write_latency--;
        if (!chan->write_latency) chan->counter_started = 1;
    }
    if (chan->counter_started) {
        if (chan->linear_reload) chan->linear_length = m_syncTimes2[chan->regs[0] & 0x7f];
        else if (chan->linear_length > 0) chan->linear_length--;
        if (not_held) chan->linear_reload = 0;
        if (chan->vbl_length && not_held) chan->vbl_length--;
    }
    if (!(chan->linear_length && chan->vbl_length)) return;
    int freq = ((chan->regs[3] & 7) << 8) + chan->regs[2] + 1;
    if (freq < 2) return;
    chan->phaseacc -= 4;
    while (chan->phaseacc < 0) {
        chan->phaseacc += freq;
        chan->adder++;
        chan->output = chan->adder & 0xf;
        if (!BIT(chan->adder, 4)) chan->output ^= 0xf;
    }
}

void Vt369Core::apuNoise(State::Noise* chan)
{
    if (!chan->enabled) { chan->output = 0; return; }
    int env_delay = m_syncTimes1[chan->regs[0] & 0x0f];
    chan->env_phase -= 4;
    while (chan->env_phase < 0) {
        chan->env_phase += env_delay;
        if (chan->regs[0] & 0x20) chan->env_vol = (chan->env_vol + 1) & 15;
        else if (chan->env_vol < 15) chan->env_vol++;
    }
    if (!(chan->regs[0] & 0x20) && chan->vbl_length > 0) chan->vbl_length--;
    if (!chan->vbl_length) { chan->output = 0; return; }
    int freq = noise_freq[chan->regs[2] & 0x0f];
    chan->phaseacc -= 4;
    while (chan->phaseacc < 0) {
        chan->phaseacc += freq;
        // nesapu_device::update_lfsr (the VT APU is NES_APU, not APU_2A03)
        chan->lfsr |= (BIT(chan->lfsr, 0) ^ BIT(chan->lfsr, (chan->regs[2] & 0x80) ? 6 : 1)) << 15;
        chan->lfsr >>= 1;
    }
    if (BIT(chan->lfsr, 0)) { chan->output = 0; return; }
    chan->output = (chan->regs[0] & 0x10) ? (chan->regs[0] & 0x0f) : (0x0f - chan->env_vol);
}

void Vt369Core::dpcmReset(State::Dpcm* chan)
{
    chan->address = 0xc000 + (uint16_t)(chan->regs[2] << 6);
    chan->length = (uint16_t)(chan->regs[3] << 4) + 1;
    chan->bits_left = chan->length << 3;
    chan->enabled = 1;
}

void Vt369Core::apuDpcm(State::Dpcm* chan)
{
    if (chan->enabled) {
        int freq = dpcm_clocks[chan->regs[0] & 0x0f];
        chan->phaseacc -= 4;
        while (chan->phaseacc < 0) {
            chan->phaseacc += freq;
            if (!chan->length) {
                chan->enabled = 0;
                if (chan->regs[0] & 0x40) dpcmReset(chan);
                else { if (chan->regs[0] & 0x80) chan->irq_occurred = 1; break; }
            }
            chan->bits_left--;
            int bit_pos = 7 - (chan->bits_left & 7);
            if (bit_pos == 7) {
                chan->cur_byte = 0x00;      // apu_read_mem is a TODO in MAME's VT SoC
                chan->address++;
                chan->length--;
            }
            if ((chan->cur_byte & (1 << bit_pos)) && (chan->vol <= 125)) chan->vol += 2;
            else if (chan->vol >= 2) chan->vol -= 2;
        }
    }
    chan->output = (uint8_t)chan->vol;
}

void Vt369Core::apuWrite(int offset, uint8_t value)
{
    State& s = m_s;
    int chan = BIT(offset, 2);
    switch (offset) {
    case 0x00: case 0x04: s.squ[chan].regs[0] = value; break;
    case 0x01: case 0x05: s.squ[chan].regs[1] = value; break;
    case 0x02: case 0x06:
        s.squ[chan].regs[2] = value;
        if (s.squ[chan].enabled) s.squ[chan].freq = ((((s.squ[chan].regs[3] & 7) << 8) + value) + 1) << 16;
        break;
    case 0x03: case 0x07:
        s.squ[chan].regs[3] = value;
        if (s.squ[chan].enabled) {
            s.squ[chan].vbl_length = m_vblTimes[value >> 3];
            s.squ[chan].env_vol = 0;
            s.squ[chan].freq = ((((value & 7) << 8) + s.squ[chan].regs[2]) + 1) << 16;
        }
        break;
    case 0x08:
        s.tri.regs[0] = value;
        if (s.tri.enabled && !s.tri.counter_started) s.tri.linear_length = m_syncTimes2[value & 0x7f];
        break;
    case 0x0a: s.tri.regs[2] = value; break;
    case 0x0b:
        s.tri.regs[3] = value;
        s.tri.write_latency = (m_sampsPerSync + 239) / 240;
        if (s.tri.enabled) {
            s.tri.counter_started = 0;
            s.tri.vbl_length = m_vblTimes[value >> 3];
            s.tri.linear_length = m_syncTimes2[s.tri.regs[0] & 0x7f];
            s.tri.linear_reload = 1;
        }
        break;
    case 0x0c: s.noi.regs[0] = value; break;
    case 0x0e: s.noi.regs[2] = value; break;
    case 0x0f:
        s.noi.regs[3] = value;
        if (s.noi.enabled) { s.noi.vbl_length = m_vblTimes[value >> 3]; s.noi.env_vol = 0; }
        break;
    case 0x10:
        s.dpcm.regs[0] = value;
        if (!(value & 0x80)) s.dpcm.irq_occurred = 0;
        break;
    case 0x11: s.dpcm.regs[1] = value & 0x7f; s.dpcm.vol = s.dpcm.regs[1]; break;
    case 0x12: s.dpcm.regs[2] = value; break;
    case 0x13: s.dpcm.regs[3] = value; break;
    case 0x17:
        s.stepMode = (value & 0x80) ? 5 : 4;
        s.frameIrqEnabled = !BIT(value, 6);
        if (s.frameIrqEnabled) s.frameTimerAt = s.dot + FRAME_TIMER_DOTS;
        else { s.frameIrqOccurred = 0; s.frameTimerAt = -1; }
        break;
    case 0x15:
        if (value & 1) s.squ[0].enabled = 1; else { s.squ[0].enabled = 0; s.squ[0].vbl_length = 0; }
        if (value & 2) s.squ[1].enabled = 1; else { s.squ[1].enabled = 0; s.squ[1].vbl_length = 0; }
        if (value & 4) s.tri.enabled = 1;
        else { s.tri.enabled = 0; s.tri.vbl_length = 0; s.tri.linear_length = 0; s.tri.counter_started = 0; s.tri.write_latency = 0; }
        if (value & 8) s.noi.enabled = 1; else { s.noi.enabled = 0; s.noi.vbl_length = 0; }
        if (value & 0x10) { if (!s.dpcm.enabled) { s.dpcm.enabled = 1; dpcmReset(&s.dpcm); } }
        else s.dpcm.enabled = 0;
        s.dpcm.irq_occurred = 0;
        break;
    }
}

uint8_t Vt369Core::apuStatus()
{
    uint8_t r = 0;
    if (m_s.squ[0].vbl_length > 0) r |= 0x01;
    if (m_s.squ[1].vbl_length > 0) r |= 0x02;
    if (m_s.tri.vbl_length > 0) r |= 0x04;
    if (m_s.noi.vbl_length > 0) r |= 0x08;
    if (m_s.dpcm.enabled) r |= 0x10;
    if (m_s.frameIrqOccurred) r |= 0x40;
    if (m_s.dpcm.irq_occurred) r |= 0x80;
    m_s.frameIrqOccurred = 0;
    return r;
}

void Vt369Core::apuSample()
{
    apuSquare(&m_s.squ[0]);
    apuSquare(&m_s.squ[1]);
    apuTriangle(&m_s.tri);
    apuNoise(&m_s.noi);
    apuDpcm(&m_s.dpcm);
    float acc = m_squareLut[m_s.squ[0].output + m_s.squ[1].output];
    acc += m_tndLut[m_s.tri.output][m_s.noi.output][m_s.dpcm.output];
    // sound CPU DACs: 0.2 each in MAME (our APU path is scaled by 0.6 below)
    acc += (m_s.dacOut[0] + m_s.dacOut[1]) * (0.2f / 0.6f / 32768.0f);
    if (m_cfg.soc == 3) acc += m_sh.r4027 * (0.5f / 255.0f);     // SH6578 8-bit DAC
    if (m_cfg.board == 2) {
        // ABL Ping Pong's speech chip drains its buffer at a steady rate. Its data
        // format is unknown (not PCM or OKI/IMA ADPCM), so the voice stays silent.
        m_s.ppAcc += PP_RATE;
        const uint32_t apuHz = m_cfg.pal ? 26601712 / 60 : 21477272 / 48;
        if (m_s.ppAcc >= apuHz) {
            m_s.ppAcc -= apuHz;
            if (m_s.ppCount) { m_s.ppSample = m_s.ppFifo[m_s.ppHead]; m_s.ppHead = (m_s.ppHead + 1) & 0xff; m_s.ppCount--; }
            else m_s.ppSample = 0x80;
        }
    }
    // box-filter down to 44.1 kHz
    m_s.resampleSum += acc;
    m_s.resampleN++;
    m_s.resampleAcc += 44100.0;
    // APU samples a second: the CPU clock / 4 (NTSC 21.477 MHz / 12, PAL 26.602 MHz / 15)
    const double apuRate = m_cfg.pal ? 26601712.0 / 15.0 / 4.0 : 21477272.0 / 12.0 / 4.0;
    if (m_s.resampleAcc >= apuRate) {
        m_s.resampleAcc -= apuRate;
        double v = m_s.resampleSum / m_s.resampleN;
        m_s.resampleSum = 0; m_s.resampleN = 0;
        // DC blocker (the NES mix is unipolar), then the level MAME routes (0.5)
        double y = v - m_s.dcPrevIn + 0.995 * m_s.dcPrevOut;
        m_s.dcPrevIn = v; m_s.dcPrevOut = y;
        int sv = (int)(y * 32767.0 * 0.6);
        m_audio.push_back((int16_t)std::clamp(sv, -32768, 32767));
    }
}

// ABL Ping Pong's speech chip. Commands: 3 (2 argument bytes) picks a sample,
// 4 starts streaming voice data into its buffer, and the stream is closed with 0 then 7.
void Vt369Core::ppByte(uint8_t b)
{
    switch (m_s.ppMode) {
    case 0:
        if (b == 3) m_s.ppMode = 1;
        else if (b == 4) { m_s.ppMode = 3; m_s.ppLast = 0x80; }
        return;
    case 1: m_s.ppMode = 2; return;
    case 2: m_s.ppMode = 0; return;
    default:
        if (b == 7 && m_s.ppLast == 0) { m_s.ppMode = 0; return; }
        m_s.ppLast = b;
        if (m_s.ppCount < sizeof(m_s.ppFifo)) { m_s.ppFifo[(m_s.ppHead + m_s.ppCount) & 0xff] = b; m_s.ppCount++; }
        return;
    }
}

void Vt369Core::apuSync() { apuSyncTo(m_s.dot); }

void Vt369Core::apuSyncTo(int64_t now)
{
    // frame_timer_cb (its IRQ output isn't connected on the VT SoC, but $4015 bit 6 is readable)
    while (m_s.frameTimerAt >= 0 && m_s.frameTimerAt <= now) {
        if (m_s.stepMode == 4 && m_s.frameIrqEnabled) m_s.frameIrqOccurred = 1;
        m_s.frameTimerAt += FRAME_TIMER_DOTS;
    }
    while (m_s.apuDot + DOTS_PER_APU_SAMPLE <= now) {
        m_s.apuDot += DOTS_PER_APU_SAMPLE;
        apuSample();
    }
}

size_t Vt369Core::readAudio(int16_t* out, size_t maxSamples)
{
    size_t n = std::min(maxSamples, m_audio.size());
    std::memcpy(out, m_audio.data(), n * sizeof(int16_t));
    m_audio.erase(m_audio.begin(), m_audio.begin() + n);
    return n;
}

/*************************************************************************
 Frame loop
*************************************************************************/

int64_t Vt369Core::nextEventDot() const
{
    // hblank_tick: cycles_to_attotime(260) / 3 into the line (depends on the CPU clock)
    int64_t ev = m_s.evPhase == 0 ? m_s.lineStart + 260 / m_s.clockMul : m_s.lineStart + DOTS_PER_LINE;
    if (m_s.nmiAt >= 0 && m_s.nmiAt < ev) ev = m_s.nmiAt;
    if (m_s.lcdcNmiAt >= 0 && m_s.lcdcNmiAt < ev) ev = m_s.lcdcNmiAt;
    if (m_s.oamDmaAt >= 0 && m_s.oamDmaAt < ev) ev = m_s.oamDmaAt;
    return ev;
}

void Vt369Core::fireEvent()
{
    int64_t ev = m_s.evPhase == 0 ? m_s.lineStart + 260 / m_s.clockMul : m_s.lineStart + DOTS_PER_LINE;
    if (m_s.nmiAt >= 0 && m_s.nmiAt <= ev) {
        m_s.nmiAt = -1;
        m_cpu.pulseNmi();
    } else if (m_s.lcdcNmiAt >= 0 && m_s.lcdcNmiAt <= ev) {   // assert_36pcase_lcdc_nmi
        m_s.lcdcNmiAt += DOTS_PER_FRAME;
        m_cpu.pulseNmi();
    } else if (m_s.oamDmaAt >= 0 && m_s.oamDmaAt <= ev) {     // flush_36pcase_deferred_oam_dma
        m_s.oamDmaAt = -1;
        flushOamDma();
    } else if (m_s.evPhase == 0) {
        m_s.evPhase = 1;
        hblankTick();
    } else {
        m_s.evPhase = 0;
        m_s.lineStart += DOTS_PER_LINE;
        scanlineTick();
        if (m_s.scanline == 0) m_frameDone = true;
    }
    // raised in the middle of an instruction: seen at the end of it
    if (m_inInst) m_cpu.m_irqSkip = m_cpu.m_nmiSkip = false;
}

void Vt369Core::runFrame()
{
#ifdef XAVIX_DEBUG
    if (getenv("VTPC")) { static int f = 0; if (++f % atoi(getenv("VTPC")) == 0) fprintf(stderr, "f%d pc %04x banks %02x %02x %02x %02x %02x %02x %02x %02x ctl %02x %02x\n", f, m_cpu.pc, m_sh.bank[0], m_sh.bank[1], m_sh.bank[2], m_sh.bank[3], m_sh.bank[4], m_sh.bank[5], m_sh.bank[6], m_sh.bank[7], m_s.regs[0], m_s.regs[1]); }
#endif
#ifdef VT_DEBUG
    g_vtFrame++;
#endif
    // one full PPU frame (262 or 312 lines), ending after the last scanline
    if (m_cfg.board == 1) {
        // the plunger: pulled back further the longer Down is held, springs back on release
        m_s.plungerHold = (m_inputs & 0x20) ? uint8_t(std::min(0xbf, m_s.plungerHold + 6)) : 0;
    }
    m_frameDone = false;
    while (!m_frameDone) {
        while (m_s.dot < nextEventDot()) {
            m_instStart = m_s.dot;
            m_access = 0;
            m_inInst = true;
#ifdef XAVIX_DEBUG
            { static const char* h = getenv("VTHIT"); static int f0 = 0; static uint16_t hist[8]; static int hi = 0; if (h && m_cpu.pc == strtol(h, nullptr, 16)) { fprintf(stderr, "hit %04x n%d from", m_cpu.pc, ++f0); for (int i = 0; i < 8; i++) fprintf(stderr, " %04x", hist[(hi + i) & 7]); fprintf(stderr, "\n"); } hist[hi++ & 7] = m_cpu.pc; }
#endif
            int cyc = m_cpu.step();
            m_inInst = false;
            m_s.dot += cyc * (3 / m_s.clockMul);
            if (m_cfg.soc == 3 && m_sh.timerCycles && (m_sh.timerCnt -= cyc) <= 0) {
                m_sh.timerCnt += m_sh.timerCycles;
                if (!(m_sh.irqMask & 0x80)) m_cpu.setIrq(true);
            }
            spuRun();
        }
        while (!m_frameDone && nextEventDot() <= m_s.dot) fireEvent();
    }
    apuSync();
    if (m_audio.size() > 44100) m_audio.erase(m_audio.begin(), m_audio.end() - 4096);
    // copy the finished picture
    for (int y = 0; y < 240; y++) std::memcpy(&m_out[y * 256], pix(y, 0), 256 * 4);
}

/*************************************************************************
 Save state
*************************************************************************/

bool Vt369Core::saveState(std::vector<uint8_t>& out)
{
    out.clear();
    uint32_t magic = 0x42335456;   // 'VT3B'
    auto put = [&](const void* p, size_t n) { const uint8_t* b = (const uint8_t*)p; out.insert(out.end(), b, b + n); };
    put(&magic, 4);
    put(&m_s, sizeof(m_s));
    put(&m_cpu.pc, 2); put(&m_cpu.a, 1); put(&m_cpu.x, 1); put(&m_cpu.y, 1); put(&m_cpu.s, 1); put(&m_cpu.p, 1);
    uint8_t lines = (m_cpu.m_irqLine ? 1 : 0) | (m_cpu.m_nmiPending ? 2 : 0);
    put(&lines, 1);
    m_s.spuRegs = { m_spu.pc, m_spu.a, m_spu.x, m_spu.y, m_spu.s, m_spu.p, m_spu.databank, (uint8_t)m_spu.m_irqLine };
    std::memcpy(out.data() + 4, &m_s, sizeof(m_s));
    put(m_bitmapBuf, sizeof(m_bitmapBuf));
    if (m_cfg.soc == 3) put(&m_sh, sizeof(m_sh));
    if (m_cfg.spiFlashWrites) {
        // the flash blocks the loader rewrote (the game copied from the card)
        put(m_flashDirty, sizeof(m_flashDirty));
        for (size_t b = 0; b < FLASH_BLOCKS; b++)
            if (m_flashDirty[b >> 3] & (1 << (b & 7))) put(&m_rom[b << 16], 0x10000);
    }
    return true;
}

bool Vt369Core::loadState(const std::vector<uint8_t>& in)
{
    size_t need = 4 + sizeof(m_s) + 8 + 1 + sizeof(m_bitmapBuf) + (m_cfg.soc == 3 ? sizeof(m_sh) : 0);
    if (m_cfg.spiFlashWrites) {
        if (in.size() < need + sizeof(m_flashDirty)) return false;
        const uint8_t* mask = in.data() + need;
        size_t blocks = 0;
        for (size_t b = 0; b < FLASH_BLOCKS; b++) if (mask[b >> 3] & (1 << (b & 7))) blocks++;
        if (in.size() != need + sizeof(m_flashDirty) + blocks * 0x10000) return false;
        const uint8_t* q = mask + sizeof(m_flashDirty);
        for (size_t b = 0; b < FLASH_BLOCKS; b++)
            if (mask[b >> 3] & (1 << (b & 7))) { if ((b << 16) < m_rom.size()) memcpy(&m_rom[b << 16], q, 0x10000); q += 0x10000; }
        memcpy(m_flashDirty, mask, sizeof(m_flashDirty));
    } else if (in.size() != need) return false;
    if (m_cfg.soc == 3) memcpy(&m_sh, in.data() + need - sizeof(m_sh), sizeof(m_sh));
    uint32_t magic; memcpy(&magic, in.data(), 4);
    if (magic != 0x42335456) return false;
    const uint8_t* p = in.data() + 4;
    memcpy(&m_s, p, sizeof(m_s)); p += sizeof(m_s);
    memcpy(&m_cpu.pc, p, 2); p += 2;
    m_cpu.a = *p++; m_cpu.x = *p++; m_cpu.y = *p++; m_cpu.s = *p++; m_cpu.p = *p++;
    uint8_t lines = *p++;
    m_cpu.m_irqLine = lines & 1; m_cpu.m_nmiPending = (lines & 2) != 0;
    memcpy(m_bitmapBuf, p, sizeof(m_bitmapBuf));
    auto& r = m_s.spuRegs;
    m_spu.pc = r.pc; m_spu.a = r.a; m_spu.x = r.x; m_spu.y = r.y; m_spu.s = r.s; m_spu.p = r.p;
    m_spu.databank = r.databank; m_spu.m_irqLine = r.irq; m_spu.m_irqSkip = m_spu.m_nmiSkip = false;
    m_cpu.setOpcodeTable(m_s.encrypted ? m_cryptTable : nullptr);
    m_audio.clear();
    return true;
}

/*************************************************************************
 SH6578 (MAME nes_sh6578.cpp + ppu2c0x_sh6578.cpp, BSD-3, David Haywood)
*************************************************************************/

uint8_t Vt369Core::shCpuRead(uint16_t a)
{
#ifdef XAVIX_DEBUG
    if (getenv("VTIO") && a >= 0x4000 && a < 0x5000) { static std::set<uint32_t> seen; if (seen.insert(uint32_t(a) << 16 | m_cpu.pc).second) fprintf(stderr, "R %04x pc %04x\n", a, m_cpu.pc); }
#endif
    if (a < 0x2000) return m_sh.ram[a];
    if (a < 0x2008) { apuSync(); return ppuRead(a & 7); }
    if (a == 0x2008) return m_sh.colsel;
    if (a >= 0x2040 && a < 0x2080) return m_sh.pal[a - 0x2040];
    if (a < 0x4000) return 0;
    if (a >= 0x8000) return m_rom[((uint32_t(m_sh.cartBank) << 20) | (uint32_t(m_sh.bank[(a >> 12) & 7]) << 12) | (a & 0xfff)) & (m_rom.size() - 1)];
    if (a >= 0x5000) return m_sh.ram5[a - 0x5000];
    switch (a) {
    case 0x4015: apuSync(); return apuStatus();
    case 0x4016: { uint8_t r = m_sh.ioLatch[0] & 1; m_sh.ioLatch[0] >>= 1; return r; }
    case 0x4017: { uint8_t r = m_sh.ioLatch[1] & 1; m_sh.ioLatch[1] >>= 1; return r; }
    case 0x4026:                             // EXT port
        if (m_cfg.extProt) {
            // the security chip answers on bit 6 (handshake) and bit 5 (data)
            // (the handshake follows bit 7 while a command comes in; once the last command bit
            // is acknowledged it drops, and while the reply goes out it is bit 7 inverted)
            bool hs = (m_sh.extOut & 0x80) != 0;
            if (m_sh.protPhase >= 3) hs = !hs;
            else if (m_sh.protPhase) m_sh.protPhase++;   // high for the two reads of the host's ack loop
            return uint8_t((m_sh.extOut & 0x9f) | (hs ? 0x40 : 0) | (m_sh.protBit ? 0x20 : 0));
        }
        return shRand();                     // an unknown line on the Bandai boards, random as MAME
    case 0x4027: return m_sh.r4027;
    case 0x4033: return shRand();            // IRQ status
    }
    if (a >= 0x4040 && a < 0x4048) return m_sh.bank[a - 0x4040];
    return 0;
}

void Vt369Core::shCpuWrite(uint16_t a, uint8_t d)
{
#ifdef XAVIX_DEBUG
    if (getenv("VTIO") && a >= 0x4016 && a < 0x5000) { static std::set<uint32_t> seen; if (seen.insert(uint32_t(a) << 16 | d << 8 | (m_cpu.pc & 0xff)).second) fprintf(stderr, "W %04x = %02x pc %04x\n", a, d, m_cpu.pc); }
#endif
    if (a < 0x2000) { m_sh.ram[a] = d; return; }
    if (a < 0x2008) { apuSync(); ppuWrite(a & 7, d); return; }
    if (a == 0x2008) { m_sh.colsel = d; return; }
    if (a >= 0x2040 && a < 0x2080) { m_sh.pal[a - 0x2040] = d; return; }
    if (a < 0x4000 || a >= 0x8000) return;   // ROM
    if (a >= 0x5000) { m_sh.ram5[a - 0x5000] = d; return; }
    if (a <= 0x4013 || a == 0x4015 || a == 0x4017) { apuSync(); apuWrite(a - 0x4000, d); return; }
    switch (a) {
    case 0x4014: {                           // sprite DMA from a CPU page
        bool inInst = m_inInst;
        m_inInst = false;
        for (int i = 0; i < 0x100; i++) { uint8_t v = shCpuRead(uint16_t((d << 8) + i)); ppuWrite(PPU_SPRITE_DATA, v); }
        m_inInst = inInst;
        return;
    }
    case 0x4016:
        // the pads are latched on the falling edge, ignoring the plain 0-3 writes
        if ((d > 3 || m_cfg.board == 3) && ((m_sh.prevIo & 1) != (d & 1)) && !(d & 1)) {
            if (m_cfg.extProt) {
                // TV Games Baseball: the d-pad, Select and Start come in on 4017; 4016 is the
                // bat, a swing (bit 1) with its strength (bit 5 hard, bit 4 medium) - A and B
                m_sh.ioLatch[0] = uint8_t((m_inputs & 1) ? 0x22 : (m_inputs & 2) ? 0x12 : 0);
                m_sh.ioLatch[1] = uint8_t(m_inputs & 0xfc);
            } else { m_sh.ioLatch[0] = (uint8_t)m_inputs; m_sh.ioLatch[1] = 0; }
        }
        m_sh.prevIo = d;
        // ABL Wikid Joystick: bit 1 picks which 1 MB half of the 2 MB flash is the cartridge
        if (m_cfg.board == 3 && m_rom.size() == 0x200000) m_sh.cartBank = (d >> 1) & 1;
        return;
    case 0x4026:
        if (m_cfg.extProt) {
            // TV Games Baseball's security chip: each exchange clocks 8 command bits out
            // (bit 5, on bit 7 going low) then 5 reply bits back (MSB first). The game
            // sends f9 f9 f9 28 03 b4 1d f5 and resets itself unless the last reply is 7.
            if ((m_sh.extOut & 0x80) && !(d & 0x80)) {
                if (m_sh.protEdge == 13) m_sh.protEdge = m_sh.protPhase = 0;   // the next exchange
                if (m_sh.protEdge < 8) {
                    m_sh.protIn = uint8_t(m_sh.protIn << 1 | ((d >> 5) & 1));
                    if (++m_sh.protEdge == 8) m_sh.protReply = m_sh.protIn == 0xf5 ? 7 : 0;
                } else {
                    m_sh.protBit = (m_sh.protReply >> (12 - m_sh.protEdge)) & 1;
                    m_sh.protEdge++;
                }
            }
        }
        if (m_cfg.extProt && !(m_sh.extOut & 0x80) && (d & 0x80) && m_sh.protEdge == 8 && !m_sh.protPhase) m_sh.protPhase = 1;
        m_sh.extOut = d;
        return;
    case 0x4027: m_sh.r4027 = d; return;
    case 0x4032:
        m_sh.irqMask = d;
        if (d & 0x80) m_cpu.setIrq(false);
        return;
    case 0x4034:                             // timer: IRQ after the set number of scanlines
        m_sh.timerLines = ((d & 0x80) && (d & 0x20)) ? m_sh.timerVal : -1;
        // bit 5 clear: a free-running timer counting CPU cycles (TV Games Baseball plays
        // its sampled speech off it, 0xdf cycles = ~8 kHz, through the 4027 DAC)
        m_sh.timerCycles = ((d & 0x80) && !(d & 0x20)) ? std::max<int>(m_sh.timerVal, 16) : 0;
        m_sh.timerCnt = m_sh.timerCycles;
        return;
    case 0x4035: m_sh.timerVal = d; return;
    }
    if (a >= 0x4040 && a < 0x4048) { m_sh.bank[a - 0x4040] = d; return; }
    if (a >= 0x4048 && a < 0x4050) {
        switch (a - 0x4048) {
        case 0: m_sh.dmaControl = d; shDma(); break;
        case 1: m_sh.dmaBank = d; break;
        case 2: m_sh.dmaSrc[0] = d; break;
        case 3: m_sh.dmaSrc[1] = d; break;
        case 4: m_sh.dmaDst[0] = d; break;
        case 5: m_sh.dmaDst[1] = d; break;
        case 6: m_sh.dmaLen[0] = d; break;
        case 7: m_sh.dmaLen[1] = d; break;
        }
    }
}

void Vt369Core::shDma()
{
    if (!(m_sh.dmaControl & 0x80)) return;
    uint16_t src = uint16_t(m_sh.dmaSrc[0] | (m_sh.dmaSrc[1] << 8));
    uint16_t dst = uint16_t(m_sh.dmaDst[0] | (m_sh.dmaDst[1] << 8));
    uint16_t len = uint16_t(m_sh.dmaLen[0] | (m_sh.dmaLen[1] << 8));
    bool inInst = m_inInst;
    m_inInst = false;
    for (int i = 0; i <= len; i++) {
        uint8_t v;
        if (src & 0x8000) v = m_rom[((uint32_t(m_sh.cartBank) << 20) | (src & 0x7fff) | ((m_sh.dmaBank & 0x1f) * 0x8000)) & (m_rom.size() - 1)];
        else v = shCpuRead(src & 0x7fff);
        if (m_sh.dmaControl & 0x20) shCpuWrite(dst, v);
        else shPpuWrite(dst, v);
        src++; dst++;
    }
    m_inInst = inInst;
}

uint8_t Vt369Core::shPpuRead(uint32_t a)
{
    a &= 0xffff;
    if (a < 0x2800) return m_sh.vram[a];
    if (a >= 0x8000) return m_sh.nvram[a - 0x8000];
    return 0;
}

void Vt369Core::shPpuWrite(uint32_t a, uint8_t d)
{
    a &= 0xffff;
    if (a < 0x2800) m_sh.vram[a] = d;
    else if (a >= 0x8000) m_sh.nvram[a - 0x8000] = d;
}

void Vt369Core::shDrawBackground(uint8_t* lp)
{
    const uint8_t sxc = m_s.refreshData & 0x001f;
    const uint8_t syc = (m_s.refreshData & 0x03e0) >> 5;
    const uint16_t nametable = m_s.refreshData & 0x0c00;
    const uint8_t syf = (m_s.refreshData & 0x7000) >> 12;
    int x = sxc;
    int tileIndex = (nametable << 1) + syc * 64;
    int startX = (m_s.xFine ^ 0x07) - 7;
    uint32_t* dest = pix(m_s.scanline, startX);
    const bool ext = BIT(m_sh.colsel, 7);
    const uint32_t backPen = m_pens[m_sh.pal[0] & 0x3f];
    m_s.tilecount = 0;
    while (m_s.tilecount < 34) {
        int index1 = tileIndex + (x << 1);
        if (m_sh.colsel & 1) { index1 &= 0x7ff; index1 += 0x2000; }
        else index1 &= 0x1fff;
        const int page2 = shPpuRead(index1);
        const int colorByte = shPpuRead(index1 + 1);
        if (startX < W) {
            int address = ((page2 | (colorByte << 8)) & 0x0fff) << 4;
            address += syf;
            int color = (colorByte >> 4) & 0xf;
            color &= ext ? 0xc : 0xf;
            uint8_t p0 = shPpuRead(address), p1 = shPpuRead(address + 8), p2 = 0, p3 = 0;
            if (ext) { p2 = shPpuRead(address + 16); p3 = shPpuRead(address + 24); }
            for (int i = 0; i < 8; i++) {
                uint8_t px = uint8_t(((p0 & 0x80) >> 7) | ((p1 & 0x80) >> 6));
                if (ext) px |= uint8_t(((p2 & 0x80) >> 5) | ((p3 & 0x80) >> 4));
                p0 <<= 1; p1 <<= 1; p2 <<= 1; p3 <<= 1;
                if ((startX + i) >= 0 && (startX + i) < W) {
                    const uint8_t palval = m_sh.pal[(px | color << 2) & 0x3f] & 0x3f;
                    const bool trans = (palval & 0x1f) == 0x1f;
                    *dest = trans ? backPen : m_pens[palval];
                    if (!trans) lp[startX + i] |= 0x02;
                }
                dest++;
            }
            startX += 8;
            x++;
            if (x > 31) { x = 0; tileIndex ^= 0x800; }
        }
        m_s.tilecount++;
    }
    if (!(m_s.regs[PPU_CONTROL1] & PPU_CONTROL1_BACKGROUND_L8)) {
        dest = pix(m_s.scanline, 0);
        const uint32_t bp = m_pens[m_s.backColor & ((m_s.regs[PPU_CONTROL1] & PPU_CONTROL1_DISPLAY_MONO) ? 0xf0 : 0xff)];
        for (int i = 0; i < 8; i++) { *dest++ = bp; lp[i] ^= 0x02; }
    }
}
