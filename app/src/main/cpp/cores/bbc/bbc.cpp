// PocketPlug - Acorn BBC Micro family on B-em (see bbc.h)
// SPDX-License-Identifier: GPL-2.0-or-later
#include "bbc.h"
#include "bem_glue.h"
#include <cstdio>
#include <cstring>
#include <cctype>
#include <sys/stat.h>

namespace {
// B-em's default Master Compact CMOS (cmosc.bin, from the B-em distribution)
const uint8_t kCompactCmos[128] = { 0x00,0xfe,0x00,0xeb,0x00,0xed,0xff,0xff,0x00,0x00,0xf7,0xe3,0x20,0x08,0x0a,0x2c,0x80,0x00,0x03,0x04,0x06,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x38,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xb0 };

const RomFile* find(const std::vector<RomFile>& roms, const char* name)
{
    for (auto& r : roms) if (r.name == name) return &r;
    return nullptr;
}

std::string dirOf(const std::string& p)
{
    size_t s = p.find_last_of('/');
    return s == std::string::npos ? "." : p.substr(0, s);
}

void mkdirs(const std::string& p)
{
    for (size_t i = 1; i <= p.size(); i++)
        if (i == p.size() || p[i] == '/') mkdir(p.substr(0, i).c_str(), 0755);
}

bool writeAll(const std::string& path, const uint8_t* d, size_t n)
{
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = fwrite(d, 1, n, f) == n;
    fclose(f);
    return ok;
}

struct Need { const char* file; const char* what; };

// one machine: its ROM files, how they go into B-em's ROM folders, and its model section
struct Machine {
    const char* id;
    const char* model;     // B-em model section body (without the header)
    const char* tube;      // B-em tube section body, or nullptr
};

const Machine kMachines[] = {
    { "bbca", "name=BBC Micro Model A\nfdc=none\n65c02=false\nb+=false\nmaster=false\nmodela=true\nos01=false\ncompact=false\nos=os12\ntube=none\nromsetup=std\nrom15=basic2\n", nullptr },
    { "bbcb", "name=BBC Micro Model B\nfdc=i8271\n65c02=false\nb+=false\nmaster=false\nmodela=false\nos01=false\ncompact=false\nos=os12\ntube=none\nromsetup=std\nrom15=basic2\nrom14=dnfs\n", nullptr },
    { "bbcb_us", "name=BBC Micro Model B (US)\nfdc=i8271\n65c02=false\nb+=false\nmaster=false\nmodela=false\nos01=false\ncompact=false\nos=usmos\ntube=none\nromsetup=std\nrom15=usbasic\nrom14=usdnfs\nrom13=view\n", nullptr },
    { "bbcbp", "name=BBC Micro Model B+\nfdc=acorn\n65c02=false\nb+=true\nmaster=false\nmodela=false\nos01=false\ncompact=false\nos=bpos\ntube=none\nromsetup=std\nrom15=basic2\nrom14=ddfs\n", nullptr },
    { "bbcbp128", "name=BBC Micro Model B+ 128K\nfdc=acorn\n65c02=false\nb+=true\nmaster=false\nmodela=false\nos01=false\ncompact=false\nos=bpos\ntube=none\nromsetup=bp128\nrom15=basic2\nrom14=ddfs\n", nullptr },
    { "bbcm", "name=BBC Master 128\nfdc=master\n65c02=true\nb+=false\nmaster=true\nmodela=false\nos01=false\ncompact=false\nos=mos\ncmos=cmos\ntube=none\nromsetup=master\n", nullptr },
    { "bbcmt", "name=BBC Master Turbo\nfdc=master\n65c02=true\nb+=false\nmaster=true\nmodela=false\nos01=false\ncompact=false\nos=mos\ncmos=cmos\ntube=65C102\nromsetup=master\n",
      "name=65C102\ncpu=6502\nromsize=0x800\nbootrom=tube65c102\nspeed=4\n" },
    { "bbcm512", "name=BBC Master 512\nfdc=master\n65c02=true\nb+=false\nmaster=true\nmodela=false\nos01=false\ncompact=false\nos=mos\ncmos=cmos\ntube=80186\nromsetup=master\n",
      "name=80186\ncpu=80186\nromsize=0x4000\nbootrom=tube80186\nspeed=8\n" },
    { "bbcmet", "name=BBC Master ET\nfdc=none\n65c02=true\nb+=false\nmaster=true\nmodela=false\nos01=false\ncompact=false\nos=mos\ncmos=cmos\ntube=none\nromsetup=master\n", nullptr },
    { "bbcmc", "name=BBC Master Compact\nfdc=master\n65c02=true\nb+=false\nmaster=true\nmodela=false\nos01=false\ncompact=true\nos=mos\ncmos=cmosc\ntube=none\nromsetup=compact\nrom15=utils\nrom14=basic48\nrom13=adfs\n", nullptr },
};

const Machine* findMachine(const std::string& id)
{
    for (auto& m : kMachines) if (id == m.id) return &m;
    return nullptr;
}
}

