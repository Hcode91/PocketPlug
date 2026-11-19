// PocketPlug - Atari 8-bit computers (1200XL, 130XE)
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Glue between Atari800's libatari800 (./a800) and PocketPlug's Core.

#include "atari800.h"
#include <cstring>
#include <cstdio>
#include <cstdlib>

extern "C" {
#include "a800/config.h"
#include "a800/atari.h"
#include "a800/akey.h"
#include "a800/afile.h"
#include "a800/antic.h"
#include "a800/cartridge.h"
#include "a800/colours.h"
#include "a800/cpu.h"
#include "a800/pia.h"
#include "a800/screen.h"
#include "a800/sio.h"
#include "a800/libatari800/libatari800.h"
extern int LIBATARI800_raw_mode;
extern int LIBATARI800_raw_key;
}

namespace {

bool g_active = false;   // Atari800 is single-instance (globals)

std::string dirOf(const std::string& p)
{
    size_t s = p.find_last_of('/');
    return s == std::string::npos ? std::string(".") : p.substr(0, s);
}

const RomFile* find(const std::vector<RomFile>& roms, const char* name)
{
    for (auto& r : roms) if (r.name == name) return &r;
    return nullptr;
}

} // namespace

Atari800Core::Atari800Core(Model m) : model_(m), fb_(kW * kH, 0xff000000u) {}

Atari800Core::~Atari800Core()
{
    if (loaded_) {
        libatari800_exit();
        g_active = false;
    }
}

namespace {

// One row per model: how Atari800 is started and which ROM files make its OS.
struct ModelInfo {
    Atari800Core::Model model;
    const char* machine;        // Atari800 machine switch
    bool pal;
    const char* osSwitch;       // "-osa_rom" / "-osb_rom" (400/800) or "-xlxe_rom"
    std::vector<const char*> os;  // OS chips, in address order; joined into one image
    const char* basic;          // built-in BASIC chip, or nullptr (BASIC is a cartridge)
    const char* name;           // for messages and the joined OS file
    const char* hint;           // where a missing file lives
};

const std::vector<ModelInfo>& models()
{
    static const std::vector<ModelInfo> m = {
        { Atari800Core::k1200XL, "-1200", false, "-xlxe_rom", { "co60616b.rom", "co60617b.rom" }, nullptr, "1200XL", "" },
        { Atari800Core::k130XE, "-xe", false, "-xlxe_rom", { "co61598b.rom" }, "co24947a.rom", "130XE",
          " (the OS chip co61598b.rom is in the a800xl zip: import it too)" },
        // 400 / 800 OS: floating point (2 KB at $D800) + two 4 KB chips ($E000, $F000).
        // "-atari" is a 400 / 800 with 48 KB, the usual upgrade, so the bigger games fit.
        { Atari800Core::k400, "-atari", false, "-osb_rom", { "co12399b.rom", "co12499b.rom", "co14599b.rom" }, nullptr, "400", "" },
        { Atari800Core::k400PAL, "-atari", true, "-osa_rom", { "co12399b.rom", "co15199.rom", "co15299.rom" }, nullptr, "400 (PAL)",
          " (co12399b.rom is in the a400 zip: import it too)" },
        { Atari800Core::k800, "-atari", false, "-osb_rom", { "co12399b.rom", "co12499b.rom", "co14599b.rom" }, nullptr, "800", "" },
        { Atari800Core::k800PAL, "-atari", true, "-osa_rom", { "co12399b.rom", "co15199.rom", "co15299.rom" }, nullptr, "800 (PAL)",
          " (co12399b.rom is in the a800 zip: import it too)" },
        // the 600XL had 16 KB; it runs here with 64 KB (the 1064 expansion) so XL software fits
        { Atari800Core::k600XL, "-xl", false, "-xlxe_rom", { "co62024.rom" }, "co60302a.rom", "600XL",
          " (BASIC, co60302a.rom, is in the a800xl zip: import it too)" },
        { Atari800Core::k800XL, "-xl", false, "-xlxe_rom", { "co61598b.rom" }, "co60302a.rom", "800XL", "" },
        { Atari800Core::k800XLPAL, "-xl", true, "-xlxe_rom", { "co61598b.rom" }, "co60302a.rom", "800XL (PAL)", "" },
        // the 65XE is an 800XL in the XE case (64 KB)
        { Atari800Core::k65XE, "-xl", false, "-xlxe_rom", { "co61598b.rom" }, "co24947a.rom", "65XE",
          " (the OS chip co61598b.rom is in the a800xl zip: import it too)" },
        { Atari800Core::k800XE, "-xl", true, "-xlxe_rom", { "c300717.rom" }, "co24947a.rom", "800XE", "" },
    };
    return m;
}

} // namespace

