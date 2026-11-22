// PocketPlug - SSD XaviX plug-and-play TV games
// SPDX-License-Identifier: BSD-3-Clause
//
// Port of MAME's tvgames/xavix.cpp, xavix_m.cpp, xavix_v.cpp, xavix_math.cpp,
// xavix_adc.cpp (David Haywood, ramacat), BSD-3-Clause.

#include "xavix.h"
#include <cmath>
#include <algorithm>
#include <set>
#include <cstdio>
#include <cstdlib>

namespace {
const double MAIN_CLOCK = 21477272.0;
const int LINES = 256;
const double CYC_PER_LINE = MAIN_CLOCK / 60.0 / LINES;
const int OUT_RATE = 48000;
}

// ---------------------------------------------------------------- setup

bool XavixCore::load(const std::vector<RomFile>& roms, std::string& error)
{
    for (const auto& r : roms) {
        if (r.name == m_cfg.romName) {
            if (r.data.size() < m_cfg.romSize) { error = m_cfg.romName + " is too small"; return false; }
            m_romData.assign(r.data.begin(), r.data.begin() + m_cfg.romSize);
        }
    }
    if (m_romData.empty()) { error = "Missing " + m_cfg.romName; return false; }
    m_rom = m_romData.data();
    m_romLen = uint32_t(m_romData.size());
    m_snd.setCallbacks(this, sndRegs, sndRegsW, sndSamples);
    reset();
    return true;
}

void XavixCore::reset()
{
    // the EEPROM keeps its contents over a reset
    static uint8_t keep[sizeof(m_s.i2c.data)];
    bool had = m_eepromInit;
    if (had) std::memcpy(keep, m_s.i2c.data, sizeof(keep));
    std::memset(&m_s, 0, sizeof(m_s));
    m_s.serIdx = -1;
    std::memset(m_s.ram, 0xff, sizeof(m_s.ram));
    m_s.soundRegbase = 0x02;
    m_s.timerLeft = -1;
    m_s.adcLatch = 0xff;
    m_s.rng = 0x12345678;
    if (had) std::memcpy(m_s.i2c.data, keep, sizeof(keep));
    else std::memset(m_s.i2c.data, 0xff, sizeof(m_s.i2c.data));
    m_eepromInit = true;
    m_s.i2c.sdar = 1;
    for (auto& p : m_pens) p = 0xff000000;
    std::fill(std::begin(m_out), std::end(m_out), 0xff000000u);
    m_snd.reset(0x80);

    std::memset(&m_cpu, 0, sizeof(m_cpu));
    m_cpu.sstackpos = 0xff;
    m_cpu.s = 0xfd;
    m_cpu.p = 0x34;
    m_cpu.pc = fetch(0xfffc) | (fetch(0xfffd) << 8);
    m_audio.clear();
    m_cycle = 0;
}

// ---------------------------------------------------------------- low bus

uint8_t XavixCore::txarray(uint16_t o) const
{
    if (o < 0x100) { o &= 0xff; return uint8_t((o >> 4) | (o << 4)); }
    if (o < 0x200) { o &= 0xff; return uint8_t((o >> 4) | (~o << 4)); }
    if (o < 0x300) { o &= 0xff; return uint8_t(((~o >> 4) & 0x0f) | (o << 4)); }
    if (o < 0x400) { o &= 0xff; return uint8_t(((~o >> 4) & 0x0f) | (~o << 4)); }
    if (o < 0x800) return m_s.txarray[0];
    if (o < 0xc00) return m_s.txarray[1];
    if (o < 0x1000) return m_s.txarray[2];
    return 0xff;
}

uint8_t XavixCore::lowBypass(uint16_t a)
{
    a &= 0x7fff;
#ifdef XAVIX_DEBUG
    static int wlo = -2, whi = 0;
    if (wlo == -2) { const char* e = getenv("XRDW"); if (e) { sscanf(e, "%x-%x", &wlo, &whi); } else wlo = -1; }
    if (wlo >= 0 && a >= wlo && a <= whi) { static std::set<uint32_t> seen; uint32_t k = (m_cpu.codebank << 16) | m_cpu.pc; if (seen.insert(k).second) fprintf(stderr, "read %04x pc %02x:%04x\n", a, m_cpu.codebank, m_cpu.pc); }
#endif
    if (a < 0x4000) return m_s.ram[a];
    if (a < 0x5000) return txarray(a & 0xfff);
    if (a >= 0x6000 && a < 0x6800) return m_s.sprite[a & 0x7ff];
    if (a >= 0x6800 && a < 0x6900) return m_s.palSh[a & 0xff];
    if (a >= 0x6900 && a < 0x6a00) return m_s.palL[a & 0xff];
    if (a >= 0x6a00 && a < 0x6a20) return m_s.segment[a & 0x1f];
    return 0;
}

uint8_t XavixCore::readBypass(uint32_t a)
{
    uint8_t bank = uint8_t(a >> 16);
    if (bank >= 0x80 || (a & 0xffff) >= 0x8000) return rom(a);
    return lowBypass(uint16_t(a));
}

// ---------------------------------------------------------------- 24Cxx EEPROM (MAME i2cmem)

enum { I2C_IDLE, I2C_DEVSEL, I2C_ADDRHIGH, I2C_ADDRLOW, I2C_DATAIN, I2C_READSELACK, I2C_DATAOUT, I2C_RESET };

void XavixCore::i2cSda(int st)
{
    I2c& e = m_s.i2c;
    st &= 1;
    if (e.sdaw == st) return;
    e.sdaw = uint8_t(st);
    if (e.scl && e.sdar) {
        if (e.sdaw) {
            if (e.pageWritten > 0) {
                int mask = m_cfg.i2cSize - 1;
                int base = e.byteaddr & mask;
                int root = base & ~(m_cfg.i2cPage - 1);
                for (int i = 0; i < e.pageWritten; i++)
                    e.data[(root | ((base + i) & (m_cfg.i2cPage - 1))) & mask] = e.page[i];
                e.pageWritten = 0;
            }
            e.state = I2C_IDLE;
        } else {
            e.state = I2C_DEVSEL;
            e.bits = 0;
        }
    }
}

