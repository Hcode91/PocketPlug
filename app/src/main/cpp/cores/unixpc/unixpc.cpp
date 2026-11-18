// PocketPlug - AT&T UNIX PC (PC 7300 / 3B1) desktop computer
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Main loop after FreeBee's main.c: a 10 MHz 68010 run in short slices, the
// disc DMA engine serviced between slices, interrupts re-presented by
// priority every slice, and the 60 Hz tick (screen, timer interrupt,
// keyboard scan) once a frame.

#include "unixpc.h"
#include <cstring>
#include <cmath>
#include <unistd.h>
#include <sys/stat.h>

extern "C" {
#include "freebee/musashi/m68k.h"
#include "freebee/state.h"
#include "freebee/memory.h"
#include "freebee/wd279x.h"
#include "freebee/wd2010.h"
#include "freebee/keyboard.h"
#include "freebee/i8274.h"
#include "freebee/dialer.h"
#include "freebee/utils.h"
}

namespace {

const uint32_t kClock = 10000000;
const uint32_t kCyclesPerFrame = kClock / 60;
const int kSlice = 200;               // FreeBee: 10 MHz / 100 Hz / 500 slices
const int kDmaWordsPerSlice = 20;     // 1 MHz DMA
const size_t kBaseRam = 2048 * 1024, kExpRam = 2048 * 1024;
const int kFloppyTracks = 40, kFloppyHeads = 2;

bool g_active = false;   // FreeBee and Musashi are single-instance

bool readFile(const std::string& path, std::vector<uint8_t>& out)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    out.resize(n > 0 ? size_t(n) : 0);
    bool ok = out.empty() || fread(out.data(), 1, out.size(), f) == out.size();
    fclose(f);
    return ok;
}

// ImageDisk -> raw sectors (cyl, head, sector order); empty on failure
std::vector<uint8_t> imdToRaw(const std::vector<uint8_t>& d, std::string& error)
{
    std::vector<uint8_t> raw;
    size_t p = 0;
    while (p < d.size() && d[p] != 0x1a) p++;
    p++;
    int spt = 0;
    struct Sec { int c, h, s; size_t off; bool fill; uint8_t v; };
    std::vector<Sec> secs;
    while (p + 5 <= d.size()) {
        uint8_t mode = d[p], cyl = d[p + 1], head = d[p + 2], n = d[p + 3], size = d[p + 4];
        (void)mode;
        p += 5;
        if (size != 2) { error = "Only 512-byte sectors are supported"; return raw; }
        if (p + n > d.size()) break;
        std::vector<uint8_t> map(d.begin() + p, d.begin() + p + n);
        p += n;
        if (head & 0x80) p += n;   // cylinder map
        if (head & 0x40) p += n;   // head map
        if (n > spt) spt = n;
        for (int i = 0; i < n && p < d.size(); i++) {
            uint8_t rec = d[p++];
            Sec s{cyl, head & 1, map[i], 0, false, 0};
            if (rec == 0) { continue; }
            if (rec & 1) {           // 1,3,5,7: full data
                s.off = p; p += 512;
            } else {                 // 2,4,6,8: one fill byte
                s.fill = true; s.v = p < d.size() ? d[p] : 0; p++;
            }
            if (p > d.size()) break;
            secs.push_back(s);
        }
    }
    if (spt == 0) { error = "Not a usable .imd image"; return raw; }
    raw.assign(size_t(kFloppyTracks) * kFloppyHeads * spt * 512, 0xe5);
    for (const Sec& s : secs) {
        if (s.c >= kFloppyTracks || s.s < 1 || s.s > spt) continue;
        uint8_t* dst = &raw[((size_t(s.c) * kFloppyHeads + s.h) * spt + (s.s - 1)) * 512];
        if (s.fill) memset(dst, s.v, 512);
        else memcpy(dst, &d[s.off], 512);
    }
    return raw;
}

} // namespace

UnixPcCore::UnixPcCore() : fb_(kW * kH, 0xff000000u) {}

UnixPcCore::~UnixPcCore()
{
    if (loaded_) {
        closeFloppy();
        state_done();
        if (hdFp_) fclose(hdFp_);
        dialer_done();
        g_active = false;
    }
}

