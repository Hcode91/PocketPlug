// PocketPlug - Sharp SM511 LCD handhelds (port of MAME's handheld/hh_sm510.cpp, hap)
// SPDX-License-Identifier: BSD-3-Clause

#include "hh_sm511.h"
#include <cstring>

struct Sm511Game {
    const char* id;
    const char* rom;
    const char* melody;
    const char* svgTop;
    const char* svgBottom;      // nullptr: one screen
};

namespace {
const Sm511Game kGames[] = {
    // Telko Bassmate Computer (1984, made by Nintendo): BM-501, vertical dual screens
    { "bassmate", "bm-501.program", "bm-501.melody", "bassmate_top.svg", "bassmate_bottom.svg" },
};
const Sm511Game* find(const std::string& id)
{
    for (auto& g : kGames) if (id == g.id) return &g;
    return nullptr;
}
const int DECAY_PIVOT = 8, DECAY_LEN = 17;
// input bits (hh_base.h numbering): buttons 1-8 = bits 4-11, then Compute 12, Alarm 13, ACL 14
const int ACL_BIT = 14;
}

bool HhSm511Core::known(const std::string& game) { return find(game) != nullptr; }

HhSm511Core::HhSm511Core(const std::string& game) : m_g(find(game)), m_cpu(this) { std::memset(&m_s, 0, sizeof(m_s)); }

static const RomFile* findRom(const std::vector<RomFile>& roms, const char* name)
{
    for (auto& r : roms) if (r.name == name) return &r;
    return nullptr;
}

bool HhSm511Core::load(const std::vector<RomFile>& roms, std::string& error)
{
    if (!m_g) { error = "Unknown game"; return false; }
    const RomFile* rom = findRom(roms, m_g->rom);
    if (!rom) { error = std::string("Missing ") + m_g->rom; return false; }
    const RomFile* mel = findRom(roms, m_g->melody);
    if (!mel) { error = std::string("Missing ") + m_g->melody; return false; }
    if (!loadScreen(roms, m_g->svgTop, 1, 1, error)) return false;
    int w = m_fbW, h1 = m_fbH, h2 = 0;
    if (m_g->svgBottom) {
        const RomFile* f = findRom(roms, m_g->svgBottom);
        if (!f) { error = std::string("Missing ") + m_g->svgBottom + " (the screen artwork in the MAME zip)"; return false; }
        if (!m_svg2.load(std::string(f->data.begin(), f->data.end()), 960, error)) return false;
        h2 = m_svg2.height();
        w = std::max(w, m_svg2.width());
        m_levels2.assign(size_t(m_svg2.keyCount()) + 1, 0);
    }
    m_fbW = w; m_fbH = h1 + h2;
    m_fb.assign(size_t(m_fbW) * m_fbH, 0xff000000u);
    m_key1.assign(3 * 16 * 4, -1); m_key2.assign(3 * 16 * 4, -1);
    for (int x = 0; x < 3; x++)
        for (int y = 0; y < 16; y++)
            for (int z = 0; z < 4; z++) {
                std::string n = std::to_string(x) + "." + std::to_string(y) + "." + std::to_string(z);
                m_key1[size_t((x * 16 + y) * 4 + z)] = int16_t(m_svg.key(n));
                if (m_g->svgBottom) m_key2[size_t((x * 16 + y) * 4 + z)] = int16_t(m_svg2.key(n));
            }
    m_cpu.setRom(rom->data.data(), int(rom->data.size()));
    m_cpu.setMelody(mel->data.data(), int(mel->data.size()));
    setClock(32768.0);      // run() counts crystal clocks
    reset();
    return true;
}

void HhSm511Core::reset()
{
    std::memset(&m_s, 0, sizeof(m_s));
    m_cpu.reset();
    updateK();
}

void HhSm511Core::inputsChanged()
{
    // ACL is tied straight to the MCU's reset pin
    bool acl = in(ACL_BIT);
    if (acl && !m_aclHeld) m_cpu.reset();
    m_aclHeld = acl;
    updateK();
}