void XavixCore::i2cScl(int st)
{
    I2c& e = m_s.i2c;
    st &= 1;
    if (e.scl == st) return;
    e.scl = uint8_t(st);
    int mask = m_cfg.i2cSize - 1;
    switch (e.state) {
    case I2C_DEVSEL: case I2C_ADDRHIGH: case I2C_ADDRLOW: case I2C_DATAIN:
        if (e.bits < 8) {
            if (e.scl) { e.shift = uint8_t((e.shift << 1) | e.sdaw); e.bits++; }
        } else if (e.scl) {
            e.bits++;
        } else if (e.bits == 8) {
            switch (e.state) {
            case I2C_DEVSEL: {
                e.devsel = e.shift;
                int amask = 0xfe & ~(mask >> 7);
                if (e.devsel == 0) e.state = I2C_RESET;
                else if ((e.devsel & amask) != (0xa0 & amask)) e.state = I2C_IDLE;
                else if (!(e.devsel & 1)) e.state = I2C_ADDRLOW;   // parts up to 2 KB: no high address byte
                else e.state = I2C_READSELACK;
                break; }
            case I2C_ADDRHIGH:
                e.addresshigh = e.shift; e.state = I2C_ADDRLOW; break;
            case I2C_ADDRLOW:
                e.byteaddr = (e.shift | (((e.devsel & 0xfe) << 7) & mask));
                e.pageOffset = 0; e.pageWritten = 0;
                e.state = I2C_DATAIN;
                break;
            case I2C_DATAIN:
                e.page[e.pageOffset] = e.shift;
                if (++e.pageOffset == m_cfg.i2cPage) e.pageOffset = 0;
                if (++e.pageWritten > m_cfg.i2cPage) e.pageWritten = m_cfg.i2cPage;
                break;
            }
            if (e.state != I2C_IDLE) e.sdar = 0;
        } else {
            e.bits = 0;
            e.sdar = 1;
        }
        break;
    case I2C_READSELACK:
        e.bits = 0; e.state = I2C_DATAOUT;
        break;
    case I2C_DATAOUT:
        if (e.bits < 8) {
            if (e.scl) e.bits++;
            else {
                if (e.bits == 0) { e.shift = e.data[e.byteaddr & mask]; e.byteaddr++; }
                e.sdar = (e.shift >> 7) & 1;
                e.shift = uint8_t(e.shift << 1);
            }
        } else {
            if (e.scl) { if (e.sdaw) e.state = I2C_IDLE; e.bits = 0; }
            else e.sdar = 1;
        }
        break;
    case I2C_RESET:
        if (e.scl) { if (e.bits > 8) { e.state = I2C_IDLE; e.sdar = 1; } e.bits++; }
        break;
    }
}

void XavixCore::io1Lines()
{
    if (!m_cfg.i2cSize) return;
    uint8_t dir = m_s.io1Dir, d = m_s.io1Data;
    if (m_cfg.superXavix) {
        // SSD 2002 boards: undriven lines float high
        uint8_t v = uint8_t(d | ~dir);
        i2cSda(v >> m_cfg.sdaBit & 1);
        i2cScl(v >> m_cfg.sclBit & 1);
        return;
    }
    i2cSda((dir & 0x08) ? (d >> 3 & 1) : 1);
    i2cScl((dir & 0x10) ? (d >> 4 & 1) : 0);
}

uint8_t XavixCore::ioRead(int port)
{
    uint8_t in = port ? uint8_t((m_inputs >> 8) | m_cfg.in1Default) : uint8_t(m_inputs);
    if (port && m_cfg.lotrIo) {
        m_s.rng = m_s.rng * 1103515245u + 12345u;
        in = uint8_t((in & ~0x06) | ((m_s.rng >> 16) & 0x06));
    }
    if (port && m_cfg.camera && !m_cfg.camOnEx) in = uint8_t((in & ~0x06) | cameraClock());
    if (port && m_cfg.drumSerial) in = uint8_t((in & ~0x01) | m_s.serLine);
    if (port && m_cfg.i2cSize) {
        int sda = (m_s.i2c.state == I2C_IDLE) ? m_s.i2c.sdaw : m_s.i2c.sdar;
        in = uint8_t((in & ~(1 << m_cfg.sdaBit)) | (sda << m_cfg.sdaBit));
    }
    uint8_t dir = port ? m_s.io1Dir : m_s.io0Dir;
    uint8_t data = port ? m_s.io1Data : m_s.io0Data;
    return uint8_t((in & ~dir) | (data & dir));
}