bool UnixPcCore::load(const std::vector<RomFile>& roms, std::string& error)
{
    for (const RomFile& r : roms) {
        if (r.name.find("14c") != std::string::npos) rom14c_ = r.data;
        else if (r.name.find("15c") != std::string::npos) rom15c_ = r.data;
    }
    if (rom14c_.empty() || rom15c_.empty()) { error = "Needs 72-00616.14c and 72-00617.15c"; return false; }
    if (g_active) { error = "Only one UNIX PC can run at a time"; return false; }
    g_active = true;
    loaded_ = true;
    memset(&state, 0, sizeof(state));
    state_set_roms(rom14c_.data(), rom14c_.size(), rom15c_.data(), rom15c_.size());
    dialer_init();
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68010);
    reset();
    if (!state.base_ram) { error = "ROM images are the wrong size"; return false; }
    return true;
}

void UnixPcCore::reset()
{
    if (!loaded_) return;
    // state_init re-creates RAM and the controllers; the drives go back in afterwards
    WD2010_CTX hd = state.hdc_ctx;
    if (state_init(kBaseRam, kExpRam) != 0) return;
    memset(&state.hdc_ctx, 0, sizeof(state.hdc_ctx));
    for (int i = 0; i < 2; i++) if (hd.data[i]) free(hd.data[i]);
    if (hdFp_) {
        wd2010_init(&state.hdc_ctx, hdFp_, 0, 512, kHdSpt, kHdHeads);
        state.hdc_disc0 = hdFp_;
    }
    if (floppyFp_) {
        wd2797_load(&state.fdc_ctx, floppyFp_, 512, kFloppyHeads, kFloppyTracks, 1);
        state.fdc_disc = floppyFp_;
    }
    m68k_pulse_reset();
    clockCycles_ = 0;
}

void UnixPcCore::runDma()
{
    if (state.dmaen) {
        int num = 0;
        while (state.dma_count < 0x4000) {
            if (num > kDmaWordsPerSlice) break;
            if (state.dma_dev == DMA_DEV_FD) {
                if (!wd2797_get_drq(&state.fdc_ctx)) break;
                fdcBusy_ = 20;
            } else if (state.dma_dev == DMA_DEV_HD0) {
                if (!wd2010_get_drq(&state.hdc_ctx)) break;
                hdcBusy_ = 20;
            } else break;
            if (!access_check_dma(state.dma_reading)) break;
            uint32_t a = mapAddr(state.dma_address, !state.dma_reading);
            uint16_t d = 0;
            if (!state.dma_reading) {
                if (state.dma_dev == DMA_DEV_FD) {
                    d = uint16_t(wd2797_read_reg(&state.fdc_ctx, WD2797_REG_DATA) << 8);
                    d |= wd2797_read_reg(&state.fdc_ctx, WD2797_REG_DATA);
                } else {
                    d = uint16_t(wd2010_read_data(&state.hdc_ctx) << 8);
                    d |= wd2010_read_data(&state.hdc_ctx);
                }
                if (a <= 0x1fffff) WR16(state.base_ram, a, state.base_ram_size - 1, d);
                else WR16(state.exp_ram, a - 0x200000, state.exp_ram_size - 1, d);
            } else {
                if (a <= 0x1fffff) d = RD16(state.base_ram, a, state.base_ram_size - 1);
                else if (a <= state.exp_ram_size + 0x200000 - 1) d = RD16(state.exp_ram, a - 0x200000, state.exp_ram_size - 1);
                else d = 0xffff;
                if (state.dma_dev == DMA_DEV_FD) {
                    wd2797_write_reg(&state.fdc_ctx, WD2797_REG_DATA, d >> 8);
                    wd2797_write_reg(&state.fdc_ctx, WD2797_REG_DATA, d & 0xff);
                    floppyDirty_ = true;
                } else {
                    wd2010_write_data(&state.hdc_ctx, d >> 8);
                    wd2010_write_data(&state.hdc_ctx, d & 0xff);
                }
            }
            state.dma_address += 2;
            num++;
            state.dma_count++;
        }
        if (state.dma_count >= 0x4000) state.dma_count = 0x3fff;
    } else if (wd2010_get_drq(&state.hdc_ctx)) {
        wd2010_dma_miss(&state.hdc_ctx);
    } else if (wd2797_get_drq(&state.fdc_ctx)) {
        wd2797_dma_miss(&state.fdc_ctx);
    }
}

