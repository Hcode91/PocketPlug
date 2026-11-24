// PocketPlug - NEC uCOM-4 handhelds (port of MAME's handheld/hh_ucom4.cpp, hap)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "hh_ucom4.h"
#include <cstring>

namespace {
enum Kind { ALNCHASE, ASTROCMD, BCCLIMBR, BGALAXN };
}

struct Ucom4Game {
    const char* id;
    Kind kind;
    UCOM4::Family family;
    bool d557l;
    double clock;
    const char* rom;
    size_t romSize;
    const char* svg;
    int rows, cols;
};

namespace {
const Ucom4Game kGames[] = {
    // Tomy Alien Chase (1984): uPD553C, two-sided VFD (player 2 looks at the mirror image)
    { "alnchase", ALNCHASE, UCOM4::UCOM43, false, 400000, "d553c_258", 0x800, "alnchase.svg", 9, 17 },
    // Epoch Astro Command (1982): uPD553C, VFD
    { "astrocmd", ASTROCMD, UCOM4::UCOM43, false, 400000, "d553c_202.s01", 0x800, "astrocmd.svg", 9, 19 },
    // Bandai Crazy Climber (1982): uPD553C, VFD; two sticks (left: up / down / left, right: up / down / right)
    { "bcclimbr", BCCLIMBR, UCOM4::UCOM43, false, 400000, "d553c_170", 0x800, "bcclimbr.svg", 6, 20 },
    // Bandai Galaxian (1981): uPD553C, VFD; FIRE is the INT line
    { "bgalaxn", BGALAXN, UCOM4::UCOM43, false, 400000, "d553c_103", 0x800, "bgalaxn.svg", 9, 16 },
};
const Ucom4Game* find(const std::string& id)
{
    for (auto& g : kGames) if (id == g.id) return &g;
    return nullptr;
}
}

bool HhUcom4Core::known(const std::string& game) { return find(game) != nullptr; }

HhUcom4Core::HhUcom4Core(const std::string& game) : m_g(find(game)) { std::memset(&m_s, 0, sizeof(m_s)); }

bool HhUcom4Core::load(const std::vector<RomFile>& roms, std::string& error)
{
    if (!m_g) { error = "Unknown game"; return false; }
    const RomFile* rom = nullptr;
    for (auto& r : roms) if (r.name == m_g->rom) rom = &r;
    if (!rom) { error = std::string("Missing ") + m_g->rom; return false; }
    if (!loadScreen(roms, m_g->svg, m_g->rows, m_g->cols, error)) return false;
    m_cpu.configure(m_g->family, this, m_g->d557l);
    m_cpu.setRom(rom->data);
    setClock(m_g->clock / 4.0);
    reset();
    return true;
}

void HhUcom4Core::reset()
{
    std::memset(&m_s, 0, sizeof(m_s));
    m_cpu.reset();
}

uint8_t HhUcom4Core::input(int port)
{
    switch (m_g->kind) {
    case ALNCHASE:
        if (port == UCOM4::PA) {
            // A: joysticks, P1 enabled by grid 0 (C0), P2 by grid 4 (D0)
            uint8_t v = 0;
            if (m_s.inpMux & 1) v |= uint8_t(in(2) | in(3) << 1 | in(1) << 2 | in(0) << 3);
            if (m_s.inpMux & 2) v |= uint8_t(in(15) | in(14) << 1 | in(13) << 2 | in(12) << 3);
            return v;
        }
        if (port == UCOM4::PB) {
            // B0: players (1 = one), B1: level (1 = pro)
            return uint8_t((m_opt[0] ? 0 : 1) | (m_opt[1] ? 2 : 0));
        }
        return 0;
    case ASTROCMD:
        // A: SELECT, START, Missile, Bomb; B: up, down, left, right
        if (port == UCOM4::PA) return uint8_t(in(4) | in(5) << 1 | in(6) << 2 | in(7) << 3);
        if (port == UCOM4::PB) return uint8_t(m_in & 0xf);
        return 0;
    case BCCLIMBR:
        // A: START, left stick up, down, left; B: factory test (off), right stick up, down, right
        if (port == UCOM4::PA) return uint8_t(in(4) | in(0) << 1 | in(1) << 2 | in(2) << 3);
        if (port == UCOM4::PB) return uint8_t(in(12) << 1 | in(13) << 2 | in(15) << 3);
        return 0;
    case BGALAXN:
        // A: right, left, factory test (off), difficulty (option 0)
        if (port == UCOM4::PA) return uint8_t(in(3) | in(2) << 1 | (m_opt[0] ? 8 : 0));
        return 0;
    }
    return 0;
}