uint8_t XavixCore::lowRead(uint16_t a)
{
    a &= 0x7fff;
#ifdef XAVIX_DEBUG
    static int wlo = -2, whi = 0;
    if (wlo == -2) { const char* e = getenv("XRDW"); if (e) { sscanf(e, "%x-%x", &wlo, &whi); } else wlo = -1; }
    if (wlo >= 0 && a >= wlo && a <= whi) { static std::set<uint32_t> seen; uint32_t k = (m_cpu.codebank << 16) | m_cpu.pc; if (seen.insert(k).second) fprintf(stderr, "read %04x pc %02x:%04x\n", a, m_cpu.codebank, m_cpu.pc); }
#endif
    if (a < 0x4000) return m_s.ram[a];
    if (a < 0x5000) return txarray(a & 0xfff);
    if (a < 0x6000) return 0;
    if (a < 0x6800) return m_s.sprite[a & 0x7ff];
    if (a < 0x6900) return m_s.palSh[a & 0xff];
    if (a < 0x6a00) return m_s.palL[a & 0xff];
    if (a < 0x6a20) return m_s.segment[a & 0x1f];
    if (a >= 0x7400 && a < 0x7580) return m_s.soundRam[a - 0x7400];
    if (m_cfg.superXavix) {
        if (a >= 0x6f78 && a <= 0x6f7f) return sxPalRead(a - 0x6f78);
        if ((a >= 0x6a40 && a < 0x6a80) || (a >= 0x6c00 && a < 0x6e00) || (a >= 0x6f60 && a < 0x6fc8)) return m_s.sxRam[a - 0x6a00];
        if (a >= 0x7a10 && a <= 0x7a12) return m_s.sxDir[a - 0x7a10];
        if (a >= 0x7a20 && a <= 0x7a22) return m_s.sxOut[a - 0x7a20];
        if (a >= 0x7a30 && a <= 0x7a32) {
            int i = a - 0x7a30;
            uint8_t in = uint8_t(m_cfg.exDefault[i] | (i == 0 ? (m_inputs >> 16) : 0));
            if (i == 1 && m_cfg.camera && m_cfg.camOnEx) in = uint8_t((in & ~0x04) | (cameraClock() & 0x04));
            return uint8_t((in & ~m_s.sxDir[i]) | (m_s.sxOut[i] & m_s.sxDir[i]));
        }
    }
    switch (a) {
    case 0x6fc8: case 0x6fc9: case 0x6fca: case 0x6fcb: case 0x6fcc: case 0x6fcd: case 0x6fce: case 0x6fcf:
        return m_s.tmap1[a & 7];
    case 0x6fd0: case 0x6fd1: case 0x6fd2: case 0x6fd3: case 0x6fd4: case 0x6fd5: case 0x6fd6: case 0x6fd7:
        return m_s.tmap2[a & 7];
    case 0x6fd8: return m_s.spritereg;
    case 0x6fe0: return 0;
    case 0x6fe8: return m_s.arenaStart;
    case 0x6fe9: return m_s.arenaEnd;
    case 0x6fea: m_s.arenaControl ^= 0x40; return m_s.arenaControl;
    case 0x6ff0: return m_s.colmixSh;
    case 0x6ff1: return m_s.colmixL;
    case 0x6ff2: return m_s.colmixCtrl;
    case 0x6ff8: return m_s.videoCtrl;
    case 0x6ff9: return 0x00;           // NTSC
    case 0x6ffa: return m_s.posirqX;
    case 0x6ffb: return m_s.posirqY;
    case 0x6ffc: case 0x6ffd: case 0x6ffe: case 0x6fff: return 0xff;
    case 0x75f0: case 0x75f1: return m_s.soundStartStop[a & 1];
    case 0x75f2: case 0x75f3: return 0;
    case 0x75f4: case 0x75f5: {
        soundSync();
        uint8_t r = 0;
        for (int i = 0; i < 8; i++) if (m_snd.voiceEnabled((a & 1) * 8 + i)) r |= 1 << i;
        return r; }
    case 0x75f6: return m_snd.volumeR();
    case 0x75f7: return m_s.soundRegbase & 0x3f;
    case 0x75f8: return m_s.cyclerate;
    case 0x75f9: return m_snd.mixerR();
    case 0x75fa: case 0x75fb: case 0x75fc: case 0x75fd: return m_s.tp[a - 0x75fa];
    case 0x75fe: return m_s.soundIrqStatus;
    case 0x75ff: return m_snd.dacR();
    case 0x7900: case 0x7901: case 0x7902: return m_s.extbus[a - 0x7900];
    case 0x7980: return 0;
    case 0x7981: case 0x7982: case 0x7983: return m_s.dmaSrc[a - 0x7981];
    case 0x7984: case 0x7985: return m_s.dmaDst[a - 0x7984];
    case 0x7986: case 0x7987: return m_s.dmaLen[a - 0x7986];
    case 0x7a00: return ioRead(0);
    case 0x7a01: return ioRead(1);
    case 0x7a02: return m_s.io0Dir;
    case 0x7a03: return m_s.io1Dir;
    case 0x7a80: return m_s.ioeventEnable;
    case 0x7a81: return m_s.ioeventActive;
    case 0x7b00: case 0x7b01: case 0x7b10: case 0x7b11: return 0xff;
    case 0x7b80: return m_s.adcLatch;
    case 0x7b81: m_s.rng = m_s.rng * 1103515245u + 12345u; return uint8_t(m_s.rng >> 16);
    case 0x7c00: return m_s.timerControl;
    case 0x7c01: return m_s.timerBase;
    case 0x7c02: return m_s.timerFreq;
    case 0x7c03: return m_s.timerCur;
    case 0x7ff0: return uint8_t((m_s.sgnMpd ? 0x80 : 0) | (m_s.mpr & 0x40) | (m_s.mpr & 0x07));
    case 0x7ff1: return (m_s.mpr & 0x40) ? uint8_t(m_s.mult[1] ^ m_s.mult[0]) : (m_s.mpr & 0x08) ? m_s.mult[1] : m_s.mult[0];
    case 0x7ff2: return uint8_t(m_s.mad | (m_s.sgnMpd ? 2 : 0) | m_s.sgnMpr);
    case 0x7ff3: return m_s.mpd;
    case 0x7ff4: return m_s.mpr;
    case 0x7ff5: case 0x7ff6: return m_s.mult[a - 0x7ff5];
    case 0x7ffa: return m_s.nmiLo;
    case 0x7ffb: return m_s.nmiHi;
    case 0x7ffc: return m_s.irqSource;
    case 0x7ffe: return m_s.irqLo;
    case 0x7fff: return m_s.irqHi;
    }
    return 0;
}