uint8_t HhSm511Core::readK()
{
    // bassmate: S1 Compute / Alarm; S2 Wind, Cover, Time of Day, Water Temp;
    // S3 Water Clarity, Structure, Water Depth, Season
    uint8_t k = 0;
    if (m_s.inpMux & 1) k |= uint8_t(in(12) | in(13) << 1);
    if (m_s.inpMux & 2) k |= uint8_t(in(11) | in(9) << 1 | in(7) << 2 | in(5) << 3);
    if (m_s.inpMux & 4) k |= uint8_t(in(10) | in(8) << 1 | in(6) << 2 | in(4) << 3);
    return k;
}

void HhSm511Core::lcd(const uint16_t rows[3][4])
{
    std::memcpy(m_s.rows, rows, sizeof(m_s.rows));
}

void HhSm511Core::tick1k()
{
    for (int x = 0; x < 3; x++)
        for (int y = 0; y < 16; y++)
            for (int z = 0; z < 4; z++) {
                uint8_t& d = m_s.decay[x][y][z];
                if (m_s.rows[x][z] >> y & 1) { if (d < DECAY_PIVOT + DECAY_LEN) d++; }
                else if (d > 0) d--;
            }
}

void HhSm511Core::runFrame()
{
    m_carry += m_hz / 60.0;
    int n = int(m_carry);
    m_carry -= n;
    int done = 0;
    while (done < n) done += runCpu(std::min(n - done, 64));
    audioTo(double(now()));
    std::fill(m_levels.begin(), m_levels.end(), 0);
    std::fill(m_levels2.begin(), m_levels2.end(), 0);
    for (int x = 0; x < 3; x++)
        for (int y = 0; y < 16; y++)
            for (int z = 0; z < 4; z++) {
                if (m_s.decay[x][y][z] < DECAY_PIVOT) continue;
                size_t i = size_t((x * 16 + y) * 4 + z);
                if (m_key1[i] >= 0) m_levels[size_t(m_key1[i])] = 255;
                if (m_key2[i] >= 0) m_levels2[size_t(m_key2[i])] = 255;
            }
    // top screen, then the bottom one under it
    const int w1 = m_svg.width(), h1 = m_svg.height();
    m_tmp.resize(size_t(w1) * h1);
    m_svg.render(m_levels.data(), m_tmp.data());
    for (int y = 0; y < h1; y++) std::memcpy(&m_fb[size_t(y) * m_fbW], &m_tmp[size_t(y) * w1], size_t(w1) * 4);
    if (m_g->svgBottom) {
        const int w2 = m_svg2.width(), h2 = m_svg2.height();
        m_tmp.resize(size_t(w2) * h2);
        m_svg2.render(m_levels2.data(), m_tmp.data());
        for (int y = 0; y < h2; y++) std::memcpy(&m_fb[size_t(h1 + y) * m_fbW], &m_tmp[size_t(y) * w2], size_t(w2) * 4);
    }
    if (m_audio.size() > 44100) m_audio.erase(m_audio.begin(), m_audio.end() - 4410);
}

bool HhSm511Core::saveState(std::vector<uint8_t>& out)
{
    out.clear();
    uint32_t magic = 0x31314d53;   // 'SM11'
    auto put = [&](const void* p, size_t n) { const uint8_t* b = (const uint8_t*)p; out.insert(out.end(), b, b + n); };
    put(&magic, 4); put(&m_cpu.s, sizeof(m_cpu.s)); put(&m_s, sizeof(m_s)); put(&m_cycles, 8); put(&m_spk, sizeof(m_spk));
    return true;
}

bool HhSm511Core::loadState(const std::vector<uint8_t>& in)
{
    if (in.size() != 4 + sizeof(m_cpu.s) + sizeof(m_s) + 8 + sizeof(m_spk)) return false;
    uint32_t magic; std::memcpy(&magic, in.data(), 4);
    if (magic != 0x31314d53) return false;
    const uint8_t* p = in.data() + 4;
    std::memcpy(&m_cpu.s, p, sizeof(m_cpu.s)); p += sizeof(m_cpu.s);
    std::memcpy(&m_s, p, sizeof(m_s)); p += sizeof(m_s);
    std::memcpy(&m_cycles, p, 8); p += 8;
    std::memcpy(&m_spk, p, sizeof(m_spk));
    m_spkPos = double(now()); m_audio.clear();
    return true;
}