void HhUcom4Core::output(int port, uint8_t data)
{
    if (port < 9) m_s.port[port] = data;
    switch (m_g->kind) {
    case ALNCHASE:
        if (port <= UCOM4::PE) {
            int shift = (port - UCOM4::PC) * 4;
            m_s.grid = (m_s.grid & ~(0xfu << shift)) | (uint32_t(data) << shift);
            m_s.inpMux = uint16_t((m_s.grid & 1) | (m_s.grid >> 3 & 2));
            if (port == UCOM4::PE) speaker(data >> 1 & 1);
        }
        if (port >= UCOM4::PE) {
            int shift = (port - UCOM4::PE) * 4;
            m_s.plate = ((m_s.plate << 2 & ~(0xfu << shift)) | (uint32_t(data) << shift)) >> 2;
        }
        m_disp.matrix(now(), m_s.grid, m_s.plate);
        break;
    case ASTROCMD:
        // C, D (, E3): grid; E, F, G, H, I: plate; E2: speaker
        if (port == UCOM4::PC || port == UCOM4::PD) {
            int shift = (port - UCOM4::PC) * 4;
            m_s.grid = (m_s.grid & ~(0xfu << shift)) | (uint32_t(data) << shift);
        } else if (port >= UCOM4::PE) {
            int shift = (port - UCOM4::PE) * 4;
            m_s.plate = (m_s.plate & ~(0xfu << shift)) | (uint32_t(data) << shift);
            if (port == UCOM4::PE) {
                speaker(data >> 2 & 1);
                m_s.grid = (m_s.grid & ~0x100u) | (uint32_t(data >> 3 & 1) << 8);
            }
        }
        m_disp.matrix(now(), m_s.grid, m_s.plate);
        break;
    case BCCLIMBR:
        // H, I0-1: grid (I2: speaker); C, D, E, F, G: plate
        if (port >= UCOM4::PH) {
            if (port == UCOM4::PI) speaker(data >> 2 & 1);
            int shift = (port - UCOM4::PH) * 4;
            m_s.grid = (m_s.grid & ~(0xfu << shift)) | (uint32_t(data) << shift);
        } else if (port >= UCOM4::PC) {
            int shift = (port - UCOM4::PC) * 4;
            m_s.plate = (m_s.plate & ~(0xfu << shift)) | (uint32_t(data) << shift);
        }
        m_disp.matrix(now(), m_s.grid, m_s.plate);
        break;
    case BGALAXN:
        // C, D, I0: grid (I1-2: 2-level speaker); E, F, G, H: plate
        if (port == UCOM4::PC || port == UCOM4::PD || port == UCOM4::PI) {
            if (port == UCOM4::PI) { static const double lv[4] = { 0.5, 1.0, 0.0, 0.5 }; speakerLevel(lv[data >> 1 & 3]); }
            int shift = port == UCOM4::PI ? 8 : (port - UCOM4::PC) * 4;
            m_s.grid = ((m_s.grid & ~(0xfu << shift)) | (uint32_t(data) << shift)) & 0x1ff;
        } else if (port >= UCOM4::PE && port <= UCOM4::PH) {
            int shift = (port - UCOM4::PE) * 4;
            m_s.plate = (m_s.plate & ~(0xfu << shift)) | (uint32_t(data) << shift);
        }
        m_disp.matrix(now(), m_s.grid, m_s.plate);
        break;
    }
}

void HhUcom4Core::inputsChanged()
{
    if (m_g && m_g->kind == BGALAXN) m_cpu.setInt(in(4));   // FIRE
}

bool HhUcom4Core::saveState(std::vector<uint8_t>& out)
{
    out.clear();
    uint32_t magic = 0x344d4355;   // 'UCM4'
    auto put = [&](const void* p, size_t n) { const uint8_t* b = (const uint8_t*)p; out.insert(out.end(), b, b + n); };
    put(&magic, 4); put(&m_cpu.s, sizeof(m_cpu.s)); put(&m_s, sizeof(m_s)); put(&m_disp, sizeof(m_disp)); put(&m_spk, sizeof(m_spk));
    return true;
}

bool HhUcom4Core::loadState(const std::vector<uint8_t>& in)
{
    if (in.size() != 4 + sizeof(m_cpu.s) + sizeof(m_s) + sizeof(m_disp) + sizeof(m_spk)) return false;
    uint32_t magic; std::memcpy(&magic, in.data(), 4);
    if (magic != 0x344d4355) return false;
    const uint8_t* p = in.data() + 4;
    std::memcpy(&m_cpu.s, p, sizeof(m_cpu.s)); p += sizeof(m_cpu.s);
    std::memcpy(&m_s, p, sizeof(m_s)); p += sizeof(m_s);
    std::memcpy(&m_disp, p, sizeof(m_disp)); p += sizeof(m_disp);
    std::memcpy(&m_spk, p, sizeof(m_spk));
    m_spkPos = double(now()); m_audio.clear();
    return true;
}
