// PocketPlug - 4004 Nixie Clock
// SPDX-License-Identifier: BSD-3-Clause

#include "nixieclock.h"
#include <cstring>
#include <ctime>

NixieClockCore::NixieClockCore() : cpu_(this)
{
    memset(rom_, 0xff, sizeof rom_);
    memset(nixie_, 0, sizeof nixie_);
    for (int i = 0; i < 8; i++) fb_[i] = 10;
    fb_[6] = 0;
}

bool NixieClockCore::load(const std::vector<RomFile>& roms, std::string& error)
{
    int found = 0;
    for (const RomFile& r : roms) {
        // clock.u30 .. clock.u45: one 256-byte page each, in order
        size_t dot = r.name.rfind(".u");
        if (dot == std::string::npos) continue;
        int n = atoi(r.name.c_str() + dot + 2) - 30;
        if (n < 0 || n > 15 || r.data.size() != 0x100) continue;
        memcpy(&rom_[n * 0x100], r.data.data(), 0x100);
        found |= 1 << n;
    }
    if (found != 0xffff) { error = "Needs clock.u30 to clock.u45"; return false; }
    reset();
    return true;
}

void NixieClockCore::reset()
{
    // the phone's time keeps going across a reset
    if (haveWall_) { wall_ += int64_t((cycles_ - wallAtCycle_) / kCyclesPerSec); wallAtCycle_ = 0; }
    cpu_.reset();
    memset(cpu_.ram, 0, sizeof cpu_.ram);
    memset(cpu_.status, 0, sizeof cpu_.status);
    memset(nixie_, 0, sizeof nixie_);
    cycles_ = 0;
    dac_ = 0;
    bootFrames_ = 0;
    running_ = false;
    updateDisplay();
}

void NixieClockCore::setOption(int id, int value)
{
    if (id == 1) tickTock_ = value != 0;
    else if (id == 2) hz_ = value == 60 ? 60 : 50;
}

bool NixieClockCore::testPin()
{
    // the mains frequency, squared up: high for one half cycle, low for the other
    return ((cycles_ * uint64_t(hz_) * 2) / kCyclesPerSec) & 1;
}

uint8_t NixieClockCore::romPortRead(uint16_t addr)
{
    // ROM chip 0's port: the two buttons (active low) and the setting jumpers
    if (((addr >> 4) & 0x0f) != 0) return 0x0f;
    uint8_t v = 0;
    if (!(buttons_ & BTN_SELECT)) v |= 1;
    if (!(buttons_ & BTN_BROWSE)) v |= 2;
    if (!tickTock_) v |= 4;
    if (hz_ == 60) v |= 8;
    return v;
}

void NixieClockCore::romPortWrite(uint16_t addr, uint8_t v)
{
    int chip = (addr >> 4) & 0x0f;
    if (chip == 15) {
        // neon dots, bit 3 first
        fb_[6] = ((v >> 3) & 1) | (((v >> 2) & 1) << 1) | (((v >> 1) & 1) << 2) | ((v & 1) << 3);
        return;
    }
    nixie_[chip] = v & 0x0f;
    updateDisplay();
}

void NixieClockCore::ramPortWrite(uint8_t addr, uint8_t v)
{
    if (addr == 0) dac_ = v & 1;   // tick-tock speaker
}

void NixieClockCore::updateDisplay()
{
    // each tube is a 1-of-10 cathode select spread over the ROM ports
    auto num = [](uint16_t v) -> uint32_t {
        for (int i = 0; i < 10; i++) if (v & (1 << i)) return uint32_t(i);
        return 10;
    };
    const uint16_t* n = nixie_;
    fb_[5] = num(uint16_t(((n[2] & 3) << 8) | (n[1] << 4) | n[0]));
    fb_[4] = num(uint16_t((n[4] << 6) | (n[3] << 2) | (n[2] >> 2)));
    fb_[3] = num(uint16_t(((n[7] & 3) << 8) | (n[6] << 4) | n[5]));
    fb_[2] = num(uint16_t((n[9] << 6) | (n[8] << 2) | (n[7] >> 2)));
    fb_[1] = num(uint16_t(((n[12] & 3) << 8) | (n[11] << 4) | n[10]));
    fb_[0] = num(uint16_t((n[14] << 6) | (n[13] << 2) | (n[12] >> 2)));
}

void NixieClockCore::runCycles(uint32_t n, bool sound)
{
    uint64_t end = cycles_ + n;
    while (cycles_ < end) {
        int c = cpu_.step();
        cycles_ += uint64_t(c);
        if (!sound) continue;
        sampleAcc_ += uint32_t(kRate) * uint32_t(c);
        while (sampleAcc_ >= uint32_t(kCyclesPerSec)) {
            sampleAcc_ -= kCyclesPerSec;
            // the speaker is AC coupled: each edge of the 1-bit output is a click
            float x = dac_ ? 9000.f : 0.f;
            hp_ = 0.995f * (hp_ + x - lastX_);
            lastX_ = x;
            audio_.push_back(int16_t(hp_));
        }
    }
}