void XavixCore::lowWrite(uint16_t a, uint8_t d)
{
    a &= 0x7fff;
#ifdef XAVIX_DEBUG
    static int wlo = -2, whi = 0;
    if (wlo == -2) { const char* e = getenv("XWRW"); if (e) { sscanf(e, "%x-%x", &wlo, &whi); } else wlo = -1; }
    if (wlo >= 0 && a >= wlo && a <= whi) { static std::set<uint32_t> seen; uint32_t k = (m_cpu.codebank << 16) | m_cpu.pc; if (seen.insert(k).second) fprintf(stderr, "write %04x=%02x pc %02x:%04x\n", a, d, m_cpu.codebank, m_cpu.pc); }
#endif
    if (a < 0x4000) { m_s.ram[a] = d; return; }
    if (a < 0x5000) {
        uint16_t o = a & 0xfff;
        if (o >= 0x400) m_s.txarray[(o - 0x400) >> 10] = d;
        return;
    }
    if (a < 0x6000) return;
    if (a < 0x6800) { spriteramWrite(a & 0x7ff, d); return; }
    if (a < 0x6900) { m_s.palSh[a & 0xff] = d; updatePen(a & 0xff); renderUpTo(m_vpos); return; }
    if (a < 0x6a00) { m_s.palL[a & 0xff] = d; updatePen(a & 0xff); renderUpTo(m_vpos); return; }
    if (a < 0x6a20) { m_s.segment[a & 0x1f] = d; return; }
    if (a >= 0x7400 && a < 0x7580) { m_s.soundRam[a - 0x7400] = d; return; }
    if (m_cfg.superXavix) {
        if (a >= 0x6f78 && a <= 0x6f7f) { sxPalWrite(a - 0x6f78, d); renderUpTo(m_vpos); return; }
        if ((a >= 0x6a40 && a < 0x6a80) || (a >= 0x6c00 && a < 0x6e00) || (a >= 0x6f60 && a < 0x6fc8)) {
            m_s.sxRam[a - 0x6a00] = d;
            if (a >= 0x6c00 && a < 0x6e00) { updatePen(0x100 + (a & 0xff)); renderUpTo(m_vpos); }
            return;
        }
        if (a >= 0x7a10 && a <= 0x7a12) { m_s.sxDir[a - 0x7a10] = d; return; }
        if (a >= 0x7a20 && a <= 0x7a22) { m_s.sxOut[a - 0x7a20] = d; return; }
    }
    switch (a) {
    case 0x6fc8: case 0x6fc9: case 0x6fca: case 0x6fcb: case 0x6fcc: case 0x6fcd: case 0x6fce: case 0x6fcf:
        m_s.tmap1[a & 7] = d; break;
    case 0x6fd0: case 0x6fd1: case 0x6fd2: case 0x6fd3: case 0x6fd4: case 0x6fd5: case 0x6fd6: case 0x6fd7:
        m_s.tmap2[a & 7] = d; break;
    case 0x6fd8: m_s.spritereg = d; break;
    case 0x6fe0: spriteDma(d); break;
    case 0x6fe1: case 0x6fe2: m_s.sfDma1[a - 0x6fe1] = d; break;
    case 0x6fe5: case 0x6fe6: m_s.sfDma2[a - 0x6fe5] = d; break;
    case 0x6fe8: m_s.arenaStart = d; break;
    case 0x6fe9: m_s.arenaEnd = d; break;
    case 0x6fea: m_s.arenaControl = d & 0x7f; break;
    case 0x6ff0: m_s.colmixSh = d; break;
    case 0x6ff1: m_s.colmixL = d; break;
    case 0x6ff2: m_s.colmixCtrl = d; break;
    case 0x6ff8: videoCtrlWrite(d); break;
    case 0x6ffa: m_s.posirqX = d; break;
    case 0x6ffb: m_s.posirqY = d; m_s.posArmed = 1; break;
    case 0x75f0: case 0x75f1: {
        soundSync();
        int o = a & 1;
        for (int i = 0; i < 8; i++) {
            int now = d >> i & 1, old = m_s.soundStartStop[o] >> i & 1;
            if (now != old) { if (now) m_snd.enableVoice(o * 8 + i, false); else m_snd.disableVoice(o * 8 + i); }
        }
        m_s.soundStartStop[o] = d;
        break; }
    case 0x75f2: case 0x75f3:
        soundSync();
        for (int i = 0; i < 8; i++) if (d >> i & 1) m_snd.enableVoice((a & 1) * 8 + i, true);
        break;
    case 0x75f6: soundSync(); m_snd.volumeW(d); break;
    case 0x75f7: m_s.soundRegbase = d & 0x3f; break;
    case 0x75f8: soundSync(); m_s.cyclerate = d; m_snd.setCyclerate(d); break;
    case 0x75f9: soundSync(); m_snd.mixerW(d); break;
    case 0x75fa: case 0x75fb: case 0x75fc: case 0x75fd: {
        int i = a - 0x75fa;
        soundSync();
        m_s.tp[i] = d;
        m_snd.setTempo(i, d);
        reprogramSoundTimer(i);
        break; }
    case 0x75fe: soundIrqStatusWrite(d); break;
    case 0x75ff: soundSync(); m_snd.dacW(d); break;
    case 0x7900: case 0x7901: case 0x7902: m_s.extbus[a - 0x7900] = d; break;
    case 0x7980: romDma(d); break;
    case 0x7981: case 0x7982: case 0x7983: m_s.dmaSrc[a - 0x7981] = d; break;
    case 0x7984: case 0x7985: m_s.dmaDst[a - 0x7984] = d; break;
    case 0x7986: case 0x7987: m_s.dmaLen[a - 0x7986] = d; break;
    case 0x7a00: m_s.io0Data = d; break;
    case 0x7a01: m_s.io1Data = d; io1Lines(); break;
    case 0x7a02: m_s.io0Dir = d; break;
    case 0x7a03: m_s.io1Dir = d; io1Lines(); break;
    case 0x7a80:
        m_s.ioeventEnable = d;
        if (!(d & 0x01) && m_cfg.drumSerial) { m_s.ioeventActive &= ~0x01; if (!(m_s.ioeventActive & 0x0f)) m_s.irqSource &= ~0x08; updateIrqs(); }
        if (m_cfg.drumSerial && (d & 0x01)) drumKick();
        break;
    case 0x7a81:
        m_s.ioeventActive &= ~(d & 0x0f);
        if (!(m_s.ioeventActive & 0x0f)) m_s.irqSource &= ~0x08;
        updateIrqs();
        break;
    case 0x7b81:
#ifdef XAVIX_DEBUG
        if (getenv("XADC")) { static uint32_t n = 0; if ((n++ % 500) == 0) fprintf(stderr, "adc %02x pc %02x:%04x n=%u\n", d, m_cpu.codebank, m_cpu.pc, n); }
#endif
        m_s.adcControl = d;
        m_s.adcLatch = 0;          // the analogue inputs are unused (0)
        if (m_cfg.camera && !(d & 0x13)) {
            // each row: one conversion to start, then 32 pixels
            uint32_t c = m_s.camCount++;
#ifdef XAVIX_DEBUG
            if (getenv("XCAM") && c % 66 == 0) fprintf(stderr, "conv %u clock %u\n", c, m_s.camClock % 4010);
#endif
            int row = int(c / 33), col = int(c % 33) - 1;
            m_s.adcLatch = (col < 0 || row > 31) ? 0 : cameraPixel(row, col, m_s.camLed);
        }
        break;
    case 0x7c00: timerControlWrite(d); break;
    case 0x7c01: m_s.timerBase = d; break;
    case 0x7c02: m_s.timerFreq = d & 0x0f; break;
    case 0x7ff0:
        m_s.mad = 0; m_s.sgnMpd = d & 0x80; m_s.sgnMpr = 0; m_s.mpr = d;
        mathDo(true);
        break;
    case 0x7ff1: m_s.mpd = d; break;
    case 0x7ff2: m_s.mad = d & 0x80; m_s.sgnMpd = d & 0x02; m_s.sgnMpr = d & 0x01; break;
    case 0x7ff3: m_s.mpd = d; break;
    case 0x7ff4: m_s.mpr = d; mathDo(false); break;
    case 0x7ff5: case 0x7ff6: m_s.mult[a - 0x7ff5] = d; break;
    case 0x7ff9: m_s.vectorEnable = d; break;
    case 0x7ffa: m_s.nmiLo = d; break;
    case 0x7ffb: m_s.nmiHi = d; break;
    case 0x7ffe: m_s.irqLo = d; break;
    case 0x7fff: m_s.irqHi = d; break;
    }
}