void UnixPcCore::updateIrq()
{
    if (state.timer_int_latch) m68k_set_irq(6);
    else if (i8274_get_irq(&state.serial_ctx)) m68k_set_irq(4);
    else if (keyboard_get_irq(&state.kbd)) m68k_set_irq(3);
    else if (wd2797_get_irq(&state.fdc_ctx) || wd2010_get_irq(&state.hdc_ctx)) m68k_set_irq(2);
    else m68k_set_irq(0);
}

void UnixPcCore::runFrame()
{
    if (!loaded_ || !state.base_ram) return;
    // mouse: one packet per frame when something changed
    int dx = int(mouseX_), dy = int(mouseY_);
    if (dx || dy || buttons_ != lastButtons_) {
        mouseX_ -= dx; mouseY_ -= dy;
        while (dx || dy || buttons_ != lastButtons_) {
            int sx = dx > 127 ? 127 : dx < -127 ? -127 : dx;
            int sy = dy > 127 ? 127 : dy < -127 ? -127 : dy;
            mouse_event(&state.kbd, sx, sy, int(buttons_ & 7));
            lastButtons_ = buttons_;
            dx -= sx; dy -= sy;
        }
    }

    uint32_t ran = 0;
    while (ran < kCyclesPerFrame) {
        ran += uint32_t(m68k_execute(kSlice));
        runDma();
        updateIrq();
    }
    clockCycles_ += ran;
    wd2010_tick(&state.hdc_ctx, 17);

    render();
    if (state.timer_clrsint) state.timer_int_latch = true;
    keyboard_scan(&state.kbd);
    if (fdcBusy_ > 0) fdcBusy_--;
    if (hdcBusy_ > 0) hdcBusy_--;

    size_t n = kRate / 60;
    size_t old = audio_.size();
    if (old < size_t(kRate) / 4) {
        audio_.resize(old + n);
        dialer_render(&audio_[old], int(n));
    }
}

void UnixPcCore::render()
{
    uint32_t fg = 0xff52ff8au, bg = 0xff000000u;   // P39-style green phosphor
    if (state.reverse_video) std::swap(fg, bg);
    uint32_t addr = 0;
    uint32_t* p = fb_.data();
    for (int y = 0; y < kH; y++) {
        for (int x = 0; x < kW; x += 16) {
            uint16_t v = RD16(state.vram, addr, sizeof(state.vram) - 1);
            addr += 2;
            for (int b = 0; b < 16; b++) { *p++ = (v & 1) ? fg : bg; v >>= 1; }
        }
    }
}

size_t UnixPcCore::readAudio(int16_t* out, size_t maxSamples)
{
    size_t n = std::min(maxSamples, audio_.size());
    if (n) memcpy(out, audio_.data(), n * sizeof(int16_t));
    audio_.erase(audio_.begin(), audio_.begin() + n);
    return n;
}

void UnixPcCore::setInputs(uint32_t pressed) { buttons_ = pressed & 7; }

void UnixPcCore::setAnalog(int stick, float x, float y)
{
    if (stick != 9) return;
    mouseX_ += x;
    mouseY_ += y;
}

void UnixPcCore::keyEvent(int code, bool down)
{
    if (!loaded_) return;
    keyboard_key(&state.kbd, code & 0x7f, down);
}

void UnixPcCore::closeFloppy()
{
    if (floppyFp_) {
        wd2797_unload(&state.fdc_ctx);
        fclose(floppyFp_);
        floppyFp_ = nullptr;
        state.fdc_disc = nullptr;
    }
}

bool UnixPcCore::mediaInsert(int drive, const std::string& path, std::string& error)
{
    if (!loaded_) { error = "Not running"; return false; }
    if (drive == 0) {
        std::vector<uint8_t> d;
        if (!readFile(path, d)) { error = "Can't read " + path; return false; }
        std::vector<uint8_t> raw;
        if (d.size() >= 4 && memcmp(d.data(), "IMD ", 4) == 0) {
            raw = imdToRaw(d, error);
            if (raw.empty()) return false;
        } else {
            size_t track = size_t(kFloppyTracks) * kFloppyHeads * 512;
            if (d.empty() || d.size() % track != 0 || d.size() / track > 18) {
                error = "Unrecognised floppy image (expected .imd or a 40-track raw .img)";
                return false;
            }
            raw = std::move(d);
        }
        closeFloppy();
        floppy_ = std::move(raw);
        floppyFp_ = fmemopen(floppy_.data(), floppy_.size(), "r+b");
        if (!floppyFp_) { error = "Out of memory"; return false; }
        if (wd2797_load(&state.fdc_ctx, floppyFp_, 512, kFloppyHeads, kFloppyTracks, 1) != WD2797_ERR_OK) {
            fclose(floppyFp_); floppyFp_ = nullptr;
            error = "Bad floppy geometry";
            return false;
        }
        state.fdc_disc = floppyFp_;
        floppyDirty_ = false;
        floppyPath_ = path;
        return true;
    }
    if (drive == 1) return attachHardDisk(path, error);
    error = "No such drive";
    return false;
}

