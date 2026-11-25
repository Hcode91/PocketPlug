// PocketPlug - TI TMS1000-family handhelds (port of MAME's handheld/hh_tms1k.cpp, hap)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "hh_tms1k.h"
#include <cstring>
#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace {
enum Kind { ALPHIE, AMAZTRON, ARCMANIA, ARRBALL, BANKSHOT, BBEAR, BCHEETAH, BIGTRAK, BLASTIT };
// the guessed output PLA (O0 = bit 0 of the index)
const uint16_t kAlphieOpla[0x20] = { 0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1, 0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1 };
}

struct Tms1kGame {
    const char* id;
    Kind kind;
    TMS1K::Type type;
    double clock;
    const char* rom;
    const char* mpla;
    const char* opla;          // nullptr: table
    const uint16_t* otable;
    const char* svg;           // "" = drawn by the skin
    int rows, cols;
};

namespace {
const Tms1kGame kGames[] = {
    // Playskool Alphie (1978): TMS1000, 5 LEDs on the robot
    { "alphie", ALPHIE, TMS1K::TMS1000, 350000, "us4280809", "tms1000_common2_micro.pla", nullptr, kAlphieOpla, "", 1, 5 },
    // Coleco Amaze-A-Tron (1978): TMS1100, 2 digits + 2 LEDs, 5x5 pressure board
    { "amaztron", AMAZTRON, TMS1K::TMS1100, 300000, "mp3405", "tms1100_common3_micro.pla", "tms1100_amaztron_output.pla", nullptr, "", 4, 7 },
    // Milton Bradley Electronic Arcade Mania (1983): TMS1100, 9 LEDs
    // A-One LSI Arrange Ball (1980): TMS1000, 2 digits + 22 LEDs (7 x 7 matrix)
    { "arrball", ARRBALL, TMS1K::TMS1000, 325000, "mp0166", "tms1000_common2_micro.pla", "tms1000_arrball_output.pla", nullptr, "", 7, 7 },
    // Parker Brothers Bank Shot (1980): TMS1400, 70 LEDs on a pool table (R2-R10 x O0-O7)
    { "bankshot", BANKSHOT, TMS1K::TMS1400, 475000, "mp7313", "tms1100_common1_micro.pla", "tms1400_bankshot_output.pla", nullptr, "", 11, 8 },
    // Hasbro / Tiger Bingo Bear (1986): TMS1100 + TMS5110A speech from 2 x 16 KB VSMs, no display
    { "bbear", BBEAR, TMS1K::TMS1100, 320000, "e7cl04n2l", "tms1100_common1_micro.pla", "tms1100_bbear_output.pla", nullptr, "", 1, 1 },
    // Bandai System Control Car: Cheetah (1979): TMS1000, a programmable buggy; the "display" is its
    // motors: column 0 steer left (O0), 1 drive (O1), 2 steer right (O2)
    { "bcheetah", BCHEETAH, TMS1K::TMS1000, 350000, "mp0915", "tms1000_common2_micro.pla", "tms1000_bcheetah_output.pla", nullptr, "", 1, 3 },
    // Milton Bradley Big Trak (1979): TMS1000, a programmable tank; the "display" is one row:
    // 0 IR LED, 1 lamp, 2 left motor forward, 3 left reverse, 4 right forward, 5 right reverse
    { "bigtrak", BIGTRAK, TMS1K::TMS1000, 200000, "mp3301a", "tms1000_common4_micro.pla", "tms1000_bigtrak_output.pla", nullptr, "", 1, 6 },
    // Entex Blast It (1980): TMS1000, 3 digits + 49 LEDs (R1-R7 x O0-O6, digits R8-R10), 1-bit sound
    { "blastit", BLASTIT, TMS1K::TMS1000, 425000, "mp0230", "tms1000_common2_micro.pla", "tms1000_blastit_output.pla", nullptr, "", 10, 7 },
    { "arcmania", ARCMANIA, TMS1K::TMS1100, 250000, "m34078a", "tms1100_common1_micro.pla", "tms1100_arcmania_output.pla", nullptr, "", 1, 9 },
};
const Tms1kGame* find(const std::string& id)
{
    for (auto& g : kGames) if (id == g.id) return &g;
    return nullptr;
}
const RomFile* file(const std::vector<RomFile>& roms, const char* name)
{
    for (auto& r : roms) if (r.name == name) return &r;
    return nullptr;
}
}

