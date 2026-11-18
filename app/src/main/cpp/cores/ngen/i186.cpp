// PocketPlug - Intel 80186 CPU with its on-chip peripherals
// SPDX-License-Identifier: BSD-3-Clause

#include "i186.h"
#include <cstring>

namespace {
const uint8_t kParity[256] = {
#define P2(n) n, n^1, n^1, n
#define P4(n) P2(n), P2(n^1), P2(n^1), P2(n)
#define P6(n) P4(n), P4(n^1), P4(n^1), P4(n)
    P6(1), P6(0), P6(0), P6(1)
#undef P2
#undef P4
#undef P6
};
// interrupt sources: TMR, DMA0, DMA1, INT0..INT3 -> bit in mask/request/in-service registers
const int kSrcBit[7] = { 0, 2, 3, 4, 5, 6, 7 };
const uint8_t kSrcType[7] = { 8, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f };
}

void I186::reset()
{
    memset(r, 0, sizeof(r));
    sr[ES] = sr[SS] = sr[DS] = 0;
    sr[CS] = 0xffff;
    ip = 0;
    flags = 0xf002;
    halted = false;
    total_ = 0;
    cyc_ = 0;
    segOver_ = -1;
    rep_ = 0;
    relocation_ = 0x20ff;
    icMask_ = 0x00fd;
    icPriMask_ = 7;
    icInServ_ = icReq_ = icTmrStat_ = 0;
    for (int i = 0; i < 7; i++) icCon_[i] = 0x000f;
    for (int i = 0; i < 4; i++) extLine_[i] = false;
    nmiPending_ = false;
    intRequested_ = false;
    memset(t_, 0, sizeof(t_));
    timerPre_ = 0;
    memset(d_, 0, sizeof(d_));
    drq_[0] = drq_[1] = false;
    chipSel_[0] = 0xfffb; chipSel_[1] = chipSel_[2] = chipSel_[3] = chipSel_[4] = 0;
    inhibitInt_ = false;
    dmaTimerReq_[0] = dmaTimerReq_[1] = false;
    tmrInAcc_[0] = tmrInAcc_[1] = 0;
}

// ---------------------------------------------------------------- flags / ALU

void I186::setSZP8(uint8_t v)
{
    flags &= ~(SF | ZF | PF);
    if (v & 0x80) flags |= SF;
    if (!v) flags |= ZF;
    if (kParity[v]) flags |= PF;
}

void I186::setSZP16(uint16_t v)
{
    flags &= ~(SF | ZF | PF);
    if (v & 0x8000) flags |= SF;
    if (!v) flags |= ZF;
    if (kParity[v & 0xff]) flags |= PF;
}

uint8_t I186::alu8(int op, uint8_t a, uint8_t b)
{
    unsigned res;
    unsigned c = (flags & CF) ? 1 : 0;
    switch (op) {
    case 0: case 2:   // ADD, ADC
        res = a + b + (op == 2 ? c : 0);
        flags &= ~(CF | AF | OF);
        if (res & 0x100) flags |= CF;
        if ((a ^ b ^ res) & 0x10) flags |= AF;
        if ((~(a ^ b) & (a ^ res)) & 0x80) flags |= OF;
        setSZP8(uint8_t(res));
        return uint8_t(res);
    case 3: case 5: case 7:   // SBB, SUB, CMP
        res = a - b - (op == 3 ? c : 0);
        flags &= ~(CF | AF | OF);
        if (res & 0x100) flags |= CF;
        if ((a ^ b ^ res) & 0x10) flags |= AF;
        if (((a ^ b) & (a ^ res)) & 0x80) flags |= OF;
        setSZP8(uint8_t(res));
        return op == 7 ? a : uint8_t(res);
    case 1: res = a | b; break;
    case 4: res = a & b; break;
    default: res = a ^ b; break;
    }
    flags &= ~(CF | AF | OF);
    setSZP8(uint8_t(res));
    return uint8_t(res);
}

uint16_t I186::alu16(int op, uint16_t a, uint16_t b)
{
    uint32_t res;
    uint32_t c = (flags & CF) ? 1 : 0;
    switch (op) {
    case 0: case 2:
        res = uint32_t(a) + b + (op == 2 ? c : 0);
        flags &= ~(CF | AF | OF);
        if (res & 0x10000) flags |= CF;
        if ((a ^ b ^ res) & 0x10) flags |= AF;
        if ((~(a ^ b) & (a ^ res)) & 0x8000) flags |= OF;
        setSZP16(uint16_t(res));
        return uint16_t(res);
    case 3: case 5: case 7:
        res = uint32_t(a) - b - (op == 3 ? c : 0);
        flags &= ~(CF | AF | OF);
        if (res & 0x10000) flags |= CF;
        if ((a ^ b ^ res) & 0x10) flags |= AF;
        if (((a ^ b) & (a ^ res)) & 0x8000) flags |= OF;
        setSZP16(uint16_t(res));
        return op == 7 ? a : uint16_t(res);
    case 1: res = a | b; break;
    case 4: res = a & b; break;
    default: res = a ^ b; break;
    }
    flags &= ~(CF | AF | OF);
    setSZP16(uint16_t(res));
    return uint16_t(res);
}

uint8_t I186::inc8(uint8_t v, bool dec)
{
    uint8_t res = dec ? v - 1 : v + 1;
    flags &= ~(AF | OF);
    if ((v ^ res) & 0x10) flags |= AF;
    if (dec ? res == 0x7f : res == 0x80) flags |= OF;
    setSZP8(res);
    return res;
}

uint16_t I186::inc16(uint16_t v, bool dec)
{
    uint16_t res = dec ? v - 1 : v + 1;
    flags &= ~(AF | OF);
    if ((v ^ res) & 0x10) flags |= AF;
    if (dec ? res == 0x7fff : res == 0x8000) flags |= OF;
    setSZP16(res);
    return res;
}