bool Atari800Core::load(const std::vector<RomFile>& roms, std::string& error)
{
    if (g_active) { error = "Only one Atari machine can run at a time"; return false; }
    const ModelInfo* mi = nullptr;
    for (auto& m : models()) if (m.model == model_) mi = &m;
    if (!mi) { error = "Unknown model"; return false; }

    std::vector<const std::vector<uint8_t>*> parts;
    std::string dir;
    for (const char* n : mi->os) {
        const RomFile* r = find(roms, n);
        if (!r) { error = std::string("Missing ") + n + mi->hint; return false; }
        parts.push_back(&r->data);
        if (dir.empty()) dir = dirOf(r->path);
    }
    if (parts.size() == 1) {
        osPath_ = find(roms, mi->os[0])->path;
    } else {
        osPath_ = dir + "/.atari-os-" + std::to_string(int(model_)) + ".rom";
        FILE* f = fopen(osPath_.c_str(), "wb");
        bool ok = f != nullptr;
        for (auto* p : parts) ok = ok && fwrite(p->data(), 1, p->size(), f) == p->size();
        if (f) fclose(f);
        if (!ok) { error = "Can't write " + osPath_; return false; }
    }
    basicBuiltIn_ = mi->basic != nullptr;
    if (mi->basic) {
        const RomFile* b = find(roms, mi->basic);
        if (!b) { error = std::string("Missing ") + mi->basic + mi->hint; return false; }
        basicPath_ = b->path;
    }

    std::vector<std::string> args = {
        "atari800",
        "-config", "/dev/null",
        mi->machine,
        mi->pal ? "-pal" : "-ntsc",
        // machines without BASIC inside start with no cartridge (see setOption)
        basicBuiltIn_ ? "-basic" : "-nobasic",
        mi->osSwitch, osPath_,
        "-audio16", "-dsprate", "44100", "-nostereo",
    };
    if (!basicPath_.empty()) { args.push_back("-basic_rom"); args.push_back(basicPath_); }
    std::vector<char*> argv;
    for (auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);
    if (!libatari800_init(int(argv.size()) - 1, argv.data())) {
        error = "Atari800 failed to start";
        return false;
    }
    LIBATARI800_raw_mode = 1;
    LIBATARI800_raw_key = AKEY_NONE;
    g_active = loaded_ = true;
    return true;
}

double Atari800Core::refreshRate() const
{
    return Atari800_tv_mode == Atari800_TV_PAL ? Atari800_FPS_PAL : Atari800_FPS_NTSC;
}

void Atari800Core::reset()
{
    if (loaded_) Atari800_Coldstart();
}

void Atari800Core::setInputs(uint32_t pressed)
{
    joy_ = pressed;
}

void Atari800Core::keyEvent(int code, bool down)
{
    if (code < 0 || code >= 72) return;
    if (code == kReset) {           // the RESET key is wired to the 6502's NMI / warm start
        if (down && !keys_[code] && loaded_) Atari800_Warmstart();
        keys_[code] = down;
        return;
    }
    keys_[code] = down;
    for (size_t i = 0; i < held_.size(); i++)
        if (held_[i] == code) { held_.erase(held_.begin() + i); break; }
    if (down && (code < 64 || code == kBreak)) held_.push_back(code);
}

int Atari800Core::pickKey() const
{
    // the Atari keyboard reports one key at a time; the newest one wins
    if (held_.empty()) return AKEY_NONE;
    int k = held_.back();
    if (k == kBreak) return AKEY_BREAK;
    if (keys_[kShift]) k |= AKEY_SHFT;
    if (keys_[kControl]) k |= AKEY_CTRL;
    return k;
}

void Atari800Core::setOption(int id, int value)
{
    if (id != 1 || basicBuiltIn_ || !loaded_) return;
    // a BASIC cartridge plugged into the 1200XL: on a machine without built-in
    // BASIC, Atari800 maps its BASIC ROM (Altirra BASIC, as this set has no
    // BASIC ROM) like a cartridge when BASIC isn't disabled
    bool on = value != 0;
    basicCart_ = on;
    if (on == !Atari800_disable_basic) return;
    Atari800_disable_basic = !on;
    Atari800_InitialiseMachine();
}

void Atari800Core::runFrame()
{
    if (!loaded_) return;
    input_template_t in;
    libatari800_clear_input_array(&in);
    in.shift = keys_[kShift];
    in.control = keys_[kControl];
    in.start = keys_[kStart];
    in.select = keys_[kSelect];
    in.option = keys_[kOption];
    in.joy0 = joy_ & 0x0f;
    in.trig0 = (joy_ >> 4) & 1;
    LIBATARI800_raw_key = pickKey();
    libatari800_next_frame(&in);

    render();
    const int16_t* s = reinterpret_cast<const int16_t*>(libatari800_get_sound_buffer());
    int n = libatari800_get_sound_buffer_len() / 2;
    if (s && n > 0 && audio_.size() < size_t(kRate)) audio_.insert(audio_.end(), s, s + n);
    if (SIO_last_op_time > 0) busy_ = 20;
    else if (busy_ > 0) busy_--;
}

void Atari800Core::render()
{
    const uint8_t* src = reinterpret_cast<const uint8_t*>(Screen_atari);
    const int x0 = (Screen_WIDTH - kW) / 2;
    for (int y = 0; y < kH; y++) {
        const uint8_t* s = src + y * Screen_WIDTH + x0;
        uint32_t* d = &fb_[y * kW];
        for (int x = 0; x < kW; x++) d[x] = 0xff000000u | uint32_t(Colours_table[s[x]]);
    }
}