bool UnixPcCore::attachHardDisk(const std::string& path, std::string& error)
{
    struct stat st;
    if (stat(path.c_str(), &st) != 0 || st.st_size == 0) {
        // a new, unformatted drive (sparse file: only written sectors take space)
        FILE* f = fopen(path.c_str(), "wb");
        if (!f) { error = "Can't create " + path; return false; }
        int ok = ftruncate(fileno(f), off_t(kHdCyl) * kHdHeads * kHdSpt * 512);
        fclose(f);
        if (ok != 0) { error = "Not enough space for the hard disk"; return false; }
    }
    FILE* f = fopen(path.c_str(), "r+b");
    if (!f) { error = "Can't open " + path; return false; }
    WD2010_CTX probe;
    memset(&probe, 0, sizeof(probe));
    if (wd2010_init(&probe, f, 0, 512, kHdSpt, kHdHeads) != WD2010_ERR_OK) {
        for (int i = 0; i < 2; i++) if (probe.data[i]) free(probe.data[i]);
        fclose(f);
        error = "Unrecognised hard disk image";
        return false;
    }
    for (int i = 0; i < 2; i++) if (probe.data[i]) free(probe.data[i]);
    if (hdFp_) fclose(hdFp_);
    hdFp_ = f;
    // the controller is re-initialised with the new drive (the app resets after a swap)
    for (int i = 0; i < 2; i++) if (state.hdc_ctx.data[i]) { free(state.hdc_ctx.data[i]); }
    memset(&state.hdc_ctx, 0, sizeof(state.hdc_ctx));
    wd2010_init(&state.hdc_ctx, hdFp_, 0, 512, kHdSpt, kHdHeads);
    state.hdc_disc0 = hdFp_;
    return true;
}

bool UnixPcCore::mediaSave(int drive, const std::string& path)
{
    if (drive == 1) { if (hdFp_) fflush(hdFp_); return hdFp_ != nullptr; }
    if (drive != 0 || !floppyFp_ || !floppyDirty_) return false;
    fflush(floppyFp_);
    // written back as a raw image (the .imd format can't hold every change)
    std::string tmp = path + ".tmp";
    FILE* f = fopen(tmp.c_str(), "wb");
    if (!f) return false;
    bool ok = fwrite(floppy_.data(), 1, floppy_.size(), f) == floppy_.size();
    ok = (fclose(f) == 0) && ok;
    if (!ok || rename(tmp.c_str(), path.c_str()) != 0) { remove(tmp.c_str()); return false; }
    floppyDirty_ = false;
    return true;
}

void UnixPcCore::mediaEject(int drive)
{
    if (drive == 0) closeFloppy();
}

int UnixPcCore::mediaActivity()
{
    // bits 0-1: floppy / hard disk busy; bits 8-11: the four status LEDs
    return (fdcBusy_ > 0 ? 1 : 0) | (hdcBusy_ > 0 ? 2 : 0) | ((state.leds & 0x0f) << 8);
}

// Save states would have to carry the whole hard disk to stay consistent
// with it, so the UNIX PC (like the real one) is shut down and booted instead.
bool UnixPcCore::saveState(std::vector<uint8_t>& out) { (void)out; return false; }
bool UnixPcCore::loadState(const std::vector<uint8_t>& in) { (void)in; return false; }

uint32_t UnixPcCore::pc() const { return m68k_get_reg(nullptr, M68K_REG_PC); }

std::string UnixPcCore::screenDump() const
{
    std::string s = "P1\n720 348\n";
    for (int y = 0; y < kH; y++) {
        for (int x = 0; x < kW; x++) s += (fb_[y * kW + x] & 0xff00) == 0xff00 ? "1 " : (fb_[y * kW + x] == 0xff000000u ? "0 " : "1 ");
        s += '\n';
    }
    return s;
}
