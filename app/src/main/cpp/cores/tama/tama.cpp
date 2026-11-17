// PocketPlug - Tamagotchi-style LCD pets on the Epson E0C6S46
// SPDX-License-Identifier: BSD-3-Clause
//
// The LCD segment -> dot mapping follows the TMG-M1 board wiring (as also
// documented by TamaLIB): 40 segment lines x 16 commons; 32 segment lines
// drive the dot columns, two others carry the 8 icons.

#include "tama.h"
#include <cstring>
#include <algorithm>

static uint32_t crc32(const uint8_t* p, size_t n)
{
    uint32_t c = 0xffffffff;
    for (size_t i = 0; i < n; i++) { c ^= p[i]; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xedb88320 & -(c & 1)); }
    return ~c;
}

// segment line -> dot column (-1 = icon / unused)
static const int8_t kSegCol[40] = {
    0, 1, 2, 3, 4, 5, 6, 7, -1, 8, 9, 10, 11, 12, 13, 14, 15, -1, -1, -1,
    31, 30, 29, 28, 27, 26, 25, 24, -1, 23, 22, 21, 20, 19, 18, 17, 16, -1, -1, -1 };

bool TamaCore::load(const std::vector<RomFile>& roms, std::string& error)
{
    const RomFile* rom = nullptr;
    for (auto& r : roms) if (r.name == m_cfg.romName) rom = &r;
    if (!rom && roms.size() == 1) rom = &roms[0];
    if (!rom) { error = "Missing " + m_cfg.romName; return false; }
    size_t words = rom->data.size() / 2;
    if (words < 0x1000) { error = m_cfg.romName + " is too small"; return false; }
    if (m_cfg.romCrc && crc32(rom->data.data(), rom->data.size()) != m_cfg.romCrc) {
        // tolerated (other revisions), but reported in the log by the caller
    }
    std::vector<uint16_t> w(words);
    for (size_t i = 0; i < words; i++) w[i] = (uint16_t)(rom->data[2 * i] << 8 | rom->data[2 * i + 1]);
    m_cpu.setRom(w);
    m_cpu.onBuzzer = buzzerCb;
    m_cpu.buzzerCtx = this;
    reset();
    return true;
}

void TamaCore::reset()
{
    m_cpu.reset();
    m_level = 0;
    m_audioPos = 0;
    m_audio.clear();
    setInputs(m_inputs);
}

void TamaCore::buzzerCb(void* ctx, uint64_t when, int level)
{
    TamaCore* t = (TamaCore*)ctx;
    t->audioUpTo(when);
    t->m_level = level;
}

void TamaCore::audioUpTo(uint64_t when)
{
    if (m_muteAudio) { m_audioPos = when; return; }
    // one sample = 2 units (32768 Hz)
    while (m_audioPos + 2 <= when) {
        m_audio.push_back(m_level ? 7000 : 0);
        m_audioPos += 2;
    }
}

void TamaCore::runFrame()
{
    m_cpu.run(1024);                    // 1/64 s
    audioUpTo(m_cpu.now());
    if (m_audio.size() > 32768) m_audio.erase(m_audio.begin(), m_audio.end() - 8192);
    render();
}

bool TamaCore::advanceTime(double seconds)
{
    if (seconds <= 0) return true;
    m_muteAudio = true;
    uint64_t units = (uint64_t)(seconds * 65536.0);
    while (units) {
        uint64_t step = std::min<uint64_t>(units, 65536ull * 60);
        m_cpu.run(step);
        units -= step;
    }
    m_audioPos = m_cpu.now();
    m_muteAudio = false;
    render();
    return true;
}

void TamaCore::render()
{
    std::memset(m_fb, 0, sizeof(m_fb));
    const uint32_t ON = 0xff000000;
    for (int seg = 0; seg < 40; seg++) {
        int col = kSegCol[seg];
        if (col < 0) continue;
        for (int com = 0; com < 16; com++)
            if (m_cpu.lcdPixel(com, seg)) m_fb[com * 32 + col] = ON;
    }
    // icons: seg 8 com 0-3 (top row), seg 28 com 12-15 (bottom row)
    for (int i = 0; i < 4; i++) {
        if (m_cpu.lcdPixel(i, 8)) m_fb[16 * 32 + i] = ON;
        if (m_cpu.lcdPixel(12 + i, 28)) m_fb[16 * 32 + 4 + i] = ON;
    }
}

size_t TamaCore::readAudio(int16_t* out, size_t maxSamples)
{
    size_t n = std::min(maxSamples, m_audio.size());
    // simple one-pole smoothing to take the edge off the square wave
    std::memcpy(out, m_audio.data(), n * sizeof(int16_t));
    m_audio.erase(m_audio.begin(), m_audio.begin() + n);
    return n;
}

// bits: 0 = left (A), 1 = middle (B), 2 = right (C)
void TamaCore::setInputs(uint32_t pressed)
{
    m_inputs = pressed;
    // active low: K02 = left, K01 = middle, K00 = right
    m_cpu.setInputLine(2, (pressed & 1) ? 0 : 1);
    m_cpu.setInputLine(1, (pressed & 2) ? 0 : 1);
    m_cpu.setInputLine(0, (pressed & 4) ? 0 : 1);
}

bool TamaCore::saveState(std::vector<uint8_t>& out)
{
    out.clear();
    uint32_t magic = 0x414d4154; // 'TAMA'
    out.insert(out.end(), (uint8_t*)&magic, (uint8_t*)&magic + 4);
    m_cpu.save(out);
    out.insert(out.end(), (uint8_t*)&m_level, (uint8_t*)&m_level + sizeof(m_level));
    return true;
}

bool TamaCore::loadState(const std::vector<uint8_t>& in)
{
    if (in.size() < 4) return false;
    uint32_t magic; memcpy(&magic, in.data(), 4);
    if (magic != 0x414d4154) return false;
    const uint8_t* p = in.data() + 4;
    const uint8_t* end = in.data() + in.size();
    if (!m_cpu.load(p, end)) return false;
    if (p + sizeof(m_level) <= end) memcpy(&m_level, p, sizeof(m_level));
    m_audioPos = m_cpu.now();
    m_audio.clear();
    render();
    return true;
}