bool HhTms1kCore::known(const std::string& game) { return find(game) != nullptr; }

HhTms1kCore::HhTms1kCore(const std::string& game) : m_g(find(game)) { std::memset(&m_s, 0, sizeof(m_s)); }

bool HhTms1kCore::load(const std::vector<RomFile>& roms, std::string& error)
{
    if (!m_g) { error = "Unknown game"; return false; }
    const RomFile* rom = file(roms, m_g->rom);
    const RomFile* mp = file(roms, m_g->mpla);
    const RomFile* op = m_g->opla ? file(roms, m_g->opla) : nullptr;
    if (!rom) { error = std::string("Missing ") + m_g->rom; return false; }
    if (!mp) { error = std::string("Missing ") + m_g->mpla; return false; }
    if (m_g->opla && !op) { error = std::string("Missing ") + m_g->opla; return false; }
    if (!loadScreen(roms, m_g->svg, m_g->rows, m_g->cols, error)) return false;
    if (m_g->kind == BBEAR) {
        const RomFile* v0 = file(roms, "cm62056");
        const RomFile* v1 = file(roms, "cm62057");
        if (!v0 || !v1) { error = "Missing the speech ROMs (cm62056, cm62057)"; return false; }
        m_vsmRom.assign(0x10000, 0xff);     // 8000-bfff: the module slot (empty)
        std::memcpy(m_vsmRom.data(), v0->data.data(), std::min<size_t>(v0->data.size(), 0x4000));
        std::memcpy(m_vsmRom.data() + 0x4000, v1->data.data(), std::min<size_t>(v1->data.size(), 0x4000));
        m_vsm.setRom(m_vsmRom.data(), 0x10000);
        m_tms.init(&m_vsm);
        m_tms.update = [this] { speechTo(now()); };
        m_speechOn = true;
    }
    m_cpu.configure(m_g->type, this);
    if (m_g->otable) m_cpu.setOutputTable(m_g->otable);
    std::string mtext(mp->data.begin(), mp->data.end());
    std::string otext = op ? std::string(op->data.begin(), op->data.end()) : std::string();
    if (!m_cpu.setRom(rom->data, mtext, otext, error)) return false;
    setClock(m_g->clock);     // one clock per subcycle, 6 per instruction
    reset();
    return true;
}

void HhTms1kCore::reset()
{
    uint64_t idle = m_s.idle;
    std::memset(&m_s, 0, sizeof(m_s));
    m_s.idle = idle;
    m_cpu.reset();
    m_s.power = 1;        // as MAME's machine_reset: on, whatever the reset wrote
    m_s.prevIn = m_in;
    if (m_speechOn) { m_vsm.reset(); m_tms.device_reset(); m_spPos = now(); m_speech.clear(); }
}

void HhTms1kCore::speechTo(uint64_t t)
{
    while (m_spPos + 40 <= t) {
        int16_t smp;
        m_tms.process(&smp, 1);
        m_speech.push_back(smp);
        m_spPos += 40;
    }
}

void HhTms1kCore::setPower(bool on)
{
    // off holds the MCU in reset (MAME asserts its RESET line); on lets it run
    if (on == (m_s.power != 0)) return;
    m_s.power = on ? 1 : 0;
    if (!on) {
        m_cpu.reset();
        m_s.power = 0;
        m_disp.matrix(now(), 0, 0);
    }
}

int HhTms1kCore::runCpu(int n)
{
    if (!m_s.power) { m_s.idle += uint64_t(n); return n; }
    return m_cpu.run(n);
}

void HhTms1kCore::runFrame()
{
    size_t before = m_audio.size();
    runFrameCommon();
    if (!m_speechOn) return;
    // mix the speech (8 kHz) into this frame's 44.1 kHz samples
    speechTo(now());
    size_t n = m_audio.size() > before ? m_audio.size() - before : 0;
    size_t sn = m_speech.size();
    if (n && sn) {
        for (size_t i = 0; i < n; i++) {
            int v = m_audio[before + i] + m_speech[std::min(sn - 1, i * sn / n)] / 2;
            m_audio[before + i] = int16_t(v < -32768 ? -32768 : v > 32767 ? 32767 : v);
        }
    }
    m_speech.clear();
}