uint8_t I186::shift8(int op, uint8_t v, int count)
{
    count &= 0x1f;
    if (!count) return v;
    cyc_ += count;
    unsigned x = v;
    switch (op) {
    case 0:   // ROL
        for (int i = 0; i < count; i++) x = ((x << 1) | (x >> 7)) & 0xff;
        flags = (flags & ~(CF | OF)) | ((x & 1) ? CF : 0);
        if (((x >> 7) ^ x) & 1) flags |= OF;
        return uint8_t(x);
    case 1:   // ROR
        for (int i = 0; i < count; i++) x = ((x >> 1) | (x << 7)) & 0xff;
        flags = (flags & ~(CF | OF)) | ((x & 0x80) ? CF : 0);
        if (((x >> 7) ^ (x >> 6)) & 1) flags |= OF;
        return uint8_t(x);
    case 2: { // RCL
        unsigned c = (flags & CF) ? 1 : 0;
        for (int i = 0; i < count; i++) { unsigned nc = (x >> 7) & 1; x = ((x << 1) | c) & 0xff; c = nc; }
        flags = (flags & ~(CF | OF)) | (c ? CF : 0);
        if (((x >> 7) ^ c) & 1) flags |= OF;
        return uint8_t(x);
    }
    case 3: { // RCR
        unsigned c = (flags & CF) ? 1 : 0;
        for (int i = 0; i < count; i++) { unsigned nc = x & 1; x = (x >> 1) | (c << 7); c = nc; }
        flags = (flags & ~(CF | OF)) | (c ? CF : 0);
        if (((x >> 7) ^ (x >> 6)) & 1) flags |= OF;
        return uint8_t(x);
    }
    case 4: case 6: { // SHL
        unsigned res = (count > 8) ? 0 : (x << count);
        flags &= ~(CF | OF | AF);
        if (count <= 8 && (res & 0x100)) flags |= CF;
        res &= 0xff;
        if (((res >> 7) & 1) ^ ((flags & CF) ? 1 : 0)) flags |= OF;
        setSZP8(uint8_t(res));
        return uint8_t(res);
    }
    case 5: { // SHR
        unsigned res = (count > 8) ? 0 : (x >> (count - 1));
        flags &= ~(CF | OF | AF);
        if (res & 1) flags |= CF;
        res >>= 1;
        if (x & 0x80) flags |= OF;
        setSZP8(uint8_t(res));
        return uint8_t(res);
    }
    default: { // SAR
        int s = int8_t(x);
        if (count > 8) count = 8;
        s >>= (count - 1);
        flags &= ~(CF | OF | AF);
        if (s & 1) flags |= CF;
        s >>= 1;
        setSZP8(uint8_t(s));
        return uint8_t(s);
    }
    }
}

uint16_t I186::shift16(int op, uint16_t v, int count)
{
    count &= 0x1f;
    if (!count) return v;
    cyc_ += count;
    uint32_t x = v;
    switch (op) {
    case 0:
        for (int i = 0; i < count; i++) x = ((x << 1) | (x >> 15)) & 0xffff;
        flags = (flags & ~(CF | OF)) | ((x & 1) ? CF : 0);
        if (((x >> 15) ^ x) & 1) flags |= OF;
        return uint16_t(x);
    case 1:
        for (int i = 0; i < count; i++) x = ((x >> 1) | (x << 15)) & 0xffff;
        flags = (flags & ~(CF | OF)) | ((x & 0x8000) ? CF : 0);
        if (((x >> 15) ^ (x >> 14)) & 1) flags |= OF;
        return uint16_t(x);
    case 2: {
        uint32_t c = (flags & CF) ? 1 : 0;
        for (int i = 0; i < count; i++) { uint32_t nc = (x >> 15) & 1; x = ((x << 1) | c) & 0xffff; c = nc; }
        flags = (flags & ~(CF | OF)) | (c ? CF : 0);
        if (((x >> 15) ^ c) & 1) flags |= OF;
        return uint16_t(x);
    }
    case 3: {
        uint32_t c = (flags & CF) ? 1 : 0;
        for (int i = 0; i < count; i++) { uint32_t nc = x & 1; x = (x >> 1) | (c << 15); c = nc; }
        flags = (flags & ~(CF | OF)) | (c ? CF : 0);
        if (((x >> 15) ^ (x >> 14)) & 1) flags |= OF;
        return uint16_t(x);
    }
    case 4: case 6: {
        uint32_t res = (count > 16) ? 0 : (x << count);
        flags &= ~(CF | OF | AF);
        if (count <= 16 && (res & 0x10000)) flags |= CF;
        res &= 0xffff;
        if (((res >> 15) & 1) ^ ((flags & CF) ? 1 : 0)) flags |= OF;
        setSZP16(uint16_t(res));
        return uint16_t(res);
    }
    case 5: {
        uint32_t res = (count > 16) ? 0 : (x >> (count - 1));
        flags &= ~(CF | OF | AF);
        if (res & 1) flags |= CF;
        res >>= 1;
        if (x & 0x8000) flags |= OF;
        setSZP16(uint16_t(res));
        return uint16_t(res);
    }
    default: {
        int32_t s = int16_t(x);
        if (count > 16) count = 16;
        s >>= (count - 1);
        flags &= ~(CF | OF | AF);
        if (s & 1) flags |= CF;
        s >>= 1;
        setSZP16(uint16_t(s));
        return uint16_t(s);
    }
    }
}

bool I186::cond(int c) const
{
    bool res;
    switch (c >> 1) {
    case 0: res = flags & OF; break;
    case 1: res = flags & CF; break;
    case 2: res = flags & ZF; break;
    case 3: res = (flags & CF) || (flags & ZF); break;
    case 4: res = flags & SF; break;
    case 5: res = flags & PF; break;
    case 6: res = bool(flags & SF) != bool(flags & OF); break;
    default: res = (flags & ZF) || (bool(flags & SF) != bool(flags & OF)); break;
    }
    return (c & 1) ? !res : res;
}

// ---------------------------------------------------------------- modrm

void I186::decodeModrm()
{
    modrm_ = fetch8();
    int mod = modrm_ >> 6, rm = modrm_ & 7;
    if (mod == 3) { modIsReg_ = true; return; }
    modIsReg_ = false;
    uint16_t off = 0;
    int seg = DS;
    switch (rm) {
    case 0: off = r[BX] + r[SI]; break;
    case 1: off = r[BX] + r[DI]; break;
    case 2: off = r[BP] + r[SI]; seg = SS; break;
    case 3: off = r[BP] + r[DI]; seg = SS; break;
    case 4: off = r[SI]; break;
    case 5: off = r[DI]; break;
    case 6: if (mod == 0) off = fetch16(); else { off = r[BP]; seg = SS; } break;
    case 7: off = r[BX]; break;
    }
    if (mod == 1) off += int8_t(fetch8());
    else if (mod == 2) off += fetch16();
    eaOff_ = off;
    eaSeg_ = segOver_ >= 0 ? segOver_ : seg;
    cyc_ += 4;
}

uint8_t I186::getRM8() { return modIsReg_ ? reg8(modrm_ & 7) : rb(eaSeg_, eaOff_); }
uint16_t I186::getRM16() { return modIsReg_ ? r[modrm_ & 7] : rw(eaSeg_, eaOff_); }
void I186::setRM8(uint8_t v) { if (modIsReg_) reg8(modrm_ & 7) = v; else wb(eaSeg_, eaOff_, v); }
void I186::setRM16(uint16_t v) { if (modIsReg_) r[modrm_ & 7] = v; else ww(eaSeg_, eaOff_, v); }

// ---------------------------------------------------------------- interrupts

void I186::interrupt(uint8_t type)
{
    push(flags | 0xf002);
    push(sr[CS]);
    push(ip);
    flags &= ~(IF | TF);
    ip = bus_->memRead(type * 4) | (bus_->memRead(type * 4 + 1) << 8);
    sr[CS] = bus_->memRead(type * 4 + 2) | (bus_->memRead(type * 4 + 3) << 8);
    halted = false;
    cyc_ += 45;
}

