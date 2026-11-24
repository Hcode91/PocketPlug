// PocketPlug - Hitachi HMCS40 handhelds (port of MAME's handheld/hh_hmcs40.cpp, hap)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "hh_hmcs40.h"
#include <cstring>

namespace {

// input bits (hh_base.h)
enum { UP = 0, DOWN, LEFT, RIGHT, B1, B2, B3, B4, B5, B6, B7, B8 };
// an input matrix bit: an input bit, or (OPT + n) = option n set
constexpr int OPT = 100;
struct InBit { uint16_t mask; int src; };
struct InRow { InBit bits[4]; };

enum Kind { ALNATTCK, BAMBBALL, BBTIME, BDORAMON, BFRISKYT };

}

struct Hmcs40Game {
    const char* id;
    Kind kind;
    HMCS40::Family family;
    bool cmos;
    double clock;          // oscillator Hz
    const char* rom;
    const char* svg;
    int rows, cols;        // PWM matrix
    int inputRows;
    InRow in[8];
};

namespace {

const Hmcs40Game kGames[] = {
    // Coleco Alien Attack (1981): HD38800A25, D7-D13 select the input rows read on D5
    { "alnattck", ALNATTCK, HMCS40::HMCS44, false, 400000, "hd38800a25", "alnattck.svg", 10, 20, 7, {
        { { { 0x20, OPT + 0 } } },          // difficulty switch (option 0: 0 = 1, 1 = 2)
        { { { 0x20, LEFT } } },
        { { { 0x20, RIGHT } } },
        { { { 0x20, UP } } },
        { { { 0x20, DOWN } } },
        { { { 0x20, B1 } } },               // Move
        { { { 0x20, B2 } } },               // Fire
    } },
    // Bambino Basketball (1979): HD38750A08, D7-D10 select the input rows read on R0x
    // (row 3 is the skill / teams switches, built from options 0 and 1 in readR)
    { "bambball", BAMBBALL, HMCS40::HMCS43, false, 400000, "hd38750a08", "bambball.svg", 9, 16, 3, {
        { { { 0x01, B1 }, { 0x02, B2 }, { 0x04, B3 }, { 0x08, UP } } },   // dribble low / medium / high, shoot
        { { { 0x01, RIGHT }, { 0x02, LEFT } } },
        { { { 0x04, B4 }, { 0x08, B5 } } },                               // start, display
    } },
    // Bandai Burger Time (1983): HD38820A65, D10-D14 select the inputs on INT0; B1 (pepper) is INT1
    { "bbtime", BBTIME, HMCS40::HMCS45, false, 400000, "hd38820a65", "bbtime.svg", 6, 28, 5, {
        { { { 0x01, RIGHT } } }, { { { 0x01, LEFT } } }, { { { 0x01, DOWN } } }, { { { 0x01, UP } } },
        { { { 0x01, B2 } } },
    } },
    // Bandai Dokodemo Dorayaki Doraemon (1983): HD38800B43; LEFT is INT0, B1 INT1, up / right / down on D4-D6
    { "bdoramon", BDORAMON, HMCS40::HMCS44, false, 400000, "hd38800b43", "bdoramon.svg", 8, 20, 0, {} },
    // Bandai Frisky Tom (1981): HD38800A77, D11-D15 select the inputs on INT1; START (B2) is INT0
    { "bfriskyt", BFRISKYT, HMCS40::HMCS44, false, 400000, "hd38800a77", "bfriskyt.svg", 8, 22, 5, {
        { { { 0x01, B1 } } }, { { { 0x01, DOWN } } }, { { { 0x01, RIGHT } } }, { { { 0x01, UP } } }, { { { 0x01, LEFT } } },
    } },
};

const Hmcs40Game* find(const std::string& id)
{
    for (auto& g : kGames) if (id == g.id) return &g;
    return nullptr;
}

}

bool HhHmcs40Core::known(const std::string& game) { return find(game) != nullptr; }

HhHmcs40Core::HhHmcs40Core(const std::string& game) : m_g(find(game))
{
    std::memset(&m_s, 0, sizeof(m_s));
}