void HhTms1kCore::inputsChanged()
{
    uint32_t pressed = m_in & ~m_s.prevIn;
    m_s.prevIn = m_in;
    switch (m_g->kind) {
    case ALPHIE: if (pressed & 1) setPower(true); break;       // the button turns it back on
    case ARCMANIA: if (pressed & 7) setPower(true); break;     // the orange buttons do
    case BBEAR: if (pressed & 0x30) setPower(true); break;     // the mouth or body sensor
    default: break;
    }
}

uint8_t HhTms1kCore::readK()
{
    switch (m_g->kind) {
    case ALPHIE: {
        // rotated matrix: K1 button, K2 question arm, K4 answer arm, K8 activity selector (R1-R5 select, d5 = Vss)
        uint32_t rows[4] = {
            in(0) ? 0x20u : 0u,
            1u << (m_opt[0] % 5),
            1u << (m_opt[1] % 5),
            1u << (m_opt[2] % 4),
        };
        uint8_t ret = 0;
        for (int i = 0; i < 4; i++) if (rows[i] & m_s.inpMux & 0x3f) ret |= uint8_t(1 << i);
        return ret;
    }
    case AMAZTRON: {
        // R0-R4: board columns (square n = bit n-1 of the inputs), R5: SELECT (bit 25), START (bit 26)
        uint8_t k = 0;
        for (int c = 0; c < 5; c++) {
            if (!(m_s.inpMux >> c & 1)) continue;
            for (int row = 0; row < 5; row++) if (in(row * 5 + c)) k |= uint8_t(1 << row);
        }
        if (m_s.inpMux & 0x20) k |= uint8_t(in(25) | in(26) << 1);
        if (k & 0x10) k |= 0xc;     // the 5th row is tied to K4 + K8
        return k & 0xf;
    }
    case ARRBALL:
        // R8: Shot, Stop, -, Speed (option 0: 1 = fast)
        return (m_s.inpMux & 1) ? uint8_t(in(0) | in(1) << 1 | (m_opt[0] ? 8 : 0)) : 0;
    case BANKSHOT: {
        // R2: Angle, Aim, Cue Up / Shoot (bits 0-2); R3: Select / Score, Ball Up, Ball Over (bits 3-5)
        uint8_t k = 0;
        if (m_s.inpMux & 1) k |= uint8_t(in(0) | in(1) << 1 | in(2) << 2);
        if (m_s.inpMux & 2) k |= uint8_t(in(3) | in(4) << 1 | in(5) << 2);
        return k;
    }
    case BCHEETAH: {
        // R0: mode (K2 = program; option 0: 1 = run); R1: left, forward, right, go; R2: K8 stop;
        // R3: K2 1, K8 2; R4: K2 3, K8 4. K4 is also tied to R5 + R6.
        // input bits: 4 left, 5 forward, 6 right, 7 go, 8 stop, 9-12 keys 1-4
        uint8_t k = 0, mux = uint8_t(m_s.inpMux);
        if (mux & 1) k |= m_opt[0] ? 0 : 2;
        if (mux & 2) k |= uint8_t(in(4) | in(5) << 1 | in(6) << 2 | in(7) << 3);
        if (mux & 4) k |= uint8_t(in(8) << 3);
        if (mux & 8) k |= uint8_t(in(9) << 1 | in(10) << 3);
        if (mux & 16) k |= uint8_t(in(11) << 1 | in(12) << 3);
        if (m_s.r & 0x60) k |= 4;
        return k;
    }
    case BBEAR: {
        // K: sensors (bit 4 mouth, 5 body (hug), 6 neck: 1 = touched) masked by the mux, and the
        // TMS5100 CTL (also tied to R1234)
        uint8_t sensors = uint8_t((in(6) ? 1 : 0) | (in(4) ? 0 : 4) | (in(5) ? 0 : 8));
        return uint8_t((m_s.inpMux & sensors) | m_tms.ctl_r() | (m_s.r & 0xf));
    }
    case BIGTRAK: {
        // R0-R5: keypad rows (input bit row * 4 + column), R8: the accessory input (bit 24); K8: the gearbox IR sensor
        gearTo(now());
        uint8_t k = 0;
        for (int row = 0; row < 6; row++) if (m_s.inpMux >> row & 1) k |= uint8_t(m_in >> (row * 4) & 0xf);
        if (m_s.inpMux & 0x40) k |= uint8_t(in(24) << 2);
        if (m_s.gear < 0 && (m_s.r >> 7 & 1)) k |= 8;
        return k;
    }
    case BLASTIT:
        // R3: right, left, fire (bits 0-2), K8 difficulty (option 0: 1 = PRO)
        return (m_s.inpMux & 1) ? uint8_t(in(0) | in(1) << 1 | in(2) << 2 | (m_opt[0] ? 0 : 8)) : 0;
    case ARCMANIA: {
        // O3: Alien Raiders (6), yellow 2 (7), Rattler (8); O4: Run Amuk (3), yellow 1 (4), Sneak Attack (5);
        // O6: orange 1-3 (0-2)
        uint8_t k = 0;
        if (m_s.inpMux & 1) k |= uint8_t(in(6) | in(7) << 1 | in(8) << 2);
        if (m_s.inpMux & 2) k |= uint8_t(in(3) | in(4) << 1 | in(5) << 2);
        if (m_s.inpMux & 4) k |= uint8_t(in(0) | in(1) << 1 | in(2) << 2);
        return k;
    }
    }
    return 0;
}