int I186::pendingSource()
{
    // highest in-service priority
    int curPri = 8;
    for (int s = 0; s < 7; s++)
        if (icInServ_ & (1 << kSrcBit[s])) {
            int p = icCon_[s] & 7;
            if (p < curPri) curPri = p;
        }
    int best = -1, bestPri = 8;
    for (int s = 0; s < 7; s++) {
        int bit = 1 << kSrcBit[s];
        bool req;
        if (s == 0) req = (icTmrStat_ & 7) != 0;
        else if (s >= 3) {
            int n = s - 3;
            bool level = (icCon_[s] & 0x10) || (n < 2 && (icCon_[s] & 0x20));
            req = level ? extLine_[n] : (icReq_ & bit);
        } else req = icReq_ & bit;
        if (!req || (icMask_ & bit)) continue;
        int p = icCon_[s] & 7;
        if (p > icPriMask_) continue;
        bool sfnm = (s == 3 || s == 4) && (icCon_[s] & 0x40);
        if (p > curPri || (p == curPri && !(sfnm && (icInServ_ & bit)))) continue;
        if (p < bestPri) { best = s; bestPri = p; }
    }
    return best;
}

void I186::checkInterrupts()
{
    intRequested_ = nmiPending_ || pendingSource() >= 0;
}

void I186::setInt(int which, bool level)
{
    if (level && !extLine_[which]) icReq_ |= 1 << kSrcBit[3 + which];
    if (!level) {
        // edge requests stay latched; level requests follow the line
    }
    extLine_[which] = level;
    checkInterrupts();
}

void I186::setDrq(int which, bool level) { drq_[which] = level; }

void I186::tmrIn(int which)
{
    Timer& t = t_[which];
    if ((t.control & 0x8004) == 0x8004) timerTick(which);
    else if ((t.control & 0x8014) == 0x8010) t.count = 0;   // retrigger
}

// ---------------------------------------------------------------- timers

void I186::timerTick(int which)
{
    Timer& t = t_[which];
    t.count++;
    bool alt = which < 2 && (t.control & 0x0002);
    bool riu = alt && (t.control & 0x1000);
    uint16_t max = riu ? t.maxB : t.maxA;
    if (t.count != max) return;
    t.count = 0;
    t.control |= 0x0020;   // MC
    if (t.control & 0x2000) {
        icTmrStat_ |= 1 << which;
        checkInterrupts();
    }
    if (which < 2) bus_->timerOut(which, alt ? (riu ? 1 : 0) : 1);
    if (which == 2) {
        for (int i = 0; i < 2; i++)
            if ((t_[i].control & 0x800c) == 0x8008) timerTick(i);
        for (int c = 0; c < 2; c++)
            if ((d_[c].control & 0x0012) == 0x0012) dmaTimerReq_[c] = true;
    }
    if (alt) {
        if (!riu) t.control |= 0x1000;
        else {
            t.control &= ~0x1000;
            if (!(t.control & 1)) t.control &= ~0x8000;
        }
    } else if (!(t.control & 1)) t.control &= ~0x8000;
}

void I186::timersAdvance(int cycles)
{
    timerPre_ += cycles;
    while (timerPre_ >= 4) {
        timerPre_ -= 4;
        // timer 2 first so it can prescale 0/1 in the same tick
        if (t_[2].control & 0x8000) timerTick(2);
        for (int i = 0; i < 2; i++)
            if ((t_[i].control & 0x800c) == 0x8000) timerTick(i);
    }
    if (tmrInPeriod_[0] > 0 || tmrInPeriod_[1] > 0) {
        for (int i = 0; i < 2; i++) {
            if (tmrInPeriod_[i] <= 0) continue;
            tmrInAcc_[i] += cycles * 1000;
            while (tmrInAcc_[i] >= tmrInPeriod_[i]) { tmrInAcc_[i] -= tmrInPeriod_[i]; tmrIn(i); }
        }
    }
}

// ---------------------------------------------------------------- DMA

int I186::dmaService()
{
    int used = 0;
    for (int c = 0; c < 2; c++) {
        Dma& d = d_[c];
        int guard = 0;
        while ((d.control & 0x0002) && guard < 65536) {
            int sync = (d.control >> 6) & 3;
            bool req;
            if (d.control & 0x0010) { req = dmaTimerReq_[c]; dmaTimerReq_[c] = false; }
            else req = (sync == 0) || drq_[c];
            if (!req) break;
            guard++;
            bool word = d.control & 1;
            uint16_t v;
            if (d.control & 0x1000) {   // source in memory
                v = bus_->memRead(d.src & 0xfffff);
                if (word) v |= bus_->memRead((d.src + 1) & 0xfffff) << 8;
            } else v = portIn(uint16_t(d.src), word);
            if (d.control & 0x8000) {
                bus_->memWrite(d.dst & 0xfffff, uint8_t(v));
                if (word) bus_->memWrite((d.dst + 1) & 0xfffff, uint8_t(v >> 8));
            } else portOut(uint16_t(d.dst), v, word);
            int step = word ? 2 : 1;
            switch (d.control & 0x6000) { case 0x2000: d.dst += step; break; case 0x4000: d.dst -= step; break; }
            switch (d.control & 0x0c00) { case 0x0400: d.src += step; break; case 0x0800: d.src -= step; break; }
            d.src &= 0xfffff; d.dst &= 0xfffff;
            d.count--;
            used += 8;
            if (d.count == 0) {
                if ((d.control & 0x0200) || sync == 0) d.control &= ~0x0002;
                if (d.control & 0x0100) { icReq_ |= 1 << kSrcBit[1 + c]; checkInterrupts(); }
            }
            if (sync != 0 && !(d.control & 0x0010)) {
                // synchronized: one transfer per request edge; the device drops DRQ when read
                continue;
            }
        }
    }
    return used;
}

// ---------------------------------------------------------------- peripheral control block

bool I186::inPcb(uint16_t port) const
{
    if (relocation_ & 0x1000) return false;   // PCB mapped to memory
    uint16_t base = uint16_t((relocation_ & 0x0fff) << 8);
    return (port & 0xff00) == base;
}