bool HhHmcs40Core::load(const std::vector<RomFile>& roms, std::string& error)
{
    if (!m_g) { error = "Unknown game"; return false; }
    const RomFile* rom = nullptr;
    for (auto& r : roms) if (r.name == m_g->rom) rom = &r;
    if (!rom) { error = std::string("Missing ") + m_g->rom; return false; }
    if (!loadScreen(roms, m_g->svg, m_g->rows, m_g->cols, error)) return false;
    // MAME region: the first 0x1000 bytes at 0, then 0x100 bytes of pattern ROM at 0x1e80
    // (HMCS42/43: 0x800 bytes at 0, then 0x80 at 0xf00, in a 0x1000 region)
    bool small = m_g->family <= HMCS40::HMCS43;
    size_t main = small ? 0x800 : 0x1000, pat = small ? 0xf00 : 0x1e80, patLen = small ? 0x80 : 0x100;
    std::vector<uint8_t> region(small ? 0x1000 : 0x2000, 0);
    size_t n = rom->data.size();
    std::memcpy(region.data(), rom->data.data(), std::min(n, main));
    if (n > main) std::memcpy(region.data() + pat, rom->data.data() + main, std::min(n - main, patLen));
    m_cpu.configure(m_g->family, m_g->cmos, this);
    m_cpu.setRom(region);
    setClock(m_g->clock / 4.0);   // 4 oscillator clocks per machine cycle
    reset();
    return true;
}

void HhHmcs40Core::reset()
{
    std::memset(&m_s, 0, sizeof(m_s));
    m_cpu.reset();
    inputsChanged();
}

uint16_t HhHmcs40Core::readInputs(int columns) const
{
    uint16_t ret = 0;
    for (int i = 0; i < columns && i < m_g->inputRows; i++) {
        if (!(m_s.inpMux >> i & 1)) continue;
        for (auto& b : m_g->in[i].bits) {
            if (!b.mask) continue;
            bool on = b.src >= OPT ? m_opt[b.src - OPT] != 0 : in(b.src);
            if (on) ret |= b.mask;
        }
    }
    return ret;
}

void HhHmcs40Core::setInterrupt(int line, bool state)
{
    if (m_s.intLine[line] == (state ? 1 : 0)) return;
    m_s.intLine[line] = state ? 1 : 0;
    m_cpu.setInput(line, state ? 1 : 0);
}

void HhHmcs40Core::inputsChanged()
{
    switch (m_g->kind) {
    case BBTIME: setInterrupt(0, readInputs(5) != 0); setInterrupt(1, in(B1)); break;
    case BDORAMON: setInterrupt(0, in(LEFT)); setInterrupt(1, in(B1)); break;
    case BFRISKYT: setInterrupt(0, in(B2)); setInterrupt(1, readInputs(5) != 0); break;
    default: break;
    }
}

void HhHmcs40Core::updateDisplay()
{
    m_disp.matrix(now(), m_s.grid, m_s.plate);
}

uint8_t HhHmcs40Core::readR(int index)
{
    if (m_g->kind == BAMBBALL && index == 0) {
        // R0x: multiplexed inputs; D10 reads skill level (option 0: 0-2) and teams (option 1: 1 = two players)
        uint8_t r = uint8_t(readInputs(3));
        if (m_s.inpMux & 8) r |= uint8_t((1 << (m_opt[0] > 2 ? 2 : m_opt[0])) | (m_opt[1] ? 0 : 8));
        return r;
    }
    return m_g->cmos ? 0xf : 0;
}

void HhHmcs40Core::writeR(int index, uint8_t data)
{
    m_s.r[index & 7] = data;
    switch (m_g->kind) {
    case ALNATTCK:
        if (index < 4) {
            // R0x-R3x: vfd plate
            int shift = index * 4;
            m_s.plate = (m_s.plate & ~(uint64_t(0xf) << shift)) | (uint64_t(data) << shift);
            updateDisplay();
        }
        break;
    case BBTIME:
        if (index < 7) {
            // R0x-R6x: vfd plate
            int shift = index * 4;
            m_s.plate = (m_s.plate & ~(uint64_t(0xf) << shift)) | (uint64_t(data) << shift);
            updateDisplay();
        }
        break;
    case BDORAMON:
    case BFRISKYT:
        if (index < 4) {
            // R0x-R3x: vfd plate
            int shift = index * 4;
            m_s.plate = (m_s.plate & ~(uint64_t(0xf) << shift)) | (uint64_t(data) << shift);
            updateDisplay();
        }
        break;
    case BAMBBALL:
        if (index >= 1 && index <= 3) {
            // R1x-R3x: vfd plate
            int shift = (index - 1) * 4;
            m_s.plate = (m_s.plate & ~(uint64_t(0xf) << shift)) | (uint64_t(data) << shift);
            updateDisplay();
        }
        break;
    }
}

