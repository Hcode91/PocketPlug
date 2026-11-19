// PocketPlug - Acorn Archimedes family (Arculator)
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Glue between Arculator (./arculator) and PocketPlug's Core. Arculator runs
// in steps of 1/100 s of emulated time; a 1/60 s frame runs 1 or 2 of them.
// Its files (ROM image, configuration, CMOS, HostFS) live in a directory
// beside the imported ROMs.

#include "arc_rename.h"
#include "archi.h"
#include "arc_glue.h"
#include "../../common/chd/chd.h"

#include <algorithm>
#include <csetjmp>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>

extern "C" {
#include "arculator/disc.h"
#include "arculator/ioc.h"
extern char discname[4][512];
extern int motoron;
void cmos_save(void);
extern const uint8_t archi_arcrom_ext[65536];
extern const uint8_t archi_cmos_arthur[256];
extern const uint8_t archi_cmos_riscos2[256];
extern const uint8_t archi_cmos_riscos3_new[256];
extern const uint8_t archi_cmos_riscos3_old[256];
}

namespace {

// ---- operating systems: Arculator's ROM set name and MAME's chips ----
enum Layout { BYTES4, WORDS2 };   // 4 chips a byte wide / 2 chips 16 bits wide
struct Os { const char* romset; Layout layout; const char* files[4]; };
const Os kOs[] = {
    /* 0 */ { "arthur030", BYTES4, { "0276,322-01.rom", "0276,323-01.rom", "0276,324-01.rom", "0276,325-01.rom" } },
    /* 1 */ { "arthur120", BYTES4, { "0277,022-02.rom", "0277,023-02.rom", "0277,024-02.rom", "0277,025-02.rom" } },
    /* 2 */ { "riscos200", BYTES4, { "0283,022-01.rom", "0283,023-01.rom", "0283,024-01.rom", "0283,025-01.rom" } },
    /* 3 */ { "riscos201", BYTES4, { "0270,601-01.rom", "0270,602-01.rom", "0270,603-01.rom", "0270,604-01.rom" } },
    /* 4 */ { "riscos300", BYTES4, { "0270,251-01.rom", "0270,252-01.rom", "0270,253-01.rom", "0270,254-01.rom" } },
    /* 5 */ { "riscos310", BYTES4, { "0296,041-01.rom", "0296,042-01.rom", "0296,043-01.rom", "0296,044-01.rom" } },
    /* 6 */ { "riscos311", BYTES4, { "0296,041-02.rom", "0296,042-02.rom", "0296,043-02.rom", "0296,044-02.rom" } },
    /* 7 */ { "riscos311", WORDS2, { "0296,061-02.ic17", "0296,062-02.ic18" } },
    /* 8 */ { "riscos311", WORDS2, { "0296,241-01.ic17", "0296,242-01.ic18" } },        // German
    /* 9 */ { "riscos310", WORDS2, { "0296,061-01.ic4", "0296,062-01.ic15" } },         // A4
    /* 10 */ { "riscos310_a500", BYTES4, { "a500_ros310_rom0.ic24", "a500_ros310_rom1.ic25", "a500_ros310_rom2.ic26", "a500_ros310_rom3.ic27" } },
    /* 11 */ { "riscos200_a500", BYTES4, { "a500_riscos206_0.ic24", "a500_riscos206_1.ic25", "a500_riscos206_2.ic26", "a500_riscos206_3.ic27" } },
};

// ---- machines, after Arculator's presets ----
// cpu: Arculator's list (0 ARM2, 1 ARM250, 3 ARM3 25 MHz, 4 ARM3 26 MHz, 8 ARM3 24 MHz)
// memc: 0 MEMC1, 1 MEMC1A 8 MHz, 2 MEMC1A 12 MHz; fdc: 0 WD1772 (old IO), 1 82C711 (new IO), 2 A500
struct Model {
    const char* id; const char* machine;
    int cpu, memc, fdc, memKb;
    const char* monitor;
    int os[6]; int nOs; int defOs;
    bool disk;           // MAME's riscos311_apps hard disk on IDE
};
const Model kModels[] = {
    { "aa305",     "a305",   0, 0, 0,  512, "multisync", { 0, 1, 2, 3 }, 4, 2, false },
    { "aa310",     "a310",   0, 0, 0, 1024, "multisync", { 0, 1, 2, 3 }, 4, 3, false },
    { "aa440",     "a440",   0, 0, 0, 4096, "multisync", { 0, 1, 2, 3 }, 4, 3, false },
    { "aa4101",    "a410/1", 0, 1, 0, 1024, "multisync", { 2, 3, 4, 5, 6 }, 5, 1, false },
    { "aa540",     "a540",   4, 2, 0, 4096, "multisync", { 3, 4, 5, 6 }, 4, 3, false },
    { "aa3000",    "a3000",  0, 1, 0, 2048, "multisync", { 2, 3, 4, 5, 6 }, 5, 4, false },
    { "aa5000",    "a5000",  3, 2, 1, 4096, "multisync", { 4, 5, 6 }, 3, 2, true },
    { "aa3010",    "a3010",  1, 2, 1, 1024, "multisync", { 7 }, 1, 0, false },
    { "aa3010_de", "a3010",  1, 2, 1, 1024, "multisync", { 8 }, 1, 0, false },
    { "aa3020",    "a3020",  1, 2, 1, 2048, "multisync", { 7 }, 1, 0, true },
    { "aa4000",    "a4000",  1, 2, 1, 2048, "multisync", { 7 }, 1, 0, true },
    { "aa4",       "a4",     8, 2, 1, 2048, "lcd",       { 9 }, 1, 0, true },
    { "aa500",     "a500",   0, 0, 2, 4096, "multisync", { 10, 11 }, 2, 0, false },
};

ArchiCore* g_core = nullptr;
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
bool exists(const std::string& p) { struct stat st; return stat(p.c_str(), &st) == 0; }

bool writeAll(const std::string& path, const uint8_t* d, size_t n)
{
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = fwrite(d, 1, n, f) == n;
    fclose(f);
    return ok;
}

// PS/2 set-2 codes (as the A7000 skin sends them) -> SDL scancodes, which
// Arculator's key table is indexed by. 0x90+ are the E0 keys.
int toScancode(int code)
{
    static const uint8_t kMain[0x84] = {
        /*00*/ 0, 66, 0, 62, 60, 58, 59, 69, 0, 67, 65, 63, 61, 43, 53, 0,
        /*10*/ 0, 226, 225, 0, 224, 20, 30, 0, 0, 0, 29, 22, 4, 26, 31, 0,
        /*20*/ 0, 6, 27, 7, 8, 33, 32, 0, 0, 44, 25, 9, 23, 21, 34, 0,
        /*30*/ 0, 17, 5, 11, 10, 28, 35, 0, 0, 0, 16, 13, 24, 36, 37, 0,
        /*40*/ 0, 54, 14, 12, 18, 39, 38, 0, 0, 55, 56, 15, 51, 19, 45, 0,
        /*50*/ 0, 0, 52, 0, 47, 46, 0, 0, 57, 229, 40, 48, 0, 49, 0, 0,
        /*60*/ 0, 100, 0, 0, 0, 0, 42, 0, 0, 89, 0, 92, 95, 0, 0, 0,
        /*70*/ 98, 99, 90, 93, 94, 96, 41, 83, 68, 87, 91, 86, 85, 97, 71, 0,
        /*80*/ 0, 0, 0, 64,
    };
    static const uint8_t kExt[] = {
        230, // 0x90 right Alt
        228, // 0x91 right Ctrl
        73,  // 0x92 Insert
        74,  // 0x93 Home
        75,  // 0x94 Page Up
        76,  // 0x95 Delete
        77,  // 0x96 End (Copy)
        78,  // 0x97 Page Down
        82,  // 0x98 Up
        80,  // 0x99 Left
        81,  // 0x9a Down
        79,  // 0x9b Right
        84,  // 0x9c keypad /
        88,  // 0x9d keypad Enter
        70,  // 0x9e Print
        72,  // 0x9f Break
    };
    if (code >= 0x90 && code < 0x90 + int(sizeof(kExt))) return kExt[code - 0x90];
    if (code >= 0 && code < int(sizeof(kMain))) return kMain[code];
    return 0;
}

} // namespace