bool BbcCore::known(const std::string& id) { return findMachine(id) != nullptr; }

BbcCore::BbcCore(const std::string& id) : id_(id) {}

bool BbcCore::load(const std::vector<RomFile>& roms, std::string& error)
{
    const Machine* m = findMachine(id_);
    if (!m) { error = "Unknown machine"; return false; }
    const RomFile* saa = find(roms, "saa5050");
    if (!saa || saa->data.size() < 960) { error = "Missing saa5050 (the teletext character ROM, saa5050.zip)"; return false; }

    dataDir_ = dirOf(saa->path) + "/.bbc-" + id_;
    mkdirs(dataDir_ + "/roms/os");
    mkdirs(dataDir_ + "/roms/general");
    mkdirs(dataDir_ + "/roms/tube");
    mkdirs(dataDir_ + "/fonts");
    const std::string os = dataDir_ + "/roms/os/", gen = dataDir_ + "/roms/general/", tube = dataDir_ + "/roms/tube/";

    auto need = [&](const char* name) -> const RomFile* {
        const RomFile* r = find(roms, name);
        if (!r || r->data.empty()) error = std::string("Missing ") + name;
        return r && !r->data.empty() ? r : nullptr;
    };
    auto put = [&](const std::string& path, const RomFile* r, size_t off = 0, size_t n = 0x4000) {
        if (r->data.size() < off + n) return false;
        return writeAll(path, r->data.data() + off, n);
    };

    const std::string id = id_;
    if (id == "bbca" || id == "bbcb") {
        const RomFile* o = need("os12.rom"); const RomFile* b = need("basic2.rom");
        if (!o || !b) return false;
        put(os + "os12.rom", o); put(gen + "basic2.rom", b);
        if (id == "bbcb") {
            const RomFile* d = need("dnfs120.rom");
            if (!d) { error += " (the disc system ROM, bbc_acorn8271.zip)"; return false; }
            put(gen + "dnfs.rom", d);
        }
    } else if (id == "bbcb_us") {
        const RomFile* o = need("usmos10.rom"); const RomFile* b = need("usbasic3.rom");
        const RomFile* d = need("usdnfs10.rom"); const RomFile* v = need("viewa210.rom");
        if (!o || !b || !d || !v) return false;
        put(os + "usmos.rom", o); put(gen + "usbasic.rom", b); put(gen + "usdnfs.rom", d); put(gen + "view.rom", v);
    } else if (id == "bbcbp" || id == "bbcbp128") {
        // bpos2.ic71: BASIC II, then the B+ OS
        const RomFile* o = need("bpos2.ic71"); const RomFile* d = need("ddfs223.rom");
        if (!o || !d || o->data.size() < 0x8000) return false;
        put(os + "bpos.rom", o, 0x4000); put(gen + "basic2.rom", o, 0); put(gen + "ddfs.rom", d);
    } else if (id == "bbcm" || id == "bbcmt" || id == "bbcm512") {
        // mos320.ic24: the MOS, then ROMs 9-15
        const RomFile* o = need("mos320.ic24");
        if (!o || o->data.size() < 0x20000) return false;
        put(os + "mos.rom", o, 0, 0x20000);
        if (const RomFile* c = find(roms, "mos320.cmos")) if (c->data.size() >= 64) writeAll(dataDir_ + "/cmos.bin", c->data.data(), 64);
        if (id == "bbcmt") {
            const RomFile* t = need("65c102_boot_110.rom");
            if (!t || t->data.size() < 0x1000) { error += " (bbc_tube_65c102.zip)"; return false; }
            put(tube + "tube65c102.rom", t, 0x800, 0x800);   // the boot code is the top 2K
        }
        if (id == "bbcm512") {
            const RomFile* lo = need("m512_lo_ic31.rom"); const RomFile* hi = need("m512_hi_ic32.rom");
            if (!lo || !hi || lo->data.size() < 0x2000 || hi->data.size() < 0x2000) { error += " (bbc_tube_80186.zip)"; return false; }
            std::vector<uint8_t> bios(0x4000);
            for (int i = 0; i < 0x2000; i++) { bios[i * 2] = lo->data[i]; bios[i * 2 + 1] = hi->data[i]; }
            // the BIOS finds itself at segment C000 (the board mirrors it there); B-em maps it at
            // F000 only, so point those two segment loads at F000 as B-em's own copy does
            for (size_t o : { (size_t)0x3e94, (size_t)0x3ec2 })
                if (bios[o] == 0xc0) bios[o] = 0xf0;
            writeAll(tube + "tube80186.rom", bios.data(), bios.size());
        }
    } else if (id == "bbcmet") {
        // 64K, mirrored: the MOS, then ROMs 9-15
        const RomFile* o = need("mos400.ic24");
        if (!o || o->data.size() < 0x10000) return false;
        std::vector<uint8_t> img(0x20000);
        memcpy(img.data(), o->data.data(), 0x10000);
        memcpy(img.data() + 0x10000, o->data.data(), 0x10000);
        writeAll(os + "mos.rom", img.data(), img.size());
        if (const RomFile* c = find(roms, "mos400.cmos")) {
            uint8_t cm[64] = {};
            memcpy(cm + 14, c->data.data(), std::min<size_t>(c->data.size(), 50));
            writeAll(dataDir_ + "/cmos.bin", cm, 64);
        }
    } else if (id == "bbcmc") {
        // mos510.ic49: the MOS, ADFS, BASIC 48, Utils
        const RomFile* o = need("mos510.ic49");
        if (!o || o->data.size() < 0x10000) return false;
        put(os + "mos.rom", o, 0); put(gen + "adfs.rom", o, 0x4000); put(gen + "basic48.rom", o, 0x8000); put(gen + "utils.rom", o, 0xc000);
        struct stat st;
        if (stat((dataDir_ + "/cmosc.bin").c_str(), &st) != 0) writeAll(dataDir_ + "/cmosc.bin", kCompactCmos, 128);
    }
    if (bem_make_font(saa->data.data(), saa->data.size(), (dataDir_ + "/fonts/saa5050.fnt").c_str()) != 0) {
        error = "Can't build the teletext font";
        return false;
    }
    std::string cfg = "model=0\ntube=-1\nkey_as=false\nkey_logical=false\nkeypad=false\nmouse_amx=false\n"
        "[video]\nmode7font=saa5050\nfullborders=0\n[sound]\nsndinternal=true\nsndbeebsid=false\nsndmusic5000=false\nsnddac=false\nsndddnoise=false\nsndtape=false\nsoundfilter=true\n"
        "[model_00]\n";
    cfg += m->model;
    if (m->tube) { cfg += "[tube_00]\n"; cfg += m->tube; }
    if (!writeAll(dataDir_ + "/b-em.cfg", (const uint8_t*)cfg.data(), cfg.size())) { error = "Can't write the settings"; return false; }

    bem_init(dataDir_.c_str(), 0);
    loaded_ = true;
    return true;
}

