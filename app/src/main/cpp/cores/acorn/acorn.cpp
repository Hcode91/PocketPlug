// PocketPlug - Acorn A7000 / A7000+ (RISC OS)
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Glue between RPCEmu (./rpcemu) and PocketPlug's Core. One frame = 1/60 s of
// emulated time. The clock moves with the instructions run (one a cycle at
// 32 MHz, or 48 MHz on the A7000+); the IOMD timer (500 Hz) and vsync follow
// it, and RISC OS's idle calls (Portable_Idle) move it on instead of sleeping.

#include "acorn.h"
#include "rpcemu_glue.h"

#include <csetjmp>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

extern "C" {
#include "rpcemu/rpcemu.h"
#include "rpcemu/fdc.h"
#include "rpcemu/keyboard.h"
#include "rpcemu/models.h"
#include "rpcemu/arm.h"
extern int motoron;
void savecmos(void);
typedef struct { const char* name; const uint8_t* data; size_t size; } acorn_module_t;
extern const acorn_module_t acorn_modules[];
extern const uint8_t acorn_default_cmos[256];
}

namespace {

AcornCore* g_core = nullptr;
jmp_buf g_fatalJump;
bool g_inFrame = false;
std::string g_fatal;

const RomFile* find(const std::vector<RomFile>& roms, const char* name)
{
    for (auto& r : roms) if (r.name == name) return &r;
    return nullptr;
}

std::string dirOf(const std::string& p)
{
    size_t s = p.find_last_of('/');
    return s == std::string::npos ? std::string(".") : p.substr(0, s);
}

void mkdirs(const std::string& p) { mkdir(p.c_str(), 0755); }

bool writeAll(const std::string& path, const uint8_t* d, size_t n)
{
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = fwrite(d, 1, n, f) == n;
    fclose(f);
    return ok;
}

bool exists(const std::string& p) { struct stat st; return stat(p.c_str(), &st) == 0; }

// E0-prefixed PS/2 set-2 keys, as codes 0x90 + index
const uint8_t kExtended[] = {
    0x11, // 0x90 right Alt
    0x14, // 0x91 right Ctrl
    0x70, // 0x92 Insert
    0x6c, // 0x93 Home
    0x7d, // 0x94 Page Up
    0x71, // 0x95 Delete
    0x69, // 0x96 End (Copy on Acorn keyboards)
    0x7a, // 0x97 Page Down
    0x75, // 0x98 Up
    0x6b, // 0x99 Left
    0x72, // 0x9a Down
    0x74, // 0x9b Right
    0x4a, // 0x9c keypad /
    0x5a, // 0x9d keypad Enter
};

} // namespace

extern "C" void acorn_core_video(const uint32_t* buffer, int xsize, int ysize, int yl, int yh,
                                 int double_size, int host_xsize, int host_ysize)
{
    if (g_core) g_core->video(buffer, xsize, ysize, yl, yh, double_size, host_xsize, host_ysize);
}

extern "C" void acorn_core_sound(uint32_t samplerate, const int16_t* stereo, uint32_t frames)
{
    if (g_core) g_core->sound(samplerate, stereo, frames);
}

extern "C" void acorn_core_fatal(const char* msg)
{
    g_fatal = msg;
    fprintf(stderr, "RPCEmu fatal: %s\n", msg);
    if (g_inFrame) longjmp(g_fatalJump, 1);
    abort();
}

AcornCore::AcornCore(Model m) : model_(m), fb_(kW * kH, 0xff000000u) {}

AcornCore::~AcornCore()
{
    if (loaded_) {
        // floppies are written as they change; the CMOS is kept for next time
        savecmos();
        g_core = nullptr;
    }
}