extern "C" void archi_core_video(const uint32_t* px, int stride, int w, int h, int dblscan)
{
    if (g_core) g_core->video(px, stride, w, h, dblscan);
}

extern "C" void archi_core_sound(const int16_t* stereo, int frames)
{
    if (g_core) g_core->sound(stereo, frames);
}

extern "C" void archi_core_fatal(const char* msg)
{
    g_fatal = msg;
    fprintf(stderr, "Arculator fatal: %s\n", msg);
    if (g_inFrame) longjmp(g_fatalJump, 1);
    abort();
}

ArchiCore::ArchiCore(const std::string& id) : id_(id), fb_(kW * kH, 0xff000000u)
{
    for (size_t i = 0; i < sizeof(kModels) / sizeof(kModels[0]); i++)
        if (id == kModels[i].id) model_ = int(i);
}

ArchiCore::~ArchiCore()
{
    if (loaded_) {
        cmos_save();
        for (int d = 0; d < 4; d++) disc_close(d);
    }
    if (g_core == this) g_core = nullptr;
}

bool ArchiCore::writeRom(int os, std::string& error)
{
    const Os& o = kOs[os];
    int n = o.layout == BYTES4 ? 4 : 2;
    const RomFile* chip[4] = {};
    for (int i = 0; i < n; i++) {
        chip[i] = find(roms_, o.files[i]);
        if (!chip[i] || chip[i]->data.empty() || chip[i]->data.size() != find(roms_, o.files[0])->data.size()) {
            error = std::string("Missing the ROM chip ") + o.files[i];
            return false;
        }
    }
    size_t each = chip[0]->data.size();
    std::vector<uint8_t> rom(each * n);
    if (o.layout == BYTES4) {
        for (size_t i = 0; i < each; i++)
            for (int c = 0; c < 4; c++) rom[i * 4 + c] = chip[c]->data[i];
    } else {
        for (size_t i = 0; i + 1 < each; i += 2) {
            rom[i * 2 + 0] = chip[0]->data[i];
            rom[i * 2 + 1] = chip[0]->data[i + 1];
            rom[i * 2 + 2] = chip[1]->data[i];
            rom[i * 2 + 3] = chip[1]->data[i + 1];
        }
    }
    std::string dir = dataDir_ + "/roms/" + o.romset;
    mkdirs(dir);
    if (!writeAll(dir + "/rom.bin", rom.data(), rom.size())) { error = "Can't write the ROM image"; return false; }
    return true;
}

