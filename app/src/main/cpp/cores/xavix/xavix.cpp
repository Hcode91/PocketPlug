// PocketPlug - SSD XaviX plug-and-play TV games
// SPDX-License-Identifier: BSD-3-Clause
//
// Port of MAME's tvgames/xavix.cpp, xavix_m.cpp, xavix_v.cpp, xavix_math.cpp,
// xavix_adc.cpp (David Haywood, ramacat), BSD-3-Clause.

#include "xavix.h"
#include <cmath>
#include <algorithm>

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
    std::memset(&m_s, 0, sizeof(m_s));
    std::memset(m_s.ram, 0xff, sizeof(m_s.ram));
    m_s.soundRegbase = 0x02;
    m_s.timerLeft = -1;
    m_s.adcLatch = 0xff;
    m_s.rng = 0x12345678;
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

uint8_t XavixCore::ioRead(int port)
{
    uint8_t in = port ? uint8_t(m_inputs >> 8) : uint8_t(m_inputs);
    uint8_t dir = port ? m_s.io1Dir : m_s.io0Dir;
    uint8_t data = port ? m_s.io1Data : m_s.io0Data;
    return uint8_t((in & ~dir) | (data & dir));
}

uint8_t XavixCore::lowRead(uint16_t a)
{
    a &= 0x7fff;
    if (a < 0x4000) return m_s.ram[a];
    if (a < 0x5000) return txarray(a & 0xfff);
    if (a < 0x6000) return 0;
    if (a < 0x6800) return m_s.sprite[a & 0x7ff];
    if (a < 0x6900) return m_s.palSh[a & 0xff];
    if (a < 0x6a00) return m_s.palL[a & 0xff];
    if (a < 0x6a20) return m_s.segment[a & 0x1f];
    if (a >= 0x7400 && a < 0x7580) return m_s.soundRam[a - 0x7400];
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
    case 0x7a01: m_s.io1Data = d; break;
    case 0x7a02: m_s.io0Dir = d; break;
    case 0x7a03: m_s.io1Dir = d; break;
    case 0x7a80: m_s.ioeventEnable = d; break;
    case 0x7a81:
        m_s.ioeventActive &= ~(d & 0x0f);
        if (!(m_s.ioeventActive & 0x0f)) m_s.irqSource &= ~0x08;
        updateIrqs();
        break;
    case 0x7b81:
        m_s.adcControl = d;
        m_s.adcLatch = 0;          // the analogue inputs are unused (0)
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
    m_s.soundTimerPeriod[i] = int64_t(MAIN_CLOCK / f * 65536.0);
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
    while (m_s.sndCycles >= 128) {
        m_s.sndCycles -= 128;
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
        double lineStart = line * CYC_PER_LINE;
        double lineEnd = (line + 1) * CYC_PER_LINE;
        if (m_s.posArmed && line == m_s.posirqY) {
            double at = lineStart + CYC_PER_LINE * m_s.posirqX / 256.0;
            while (done < at) { int c = cpuStep(); advance(c); done += c; }
            posIrq();
        }
        while (done < lineEnd) { int c = cpuStep(); advance(c); done += c; }
    }
    frameCycles = done;
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
    for (int i = 0; i < 256; i++) updatePen(i);
    m_audio.clear();
    return true;
}