uint16_t I186::pcbRead(uint16_t off)
{
    off &= 0xfe;
    switch (off) {
    case 0x22: return 0;
    case 0x24: case 0x26: {   // poll / poll status
        int s = pendingSource();
        if (s < 0) return 0;
        uint8_t type = kSrcType[s];
        if (s == 0) type = (icTmrStat_ & 1) ? 8 : (icTmrStat_ & 2) ? 0x12 : 0x13;
        if (off == 0x24) {
            icInServ_ |= 1 << kSrcBit[s];
            if (s == 0) icTmrStat_ &= icTmrStat_ - 1;
            else icReq_ &= ~(1 << kSrcBit[s]);
            checkInterrupts();
        }
        return 0x8000 | type;
    }
    case 0x28: return icMask_;
    case 0x2a: return icPriMask_;
    case 0x2c: return icInServ_;
    case 0x2e: {
        uint16_t v = icReq_ & 0xfc;
        if (icTmrStat_ & 7) v |= 1;
        for (int n = 0; n < 4; n++)
            if ((icCon_[3 + n] & 0x10) && extLine_[n]) v |= 1 << kSrcBit[3 + n];
        return v;
    }
    case 0x30: return icTmrStat_;
    case 0x32: case 0x34: case 0x36: case 0x38: case 0x3a: case 0x3c: case 0x3e:
        return icCon_[(off - 0x32) / 2];
    case 0x50: case 0x58: case 0x60: return t_[(off - 0x50) / 8].count;
    case 0x52: case 0x5a: case 0x62: return t_[(off - 0x50) / 8].maxA;
    case 0x54: case 0x5c: return t_[(off - 0x50) / 8].maxB;
    case 0x56: case 0x5e: case 0x66: return t_[(off - 0x50) / 8].control;
    case 0xa0: case 0xa2: case 0xa4: case 0xa6: case 0xa8: return chipSel_[(off - 0xa0) / 2];
    case 0xc0: case 0xd0: return uint16_t(d_[(off >> 4) & 1].src);
    case 0xc2: case 0xd2: return uint16_t(d_[(off >> 4) & 1].src >> 16);
    case 0xc4: case 0xd4: return uint16_t(d_[(off >> 4) & 1].dst);
    case 0xc6: case 0xd6: return uint16_t(d_[(off >> 4) & 1].dst >> 16);
    case 0xc8: case 0xd8: return d_[(off >> 4) & 1].count;
    case 0xca: case 0xda: return d_[(off >> 4) & 1].control;
    case 0xfe: return relocation_;
    }
    return 0;
}

void I186::pcbWrite(uint16_t off, uint16_t v)
{
    off &= 0xfe;
    switch (off) {
    case 0x22: {   // EOI
        if (v & 0x8000) {
            int best = -1, bestPri = 8;
            for (int s = 0; s < 7; s++)
                if (icInServ_ & (1 << kSrcBit[s])) {
                    int p = icCon_[s] & 7;
                    if (p < bestPri) { bestPri = p; best = s; }
                }
            if (best >= 0) icInServ_ &= ~(1 << kSrcBit[best]);
        } else {
            // specific EOI: the low three bits of the type select the in-service
            // bit (BTOS writes 0 for the timers)
            int type = v & 0x1f;
            int bit = (type == 0x12 || type == 0x13) ? 0 : (type & 7);
            icInServ_ &= ~(1 << bit);
        }
        break;
    }
    case 0x28:
        icMask_ = v & 0xfd;
        for (int s = 0; s < 7; s++)
            icCon_[s] = (icCon_[s] & ~8) | ((icMask_ >> kSrcBit[s]) & 1 ? 8 : 0);
        break;
    case 0x2a: icPriMask_ = v & 7; break;
    case 0x2c: icInServ_ = v & 0xfd; break;
    case 0x2e: icReq_ = (icReq_ & ~0xcc) | (v & 0xcc); break;
    case 0x30: icTmrStat_ = v & 7; break;
    case 0x32: case 0x34: case 0x36: case 0x38: case 0x3a: case 0x3c: case 0x3e: {
        int s = (off - 0x32) / 2;
        uint16_t m = (s == 3 || s == 4) ? 0x7f : (s >= 5 ? 0x1f : 0x0f);
        icCon_[s] = v & m;
        icMask_ = (icMask_ & ~(1 << kSrcBit[s])) | ((v & 8) ? (1 << kSrcBit[s]) : 0);
        break;
    }
    case 0x50: case 0x58: case 0x60: t_[(off - 0x50) / 8].count = v; break;
    case 0x52: case 0x5a: case 0x62: t_[(off - 0x50) / 8].maxA = v; break;
    case 0x54: case 0x5c: t_[(off - 0x50) / 8].maxB = v; break;
    case 0x56: case 0x5e: case 0x66: {
        int w = (off - 0x50) / 8;
        Timer& t = t_[w];
        uint16_t keep = (w == 2) ? 0x1fde : 0x1fc0;
        uint16_t nc = (v & ~keep) | (t.control & keep);
        if (!(v & 0x4000)) nc = (nc & ~0x8000) | (t.control & 0x8000);
        nc &= ~0x4000;
        if (!(nc & 0x0002)) nc &= ~0x1000;
        t.control = nc;
        break;
    }
    case 0xa0: case 0xa2: case 0xa4: case 0xa6: case 0xa8:
        chipSel_[(off - 0xa0) / 2] = v;
        bus_->chipSelect(0xffa0 + (off - 0xa0), v);
        break;
    case 0xc0: case 0xd0: { Dma& d = d_[(off >> 4) & 1]; d.src = (d.src & 0xf0000) | v; break; }
    case 0xc2: case 0xd2: { Dma& d = d_[(off >> 4) & 1]; d.src = (d.src & 0xffff) | (uint32_t(v & 0xf) << 16); break; }
    case 0xc4: case 0xd4: { Dma& d = d_[(off >> 4) & 1]; d.dst = (d.dst & 0xf0000) | v; break; }
    case 0xc6: case 0xd6: { Dma& d = d_[(off >> 4) & 1]; d.dst = (d.dst & 0xffff) | (uint32_t(v & 0xf) << 16); break; }
    case 0xc8: case 0xd8: d_[(off >> 4) & 1].count = v; break;
    case 0xca: case 0xda: {
        Dma& d = d_[(off >> 4) & 1];
        uint16_t nc = v;
        if (!(v & 0x0004)) nc = (nc & ~0x0002) | (d.control & 0x0002);
        nc &= ~0x0004;
        d.control = nc;
        break;
    }
    case 0xfe: relocation_ = v; break;
    }
    checkInterrupts();
}

uint16_t I186::portIn(uint16_t port, bool word)
{
    if (inPcb(port)) {
        uint16_t v = pcbRead(port & 0xff);
        if (!word && (port & 1)) v >>= 8;
        return word ? v : (v & 0xff);
    }
    return bus_->ioRead(port, word);
}

void I186::portOut(uint16_t port, uint16_t v, bool word)
{
    if (inPcb(port)) {
        // a byte OUT to the PCB writes the whole of AX (BTOS relies on this:
        // mov ax,8000h / out dx,al for a non-specific EOI)
        pcbWrite(port & 0xff, v);
        return;
    }
    bus_->ioWrite(port, word ? v : (v & 0xff), word);
}

// ---------------------------------------------------------------- execution