bool AcornCore::load(const std::vector<RomFile>& roms, std::string& error)
{
    if (g_core) { error = "Only one Acorn machine can run at a time"; return false; }
    // the ROM is two 16-bit-wide chips: interleave them a halfword at a time
    const char* lo = model_ == kA7000 ? "1203,101-01.bin" : "1203,261-01.bin";
    const char* hi = model_ == kA7000 ? "1203,102-01.bin" : "1203,262-01.bin";
    const RomFile* a = find(roms, lo);
    const RomFile* b = find(roms, hi);
    if (!a || !b || a->data.size() != b->data.size() || a->data.empty()) {
        error = std::string("Missing the RISC OS ROMs (") + lo + ", " + hi + ")";
        return false;
    }
    std::vector<uint8_t> rom(a->data.size() * 2);
    for (size_t i = 0; i + 1 < a->data.size(); i += 2) {
        rom[i * 2 + 0] = a->data[i];
        rom[i * 2 + 1] = a->data[i + 1];
        rom[i * 2 + 2] = b->data[i];
        rom[i * 2 + 3] = b->data[i + 1];
    }

    // RPCEmu's data directory: ROM, CMOS, HostFS, support modules
    dataDir_ = dirOf(a->path) + (model_ == kA7000 ? "/.acorn-a7000" : "/.acorn-a7000p");
    mkdirs(dataDir_);
    mkdirs(dataDir_ + "/roms");
    mkdirs(dataDir_ + "/poduleroms");
    mkdirs(hostfsDir());
    if (DIR* d = opendir((dataDir_ + "/roms").c_str())) {
        while (dirent* e = readdir(d)) if (e->d_name[0] != '.') unlink((dataDir_ + "/roms/" + e->d_name).c_str());
        closedir(d);
    }
    if (!writeAll(dataDir_ + "/roms/riscos.rom", rom.data(), rom.size())) { error = "Can't write the ROM image"; return false; }
    for (const acorn_module_t* m = acorn_modules; m->name; m++)
        writeAll(dataDir_ + "/poduleroms/" + m->name, m->data, m->size);
    if (!exists(dataDir_ + "/cmos.ram"))
        writeAll(dataDir_ + "/cmos.ram", acorn_default_cmos, 256);

    acorn_glue_set_datadir(dataDir_.c_str());
    g_core = this;
    g_inFrame = true;
    if (setjmp(g_fatalJump)) {
        g_inFrame = false;
        g_core = nullptr;
        error = "RPCEmu: " + g_fatal;
        return false;
    }
    config_load(&config);
    acorn_glue_set_mhz(model_ == kA7000 ? 32 : 48);
    rpcemu_model_changed(model_ == kA7000 ? Model_A7000 : Model_A7000Plus);
    rpcemu_start();
    g_inFrame = false;
    loaded_ = true;
    return true;
}

void AcornCore::reset()
{
    if (loaded_) resetrpc();
}

void AcornCore::setInputs(uint32_t pressed)
{
    buttons_ = pressed & 7;
}

void AcornCore::setAnalog(int stick, float x, float y)
{
    if (stick != 9) return;
    mx_ += x;
    my_ += y;
}

void AcornCore::keyEvent(int code, bool down)
{
    if (code <= 0 || code > 0xff) return;
    keyEvents_.push_back(code | (down ? 0x1000 : 0));
}

void AcornCore::runFrame()
{
    if (!loaded_) return;
    g_inFrame = true;
    if (setjmp(g_fatalJump)) {
        g_inFrame = false;
        loaded_ = false;   // the machine has stopped
        return;
    }

    // keyboard: PS/2 set 2 make / break codes
    for (int e : keyEvents_) {
        int code = e & 0xff;
        bool down = e & 0x1000;
        uint8_t sc[3];
        if (code >= 0x90 && code < 0x90 + int(sizeof(kExtended))) {
            uint8_t k = kExtended[code - 0x90];
            if (down) { sc[0] = 0xe0; sc[1] = k; keyboard_key_press(sc); }
            else { sc[0] = 0xe0; sc[1] = 0xf0; sc[2] = k; keyboard_key_release(sc); }
        } else if (code <= 0x83) {
            sc[0] = uint8_t(code);
            if (down) keyboard_key_press(sc); else keyboard_key_release(sc);
        }
    }
    keyEvents_.clear();

    // mouse: RPCEmu counts buttons as left 1, right 2, middle 4
    int dx = int(mx_), dy = int(my_);
    if (dx || dy) {
        mx_ -= dx; my_ -= dy;
        mouse_mouse_move_relative(dx, dy);
    }
    if (buttons_ != sentButtons_) {
        auto map = [](uint32_t b) { return int((b & 1) | (b & 2 ? 4 : 0) | (b & 4 ? 2 : 0)); };
        int on = map(buttons_ & ~sentButtons_), off = map(sentButtons_ & ~buttons_);
        if (on) mouse_mouse_press(on);
        if (off) mouse_mouse_release(off);
        sentButtons_ = buttons_;
    }

    // run 1/60 s of emulated time
    const uint64_t frameNs = 1000000000ull / 60;
    uint64_t end = rpcemu_nsec_timer_ticks() + frameNs;
    acorn_glue_set_frame_end(end);
    g_acorn_idle_break = 0;
    while (rpcemu_nsec_timer_ticks() < end) {
        execrpcemu();                      // 20000 instructions, the clock moving with them
        acorn_glue_timers();
        if (g_acorn_idle_break) break;     // idle: the idle loop moved the clock on
    }
    g_acorn_idle_break = 0;
    if (motoron) busy_ = 20;
    else if (busy_ > 0) busy_--;
    g_inFrame = false;
}