void XavixCore::mathDo(bool mulShf)
{
    uint16_t result;
    uint8_t mpd = m_s.mpd;
    uint8_t mulinp = mulShf ? uint8_t(1 << (m_s.mpr & 7)) : m_s.mpr;
    if (m_s.sgnMpd && (mpd & 0x80) && m_s.sgnMpr && (mulinp & 0x80)) result = uint16_t(mpd * mulinp - (mpd << 8) - (mulinp << 8));
    else if (m_s.sgnMpd && (mpd & 0x80)) result = uint16_t(mpd * mulinp - (mulinp << 8));
    else if (m_s.sgnMpr && (mulinp & 0x80)) result = uint16_t(mpd * mulinp - (mpd << 8));
    else result = uint16_t(mpd * mulinp);
    if (m_s.mad) result = uint16_t(((m_s.mult[1] << 8) | m_s.mult[0]) + result);
    m_s.mult[1] = result >> 8;
    m_s.mult[0] = result & 0xff;
}

// ---------------------------------------------------------------- camera

// the camera streams frames continuously: a sync (clock held high for 8 reads),
// then the pixel clock toggling on every read; pixels count from the sync and
// the LEDs' state at the sync decides whether the stickers light up
uint8_t XavixCore::cameraClock()
{
    // bit 1: frame sync, high for the 8 reads of the sync; bit 2: pixel clock,
    // held high through the sync and toggling on every read after it
    const uint32_t FRAME = 2 + 8 + 4000;
    uint32_t p = m_s.camClock++ % FRAME;
    if (p < 2) return 0;
    if (p < 10) {
        if (p == 2) {
            m_s.camCount = 0;
            m_s.camLed = m_cfg.camOnEx ? (m_s.sxOut[1] >> 5 & 1) : (m_s.io1Data >> 5 & 1);
#ifdef XAVIX_DEBUG
            if (getenv("XCAM")) fprintf(stderr, "sync led=%d io1=%02x pc=%04x\n", m_s.camLed, m_s.io1Data, m_cpu.pc);
#endif
        }
        return 0x06;
    }
    return (p & 1) ? 0x04 : 0;
}

