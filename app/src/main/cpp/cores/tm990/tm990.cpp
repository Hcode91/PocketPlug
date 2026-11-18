// PocketPlug - Texas Instruments TM 990/189 University Board (+ Video Board)
// SPDX-License-Identifier: BSD-3-Clause

#include "tm990.h"
#include <cstring>
#include <algorithm>

namespace {
constexpr uint64_t kClockHz(uint64_t us) { return us * 2; }   // microseconds -> 2 MHz cycles
const int kLines = 262;
const int kTopBorder = 40;                    // VERT_DISPLAY_START_NTSC
const uint64_t kDisplayEna = kClockHz(4500);  // 74LS123 watchdog: 4.5 ms
// display refresh: 30 Hz, starting 150 ms in (as MAME)
const uint64_t kDisplayStart = kClockHz(150000);
}

Tm990Core::Tm990Core(bool video) : video_(video), cpu_(this), fb_(256 * 193, 0xff000000u)
{
    memset(mem_, 0, sizeof mem_);
    memset(romMask_, 0, sizeof romMask_);
    memset(&s_, 0, sizeof s_);
    memset(keys_, 0, sizeof keys_);
    memset(digitOut_, 0, sizeof digitOut_);
    memset(vdp_.vram, 0, sizeof vdp_.vram);
    vdp_.reset();
    memset(vdp_.out, 0, sizeof vdp_.out);
}

bool Tm990Core::load(const std::vector<RomFile>& roms, std::string& error)
{
    struct Spec { const char* name; uint16_t at; uint16_t size; bool video; };
    static const Spec specs[] = {
        { "990-469.u32", 0x0800, 0x0800, false },   // extra ROM: the rest of University BASIC
        { "990-469.u33", 0x3000, 0x1000, false },   // boot ROM: University BASIC
        { "demo1000.u13", 0x1000, 0x0800, true },   // video board demo ROMs
        { "demo1800.u11", 0x1800, 0x0800, true },
    };
    for (const Spec& sp : specs) {
        if (sp.video && !video_) continue;
        const RomFile* r = nullptr;
        for (const RomFile& f : roms) if (f.name == sp.name) r = &f;
        if (!r || r->data.size() != sp.size) { error = std::string("Missing ") + sp.name; return false; }
        memcpy(&mem_[sp.at], r->data.data(), sp.size);
        for (int b = sp.at / 0x800; b < (sp.at + sp.size) / 0x800; b++) romMask_[b] = true;
    }
    reset();
    return true;
}

void Tm990Core::reset()
{
    for (int a = 0; a < 0x800; a++) mem_[a] = 0;
    Tms9901 fresh;
    s_.usr = fresh;
    s_.sys = fresh;
    s_.usr.lastLevel = s_.sys.lastLevel = 15;
    s_.digitsel = 0;
    s_.segment = 0;
    s_.displayEnaUntil = 0;
    memset(s_.segState, 0, sizeof s_.segState);
    memset(s_.oldSegState, 0, sizeof s_.oldSegState);
    s_.ledState = 0;
    s_.speaker = 0;
    s_.bogusRead = 0;
    s_.cycles = 0;
    s_.lineNo = 0;
    s_.nextDisplay = kDisplayStart;
    s_.displayCount = 0;
    vdp_.reset();
    cpu_.reset();
    cpu_.irqLevel = 0;
    holdLoad();                               // the board starts through LOAD
}

void Tm990Core::holdLoad()
{
    s_.loadState = true;
    cpu_.loadLine = true;
    s_.loadUntil = s_.cycles + kClockHz(100000);
}

// ---- memory ----
uint8_t Tm990Core::read8(uint16_t a)
{
    a &= 0x3fff;
    if (a < 0x1000) return mem_[a];
    if (a >= 0x3000) return mem_[a];
    if (!video_) return 0;
    if (a < 0x2000) return mem_[a];
    if (a < 0x2800) {
        // TMS9918 read ports: the 9980 reads both bytes, so only the even one is real
        int off = a - 0x2000;
        uint8_t reply;
        if (!(off & 1)) {
            reply = (off & 2) ? vdp_.registerRead() : vdp_.vramRead();
            s_.bogusRead = reply;
        } else reply = s_.bogusRead;
        return reply;
    }
    // joystick port: the buttons (the axis pulse timers never read as expired)
    uint8_t v = 0;
    if (joyButtons_ & 1) v |= 0x04;
    if (joyButtons_ & 2) v |= 0x20;
    return v;
}

