// PocketPlug - Rockwell B5000-family handhelds (port of MAME's handheld/hh_rw5000.cpp, hap)
// SPDX-License-Identifier: BSD-3-Clause

#include "hh_rw5000.h"
#include <cstring>

namespace {
enum Kind { AUTORACE };
}

struct Rw5000Game {
    const char* id;
    Kind kind;
    double clock;
    const char* rom;
    int rows, cols;
};

namespace {
const Rw5000Game kGames[] = {
    // Mattel Auto Race (1976): B6000, 2 digits + 21 LEDs
    { "autorace", AUTORACE, 160000, "b6000ca", 9, 8 },
};
const Rw5000Game* find(const std::string& id)
{
    for (auto& g : kGames) if (id == g.id) return &g;
    return nullptr;
}
}

bool HhRw5000Core::known(const std::string& game) { return find(game) != nullptr; }

HhRw5000Core::HhRw5000Core(const std::string& game) : m_g(find(game)), m_cpu(this) { std::memset(&m_s, 0, sizeof(m_s)); }

bool HhRw5000Core::load(const std::vector<RomFile>& roms, std::string& error)
{
    if (!m_g) { error = "Unknown game"; return false; }
    const RomFile* rom = nullptr;
    for (auto& r : roms) if (r.name == m_g->rom) rom = &r;
    if (!rom) { error = std::string("Missing ") + m_g->rom; return false; }
    if (!loadScreen(roms, "", m_g->rows, m_g->cols, error)) return false;
    m_disp.setLevel(0.02);
    m_cpu.setRom(rom->data.data(), rom->data.size());
    setClock(m_g->clock / 4.0);
    reset();
    return true;
}

void HhRw5000Core::reset()
{
    m_s.str = m_s.seg = 0;
    m_cpu.reset();
}

void HhRw5000Core::inputsChanged()
{
    // option 2: the START / RESET slider (1 = held in reset, the display goes dark)
    if (m_opt[2] && !m_s.held) { m_s.held = 1; m_disp.matrix(now(), 0, 0); }
    else if (!m_opt[2] && m_s.held) { m_s.held = 0; reset(); }
}

int HhRw5000Core::runCpu(int n)
{
    if (m_s.held) {
        // the CPU sits in reset but time goes on
        m_cpu.s.cycles += uint64_t(n);
        return n;
    }
    return m_cpu.run(n);
}

uint8_t HhRw5000Core::readKB()
{
    // KB1 right, KB2 left (option 1: the steering slider 0 left, 1 centre, 2 right),
    // KB4+KB8: the gear (option 0: gears 1-4)
    static const uint8_t gear[4] = { 0x0c, 0x04, 0x00, 0x08 };
    uint8_t k = gear[m_opt[0] & 3];
    if (m_opt[1] == 2) k |= 1;
    if (m_opt[1] == 0) k |= 2;
    return k;
}

bool HhRw5000Core::saveState(std::vector<uint8_t>& out)
{
    out.clear();
    uint32_t magic = 0x35575252;   // 'RRW5'
    auto put = [&](const void* p, size_t n) { const uint8_t* b = (const uint8_t*)p; out.insert(out.end(), b, b + n); };
    put(&magic, 4); put(&m_cpu.s, sizeof(m_cpu.s)); put(&m_s, sizeof(m_s)); put(&m_disp, sizeof(m_disp)); put(&m_spk, sizeof(m_spk));
    return true;
}

bool HhRw5000Core::loadState(const std::vector<uint8_t>& in)
{
    if (in.size() != 4 + sizeof(m_cpu.s) + sizeof(m_s) + sizeof(m_disp) + sizeof(m_spk)) return false;
    uint32_t magic; std::memcpy(&magic, in.data(), 4);
    if (magic != 0x35575252) return false;
    const uint8_t* p = in.data() + 4;
    std::memcpy(&m_cpu.s, p, sizeof(m_cpu.s)); p += sizeof(m_cpu.s);
    std::memcpy(&m_s, p, sizeof(m_s)); p += sizeof(m_s);
    std::memcpy(&m_disp, p, sizeof(m_disp)); p += sizeof(m_disp);
    std::memcpy(&m_spk, p, sizeof(m_spk));
    m_spkPos = double(now()); m_audio.clear();
    return true;
}