void AcornCore::video(const uint32_t* buf, int xs, int ys, int yl, int yh, int dbl, int hx, int hy)
{
    (void)yl; (void)yh; (void)dbl;
    if (xs <= 0 || ys <= 0 || hx <= 0 || hy <= 0) return;
    // the mode's picture (doubled for low-resolution modes) scaled into 800 x 600
    float s = std::min(float(kW) / hx, float(kH) / hy);
    int ow = int(hx * s), oh = int(hy * s);
    int ox = (kW - ow) / 2, oy = (kH - oh) / 2;
    for (int y = 0; y < kH; y++) {
        uint32_t* d = &fb_[y * kW];
        if (y < oy || y >= oy + oh) { for (int x = 0; x < kW; x++) d[x] = 0xff000000u; continue; }
        int sy = (y - oy) * ys / oh;
        const uint32_t* srow = buf + sy * xs;
        for (int x = 0; x < kW; x++) {
            if (x < ox || x >= ox + ow) { d[x] = 0xff000000u; continue; }
            int sx = (x - ox) * xs / ow;
            d[x] = 0xff000000u | (srow[sx] & 0xffffff);
        }
    }
}

void AcornCore::sound(uint32_t rate, const int16_t* st, uint32_t frames)
{
    if (!rate || audio_.size() > size_t(kRate)) return;
    // stereo at the VIDC's rate -> mono 44.1 kHz
    double step = double(rate) / kRate;
    while (resamplePos_ < frames) {
        size_t i = size_t(resamplePos_);
        audio_.push_back(int16_t((int(st[i * 2]) + int(st[i * 2 + 1])) / 2));
        resamplePos_ += step;
    }
    resamplePos_ -= frames;
}

size_t AcornCore::readAudio(int16_t* out, size_t maxSamples)
{
    size_t n = std::min(audio_.size(), maxSamples);
    memcpy(out, audio_.data(), n * sizeof(int16_t));
    audio_.erase(audio_.begin(), audio_.begin() + n);
    return n;
}

bool AcornCore::mediaInsert(int drive, const std::string& path, std::string& error)
{
    if (!loaded_) { error = "Not running"; return false; }
    if (drive == 0) {
        // RPCEmu tells the format by the extension (.adf / .adl / .hfe ...)
        std::string p = path;
        if (p.size() < 5 || p[p.size() - 4] != '.') { error = "Floppy images need an extension (.adf, .adl or .hfe)"; return false; }
        fdc_image_save(p.c_str(), 0);
        fdc_image_load(p.c_str(), 0);
        return true;
    }
    if (drive == 2) {
        // into HostFS: RISC OS sees it in the HostFS drive on the icon bar
        size_t s = path.find_last_of('/');
        std::string name = s == std::string::npos ? path : path.substr(s + 1);
        if (name.find(',') == std::string::npos) {
            // a RISC OS file type from the extension, as ",xxx"
            static const struct { const char* ext; const char* type; } types[] = {
                { ".zip", ",a91" }, { ".txt", ",fff" }, { ".bas", ",ffb" }, { ".jpg", ",c85" },
                { ".jpeg", ",c85" }, { ".gif", ",695" }, { ".png", ",b60" }, { ".pdf", ",adf" },
                { ".htm", ",faf" }, { ".html", ",faf" }, { ".arc", ",ddc" }, { ".spk", ",ddc" },
            };
            std::string lower = name;
            for (auto& ch : lower) ch = char(tolower(ch));
            for (auto& t : types) {
                size_t n = strlen(t.ext);
                if (lower.size() > n && lower.compare(lower.size() - n, n, t.ext) == 0) {
                    name = name.substr(0, name.size() - n) + t.type;
                    break;
                }
            }
        }
        FILE* in = fopen(path.c_str(), "rb");
        if (!in) { error = "Can't read the file"; return false; }
        FILE* out = fopen((hostfsDir() + "/" + name).c_str(), "wb");
        if (!out) { fclose(in); error = "Can't write into HostFS"; return false; }
        char buf[65536];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, in)) > 0) fwrite(buf, 1, n, out);
        fclose(in);
        fclose(out);
        return true;
    }
    error = "No such drive";
    return false;
}

void AcornCore::mediaEject(int drive)
{
    if (loaded_ && drive == 0) fdc_image_save("", 0);
}

int AcornCore::mediaActivity()
{
    return busy_ > 0 ? 1 : 0;
}