// two reflective stickers (the gloves / the sword) as soft round blobs that only
// show while the LEDs are lit; a punch brings the sticker closer, so it grows
uint8_t XavixCore::cameraPixel(int row, int col, bool led)
{
    int v = 0x18 + ((row * 7 + col * 13) & 7);
    if (!led) return uint8_t(v);
    // hands held together read as one shape (a short bar between them); apart, two spots
    if (m_cfg.camBar && std::hypot((m_bx[0] - m_bx[1]) * 14.0f, (m_by[0] - m_by[1]) * 13.0f) < 7.0f) {
        float ax = 15.5f + m_bx[0] * 14.0f, ay = 15.0f + m_by[0] * 13.0f;
        float bx = 15.5f + m_bx[1] * 14.0f, by = 15.0f + m_by[1] * 13.0f;
        float dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy;
        float t = l2 > 0 ? ((col - ax) * dx + (row - ay) * dy) / l2 : 0;
        t = std::max(0.0f, std::min(1.0f, t));
        float ex = col - (ax + t * dx), ey = row - (ay + t * dy);
        float d2 = ex * ex + ey * ey;
        if (d2 < 2.25f) v += int(200 * (1.0f - d2 / 3.6f));
        return uint8_t(v > 255 ? 255 : v);
    }
    for (int g = 0; g < m_cfg.camBlobs; g++) {
        float px = m_bx[g], py = m_by[g] - (m_cfg.camBlobs == 1 ? 0.0f : 0.6f) * m_reach[g];   // the arm reaches up and out
        float cx = 15.5f + px * 14.0f;
        float cy = 15.0f + py * 13.0f;
        // a sword sticker seen from across the room covers only a few pixels: Onmyou Taisenki
        // takes 16+ lit pixels for half a second as the drive held up to the unit (its menu);
        // there the punch input stands for that, growing the spot
        float r = m_cfg.camBlobs == 1 ? 1.3f + 2.2f * m_reach[g] : 2.5f + m_reach[g];
        float dx = col - cx, dy = row - cy;
        float d2 = dx * dx + dy * dy;
        if (d2 < r * r) v += int(200 * (1.0f - d2 / (r * r * 1.6f)));
    }
    return uint8_t(v > 255 ? 255 : v);
}

// ---------------------------------------------------------------- drum unit

// The drum unit sends a byte per stick while a hit lasts; the game decodes bits 6-1
// through a table into a code whose low bit is the stick, and wants each code
// twice in a row before it counts. Head hits send 0x1x codes, rim hits 0xEx.
// (Worked out from the game: MAME has no drum unit, so the battles stall there.)
void XavixCore::drumFrame()
{
    // bit 1 of a code marks a real strike (0x10 / 0x11 alone don't count); bit 2 a hard one
    static uint8_t headByte[2] = { 0x2d, 0x2b };   // codes 0x16 / 0x17
    static uint8_t rimByte[2]  = { 0x15, 0x13 };   // codes 0xe6 / 0xe7
#ifdef XAVIX_DEBUG
    if (const char* e = getenv("XDRUM")) { unsigned a, b; if (sscanf(e, "%x,%x", &a, &b) == 2) { headByte[0] = a; headByte[1] = b; } }
#endif
    if (m_s.serQLen == 0 && m_s.serIdx < 0) {
        // stick 0 = left (head 0x02, rim 0x08), stick 1 = right (head 0x01, rim 0x04)
        for (int g = 0; g < 2; g++) {
            bool head = m_inputs & (g == 0 ? 0x02 : 0x01), rim = m_inputs & (g == 0 ? 0x08 : 0x04);
            if (head) m_s.serQueue[m_s.serQLen++] = headByte[g];
            else if (rim) m_s.serQueue[m_s.serQLen++] = rimByte[g];
        }
    }
    drumKick();
}

void XavixCore::drumKick()
{
    if (m_s.serIdx >= 0 || m_s.serQLen == 0 || !(m_s.ioeventEnable & 0x01)) return;
    m_s.serByte = m_s.serQueue[0];
    for (int i = 1; i < m_s.serQLen; i++) m_s.serQueue[i - 1] = m_s.serQueue[i];
    m_s.serQLen--;
    m_s.serIdx = 0;
    m_s.serLine = 0;                       // the start edge
    m_s.ioeventActive |= 0x01;
    m_s.irqSource |= 0x08;
    updateIrqs();
}

// ---------------------------------------------------------------- devices

void XavixCore::updateIrqs() { m_cpu.irqLine = m_s.irqSource != 0; }

void XavixCore::romDma(uint8_t data)
{
    if ((data & 0x80) && (m_s.irqSource & 0x20)) { m_s.irqSource &= ~0x20; updateIrqs(); }
    if (data & 0x01) {
        uint32_t src = (m_s.dmaSrc[2] << 16) | (m_s.dmaSrc[1] << 8) | m_s.dmaSrc[0];
        uint16_t dst = (m_s.dmaDst[1] << 8) | m_s.dmaDst[0];
        uint16_t len = (m_s.dmaLen[1] << 8) | m_s.dmaLen[0];
        for (int i = 0; i < len; i++) {
            uint8_t d = readBypass(src + i);
            uint32_t ad = uint32_t(dst) + i;
            writeFull(uint8_t(ad >> 16), uint16_t(ad), d);
        }
        if (data & 0x40) { m_s.irqSource |= 0x20; updateIrqs(); }
        m_s.dmaLen[0] = m_s.dmaLen[1] = 0;
    }
}

void XavixCore::spriteramHighX(uint16_t o, uint8_t d)
{
    o &= 0xff;
    m_s.sprite[o] = uint8_t((m_s.sprite[o] & 0xfe) | (d ? 1 : 0));
    m_s.sprite[0x400 + o] = d ? 1 : 0;
}

void XavixCore::spriteramWrite(uint16_t o, uint8_t d)
{
    if (o < 0x100) { m_s.sprite[o] = d & 0xfe; spriteramHighX(o, d & 1); }
    else if (o < 0x300) m_s.sprite[o] = d;
    else if (o < 0x400) { m_s.sprite[o] = d; spriteramHighX(o, d & 0x80); }
    else if (o < 0x500) spriteramHighX(o, d & 1);
    else m_s.sprite[o] = d;
}