int I186::run(int cycles)
{
    int done = 0;
    while (done < cycles) {
        cyc_ = 0;
        if (!inhibitInt_ && intRequested_) {
            if (nmiPending_) {
                nmiPending_ = false;
                interrupt(2);
            } else if (flags & IF) {
                int s = pendingSource();
                if (s >= 0) {
                    uint8_t type;
                    icInServ_ |= 1 << kSrcBit[s];
                    if (s == 0) {
                        int w = (icTmrStat_ & 1) ? 0 : (icTmrStat_ & 2) ? 1 : 2;
                        icTmrStat_ &= ~(1 << w);
                        type = w == 0 ? 8 : (w == 1 ? 0x12 : 0x13);
                    } else {
                        type = kSrcType[s];
                        if (s < 3 || !((icCon_[s] & 0x10) || (s <= 4 && (icCon_[s] & 0x20))))
                            icReq_ &= ~(1 << kSrcBit[s]);
                        if (s == 3 && (icCon_[3] & 0x20)) type = bus_->intAck();
                        else if (s == 4 && (icCon_[4] & 0x20)) type = bus_->intAck();
                    }
                    interrupt(type);
                }
            }
            checkInterrupts();
        }
        inhibitInt_ = false;
        if (halted) {
            cyc_ = cycles - done;
            if (cyc_ > 64) cyc_ = 64;
            if (cyc_ < 4) cyc_ = 4;
        } else {
            execute();
        }
        cyc_ += dmaService();
        timersAdvance(cyc_);
        done += cyc_;
        total_ += cyc_;
    }
    return done;
}

void I186::grp3_8()
{
    int op = regField();
    uint8_t v = getRM8();
    switch (op) {
    case 0: case 1: alu8(4, v, fetch8()); cyc_ += 4; break;
    case 2: setRM8(~v); cyc_ += 3; break;
    case 3: setRM8(alu8(5, 0, v)); if (v) flags |= CF; else flags &= ~CF; cyc_ += 3; break;
    case 4: {
        uint16_t res = uint16_t(reg8(0)) * v;
        r[AX] = res;
        if (res & 0xff00) flags |= CF | OF; else flags &= ~(CF | OF);
        setSZP8(uint8_t(res));
        cyc_ += 27;
        break;
    }
    case 5: {
        int16_t res = int16_t(int8_t(reg8(0))) * int8_t(v);
        r[AX] = uint16_t(res);
        if (res != int8_t(res)) flags |= CF | OF; else flags &= ~(CF | OF);
        setSZP8(uint8_t(res));
        cyc_ += 33;
        break;
    }
    case 6: {
        if (!v) { interrupt(0); return; }
        uint16_t q = r[AX] / v, rem = r[AX] % v;
        if (q > 0xff) { interrupt(0); return; }
        r[AX] = uint16_t(q | (rem << 8));
        cyc_ += 29;
        break;
    }
    case 7: {
        if (!v) { interrupt(0); return; }
        int a = int16_t(r[AX]), b = int8_t(v);
        int q = a / b, rem = a % b;
        if (q > 127 || q < -128) { interrupt(0); return; }
        r[AX] = uint16_t((q & 0xff) | ((rem & 0xff) << 8));
        cyc_ += 44;
        break;
    }
    }
}

void I186::grp3_16()
{
    int op = regField();
    uint16_t v = getRM16();
    switch (op) {
    case 0: case 1: alu16(4, v, fetch16()); cyc_ += 4; break;
    case 2: setRM16(~v); cyc_ += 3; break;
    case 3: setRM16(alu16(5, 0, v)); if (v) flags |= CF; else flags &= ~CF; cyc_ += 3; break;
    case 4: {
        uint32_t res = uint32_t(r[AX]) * v;
        r[AX] = uint16_t(res); r[DX] = uint16_t(res >> 16);
        if (r[DX]) flags |= CF | OF; else flags &= ~(CF | OF);
        setSZP16(r[AX]);
        cyc_ += 36;
        break;
    }
    case 5: {
        int32_t res = int32_t(int16_t(r[AX])) * int16_t(v);
        r[AX] = uint16_t(res); r[DX] = uint16_t(uint32_t(res) >> 16);
        if (res != int16_t(res)) flags |= CF | OF; else flags &= ~(CF | OF);
        setSZP16(r[AX]);
        cyc_ += 42;
        break;
    }
    case 6: {
        if (!v) { interrupt(0); return; }
        uint32_t a = (uint32_t(r[DX]) << 16) | r[AX];
        uint32_t q = a / v, rem = a % v;
        if (q > 0xffff) { interrupt(0); return; }
        r[AX] = uint16_t(q); r[DX] = uint16_t(rem);
        cyc_ += 38;
        break;
    }
    case 7: {
        if (!v) { interrupt(0); return; }
        int32_t a = int32_t((uint32_t(r[DX]) << 16) | r[AX]);
        int32_t b = int16_t(v);
        if (a == INT32_MIN && b == -1) { interrupt(0); return; }
        int32_t q = a / b, rem = a % b;
        if (q > 32767 || q < -32768) { interrupt(0); return; }
        r[AX] = uint16_t(q); r[DX] = uint16_t(rem);
        cyc_ += 53;
        break;
    }
    }
}

void I186::stringOp(uint8_t op)
{
    int srcSeg = segOver_ >= 0 ? segOver_ : DS;
    int step = (flags & DF) ? -1 : 1;
    bool word = op & 1;
    int sz = word ? 2 : 1;
    auto once = [&]() -> bool {   // returns false when a REPE/REPNE comparison ends the loop
        switch (op) {
        case 0xa4: case 0xa5:
            if (word) ww(ES, r[DI], rw(srcSeg, r[SI])); else wb(ES, r[DI], rb(srcSeg, r[SI]));
            r[SI] += step * sz; r[DI] += step * sz; cyc_ += 8; return true;
        case 0xa6: case 0xa7:
            if (word) alu16(7, rw(srcSeg, r[SI]), rw(ES, r[DI])); else alu8(7, rb(srcSeg, r[SI]), rb(ES, r[DI]));
            r[SI] += step * sz; r[DI] += step * sz; cyc_ += 22; return false;
        case 0xaa: case 0xab:
            if (word) ww(ES, r[DI], r[AX]); else wb(ES, r[DI], uint8_t(r[AX]));
            r[DI] += step * sz; cyc_ += 6; return true;
        case 0xac: case 0xad:
            if (word) r[AX] = rw(srcSeg, r[SI]); else reg8(0) = rb(srcSeg, r[SI]);
            r[SI] += step * sz; cyc_ += 6; return true;
        case 0xae: case 0xaf:
            if (word) alu16(7, r[AX], rw(ES, r[DI])); else alu8(7, uint8_t(r[AX]), rb(ES, r[DI]));
            r[DI] += step * sz; cyc_ += 15; return false;
        case 0x6c: case 0x6d: {
            uint16_t v = portIn(r[DX], word);
            if (word) ww(ES, r[DI], v); else wb(ES, r[DI], uint8_t(v));
            r[DI] += step * sz; cyc_ += 8; return true;
        }
        case 0x6e: case 0x6f: {
            uint16_t v = word ? rw(srcSeg, r[SI]) : rb(srcSeg, r[SI]);
            portOut(r[DX], v, word);
            r[SI] += step * sz; cyc_ += 8; return true;
        }
        }
        return true;
    };
    if (!rep_) { once(); return; }
    cyc_ += 6;
    while (r[CX]) {
        bool plain = once();
        r[CX]--;
        if (!plain) {
            bool z = flags & ZF;
            if (rep_ == 0xf3 && !z) break;
            if (rep_ == 0xf2 && z) break;
        }
    }
}