size_t Atari800Core::readAudio(int16_t* out, size_t maxSamples)
{
    size_t n = audio_.size() < maxSamples ? audio_.size() : maxSamples;
    memcpy(out, audio_.data(), n * sizeof(int16_t));
    audio_.erase(audio_.begin(), audio_.begin() + n);
    return n;
}

bool Atari800Core::mediaInsert(int drive, const std::string& path, std::string& error)
{
    if (!loaded_) { error = "Not running"; return false; }
    if (drive == 0 || drive == 1) {
        if (!SIO_Mount(drive + 1, path.c_str(), FALSE) && !SIO_Mount(drive + 1, path.c_str(), TRUE)) {
            error = "Not a disk image Atari800 can read (.atr .xfd .atx .dcm .pro)";
            return false;
        }
        return true;
    }
    if (drive != 2) { error = "No such drive"; return false; }
    // Games expect the RAM under BASIC: on the real machine you hold OPTION
    // while it starts. Do that for everything except BASIC programs; a
    // cartridge or BASIC listing keeps the BASIC setting the machine has.
    int type = AFILE_DetectFileType(path.c_str());
    if (type == AFILE_ERROR) { error = "Atari800 doesn't recognise this file"; return false; }
    bool wantBasic = type == AFILE_BAS || type == AFILE_LST;
    Atari800_disable_basic = wantBasic ? !basicFitted() : TRUE;
    int r = AFILE_OpenFile(path.c_str(), TRUE, 1, FALSE);
    if (r == AFILE_ERROR) { error = "Atari800 doesn't recognise this file"; return false; }
    if ((r & 0xff) == AFILE_ROM && (r >> 8) > 0) {
        // a raw cartridge dump: the size alone has to pick the board
        int kb = r >> 8, type = CARTRIDGE_NONE;
        if (kb == 8) type = CARTRIDGE_STD_8;
        else if (kb == 16) type = CARTRIDGE_STD_16;
        else if (kb == 32) type = CARTRIDGE_XEGS_32;
        else if (kb == 64) type = CARTRIDGE_XEGS_07_64;
        else if (kb == 128) type = CARTRIDGE_XEGS_128;
        if (type == CARTRIDGE_NONE) {
            CARTRIDGE_Remove();
            error = "Unknown cartridge type: use a .car file (it names the board)";
            return false;
        }
        CARTRIDGE_SetTypeAutoReboot(&CARTRIDGE_main, type);
    }
    return true;
}

bool Atari800Core::mediaSave(int drive, const std::string& path)
{
    // Atari800 writes disk sectors straight into the image file
    (void)drive; (void)path;
    return true;
}

void Atari800Core::mediaEject(int drive)
{
    if (!loaded_) return;
    if (drive == 0 || drive == 1) SIO_Dismount(drive + 1);
    else if (drive == 2) {
        // back to the bare computer: BASIC as the machine has it
        Atari800_disable_basic = !basicFitted();
        CARTRIDGE_Remove();
        Atari800_Coldstart();
    }
}

int Atari800Core::mediaActivity()
{
    int a = busy_ > 0 ? 1 : 0;
    if ((model_ == k1200XL) && loaded_) {
        uint8_t pb = PIA_PORTB | PIA_PORTB_mask;
        if (!(pb & 0x04)) a |= 0x10;
        if (!(pb & 0x08)) a |= 0x20;
    }
    return a;
}

bool Atari800Core::saveState(std::vector<uint8_t>& out)
{
    if (!loaded_) return false;
    emulator_state_t* st = static_cast<emulator_state_t*>(calloc(1, sizeof(emulator_state_t)));
    if (!st) return false;
    libatari800_get_current_state(st);
    size_t n = offsetof(emulator_state_t, state) + st->tags.size;
    if (n > sizeof(emulator_state_t)) n = sizeof(emulator_state_t);
    const uint8_t* p = reinterpret_cast<const uint8_t*>(st);
    out.assign(p, p + n);
    out.push_back(uint8_t(!Atari800_disable_basic));
    free(st);
    return true;
}

bool Atari800Core::loadState(const std::vector<uint8_t>& in)
{
    if (!loaded_ || in.size() < offsetof(emulator_state_t, state) + 1) return false;
    emulator_state_t* st = static_cast<emulator_state_t*>(calloc(1, sizeof(emulator_state_t)));
    if (!st) return false;
    size_t n = in.size() - 1;
    if (n > sizeof(emulator_state_t)) n = sizeof(emulator_state_t);
    memcpy(st, in.data(), n);
    if ((in.back() != 0) != !Atari800_disable_basic) {
        Atari800_disable_basic = !in.back();
        Atari800_InitialiseMachine();
    }
    libatari800_restore_state(st);
    free(st);
    return true;
}

uint16_t Atari800Core::pc() const
{
    return CPU_regPC;
}