uint16_t HhHmcs40Core::readD()
{
    switch (m_g->kind) {
    case ALNATTCK: return readInputs(7) & 0x20;   // D5: multiplexed inputs
    case BDORAMON: return uint16_t((in(UP) ? 0x10 : 0) | (in(RIGHT) ? 0x20 : 0) | (in(DOWN) ? 0x40 : 0));
    default: break;
    }
    return m_g->cmos ? 0xffff : 0;
}

void HhHmcs40Core::writeD(uint16_t data, uint16_t mask)
{
    (void)mask;
    m_s.d = data;
    switch (m_g->kind) {
    case ALNATTCK:
        speaker(data >> 4 & 1);                     // D4: speaker
        m_s.inpMux = data >> 7 & 0x7f;              // D7-D13: input mux
        m_s.grid = data >> 6 & 0x3ff;               // D6-D15: vfd grid
        m_s.plate = (m_s.plate & ~(uint64_t(0xf) << 16)) | (uint64_t(data & 0xf) << 16);   // D0-D3: plate
        updateDisplay();
        break;
    case BBTIME:
        speaker(data >> 3 & 1);                     // D3: speaker
        m_s.inpMux = data >> 10 & 0x1f;             // D10-D14: input mux (INT0)
        setInterrupt(0, readInputs(5) != 0);
        m_s.grid = data >> 4 & 0x3f;                // D4-D9: vfd grid
        updateDisplay();
        break;
    case BDORAMON:
        speaker(data >> 7 & 1);                     // D7: speaker
        m_s.grid = data >> 8 & 0xff;                // D8-D15: vfd grid
        m_s.plate = (m_s.plate & ~(uint64_t(0xf) << 16)) | (uint64_t(data & 0xf) << 16);   // D0-D3: plate
        updateDisplay();
        break;
    case BFRISKYT:
        speaker(data >> 6 & 1);                     // D6: speaker
        m_s.inpMux = data >> 11 & 0x1f;             // D11-D15: input mux (INT1)
        setInterrupt(1, readInputs(5) != 0);
        m_s.grid = data >> 8 & 0xff;                // D8-D15: vfd grid
        m_s.plate = (m_s.plate & 0xffff) | (uint64_t(data & 0x3f) << 16);   // D0-D5: plate
        updateDisplay();
        break;
    case BAMBBALL:
        speaker(data >> 4 & 1);                     // D4: speaker
        m_s.inpMux = data >> 7 & 0xf;               // D7-D10: input mux
        m_s.grid = data >> 7 & 0x1ff;               // D7-D15: vfd grid
        m_s.plate = (m_s.plate & ~(uint64_t(0xf) << 12)) | (uint64_t(data & 0xf) << 12);   // D0-D3: plate
        updateDisplay();
        break;
    }
}

bool HhHmcs40Core::saveState(std::vector<uint8_t>& out)
{
    out.clear();
    uint32_t magic = 0x30344d48;   // 'HM40'
    auto put = [&](const void* p, size_t n) { const uint8_t* b = (const uint8_t*)p; out.insert(out.end(), b, b + n); };
    put(&magic, 4);
    put(&m_cpu.s, sizeof(m_cpu.s));
    put(&m_s, sizeof(m_s));
    put(&m_disp, sizeof(m_disp));
    put(&m_spk, sizeof(m_spk));
    return true;
}

bool HhHmcs40Core::loadState(const std::vector<uint8_t>& in)
{
    if (in.size() != 4 + sizeof(m_cpu.s) + sizeof(m_s) + sizeof(m_disp) + sizeof(m_spk)) return false;
    uint32_t magic; std::memcpy(&magic, in.data(), 4);
    if (magic != 0x30344d48) return false;
    const uint8_t* p = in.data() + 4;
    std::memcpy(&m_cpu.s, p, sizeof(m_cpu.s)); p += sizeof(m_cpu.s);
    std::memcpy(&m_s, p, sizeof(m_s)); p += sizeof(m_s);
    std::memcpy(&m_disp, p, sizeof(m_disp)); p += sizeof(m_disp);
    std::memcpy(&m_spk, p, sizeof(m_spk));
    m_spkPos = double(now());
    m_audio.clear();
    return true;
}