void ArchiCore::writeConfig(int os)
{
    const Model& m = kModels[model_];
    std::string fn = dataDir_ + "/machine.cfg";
    FILE* f = fopen(fn.c_str(), "w");
    if (!f) return;
    // a fixed unique ID per machine (the new-IO machines' serial number chip)
    uint32_t uid = 0x5a3c0000u;
    for (const char* p = m.id; *p; p++) uid = uid * 31 + uint8_t(*p);
    fprintf(f, "machine = %s\n", m.machine);
    fprintf(f, "cpu_type = %d\nmemc_type = %d\nfpa = 0\nfpu_type = %d\n", m.cpu, m.memc, m.cpu >= 2 ? 0 : 1);
    fprintf(f, "fdc_type = %d\nst506_present = %d\n", m.fdc, m.fdc == 1 ? 0 : 1);
    fprintf(f, "mem_size = %d\nrom_set = %s\nmonitor_type = %s\n", m.memKb, kOs[os].romset, m.monitor);
    fprintf(f, "display_mode = 0\ndouble_scan = 1\nunique_id = %u\n", uid);
    if (m.disk && exists(dataDir_ + "/hd4.hdf")) {
        int c = 823, h = 4, s = 38;
        if (FILE* g = fopen((dataDir_ + "/hd4.geom").c_str(), "r")) { if (fscanf(g, "%d %d %d", &c, &h, &s) != 3) { c = 823; h = 4; s = 38; } fclose(g); }
        fprintf(f, "hd4_fn = %s/hd4.hdf\nhd4_cylinders = %d\nhd4_heads = %d\nhd4_sectors = %d\n", dataDir_.c_str(), c, h, s);
    }
    fclose(f);
    if (FILE* g = fopen((dataDir_ + "/arc.cfg").c_str(), "w")) {
        fprintf(g, "sound_enable = 1\nstereo = 1\nsound_gain = 0\nsound_filter = 0\ndisc_noise_gain = -9999\nfirst_fullscreen = 0\n");
        fclose(g);
    }
}