void I186::execute()
{
    segOver_ = -1;
    rep_ = 0;
    opIp_ = ip;
    uint8_t op;
    for (;;) {
        op = fetch8();
        if (op == 0x26 || op == 0x2e || op == 0x36 || op == 0x3e) { segOver_ = (op >> 3) & 3; cyc_ += 2; continue; }
        if (op == 0xf2 || op == 0xf3) { rep_ = op; cyc_ += 2; continue; }
        if (op == 0xf0) { cyc_ += 2; continue; }
        break;
    }
    int seg = segOver_ >= 0 ? segOver_ : DS;

    if (op < 0x40 && (op & 7) < 6) {
        int aop = op >> 3;
        switch (op & 7) {
        case 0: { decodeModrm(); uint8_t v = alu8(aop, getRM8(), reg8(regField())); if (aop != 7) setRM8(v); cyc_ += 3; break; }
        case 1: { decodeModrm(); uint16_t v = alu16(aop, getRM16(), r[regField()]); if (aop != 7) setRM16(v); cyc_ += 3; break; }
        case 2: { decodeModrm(); uint8_t v = alu8(aop, reg8(regField()), getRM8()); if (aop != 7) reg8(regField()) = v; cyc_ += 3; break; }
        case 3: { decodeModrm(); uint16_t v = alu16(aop, r[regField()], getRM16()); if (aop != 7) r[regField()] = v; cyc_ += 3; break; }
        case 4: { uint8_t v = alu8(aop, uint8_t(r[AX]), fetch8()); if (aop != 7) reg8(0) = v; cyc_ += 3; break; }
        case 5: { uint16_t v = alu16(aop, r[AX], fetch16()); if (aop != 7) r[AX] = v; cyc_ += 4; break; }
        }
        return;
    }

    switch (op) {
    case 0x06: push(sr[ES]); cyc_ += 9; break;
    case 0x07: sr[ES] = pop(); cyc_ += 8; break;
    case 0x0e: push(sr[CS]); cyc_ += 9; break;
    case 0x16: push(sr[SS]); cyc_ += 9; break;
    case 0x17: sr[SS] = pop(); inhibitInt_ = true; cyc_ += 8; break;
    case 0x1e: push(sr[DS]); cyc_ += 9; break;
    case 0x1f: sr[DS] = pop(); cyc_ += 8; break;
    case 0x27: {   // DAA
        uint8_t al = uint8_t(r[AX]), old = al;
        bool cf = flags & CF;
        if ((al & 0x0f) > 9 || (flags & AF)) { al += 6; flags |= AF; } else flags &= ~AF;
        if (old > 0x99 || cf) { al += 0x60; flags |= CF; } else flags &= ~CF;
        reg8(0) = al; setSZP8(al); cyc_ += 4; break;
    }
    case 0x2f: {   // DAS
        uint8_t al = uint8_t(r[AX]), old = al;
        bool cf = flags & CF;
        if ((al & 0x0f) > 9 || (flags & AF)) { al -= 6; flags |= AF; } else flags &= ~AF;
        if (old > 0x99 || cf) { al -= 0x60; flags |= CF; } else flags &= ~CF;
        reg8(0) = al; setSZP8(al); cyc_ += 4; break;
    }
    case 0x37: case 0x3f: {   // AAA / AAS
        if ((r[AX] & 0x0f) > 9 || (flags & AF)) {
            if (op == 0x37) { r[AX] += 0x106; } else { r[AX] -= 0x106; }
            flags |= AF | CF;
        } else flags &= ~(AF | CF);
        r[AX] &= 0xff0f;
        cyc_ += 8; break;
    }
    case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45: case 0x46: case 0x47:
        r[op & 7] = inc16(r[op & 7], false); cyc_ += 3; break;
    case 0x48: case 0x49: case 0x4a: case 0x4b: case 0x4c: case 0x4d: case 0x4e: case 0x4f:
        r[op & 7] = inc16(r[op & 7], true); cyc_ += 3; break;
    case 0x50: case 0x51: case 0x52: case 0x53: case 0x54: case 0x55: case 0x56: case 0x57:
        if (op == 0x54) { uint16_t v = r[SP] - 2; push(v); } else push(r[op & 7]);
        cyc_ += 10; break;
    case 0x58: case 0x59: case 0x5a: case 0x5b: case 0x5c: case 0x5d: case 0x5e: case 0x5f:
        r[op & 7] = pop(); cyc_ += 10; break;
    case 0x60: {   // PUSHA
        uint16_t sp = r[SP];
        push(r[AX]); push(r[CX]); push(r[DX]); push(r[BX]); push(sp); push(r[BP]); push(r[SI]); push(r[DI]);
        cyc_ += 36; break;
    }
    case 0x61:   // POPA
        r[DI] = pop(); r[SI] = pop(); r[BP] = pop(); pop(); r[BX] = pop(); r[DX] = pop(); r[CX] = pop(); r[AX] = pop();
        cyc_ += 51; break;
    case 0x62: {   // BOUND
        decodeModrm();
        int16_t idx = int16_t(r[regField()]);
        int16_t lo = int16_t(rw(eaSeg_, eaOff_)), hi = int16_t(rw(eaSeg_, uint16_t(eaOff_ + 2)));
        cyc_ += 33;
        if (idx < lo || idx > hi) { ip = opIp_; interrupt(5); }
        break;
    }
    case 0x68: push(fetch16()); cyc_ += 10; break;
    case 0x6a: push(uint16_t(int8_t(fetch8()))); cyc_ += 10; break;
    case 0x69: case 0x6b: {
        decodeModrm();
        int16_t a = int16_t(getRM16());
        int16_t b = op == 0x69 ? int16_t(fetch16()) : int16_t(int8_t(fetch8()));
        int32_t res = int32_t(a) * b;
        r[regField()] = uint16_t(res);
        if (res != int16_t(res)) flags |= CF | OF; else flags &= ~(CF | OF);
        cyc_ += 25; break;
    }
    case 0x6c: case 0x6d: case 0x6e: case 0x6f:
    case 0xa4: case 0xa5: case 0xa6: case 0xa7: case 0xaa: case 0xab: case 0xac: case 0xad: case 0xae: case 0xaf:
        stringOp(op); break;
    case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75: case 0x76: case 0x77:
    case 0x78: case 0x79: case 0x7a: case 0x7b: case 0x7c: case 0x7d: case 0x7e: case 0x7f: {
        int8_t d = int8_t(fetch8());
        if (cond(op & 0xf)) { ip += d; cyc_ += 13; } else cyc_ += 4;
        break;
    }
    case 0x80: case 0x82: { decodeModrm(); uint8_t b = fetch8(); uint8_t v = alu8(regField(), getRM8(), b); if (regField() != 7) setRM8(v); cyc_ += 4; break; }
    case 0x81: { decodeModrm(); uint16_t b = fetch16(); uint16_t v = alu16(regField(), getRM16(), b); if (regField() != 7) setRM16(v); cyc_ += 4; break; }
    case 0x83: { decodeModrm(); uint16_t b = uint16_t(int8_t(fetch8())); uint16_t v = alu16(regField(), getRM16(), b); if (regField() != 7) setRM16(v); cyc_ += 4; break; }
    case 0x84: decodeModrm(); alu8(4, getRM8(), reg8(regField())); cyc_ += 3; break;
    case 0x85: decodeModrm(); alu16(4, getRM16(), r[regField()]); cyc_ += 3; break;
    case 0x86: { decodeModrm(); uint8_t a = getRM8(); setRM8(reg8(regField())); reg8(regField()) = a; cyc_ += 4; break; }
    case 0x87: { decodeModrm(); uint16_t a = getRM16(); setRM16(r[regField()]); r[regField()] = a; cyc_ += 4; break; }
    case 0x88: decodeModrm(); setRM8(reg8(regField())); cyc_ += 2; break;
    case 0x89: decodeModrm(); setRM16(r[regField()]); cyc_ += 2; break;
    case 0x8a: decodeModrm(); reg8(regField()) = getRM8(); cyc_ += 2; break;
    case 0x8b: decodeModrm(); r[regField()] = getRM16(); cyc_ += 2; break;
    case 0x8c: decodeModrm(); setRM16(sr[regField() & 3]); cyc_ += 2; break;
    case 0x8d: decodeModrm(); r[regField()] = eaOff_; cyc_ += 2; break;
    case 0x8e: decodeModrm(); sr[regField() & 3] = getRM16(); if ((regField() & 3) == SS) inhibitInt_ = true; cyc_ += 2; break;
    case 0x8f: { uint16_t v = pop(); decodeModrm(); setRM16(v); cyc_ += 10; break; }
    case 0x90: cyc_ += 3; break;
    case 0x91: case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: case 0x97: {
        uint16_t t = r[AX]; r[AX] = r[op & 7]; r[op & 7] = t; cyc_ += 4; break;
    }
    case 0x98: r[AX] = uint16_t(int8_t(r[AX])); cyc_ += 2; break;
    case 0x99: r[DX] = (r[AX] & 0x8000) ? 0xffff : 0; cyc_ += 4; break;
    case 0x9a: { uint16_t o = fetch16(), s = fetch16(); push(sr[CS]); push(ip); sr[CS] = s; ip = o; cyc_ += 23; break; }
    case 0x9b: cyc_ += 6; break;
    case 0x9c: push(flags | 0xf002); cyc_ += 9; break;
    case 0x9d: flags = (pop() & 0x0fd5) | 0xf002; cyc_ += 8; checkInterrupts(); break;
    case 0x9e: flags = (flags & 0xff00) | (uint8_t(r[AX] >> 8) & 0xd5) | 2; cyc_ += 3; break;
    case 0x9f: reg8(4) = uint8_t(flags); cyc_ += 2; break;
    case 0xa0: { uint16_t o = fetch16(); reg8(0) = rb(seg, o); cyc_ += 8; break; }
    case 0xa1: { uint16_t o = fetch16(); r[AX] = rw(seg, o); cyc_ += 8; break; }
    case 0xa2: { uint16_t o = fetch16(); wb(seg, o, uint8_t(r[AX])); cyc_ += 9; break; }
    case 0xa3: { uint16_t o = fetch16(); ww(seg, o, r[AX]); cyc_ += 9; break; }
    case 0xa8: alu8(4, uint8_t(r[AX]), fetch8()); cyc_ += 3; break;
    case 0xa9: alu16(4, r[AX], fetch16()); cyc_ += 4; break;
    case 0xb0: case 0xb1: case 0xb2: case 0xb3: case 0xb4: case 0xb5: case 0xb6: case 0xb7:
        reg8(op & 7) = fetch8(); cyc_ += 3; break;
    case 0xb8: case 0xb9: case 0xba: case 0xbb: case 0xbc: case 0xbd: case 0xbe: case 0xbf:
        r[op & 7] = fetch16(); cyc_ += 4; break;
    case 0xc0: { decodeModrm(); uint8_t n = fetch8(); setRM8(shift8(regField(), getRM8(), n)); cyc_ += 5; break; }
    case 0xc1: { decodeModrm(); uint8_t n = fetch8(); setRM16(shift16(regField(), getRM16(), n)); cyc_ += 5; break; }
    case 0xc2: { uint16_t n = fetch16(); ip = pop(); r[SP] += n; cyc_ += 18; break; }
    case 0xc3: ip = pop(); cyc_ += 16; break;
    case 0xc4: case 0xc5: {
        decodeModrm();
        r[regField()] = rw(eaSeg_, eaOff_);
        sr[op == 0xc4 ? ES : DS] = rw(eaSeg_, uint16_t(eaOff_ + 2));
        cyc_ += 18; break;
    }
    case 0xc6: decodeModrm(); setRM8(fetch8()); cyc_ += 4; break;
    case 0xc7: decodeModrm(); setRM16(fetch16()); cyc_ += 4; break;
    case 0xc8: {   // ENTER
        uint16_t size = fetch16();
        uint8_t level = fetch8() & 0x1f;
        push(r[BP]);
        uint16_t frame = r[SP];
        if (level > 0) {
            for (int i = 1; i < level; i++) { r[BP] -= 2; push(rw(SS, r[BP])); }
            push(frame);
        }
        r[BP] = frame;
        r[SP] -= size;
        cyc_ += 15 + 16 * level; break;
    }
    case 0xc9: r[SP] = r[BP]; r[BP] = pop(); cyc_ += 8; break;
    case 0xca: { uint16_t n = fetch16(); ip = pop(); sr[CS] = pop(); r[SP] += n; cyc_ += 25; break; }
    case 0xcb: ip = pop(); sr[CS] = pop(); cyc_ += 22; break;
    case 0xcc: interrupt(3); break;
    case 0xcd: { uint8_t t = fetch8(); interrupt(t); break; }
    case 0xce: if (flags & OF) interrupt(4); else cyc_ += 4; break;
    case 0xcf: ip = pop(); sr[CS] = pop(); flags = (pop() & 0x0fd5) | 0xf002; cyc_ += 28; checkInterrupts(); break;
    case 0xd0: decodeModrm(); setRM8(shift8(regField(), getRM8(), 1)); cyc_ += 2; break;
    case 0xd1: decodeModrm(); setRM16(shift16(regField(), getRM16(), 1)); cyc_ += 2; break;
    case 0xd2: decodeModrm(); setRM8(shift8(regField(), getRM8(), r[CX] & 0xff)); cyc_ += 5; break;
    case 0xd3: decodeModrm(); setRM16(shift16(regField(), getRM16(), r[CX] & 0xff)); cyc_ += 5; break;
    case 0xd4: {   // AAM
        uint8_t b = fetch8();
        if (!b) { interrupt(0); break; }
        uint8_t al = uint8_t(r[AX]);
        r[AX] = uint16_t(((al / b) << 8) | (al % b));
        setSZP8(uint8_t(r[AX]));
        cyc_ += 19; break;
    }
    case 0xd5: {   // AAD
        uint8_t b = fetch8();
        uint8_t al = uint8_t(uint8_t(r[AX]) + uint8_t(r[AX] >> 8) * b);
        r[AX] = al;
        setSZP8(al);
        cyc_ += 15; break;
    }
    case 0xd7: reg8(0) = rb(seg, uint16_t(r[BX] + uint8_t(r[AX]))); cyc_ += 11; break;
    case 0xd8: case 0xd9: case 0xda: case 0xdb: case 0xdc: case 0xdd: case 0xde: case 0xdf:
        decodeModrm(); cyc_ += 6; break;   // no coprocessor fitted
    case 0xe0: case 0xe1: case 0xe2: {
        int8_t d = int8_t(fetch8());
        r[CX]--;
        bool take = r[CX] != 0;
        if (op == 0xe0) take = take && !(flags & ZF);
        if (op == 0xe1) take = take && (flags & ZF);
        if (take) { ip += d; cyc_ += 16; } else cyc_ += 6;
        break;
    }
    case 0xe3: { int8_t d = int8_t(fetch8()); if (!r[CX]) { ip += d; cyc_ += 15; } else cyc_ += 5; break; }
    case 0xe4: reg8(0) = uint8_t(portIn(fetch8(), false)); cyc_ += 10; break;
    case 0xe5: r[AX] = portIn(fetch8(), true); cyc_ += 10; break;
    case 0xe6: portOut(fetch8(), r[AX], false); cyc_ += 9; break;
    case 0xe7: portOut(fetch8(), r[AX], true); cyc_ += 9; break;
    case 0xe8: { int16_t d = int16_t(fetch16()); push(ip); ip += d; cyc_ += 15; break; }
    case 0xe9: { int16_t d = int16_t(fetch16()); ip += d; cyc_ += 14; break; }
    case 0xea: { uint16_t o = fetch16(), s = fetch16(); ip = o; sr[CS] = s; cyc_ += 14; break; }
    case 0xeb: { int8_t d = int8_t(fetch8()); ip += d; cyc_ += 14; break; }
    case 0xec: reg8(0) = uint8_t(portIn(r[DX], false)); cyc_ += 8; break;
    case 0xed: r[AX] = portIn(r[DX], true); cyc_ += 8; break;
    case 0xee: portOut(r[DX], r[AX], false); cyc_ += 7; break;
    case 0xef: portOut(r[DX], r[AX], true); cyc_ += 7; break;
    case 0xf4: halted = true; cyc_ += 2; break;
    case 0xf5: flags ^= CF; cyc_ += 2; break;
    case 0xf6: decodeModrm(); grp3_8(); break;
    case 0xf7: decodeModrm(); grp3_16(); break;
    case 0xf8: flags &= ~CF; cyc_ += 2; break;
    case 0xf9: flags |= CF; cyc_ += 2; break;
    case 0xfa: flags &= ~IF; cyc_ += 2; break;
    case 0xfb: flags |= IF; inhibitInt_ = true; cyc_ += 2; checkInterrupts(); break;
    case 0xfc: flags &= ~DF; cyc_ += 2; break;
    case 0xfd: flags |= DF; cyc_ += 2; break;
    case 0xfe: {
        decodeModrm();
        int f = regField();
        if (f < 2) setRM8(inc8(getRM8(), f == 1));
        else { ip = opIp_; interrupt(6); }
        cyc_ += 3; break;
    }
    case 0xff: {
        decodeModrm();
        int f = regField();
        switch (f) {
        case 0: setRM16(inc16(getRM16(), false)); cyc_ += 3; break;
        case 1: setRM16(inc16(getRM16(), true)); cyc_ += 3; break;
        case 2: { uint16_t t = getRM16(); push(ip); ip = t; cyc_ += 13; break; }
        case 3: { uint16_t o = rw(eaSeg_, eaOff_), s = rw(eaSeg_, uint16_t(eaOff_ + 2)); push(sr[CS]); push(ip); sr[CS] = s; ip = o; cyc_ += 26; break; }
        case 4: ip = getRM16(); cyc_ += 11; break;
        case 5: { uint16_t o = rw(eaSeg_, eaOff_), s = rw(eaSeg_, uint16_t(eaOff_ + 2)); sr[CS] = s; ip = o; cyc_ += 17; break; }
        default: push(getRM16()); cyc_ += 16; break;
        }
        break;
    }
    default:
        // undefined opcode: 80186 type 6 trap
        ip = opIp_;
        interrupt(6);
        break;
    }
}