void HhTms1kCore::writeO(uint16_t o)
{
    m_s.o = o;
    switch (m_g->kind) {
    case ALPHIE: speaker(o & 1); break;
    case BBEAR: m_s.inpMux = uint16_t((m_s.inpMux & ~1) | (o & 1)); break;   // O0: input mux low
    case BCHEETAH: m_disp.matrix(now(), 1, o & 7); break;                       // the motors
    case BIGTRAK: gearTo(now()); m_s.o = o; bigtrakOut(); break;
    case AMAZTRON: m_disp.matrix(now(), m_s.r, o & 0x7f); break;
    case ARRBALL: m_disp.matrix(now(), m_s.r & 0x7f, o & 0x7f); break;
    case BANKSHOT: m_disp.matrix(now(), m_s.r & 0x7fc, o & 0xff); break;
    case BLASTIT: m_disp.matrix(now(), m_s.r >> 1, o & 0x7f); break;
    case ARCMANIA:
        speakerLevel(__builtin_popcount(unsigned(o & 7)) / 3.0);
        m_s.inpMux = uint16_t((o >> 3 & 3) | (o >> 4 & 4));
        if (!(o & 0x20)) setPower(false);
        break;
    }
}

void HhTms1kCore::writeR(uint32_t r)
{
    switch (m_g->kind) {
    case ALPHIE:
        m_s.inpMux = uint16_t((r >> 1 & 0x1f) | 0x20);
        m_disp.matrix(now(), 1, r >> 6);
        if (~r & m_s.r & 1) setPower(false);   // R0 falling edge: power off
        m_s.r = r;
        break;
    case AMAZTRON:
        m_s.inpMux = uint16_t(r & 0x3f);
        speaker(r >> 10 & 1);
        m_s.r = r >> 6 & 0xf;
        m_disp.matrix(now(), m_s.r, m_s.o & 0x7f);
        break;
    case ARRBALL: {
        m_s.inpMux = uint16_t(r >> 8 & 1);
        int lv = int(r >> 9 & 3);                     // levels 0, 1, -1, 0
        speakerLevel(lv == 1 ? 1.0 : lv == 2 ? 0.0 : 0.5);
        m_s.r = r;
        m_disp.matrix(now(), r & 0x7f, m_s.o & 0x7f);
        break;
    }
    case BANKSHOT:
        speaker(r & 1);
        m_s.inpMux = uint16_t(r >> 2 & 3);
        m_s.r = r;
        m_disp.matrix(now(), r & 0x7fc, m_s.o & 0xff);
        break;
    case BCHEETAH:
        m_s.inpMux = uint16_t(r & 0x1f);    // R0-R4: input mux
        m_s.r = r;
        break;
    case BBEAR: {
        // R1234: TMS5100 CTL8421, R0: its PDC pin, R6-R8: input mux high, R9: power off (falling edge)
        uint32_t sw = ((r >> 0 & 1) << 4) | ((r >> 1 & 1) << 3) | ((r >> 2 & 1) << 2) | ((r >> 3 & 1) << 1) | (r >> 4 & 1);
        uint32_t rr = sw | (r & ~0x1fu);
        m_tms.ctl_w(uint8_t(rr & 0xf));
        m_tms.pdc_w(r & 1);
        m_s.inpMux = uint16_t((m_s.inpMux & 1) | (r >> 5 & 0xe));
        bool off = (~r & m_s.o2 & 0x200) != 0;
        m_s.o2 = r;
        m_s.r = rr;
        if (off) setPower(false);
        break;
    }
    case BLASTIT:
        m_s.inpMux = uint16_t(r >> 3 & 1);
        speaker(r & 1);
        m_s.r = r;
        m_disp.matrix(now(), r >> 1, m_s.o & 0x7f);
        break;
    case ARCMANIA:
        m_disp.matrix(now(), 1, r >> 1);
        m_s.r = r;
        break;
    case BIGTRAK:
        m_s.inpMux = uint16_t((r & 0x3f) | (r >> 2 & 0x40));   // R0-R5, R8
        m_s.r = r;
        bigtrakOut();
        break;
    default: break;
    }
}

