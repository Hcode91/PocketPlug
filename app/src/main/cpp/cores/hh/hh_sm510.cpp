// PocketPlug - Sharp SM5xx LCD handhelds (port of MAME's handheld/hh_sm510.cpp, hap)
// SPDX-License-Identifier: BSD-3-Clause

#include "hh_sm510.h"
#include <cstring>

namespace {
enum Kind { GNW_MMOUSE, AUSLALOM };
}

struct Sm510Game {
    const char* id;
    Kind kind;
    const char* rom;
    const char* svg;
};

namespace {
const Sm510Game kGames[] = {
    // Elektronika Ataka asteroidov (1992): KB1013VK1-2, the Mickey Mouse program on new LCD art
    { "atakaast", GNW_MMOUSE, "im-53.bin", "atakaast.svg" },
    // Elektronika Autoslalom (1990): KB1013VK1-2
    { "auslalom", AUSLALOM, "im-23.bin", "auslalom.svg" },
};
const Sm510Game* find(const std::string& id)
{
    for (auto& g : kGames) if (id == g.id) return &g;
    return nullptr;
}
const int DECAY_PIVOT = 8, DECAY_LEN = 17;
}

bool HhSm510Core::known(const std::string& game) { return find(game) != nullptr; }

HhSm510Core::HhSm510Core(const std::string& game) : m_g(find(game)), m_cpu(this) { std::memset(&m_s, 0, sizeof(m_s)); }

bool HhSm510Core::load(const std::vector<RomFile>& roms, std::string& error)
{
    if (!m_g) { error = "Unknown game"; return false; }
    const RomFile* rom = nullptr;
    for (auto& r : roms) if (r.name == m_g->rom) rom = &r;
    if (!rom) { error = std::string("Missing ") + m_g->rom; return false; }
    if (!loadScreen(roms, m_g->svg, 1, 1, error)) return false;
    m_segKey.assign(16 * 4 * 2, -1);
    for (int x = 0; x < 16; x++)
        for (int y = 0; y < 4; y++)
            for (int z = 0; z < 2; z++)
                m_segKey[size_t((x * 4 + y) * 2 + z)] =
                    int16_t(m_svg.key(std::to_string(x) + "." + std::to_string(y) + "." + std::to_string(z)));
    m_cpu.setRom(rom->data.data(), int(rom->data.size()));
    setClock(16384.0);      // 32.768 kHz, 2 clocks per machine cycle
    reset();
    return true;
}

void HhSm510Core::reset()
{
    std::memset(&m_s, 0, sizeof(m_s));
    m_cpu.reset();
    updateK();
}

uint8_t HhSm510Core::inputLine(int line) const
{
    switch (m_g->kind) {
    case GNW_MMOUSE:
        // R3: right-down, right-up, left-down, left-up; R4: Time, Game B, Game A, Alarm
        if (line == 1) return uint8_t(in(5) | in(4) << 1 | in(1) << 2 | in(0) << 3);
        if (line == 2) return uint8_t(in(8) | in(7) << 1 | in(6) << 2 | in(9) << 3);
        return 0;
    case AUSLALOM:
        // R3: right, Start, left, Speed; R4: Time, Game B, Game A, Alarm
        if (line == 1) return uint8_t(in(3) | in(5) << 1 | in(2) << 2 | in(4) << 3);
        if (line == 2) return uint8_t(in(8) | in(7) << 1 | in(6) << 2 | in(9) << 3);
        return 0;
    }
    return 0;
}

uint8_t HhSm510Core::readK()
{
    uint8_t k = 0;
    for (int i = 0; i < 3; i++) if (m_s.inpMux >> i & 1) k |= inputLine(i);
    return k;
}

void HhSm510Core::writeR(uint8_t r)
{
    // piezo_input_w: R1 to the piezo, the rest select the input lines
    speaker(r & 1);
    m_s.inpMux = r >> 1;
    updateK();
}

void HhSm510Core::lcd(const uint8_t segs[9][2])
{
    for (int o = 0; o < 9; o++)
        for (int h = 0; h < 2; h++) m_s.state[o << 1 | h] = segs[o][h];
}

void HhSm510Core::tick1k()
{
    // hh_sm510_state::update_display (deflicker)
    for (int zx = 0; zx < 32; zx++)
        for (int y = 0; y < 4; y++) {
            uint8_t& d = m_s.decay[y][zx];
            if (m_s.state[zx] >> y & 1) { if (d < DECAY_PIVOT + DECAY_LEN) d++; }
            else if (d > 0) d--;
        }
}

void HhSm510Core::runFrame()
{
    m_carry += m_hz / 60.0;
    int n = int(m_carry);
    m_carry -= n;
    int done = 0;
    while (done < n) done += runCpu(std::min(n - done, 64));
    audioTo(double(now()));
    std::fill(m_levels.begin(), m_levels.end(), 0);
    for (int zx = 0; zx < 32; zx++)
        for (int y = 0; y < 4; y++) {
            if (m_s.decay[y][zx] < DECAY_PIVOT) continue;
            int k = m_segKey[size_t(((zx >> 1) * 4 + y) * 2 + (zx & 1))];
            if (k >= 0) m_levels[size_t(k)] = 255;
        }
    m_svg.render(m_levels.data(), m_fb.data());
#ifndef __ANDROID__
    static int fno = 0; fno++;
    static int cnt[16][4][2];
    if (getenv("HHCOUNT")) {
        for (int zx = 0; zx < 32; zx++) for (int y = 0; y < 4; y++) if (m_s.decay[y][zx] >= DECAY_PIVOT) cnt[zx >> 1][y][zx & 1]++;
        if (fno == atoi(getenv("HHCOUNT")))
            for (int x = 0; x < 16; x++) for (int y = 0; y < 4; y++) for (int z = 0; z < 2; z++)
                if (cnt[x][y][z]) printf("C %d.%d.%d=%d\n", x, y, z, cnt[x][y][z]);
    }
#endif
    if (m_audio.size() > 44100) m_audio.erase(m_audio.begin(), m_audio.end() - 4410);
}

bool HhSm510Core::saveState(std::vector<uint8_t>& out)
{
    out.clear();
    uint32_t magic = 0x30314d53;   // 'SM10'
    auto put = [&](const void* p, size_t n) { const uint8_t* b = (const uint8_t*)p; out.insert(out.end(), b, b + n); };
    put(&magic, 4); put(&m_cpu.s, sizeof(m_cpu.s)); put(&m_s, sizeof(m_s)); put(&m_cycles, 8); put(&m_spk, sizeof(m_spk));
    return true;
}

bool HhSm510Core::loadState(const std::vector<uint8_t>& in)
{
    if (in.size() != 4 + sizeof(m_cpu.s) + sizeof(m_s) + 8 + sizeof(m_spk)) return false;
    uint32_t magic; std::memcpy(&magic, in.data(), 4);
    if (magic != 0x30314d53) return false;
    const uint8_t* p = in.data() + 4;
    std::memcpy(&m_cpu.s, p, sizeof(m_cpu.s)); p += sizeof(m_cpu.s);
    std::memcpy(&m_s, p, sizeof(m_s)); p += sizeof(m_s);
    std::memcpy(&m_cycles, p, 8); p += 8;
    std::memcpy(&m_spk, p, sizeof(m_spk));
    m_spkPos = double(now()); m_audio.clear();
    return true;
}