void Tm990Core::write8(uint16_t a, uint8_t v)
{
    a &= 0x3fff;
    if (a < 0x0800) { mem_[a] = v; return; }
    if (!video_ || a < 0x2800 || a >= 0x3000) return;
    int off = a - 0x2800;
    if (off & 1) {
        if (off & 2) vdp_.registerWrite(v);
        else vdp_.vramWrite(v);
    }
}

// ---- TMS9901s ----
void Tm990Core::prioritize(Tms9901& c, bool usr)
{
    uint16_t masked = c.intLines;
    if (c.clockReg != 0) {
        masked &= ~(1 << 3);
        if (c.timerPending) masked |= (1 << 3);
    }
    int level = 1;
    masked = uint16_t((masked & c.intMask) >> 1);
    bool found = false;
    while (masked && !found) {
        if ((masked & 1) && (level < 7 || !((c.pioDir >> (22 - level)) & 1))) found = true;
        else { level++; masked >>= 1; }
    }
    if (!found) level = 15;
    c.intLevel = level;
    c.intPending = found;
    if (c.intLevel == c.lastLevel) return;
    c.lastLevel = c.intLevel;
    if (usr) {
        // usr9901_interrupt_callback: the level code goes straight to IC0-IC2
        if (!s_.loadState) cpuIntFromUsr(c.intLevel & 7);
    } else {
        // sys9901_interrupt_callback -> user 9901 INT5
        if (found) s_.usr.intLines |= (1 << 5); else s_.usr.intLines &= ~(1 << 5);
        prioritize(s_.usr, true);
    }
}

void Tm990Core::cpuIntFromUsr(int code)
{
    // TMS9980A: 0/1 reset, 2 LOAD, 3-6 levels 1-4, 7 = clear
    if (code >= 3 && code <= 6) cpu_.irqLevel = code - 2;
    else if (code == 7) cpu_.irqLevel = 0;
    else if (code == 2) cpu_.loadLine = true;
}

void Tm990Core::tick9901(Tms9901& c, bool usr)
{
    c.decrementer = (c.decrementer - 1) & 0x3fff;
    if (!c.clockMode) c.clockRead = c.decrementer;
    if (c.decrementer == 0) {
        if (c.clockReg != 0) { c.timerPending = true; prioritize(c, usr); }
        c.decrementer = c.clockReg;
    }
}

void Tm990Core::pioWrite(Tms9901& c, bool usr, int p, bool v)
{
    c.pioDir |= uint16_t(1 << p);
    if (v) c.pioOut |= uint16_t(1 << p); else c.pioOut &= uint16_t(~(1 << p));
    if (usr) {
        if (p < 4) { if (v) s_.ledState |= uint8_t(1 << p); else s_.ledState &= uint8_t(~(1 << p)); }
    } else sysOut(p, v);
}

void Tm990Core::sysOut(int p, bool v)
{
    if (p < 4) {
        if (v) s_.digitsel |= 1 << p; else s_.digitsel &= ~(1 << p);
    } else if (p < 12) {
        int bit = p - 4;
        if (v) s_.segment |= 1 << bit;
        else {
            s_.segment &= ~(1 << bit);
            if (s_.cycles < s_.displayEnaUntil && s_.digitsel < 10) drawDigit();
        }
    } else if (p == 12) {
        if (!v && s_.digitsel < 10) { s_.displayEnaUntil = s_.cycles + kDisplayEna; drawDigit(); }
    } else if (p == 13) {
        if (v) s_.ledState |= 0x10; else s_.ledState &= ~0x10;
    } else if (p == 14) {
        s_.speaker = v ? 1 : 0;
    }
}

void Tm990Core::drawDigit()
{
    s_.segState[s_.digitsel] |= uint8_t(~s_.segment);
}