void XavixCore::spriteDma(uint8_t data)
{
    uint16_t len = data & 7;
    uint16_t src = (m_s.sfDma1[1] << 8) | m_s.sfDma1[0];
    uint16_t dst = m_s.sfDma2[0] << 8;
    uint8_t blk = uint8_t(m_s.sfDma2[1] - 1);
    if (len == 0) len = 8;
    len <<= 8;
    if (data & 0x40) {
        int ra = 0;
        for (int i = 0; i < len; i++) {
            if ((i & 0xff) > blk) continue;
            spriteramWrite((dst + i) & 0x7ff, readBypass(uint16_t(src + ra)));
            ra++;
        }
    }
}

void XavixCore::videoCtrlWrite(uint8_t data)
{
    if (data & 0x40) { m_s.irqSource &= ~0x40; updateIrqs(); }
    if (data & 0x80) m_cpu.nmiLine = 0;
    uint8_t old = m_s.videoCtrl;
    m_s.videoCtrl = data & 0x3f;
    if ((old & 0x10) != (m_s.videoCtrl & 0x10) && !(m_s.videoCtrl & 0x10)) {
        m_s.videoCtrl &= ~0x40;
        m_s.irqSource &= ~0x40;
        updateIrqs();
    }
}

void XavixCore::posIrq()
{
    if (m_s.videoCtrl & 0x10) {
        m_s.videoCtrl |= 0x40;
        m_s.irqSource |= 0x40;
        updateIrqs();
        renderUpTo(m_vpos);
    }
}

void XavixCore::timerStart() { m_s.timerLeft = int64_t(1) << ((m_s.timerFreq & 0x0f) + 1); }

void XavixCore::timerControlWrite(uint8_t data)
{
    m_s.timerControl = data;
    if ((data & 0x80) && (m_s.irqSource & 0x10)) { m_s.irqSource &= ~0x10; updateIrqs(); }
    if (data & 0x01) { m_s.timerCur = m_s.timerBase; timerStart(); }
    else m_s.timerLeft = -1;
}

void XavixCore::timerDone()
{
    m_s.timerCur--;
    m_s.timerLeft = -1;
    if (m_s.timerCur == 0xff) {
        if (m_cfg.drumSerial && m_s.serIdx >= 0) {
            // the game samples one bit per timer interrupt: start 0, six data bits, stop 1
            m_s.serLine = (m_s.serByte >> (7 - m_s.serIdx)) & 1;
            if (++m_s.serIdx >= 8) m_s.serIdx = -1;
        }
        if (m_s.timerControl & 0x40) {
            m_s.irqSource |= 0x10;
            m_s.timerControl |= 0x80;
            updateIrqs();
        }
    } else timerStart();
}

void XavixCore::refreshSoundIrq()
{
    uint8_t en = m_s.soundIrqStatus & 0x0f, pend = m_s.soundIrqStatus >> 4;
    if (en & pend) m_s.irqSource |= 0x80; else m_s.irqSource &= ~0x80;
}

void XavixCore::soundIrqStatusWrite(uint8_t data)
{
    uint8_t oldEn = m_s.soundIrqStatus & 0x0f;
    uint8_t clr = data >> 4;
    if (clr) m_s.soundIrqStatus &= ~(clr << 4);
    uint8_t newEn = data & 0x0f;
    m_s.soundIrqStatus = (m_s.soundIrqStatus & 0xf0) | newEn;
    uint8_t changed = oldEn ^ newEn;
    for (int t = 0; t < 4; t++) if (changed & (1 << t)) reprogramSoundTimer(t);
    refreshSoundIrq();
    updateIrqs();
}

void XavixCore::reprogramSoundTimer(int i)
{
    m_s.soundTimerPeriod[i] = 0;
    if (!(m_s.soundIrqStatus & (1 << i)) || m_s.tp[i] == 0) return;
    double f = m_snd.tempoTickHz(m_s.tp[i]);
    if (f <= 0) return;
    m_s.soundTimerPeriod[i] = int64_t((m_cfg.superXavix ? 2 : 1) * MAIN_CLOCK / f * 65536.0);
    m_s.soundTimerLeft[i] = m_s.soundTimerPeriod[i];
}

void XavixCore::soundTimerDone(int i)
{
    if (!(m_s.soundIrqStatus >> i & 1)) return;
    m_s.soundIrqStatus |= 0x10 << i;
    refreshSoundIrq();
    updateIrqs();
}

void XavixCore::advance(int cycles)
{
    m_cycle += cycles;
    m_s.sndCycles += cycles;
    if (m_s.timerLeft >= 0) {
        m_s.timerLeft -= cycles;
        if (m_s.timerLeft <= 0) timerDone();
    }
    for (int i = 0; i < 4; i++) {
        if (!m_s.soundTimerPeriod[i]) continue;
        m_s.soundTimerLeft[i] -= int64_t(cycles) << 16;
        while (m_s.soundTimerPeriod[i] && m_s.soundTimerLeft[i] <= 0) {
            m_s.soundTimerLeft[i] += m_s.soundTimerPeriod[i];
            soundTimerDone(i);
        }
    }
}

// ---------------------------------------------------------------- sound glue

uint8_t XavixCore::sndRegs(void* c, uint32_t a)
{
    XavixCore* x = static_cast<XavixCore*>(c);
    uint8_t page = x->m_s.soundRegbase & 0x3f;
    if (!page) return 0;
    return x->m_s.ram[((page << 8) + (a & 0xff)) & 0x3fff];
}

void XavixCore::sndRegsW(void* c, uint32_t a, uint8_t d)
{
    XavixCore* x = static_cast<XavixCore*>(c);
    uint8_t page = x->m_s.soundRegbase & 0x3f;
    if (page) x->m_s.ram[((page << 8) + (a & 0xff)) & 0x3fff] = d;
}

uint8_t XavixCore::sndSamples(void* c, uint32_t a) { return static_cast<XavixCore*>(c)->readBypass(a); }