void HhTms1kCore::bigtrakOut()
{
    // O0, O7, R10 tied together: a 4-level speaker; R7 IR LED, R9 lamp, O1-O4 the motors
    unsigned spk = (m_s.o & 1) | (m_s.o >> 6 & 2) | (m_s.r >> 8 & 4);
    speakerLevel(__builtin_popcount(spk) / 3.0);
    m_disp.matrix(now(), 1, (m_s.r >> 7 & 1) | (m_s.r >> 8 & 2) | ((m_s.o >> 1 & 0xf) << 2));
}

void HhTms1kCore::gearTo(uint64_t t)
{
    // the last gear has 12 holes between an IR LED and receiver: once a millisecond while a motor
    // runs the wheel moves on a step (period 34 steps, half of it blocking the light)
    const uint64_t ms = uint64_t(m_g->clock / 1000);
    if (t < m_s.gearT) m_s.gearT = t;
    while (m_s.gearT + ms <= t) {
        m_s.gearT += ms;
        if (m_s.gear >= 17) m_s.gear = -17;
        if (m_s.o & 0x1e) m_s.gear++;
    }
}

bool HhTms1kCore::saveState(std::vector<uint8_t>& out)
{
    out.clear();
    uint32_t magic = 0x4b31534d;   // 'MS1K'
    auto put = [&](const void* p, size_t n) { const uint8_t* b = (const uint8_t*)p; out.insert(out.end(), b, b + n); };
    put(&magic, 4); put(&m_cpu.s, sizeof(m_cpu.s)); put(&m_s, sizeof(m_s)); put(&m_disp, sizeof(m_disp)); put(&m_spk, sizeof(m_spk));
    return true;
}

bool HhTms1kCore::loadState(const std::vector<uint8_t>& in)
{
    if (in.size() != 4 + sizeof(m_cpu.s) + sizeof(m_s) + sizeof(m_disp) + sizeof(m_spk)) return false;
    uint32_t magic; std::memcpy(&magic, in.data(), 4);
    if (magic != 0x4b31534d) return false;
    const uint8_t* p = in.data() + 4;
    std::memcpy(&m_cpu.s, p, sizeof(m_cpu.s)); p += sizeof(m_cpu.s);
    std::memcpy(&m_s, p, sizeof(m_s)); p += sizeof(m_s);
    std::memcpy(&m_disp, p, sizeof(m_disp)); p += sizeof(m_disp);
    std::memcpy(&m_spk, p, sizeof(m_spk));
    m_spkPos = double(now()); m_audio.clear();
    return true;
}