void BbcCore::reset() { if (loaded_) bem_break(); }

void BbcCore::runFrame()
{
    if (!loaded_) return;
    // scripts wait until the machine has come up after power on
    if (frames_ < 100) frames_++;
    while (frames_ >= 100 && scriptPos_ < script_.size() && scriptWait_ <= 0) {
        const Step& st = script_[scriptPos_++];
        if (st.code >= 0) keyEvent(st.code & 0xff, st.code & 0x100);
        scriptWait_ = st.wait;
    }
    if (frames_ >= 100 && scriptWait_ > 0) scriptWait_--;
    if (scriptPos_ >= script_.size()) { script_.clear(); scriptPos_ = 0; }
    bem_run_frame();
}

const uint32_t* BbcCore::frameBuffer() const { return bem_frame(); }

size_t BbcCore::readAudio(int16_t* out, size_t maxSamples) { return (size_t)bem_read_audio(out, (int)maxSamples); }

void BbcCore::keyEvent(int code, bool down)
{
    if (!loaded_) return;
    if (code == 0xff) { if (down) bem_break(); return; }
    bem_key(code, down ? 1 : 0);
}

// the on-screen pad: the keys most games use (B-em matrix codes)
void BbcCore::setInputs(uint32_t pressed)
{
    static const uint8_t zx[4] = { 0x61, 0x42, 0x48, 0x68 };     // Z X : /
    static const uint8_t cursor[4] = { 0x19, 0x79, 0x39, 0x29 }; // left right up down
    uint8_t keys[8] = {
        0, 0, 0, 0,             // left right up down
        0x49,   // RETURN
        0x62,   // SPACE
        0x70,   // ESCAPE
        0x00,   // SHIFT
    };
    for (int i = 0; i < 4; i++) keys[i] = padMode_ ? cursor[i] : zx[i];
    uint32_t changed = pressed ^ pad_;
    for (int i = 0; i < 8; i++)
        if (changed >> i & 1) keyEvent(keys[i], pressed >> i & 1);
    pad_ = pressed;
}

