// PocketPlug - TI TMS1000-family handhelds (port of MAME's handheld/hh_tms1k.cpp, hap)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "hh_tms1k.h"
#include <cstring>

namespace {
enum Kind { ALPHIE, AMAZTRON, ARCMANIA, ARRBALL };
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
    runFrameCommon();
}

void HhTms1kCore::inputsChanged()
{
    uint32_t pressed = m_in & ~m_s.prevIn;
    m_s.prevIn = m_in;
    switch (m_g->kind) {
    case ALPHIE: if (pressed & 1) setPower(true); break;       // the button turns it back on
    case ARCMANIA: if (pressed & 7) setPower(true); break;     // the orange buttons do
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
    case AMAZTRON: m_disp.matrix(now(), m_s.r, o & 0x7f); break;
    case ARRBALL: m_disp.matrix(now(), m_s.r & 0x7f, o & 0x7f); break;
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
    case ARCMANIA:
        m_disp.matrix(now(), 1, r >> 1);
        m_s.r = r;
        break;
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