bool ArchiCore::load(const std::vector<RomFile>& roms, std::string& error)
{
    if (model_ < 0) { error = "Unknown Archimedes model " + id_; return false; }
    if (g_core) { error = "Only one Archimedes can run at a time"; return false; }
    const Model& m = kModels[model_];
    roms_ = roms;
    if (roms.empty()) { error = "No ROMs"; return false; }

    dataDir_ = dirOf(roms[0].path) + "/.archi-" + id_;
    mkdirs(dataDir_);
    mkdirs(dataDir_ + "/roms");
    mkdirs(dataDir_ + "/cmos");
    mkdirs(dataDir_ + "/configs");
    mkdirs(hostfsDir());
    writeAll(dataDir_ + "/roms/arcrom_ext", archi_arcrom_ext, sizeof(archi_arcrom_ext));
    static const struct { const char* name; const uint8_t* d; } cm[] = {
        { "arthur", archi_cmos_arthur }, { "riscos2", archi_cmos_riscos2 },
        { "riscos3_new", archi_cmos_riscos3_new }, { "riscos3_old", archi_cmos_riscos3_old },
    };
    for (auto& c : cm) {
        uint8_t ram[256];
        memcpy(ram, c.d, 256);
        if (m.disk && !strcmp(c.name, "riscos3_new")) {
            // *Configure IDEDiscs 1: logical byte &87 bits 6-7 (physical &C7),
            // then the checksum at logical &EF (physical &3F)
            ram[0xc7] = uint8_t((ram[0xc7] & 0x3f) | 0x40);
            unsigned sum = 1;
            for (int i = 0x10; i < 0x100; i++) if (i != 0x3f) sum += ram[i];
            ram[0x3f] = uint8_t(sum);
        }
        mkdirs(dataDir_ + "/cmos/" + c.name);
        writeAll(dataDir_ + "/cmos/" + c.name + "/cmos.bin", ram, 256);
    }
    if (const RomFile* ext = find(roms, "0296,063-01.ic38"))           // the A4's extension ROM
        writeAll(dataDir_ + "/roms/A4 5th Column.rom", ext->data.data(), ext->data.size());

    // the hard disk: MAME's CHD unpacked once into a writable image
    if (m.disk && !exists(dataDir_ + "/hd4.hdf")) {
        if (const RomFile* hd = find(roms, "riscos311_apps.chd")) {
            ChdFile chd;
            std::string e;
            int c, h, s;
            if (chd.open(hd->path, e) && chd.diskGeometry(c, h, s)) {
                std::string tmp = dataDir_ + "/hd4.tmp";
                FILE* out = fopen(tmp.c_str(), "wb");
                bool ok = out != nullptr;
                std::vector<uint8_t> buf(1 << 16);
                for (uint64_t at = 0; ok && at < chd.size(); at += buf.size()) {
                    size_t k = size_t(std::min<uint64_t>(buf.size(), chd.size() - at));
                    ok = chd.read(at, buf.data(), k) && fwrite(buf.data(), 1, k, out) == k;
                }
                if (out) fclose(out);
                if (ok) {
                    rename(tmp.c_str(), (dataDir_ + "/hd4.hdf").c_str());
                    if (FILE* g = fopen((dataDir_ + "/hd4.geom").c_str(), "w")) { fprintf(g, "%d %d %d\n", c, h, s); fclose(g); }
                } else unlink(tmp.c_str());
            }
        }
    }

    os_ = m.os[m.defOs];
    if (os_ < 0 || !writeRom(os_, error)) return false;
    writeConfig(os_);

    snprintf(exname, sizeof(exname), "%s/", dataDir_.c_str());
    snprintf(machine_config_file, sizeof(machine_config_file), "%s/machine.cfg", dataDir_.c_str());
    snprintf(machine_config_name, sizeof(machine_config_name), "%s", id_.c_str());

    g_core = this;
    g_inFrame = true;
    if (setjmp(g_fatalJump)) {
        g_inFrame = false;
        g_core = nullptr;
        error = "Arculator: " + g_fatal;
        return false;
    }
    if (arc_init() != 0) {
        g_inFrame = false;
        g_core = nullptr;
        error = "Arculator couldn't start (ROM image missing?)";
        return false;
    }
    g_inFrame = false;
    loaded_ = true;
    return true;
}

void ArchiCore::reset()
{
    if (!loaded_) return;
    g_inFrame = true;
    if (setjmp(g_fatalJump)) { g_inFrame = false; loaded_ = false; return; }
    arc_reset();
    g_inFrame = false;
}

void ArchiCore::setOption(int id, int value)
{
    if (id != 1 || model_ < 0) return;
    const Model& m = kModels[model_];
    if (value < 0 || value >= m.nOs) return;
    int os = m.os[value];
    if (os == os_ && loaded_) return;
    std::string e;
    if (!writeRom(os, e)) return;
    os_ = os;
    writeConfig(os);
    if (!loaded_) return;
    // the new OS needs its own settings: reload the configuration and restart
    g_inFrame = true;
    if (setjmp(g_fatalJump)) { g_inFrame = false; loaded_ = false; return; }
    cmos_save();
    archi_glue_reload_config();
    arc_reset();
    g_inFrame = false;
}

void ArchiCore::setInputs(uint32_t pressed)
{
    buttons_ = pressed & 7;
}

void ArchiCore::setAnalog(int stick, float x, float y)
{
    if (stick != 9) return;
    mx_ += x;
    my_ += y;
}