void NixieClockCore::runFrame()
{
    if (audio_.size() > size_t(kRate) / 4) audio_.clear();
    // after power-on the tubes stay dark with the dots flashing until Select
    // is pressed; it then starts at 00:00:00 and we set it from the phone
    uint32_t held = buttons_;
    if (!running_) {
        bootFrames_++;
        if (bootFrames_ >= 60 && bootFrames_ < 70) buttons_ |= BTN_SELECT;
    }
    // 78125 / 60 = 1302 1/12 cycles a frame
    runCycles(1302 + (++frames_ % 12 == 0 ? 1 : 0), true);
    buttons_ = held;
    if (!running_ && bootFrames_ >= 300) {
        running_ = true;
        applyWallClock();
    }
}

void NixieClockCore::applyWallClock()
{
    if (!haveWall_ || !running_) return;
    int64_t t = wall_ + int64_t((cycles_ - wallAtCycle_) / kCyclesPerSec);
    time_t tt = time_t(t);
    struct tm tm;
    gmtime_r(&tt, &tm);
    uint8_t* r = cpu_.ram;
    r[kRamTime + 0] = uint8_t(6 + tm.tm_sec % 10);
    r[kRamTime + 1] = uint8_t(10 + tm.tm_sec / 10);
    r[kRamTime + 2] = uint8_t(6 + tm.tm_min % 10);
    r[kRamTime + 3] = uint8_t(10 + tm.tm_min / 10);
    r[kRamTime + 4] = uint8_t(6 + tm.tm_hour % 10);
    r[kRamTime + 5] = uint8_t(13 + tm.tm_hour / 10);
    int mon = tm.tm_mon + 1, year = tm.tm_year % 100;
    r[kRamDate + 0] = uint8_t(6 + tm.tm_mday % 10);
    r[kRamDate + 1] = uint8_t(12 + tm.tm_mday / 10);
    r[kRamDate + 2] = uint8_t(6 + mon % 10);
    r[kRamDate + 3] = uint8_t(14 + mon / 10);
    r[kRamDate + 4] = uint8_t(6 + year % 10);
    r[kRamDate + 5] = uint8_t(6 + year / 10);
}

size_t NixieClockCore::readAudio(int16_t* out, size_t maxSamples)
{
    size_t n = std::min(maxSamples, audio_.size());
    if (n) memcpy(out, audio_.data(), n * sizeof(int16_t));
    audio_.erase(audio_.begin(), audio_.begin() + long(n));
    return n;
}

bool NixieClockCore::advanceTime(double seconds)
{
    // the clock counts mains cycles, so catching up is just running it
    if (seconds <= 0) return true;
    runCycles(uint32_t(seconds * kCyclesPerSec), false);
    return true;
}

void NixieClockCore::setWallClock(int64_t localSeconds)
{
    wall_ = localSeconds;
    wallAtCycle_ = cycles_;
    haveWall_ = true;
    applyWallClock();
}

// ---- save states ----
namespace {
const uint32_t kMagic = 0x34303034;   // "4004"
template <class T> void put(std::vector<uint8_t>& o, const T& v)
{
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&v);
    o.insert(o.end(), p, p + sizeof v);
}
template <class T> bool get(const std::vector<uint8_t>& in, size_t& pos, T& v)
{
    if (pos + sizeof v > in.size()) return false;
    memcpy(&v, &in[pos], sizeof v);
    pos += sizeof v;
    return true;
}
}

bool NixieClockCore::saveState(std::vector<uint8_t>& out)
{
    out.clear();
    put(out, kMagic);
    put(out, cpu_.ram); put(out, cpu_.status);
    put(out, cpu_.a); put(out, cpu_.c); put(out, cpu_.r); put(out, cpu_.stack);
    put(out, cpu_.sp); put(out, cpu_.src); put(out, cpu_.bank);
    put(out, nixie_); put(out, fb_[6]); put(out, cycles_); put(out, dac_);
    put(out, bootFrames_); put(out, running_);
    return true;
}

bool NixieClockCore::loadState(const std::vector<uint8_t>& in)
{
    size_t pos = 0;
    uint32_t magic = 0;
    int64_t now = wall_ + int64_t((cycles_ - wallAtCycle_) / kCyclesPerSec);
    if (!get(in, pos, magic) || magic != kMagic) return false;
    bool ok = get(in, pos, cpu_.ram) && get(in, pos, cpu_.status)
        && get(in, pos, cpu_.a) && get(in, pos, cpu_.c) && get(in, pos, cpu_.r) && get(in, pos, cpu_.stack)
        && get(in, pos, cpu_.sp) && get(in, pos, cpu_.src) && get(in, pos, cpu_.bank)
        && get(in, pos, nixie_) && get(in, pos, fb_[6]) && get(in, pos, cycles_) && get(in, pos, dac_)
        && get(in, pos, bootFrames_) && get(in, pos, running_);
    if (!ok) { reset(); return false; }
    // a clock shows the time now, not the time the state was saved
    wall_ = now;
    wallAtCycle_ = cycles_;
    updateDisplay();
    applyWallClock();
    return ok;
}