int Tm990Core::sysRead(int crubit)
{
    // keyboard row (the digit select) on INT1-INT5, active low
    if (crubit >= 1 && crubit <= 5) {
        if (s_.digitsel < 9) return (keys_[s_.digitsel] >> (crubit - 1)) & 1 ? 0 : 1;
        return 0;
    }
    return 0;   // tape input and everything else
}

int Tm990Core::cruRead(uint16_t bit)
{
    if (bit >= 0x400) return 0;          // TMS9902 (not fitted) and expansion
    bool usr = bit < 0x200;
    Tms9901& c = usr ? s_.usr : s_.sys;
    int cb = bit & 0x1f;
    if (cb == 0) return c.clockMode;
    if (cb > 15) {
        int p = cb - 16;
        if ((c.pioDir >> p) & 1) return (c.pioOut >> p) & 1;
        return 0;
    }
    if (c.clockMode) {
        if (cb == 15) return !c.intPending;
        return (c.clockRead >> (cb - 1)) & 1;
    }
    if (cb > 6 && ((c.pioDir >> (22 - cb)) & 1)) return (c.pioOut >> (22 - cb)) & 1;
    return usr ? 1 : sysRead(cb);
}

void Tm990Core::cruWrite(uint16_t bit, int v)
{
    if (bit >= 0x400) return;
    bool usr = bit < 0x200;
    Tms9901& c = usr ? s_.usr : s_.sys;
    int cb = bit & 0x1f;
    if (cb >= 16) { pioWrite(c, usr, cb - 16, v != 0); return; }
    if (cb == 0) { c.clockMode = v != 0; return; }
    if (cb == 15) {
        if (c.clockMode) {
            if (!v) { c.pioDir = 0; c.pioOut = 0; c.intMask = 0; }
        } else {
            if (v) c.intMask |= 1 << 15; else c.intMask &= ~(1 << 15);
            prioritize(c, usr);
        }
        return;
    }
    if (c.clockMode) {
        if (v) c.clockReg |= uint16_t(1 << (cb - 1)); else c.clockReg &= uint16_t(~(1 << (cb - 1)));
        c.decrementer = c.clockReg;
    } else {
        if (v) c.intMask |= uint16_t(1 << cb); else c.intMask &= uint16_t(~(1 << cb));
        if (cb == 3) c.timerPending = false;
        prioritize(c, usr);
    }
}

void Tm990Core::extOp(int code)
{
    switch (code) {
    case 2: s_.ledState |= 0x40; break;      // IDLE
    case 5: s_.ledState |= 0x20; break;      // CKON: tape deck control
    case 6: s_.ledState &= ~0x20; break;     // CKOF
    case 7: holdLoad(); break;               // LREX
    }
}

void Tm990Core::keyEvent(int code, bool down)
{
    if (code == 0x50) { if (down) holdLoad(); return; }
    int row = code >> 3, bit = code & 7;
    if (row < 0 || row > 8 || bit > 4) return;
    if (down) keys_[row] |= uint16_t(1 << bit); else keys_[row] &= uint16_t(~(1 << bit));
}

void Tm990Core::displayCallback()
{
    for (int i = 0; i < 10; i++) {
        s_.oldSegState[i] |= s_.segState[i];
        digitOut_[i] = s_.oldSegState[i];
        s_.oldSegState[i] = s_.segState[i];
        s_.segState[i] = 0;
    }
    ledOut_ = 0;
    for (int i = 0; i < 7; i++) if (!((s_.ledState >> i) & 1)) ledOut_ |= uint8_t(1 << i);
}

void Tm990Core::renderAudio(int cycles)
{
    s_.sampleAcc += uint32_t(kRate) * uint32_t(cycles);
    while (s_.sampleAcc >= uint32_t(kClock)) {
        s_.sampleAcc -= kClock;
        float x = s_.speaker ? 8000.f : 0.f;
        hp_ = 0.997f * (hp_ + x - lastX_);
        lastX_ = x;
        audio_.push_back(int16_t(hp_));
    }
}