// the sequencer runs at master clock / 128; its samples are averaged down to 48 kHz
void XavixCore::soundSync()
{
    const int div = m_cfg.superXavix ? 256 : 128;
    while (m_s.sndCycles >= div) {
        m_s.sndCycles -= div;
        int32_t l, r;
        m_snd.tick(l, r);
        m_s.outAccL += (l + r) / 2;
        m_s.outAccN++;
        m_s.outPhase += OUT_RATE;
        if (m_s.outPhase >= XavixSound::RATE_HZ) {
            m_s.outPhase -= XavixSound::RATE_HZ;
            int32_t v = m_s.outAccN ? m_s.outAccL / m_s.outAccN : 0;
            m_audio.push_back(int16_t(std::max(-32768, std::min(32767, v))));
            m_s.outAccL = 0; m_s.outAccN = 0;
        }
    }
}

size_t XavixCore::readAudio(int16_t* out, size_t maxSamples)
{
    size_t n = std::min(maxSamples, m_audio.size());
    std::copy(m_audio.begin(), m_audio.begin() + n, out);
    m_audio.erase(m_audio.begin(), m_audio.begin() + n);
    if (m_audio.size() > OUT_RATE / 2) m_audio.clear();
    return n;
}

// ---------------------------------------------------------------- frame

void XavixCore::runFrame()
{
    double frameCycles = 0;
    double done = 0;
    m_rendered = -1;
    if (m_cfg.drumSerial) drumFrame();
    // a punch glides out over a few frames: the games track the sticker from
    // frame to frame and throw away any jump of five camera pixels or more
    for (int g = 0; g < 2; g++) {
        float target = (m_inputs >> (9 + g) & 1) ? 1.0f : 0.0f;
        if (m_reach[g] < target) m_reach[g] = std::min(target, m_reach[g] + 0.2f);
        else if (m_reach[g] > target) m_reach[g] = std::max(target, m_reach[g] - 0.2f);
        // a real sticker moves at arm speed: chase the finger at most ~2.5 camera pixels a frame
        const float STEP = 0.18f;
        m_bx[g] += std::max(-STEP, std::min(STEP, m_ax[g] - m_bx[g]));
        m_by[g] += std::max(-STEP, std::min(STEP, m_ay[g] - m_by[g]));
    }
    for (int line = 0; line < LINES; line++) {
        m_vpos = line;
        if (line == 240) {
            renderUpTo(239);
            // vblank: the picture interrupt (NMI) when it is enabled
            if (m_s.videoCtrl & 0x20) {
                if (!m_cpu.nmiLine) m_cpu.nmiPending = 1;
                m_cpu.nmiLine = 1;
                m_s.videoCtrl |= 0x80;
            }
        }
        double cpl = m_cfg.superXavix ? CYC_PER_LINE * 2 : CYC_PER_LINE;   // the SSD 2002 runs at twice the clock
        double lineStart = line * cpl;
        double lineEnd = (line + 1) * cpl;
        if (m_s.posArmed && line == m_s.posirqY) {
            double at = lineStart + cpl * m_s.posirqX / 256.0;
            while (done < at) { int c = cpuStep(); advance(c); done += c; }
            posIrq();
        }
        while (done < lineEnd) { int c = cpuStep(); advance(c); done += c; }
    }
    frameCycles = done;
#ifdef XAVIX_DEBUG
    if (const char* w = getenv("XWATCH")) {   // "addr,addr,..": print those RAM bytes every frame
        static int wf = 0; wf++;
        fprintf(stderr, "%d:", wf);
        std::string ws(w); size_t p0 = 0;
        while (p0 < ws.size()) { size_t p1 = ws.find(',', p0); if (p1 == std::string::npos) p1 = ws.size(); fprintf(stderr, " %02x", m_s.ram[strtoul(ws.substr(p0, p1 - p0).c_str(), 0, 16) & 0x3fff]); p0 = p1 + 1; }
        fprintf(stderr, "\n");
    }
    if (getenv("XAVIX_PCALL")) { static int f2 = 0; if ((++f2 % 150) == 0) fprintf(stderr, "frame %d pc %02x:%04x\n", f2, m_cpu.codebank, m_cpu.pc); }
    if (const char* dumpAt = getenv("XAVIX_RAMDUMP")) {   // debugging aid for the headless runner
        static int dbgFrame = 0;
        if (++dbgFrame == atoi(dumpAt)) { FILE* f = fopen("/tmp/xram.bin", "wb"); if (f) { fwrite(m_s.ram, 1, 0x4000, f); fclose(f); } }
        if (getenv("XAVIX_PC")) fprintf(stderr, "frame %d pc %02x:%04x\n", dbgFrame, m_cpu.codebank, m_cpu.pc);
    }
#endif
    (void)frameCycles;
    soundSync();
}

// ---------------------------------------------------------------- save states

bool XavixCore::saveState(std::vector<uint8_t>& out)
{
    out.resize(sizeof(m_s) + sizeof(m_cpu) + sizeof(m_snd.s));
    uint8_t* p = out.data();
    std::memcpy(p, &m_s, sizeof(m_s)); p += sizeof(m_s);
    std::memcpy(p, &m_cpu, sizeof(m_cpu)); p += sizeof(m_cpu);
    std::memcpy(p, &m_snd.s, sizeof(m_snd.s));
    return true;
}

bool XavixCore::loadState(const std::vector<uint8_t>& in)
{
    if (in.size() != sizeof(m_s) + sizeof(m_cpu) + sizeof(m_snd.s)) return false;
    const uint8_t* p = in.data();
    std::memcpy(&m_s, p, sizeof(m_s)); p += sizeof(m_s);
    std::memcpy(&m_cpu, p, sizeof(m_cpu)); p += sizeof(m_cpu);
    std::memcpy(&m_snd.s, p, sizeof(m_snd.s));
    for (int i = 0; i < 512; i++) updatePen(i);
    m_audio.clear();
    return true;
}