void ArchiCore::keyEvent(int code, bool down)
{
    int sc = toScancode(code);
    if (sc) archi_glue_key(sc, down ? 1 : 0);
}

void ArchiCore::runFrame()
{
    if (!loaded_) return;
    g_inFrame = true;
    if (setjmp(g_fatalJump)) {
        g_inFrame = false;
        loaded_ = false;      // the machine has stopped
        return;
    }
    // mouse: Arculator counts buttons as left 1, right 2, middle 4
    int dx = int(mx_), dy = int(my_);
    mx_ -= dx; my_ -= dy;
    int b = int((buttons_ & 1) | (buttons_ & 2 ? 4 : 0) | (buttons_ & 4 ? 2 : 0));
    archi_glue_mouse(dx, dy, b);

    // 1/60 s = 5/3 of Arculator's 1/100 s steps
    runAcc_ += 100.0 / 60.0;
    while (runAcc_ >= 1.0) {
        arc_run();
        runAcc_ -= 1.0;
    }
    if (motoron) busy_ = 20;
    else if (busy_ > 0) busy_--;
    g_inFrame = false;
}

void ArchiCore::video(const uint32_t* px, int stride, int w, int h, int dbl)
{
    if (w <= 0 || h <= 0) return;
    int dh = dbl ? h * 2 : h;
    // keep the shape: Archimedes pixels are square in the 4:3 modes when
    // the picture is 640 x 512 (or doubled 640 x 256)
    float s = std::min(float(kW) / w, float(kH) / dh);
    int ow = int(w * s), oh = int(dh * s);
    int ox = (kW - ow) / 2, oy = (kH - oh) / 2;
    for (int y = 0; y < kH; y++) {
        uint32_t* d = &fb_[y * kW];
        if (y < oy || y >= oy + oh) { for (int x = 0; x < kW; x++) d[x] = 0xff000000u; continue; }
        int sy = (y - oy) * h / oh;
        const uint32_t* srow = px + sy * stride;
        for (int x = 0; x < kW; x++) {
            if (x < ox || x >= ox + ow) { d[x] = 0xff000000u; continue; }
            int sx = (x - ox) * w / ow;
            d[x] = 0xff000000u | (srow[sx] & 0xffffff);
        }
    }
}

void ArchiCore::sound(const int16_t* st, int frames)
{
    if (audio_.size() > size_t(kRate)) return;
    // stereo 48 kHz -> mono 44.1 kHz
    const double step = 48000.0 / kRate;
    while (resamplePos_ < frames) {
        size_t i = size_t(resamplePos_);
        audio_.push_back(int16_t((int(st[i * 2]) + int(st[i * 2 + 1])) / 2));
        resamplePos_ += step;
    }
    resamplePos_ -= frames;
}

size_t ArchiCore::readAudio(int16_t* out, size_t maxSamples)
{
    size_t n = std::min(audio_.size(), maxSamples);
    memcpy(out, audio_.data(), n * sizeof(int16_t));
    audio_.erase(audio_.begin(), audio_.begin() + n);
    return n;
}

bool ArchiCore::mediaInsert(int drive, const std::string& path, std::string& error)
{
    if (!loaded_) { error = "Not running"; return false; }
    if (drive == 0 || drive == 1) {
        size_t dot = path.find_last_of('.');
        if (dot == std::string::npos || path.size() - dot > 5) { error = "Floppy images need an extension (.adf, .adl, .apd, .hfe, .jfd, .scp)"; return false; }
        disc_close(drive);
        snprintf(discname[drive], sizeof(discname[drive]), "%s", path.c_str());
        disc_load(drive, discname[drive]);
        ioc_discchange(drive);
        return true;
    }
    if (drive == 2) {
        // into HostFS: RISC OS sees it under the HostFS icon
        size_t s = path.find_last_of('/');
        std::string name = s == std::string::npos ? path : path.substr(s + 1);
        if (name.find(',') == std::string::npos) {
            static const struct { const char* ext; const char* type; } types[] = {
                { ".zip", ",a91" }, { ".txt", ",fff" }, { ".bas", ",ffb" }, { ".jpg", ",c85" },
                { ".jpeg", ",c85" }, { ".gif", ",695" }, { ".arc", ",ddc" }, { ".spk", ",ddc" },
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

void ArchiCore::mediaEject(int drive)
{
    if (!loaded_ || drive < 0 || drive > 1) return;
    ioc_discchange(drive);
    disc_close(drive);
    discname[drive][0] = 0;
}

int ArchiCore::mediaActivity()
{
    return busy_ > 0 ? 1 : 0;
}