void Tm990Core::runFrame()
{
    if (audio_.size() > size_t(kRate) / 4) audio_.clear();
    // 2 MHz / 60 Hz over 262 lines; targets are absolute so overshoot carries over
    for (int line = 0; line < kLines; line++) {
        s_.lineNo++;
        uint64_t target = s_.lineNo * uint64_t(kClock) / (60 * kLines);
        while (s_.cycles < target) {
            if (s_.loadState && s_.cycles >= s_.loadUntil) {
                // the LOAD line goes back up: TMS9980A level 7 clears everything
                s_.loadState = false;
                cpu_.loadLine = false;
                cpu_.irqLevel = 0;
            }
            int c = cpu_.step();
            if (!cpu_.idle) s_.ledState &= ~0x40;
            uint64_t before = s_.cycles;
            s_.cycles += uint64_t(c);
            // both 9901 decrementers run at 2 MHz / 64
            for (uint64_t t = (before / 64 + 1) * 64; t <= s_.cycles; t += 64) {
                tick9901(s_.usr, true);
                tick9901(s_.sys, false);
            }
            while (s_.cycles >= s_.nextDisplay) {
                displayCallback();
                s_.displayCount++;
                s_.nextDisplay = kDisplayStart + s_.displayCount * uint64_t(kClock) / 30;
            }
            renderAudio(c);
        }
        if (video_) vdp_.line(line - kTopBorder);
    }
    if (video_) memcpy(fb_.data(), vdp_.out, sizeof vdp_.out);
    uint32_t* row = &fb_[192 * 256];
    for (int i = 0; i < 10; i++) row[i] = digitOut_[i];
    row[10] = ledOut_;
}

size_t Tm990Core::readAudio(int16_t* out, size_t maxSamples)
{
    size_t n = std::min(maxSamples, audio_.size());
    if (n) memcpy(out, audio_.data(), n * sizeof(int16_t));
    audio_.erase(audio_.begin(), audio_.begin() + long(n));
    return n;
}

// ---- save states ----
namespace {
const uint32_t kMagic = 0x39393031;   // "9901"
}

bool Tm990Core::saveState(std::vector<uint8_t>& out)
{
    out.clear();
    auto put = [&](const void* p, size_t n) { const uint8_t* b = (const uint8_t*)p; out.insert(out.end(), b, b + n); };
    put(&kMagic, 4);
    put(&s_, sizeof s_);
    put(mem_, 0x800);
    put(&cpu_.pc, 2); put(&cpu_.wp, 2); put(&cpu_.st, 2);
    uint8_t flags = uint8_t((cpu_.loadLine ? 1 : 0) | (cpu_.idle ? 2 : 0));
    put(&flags, 1);
    put(&cpu_.irqLevel, sizeof cpu_.irqLevel);
    put(&cpu_.lastOp, 2);
    put(vdp_.vram, sizeof vdp_.vram);
    put(vdp_.regs, 8);
    put(&vdp_.status, 1); put(&vdp_.readAhead, 1); put(&vdp_.latch, 1); put(&vdp_.fifth, 1); put(&vdp_.addrReg, 2);
    put(digitOut_, 10); put(&ledOut_, 1);
    return true;
}

bool Tm990Core::loadState(const std::vector<uint8_t>& in)
{
    size_t pos = 0;
    auto get = [&](void* p, size_t n) { if (pos + n > in.size()) return false; memcpy(p, &in[pos], n); pos += n; return true; };
    uint32_t magic = 0;
    if (!get(&magic, 4) || magic != kMagic) return false;
    uint8_t flags = 0;
    bool ok = get(&s_, sizeof s_) && get(mem_, 0x800)
        && get(&cpu_.pc, 2) && get(&cpu_.wp, 2) && get(&cpu_.st, 2) && get(&flags, 1)
        && get(&cpu_.irqLevel, sizeof cpu_.irqLevel) && get(&cpu_.lastOp, 2)
        && get(vdp_.vram, sizeof vdp_.vram) && get(vdp_.regs, 8)
        && get(&vdp_.status, 1) && get(&vdp_.readAhead, 1) && get(&vdp_.latch, 1) && get(&vdp_.fifth, 1) && get(&vdp_.addrReg, 2)
        && get(digitOut_, 10) && get(&ledOut_, 1);
    if (!ok) { reset(); return false; }
    cpu_.loadLine = flags & 1;
    cpu_.idle = flags & 2;
    return true;
}