bool BbcCore::mediaInsert(int drive, const std::string& path, std::string& error)
{
    if (!loaded_) { error = "Not running"; return false; }
    std::string file = path;
    bool boot = false;
    if (file.size() > 5 && file.compare(file.size() - 5, 5, "|boot") == 0) { boot = true; file.resize(file.size() - 5); }
    script_.clear(); scriptPos_ = 0; scriptWait_ = 0;
    if (drive == 2) {
        bem_load_tape(file.c_str());
        if (boot) {
            script_.push_back({ 0x1ff, 60 });
            typeText("*TAPE\r");
            script_.push_back({ -1, 10 });
            typeText("CHAIN\"\"\r");
        }
        return true;
    }
    if (drive < 0 || drive > 1) { error = "No such drive"; return false; }
    if (bem_load_disc(drive, file.c_str()) != 0) { error = "Can't read that disc image"; return false; }
    if (boot && drive == 0) {
        // SHIFT held through BREAK boots the disc; on the Masters A as well picks ADFS for
        // ADFS images (the Compact starts in ADFS anyway)
        std::string ext = file.substr(file.find_last_of('.') + 1);
        for (auto& ch : ext) ch = (char)tolower(ch);
        bool adfs = id_ != "bbcmc" && (id_ == "bbcm" || id_ == "bbcmt" || id_ == "bbcm512" || id_ == "bbcmet")
            && (ext == "adf" || ext == "adl" || ext == "ads" || ext == "adm" || ext == "img");
        bool shift = true;
        if (shift) script_.push_back({ 0x100, 1 });
        if (adfs) script_.push_back({ 0x141, 1 });
        script_.push_back({ 0x1ff, 40 });
        if (adfs) script_.push_back({ 0x041, 1 });
        if (shift) script_.push_back({ 0x000, 1 });
    }
    return true;
}

void BbcCore::setOption(int id, int value)
{
    if (id == 1) { setInputs(0); padMode_ = value ? 1 : 0; }
}

// types ASCII text through the keyboard matrix (BASIC-style upper case)
void BbcCore::typeText(const char* s)
{
    for (; *s; s++) {
        char c = *s;
        int code = -1; bool shift = false;
        static const char* letters = "QWERTYUIOPASDFGHJKLZXCVBNM";
        static const uint8_t lcodes[26] = { 0x10,0x21,0x22,0x33,0x23,0x44,0x35,0x25,0x36,0x37,
            0x41,0x51,0x32,0x43,0x53,0x54,0x45,0x46,0x56, 0x61,0x42,0x52,0x63,0x64,0x55,0x65 };
        static const uint8_t dcodes[10] = { 0x27,0x30,0x31,0x11,0x12,0x13,0x34,0x24,0x15,0x26 };
        if (c >= 'a' && c <= 'z') c -= 32;
        if (const char* p = c >= 'A' && c <= 'Z' ? strchr(letters, c) : nullptr) code = lcodes[p - letters];
        else if (c >= '0' && c <= '9') code = dcodes[c - '0'];
        else switch (c) {
            case '\r': code = 0x49; break;
            case ' ': code = 0x62; break;
            case '"': code = 0x31; shift = true; break;
            case '*': code = 0x48; shift = true; break;
            case ':': code = 0x48; break;
            case '.': code = 0x67; break;
            case ',': code = 0x66; break;
            case '/': code = 0x68; break;
            case '-': code = 0x17; break;
            case '=': code = 0x17; shift = true; break;
            default: break;
        }
        if (code < 0) continue;
        if (shift) script_.push_back({ 0x100, 1 });
        script_.push_back({ 0x100 | code, 2 });
        script_.push_back({ code, 2 });
        if (shift) script_.push_back({ 0x000, 2 });
    }
}

void BbcCore::mediaEject(int drive) { if (loaded_ && drive >= 0 && drive <= 1) bem_eject_disc(drive); }

int BbcCore::mediaActivity() { return bem_disc_led(); }