// ---------------------------------------------------------------- save states

namespace {
template <typename T> void put(std::vector<uint8_t>& o, const T& v) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&v); o.insert(o.end(), p, p + sizeof(T));
}
template <typename T> bool get(const uint8_t*& p, const uint8_t* end, T& v) {
    if (end - p < (long)sizeof(T)) return false;
    memcpy(&v, p, sizeof(T));
    p += sizeof(T);
    return true;
}
}

void I186::saveState(std::vector<uint8_t>& o) const
{
    put(o, r); put(o, sr); put(o, ip); put(o, flags); put(o, halted); put(o, total_);
    put(o, relocation_); put(o, icMask_); put(o, icPriMask_); put(o, icInServ_); put(o, icReq_); put(o, icTmrStat_);
    put(o, icCon_); put(o, extLine_); put(o, nmiPending_); put(o, t_); put(o, timerPre_); put(o, d_); put(o, drq_);
    put(o, chipSel_); put(o, inhibitInt_); put(o, dmaTimerReq_); put(o, tmrInAcc_);
}

bool I186::loadState(const uint8_t*& p, const uint8_t* e)
{
    bool ok = get(p, e, r) && get(p, e, sr) && get(p, e, ip) && get(p, e, flags) && get(p, e, halted) && get(p, e, total_)
        && get(p, e, relocation_) && get(p, e, icMask_) && get(p, e, icPriMask_) && get(p, e, icInServ_) && get(p, e, icReq_)
        && get(p, e, icTmrStat_) && get(p, e, icCon_) && get(p, e, extLine_) && get(p, e, nmiPending_) && get(p, e, t_)
        && get(p, e, timerPre_) && get(p, e, d_) && get(p, e, drq_) && get(p, e, chipSel_) && get(p, e, inhibitInt_)
        && get(p, e, dmaTimerReq_) && get(p, e, tmrInAcc_);
    if (ok) checkInterrupts();
    return ok;
}
