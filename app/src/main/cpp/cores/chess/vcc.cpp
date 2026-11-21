// PocketPlug - Fidelity (Advanced) Voice Chess Challenger (port of MAME's fidelity/vcc.cpp). See vcc.h.
// SPDX-License-Identifier: GPL-2.0-or-later

#include "vcc.h"
#include <algorithm>
#include <cstring>

namespace {
constexpr int CPU_HZ = 4000000;
constexpr int SPEECH_DIV = 160;            // 4 MHz / 25 kHz
constexpr int AUDIO_HZ = 44100;

uint8_t zRead(void* u, uint16_t a) { return static_cast<VccCore*>(u)->memRead(a); }
void zWrite(void* u, uint16_t a, uint8_t d) { static_cast<VccCore*>(u)->memWrite(a, d); }
uint8_t zIn(z80* z, uint8_t p) { return static_cast<VccCore*>(z->userdata)->ioRead(p); }
void zOut(z80* z, uint8_t p, uint8_t d) { static_cast<VccCore*>(z->userdata)->ioWrite(p, d); }

const RomFile* find(const std::vector<RomFile>& roms, const char* name, size_t size)
{
    for (auto& r : roms) if (r.name == name && r.data.size() == size) return &r;
    return nullptr;
}
}

bool VccCore::load(const std::vector<RomFile>& roms, std::string& error)
{
    std::memset(m_rom, 0xff, sizeof(m_rom));
    if (m_advanced) {
        auto a = find(roms, "101-64017", 0x2000);
        auto b = find(roms, "101-32010", 0x1000);
        if (!b) b = find(roms, "cn19175n_vcc_3", 0x1000);
        if (!a || !b) { error = "Missing 101-64017 (avcc zip) or cn19175n_vcc_3 (vcc zip)"; return false; }
        std::memcpy(m_rom, a->data.data(), 0x2000);
        std::memcpy(m_rom + 0x2000, b->data.data(), 0x1000);
    } else {
        const char* names[3] = { "cn19256n_101-32013", "cn19174n_vcc_2", "cn19175n_vcc_3" };
        for (int i = 0; i < 3; i++) {
            auto r = find(roms, names[i], 0x1000);
            if (!r) { error = std::string("Missing ") + names[i]; return false; }
            std::memcpy(m_rom + i * 0x1000, r->data.data(), 0x1000);
        }
    }
    for (auto& v : m_speechRom) v.clear();
    if (auto en = find(roms, "101-32107", 0x1000)) {
        m_speechRom[0].assign(en->data.begin(), en->data.end());
        m_speechRom[0].insert(m_speechRom[0].end(), en->data.begin(), en->data.end());
    }
    const char* other[3] = { "101-64101", "101-64105", "101-64106" };
    for (int i = 0; i < 3; i++) if (auto r = find(roms, other[i], 0x2000)) m_speechRom[i + 1] = r->data;
    if (m_speechRom[0].empty()) { error = "Missing 101-32107 (speech ROM, vcc zip)"; return false; }

    std::memset(&m_s, 0, sizeof(m_s));
    std::memset(m_s.ram, 0xff, sizeof(m_s.ram));    // the program relies on RAM filled with FF at power-on
    m_s.board.init(false, 0);
    m_ppi.triA = 0; m_ppi.triB = 0;
    m_ppi.outA = [this](uint8_t d) {
        // d0-d6: segments xABCDEFG, d6: language latch data, d7: latch clock + speech start
        uint8_t seg = 0;
        for (int i = 0; i < 7; i++) if (d >> (6 - i) & 1) seg |= uint8_t(1 << i);
        m_s.segData = seg;
        updateDisplay();
        if (d & 0x80) m_speech.setBank(d >> 6 & 1);
        m_speech.dataW(d & 0x3f);
        m_speech.startW(d >> 7 & 1);
    };
    m_ppi.inB = [this]() -> uint8_t { return m_speech.busy() ? 0x80 : 0x00; };
    m_ppi.outB = [this](uint8_t d) { m_s.ledSelect = d; updateDisplay(); };
    m_ppi.inC = [this]() -> uint8_t {
        uint8_t data = 0;
        for (int i = 0; i < 4; i++) if (m_s.inpMux >> i & 1) data |= uint8_t(m_keys >> (i * 4) & 0xf);
        if (~m_s.ledSelect & 0x40) data |= m_s.language;
        return uint8_t(~data & 0xf);
    };
    m_ppi.outC = [this](uint8_t d) { m_s.inpMux = uint8_t(~d >> 4 & 0xf); };
    selectSpeech();
    z80_init(&m_z80);
    m_z80.read_byte = zRead; m_z80.write_byte = zWrite;
    m_z80.port_in = zIn; m_z80.port_out = zOut;
    m_z80.userdata = this;
    m_ppi.reset();
    std::memset(m_acc, 0, sizeof(m_acc));
    return true;
}

void VccCore::selectSpeech()
{
    static const uint8_t jumper[4] = { 0, 1, 2, 4 };
    int l = m_speechRom[m_lang].empty() ? 0 : m_lang;
    m_s.language = jumper[l];
    m_speech.setRom(m_speechRom[l].data(), m_speechRom[l].size());
}

void VccCore::setOption(int id, int value)
{
    if (id == 0 && value >= 0 && value < 4) m_lang = value;
}

void VccCore::reset()
{
    selectSpeech();
    auto keep = m_z80;
    z80_init(&m_z80);
    m_z80.read_byte = keep.read_byte; m_z80.write_byte = keep.write_byte;
    m_z80.port_in = keep.port_in; m_z80.port_out = keep.port_out; m_z80.userdata = keep.userdata;
}

void VccCore::setInputs(uint32_t pressed)
{
    bool rst = pressed & 0x10000;
    if (rst && !m_inReset) reset();
    m_inReset = rst;
    m_keys = pressed & 0xffff;
}

uint8_t VccCore::memRead(uint16_t a)
{
    if (a < 0x3000) return m_rom[a];
    if (a >= 0x4000 && a < 0x6000) return m_s.ram[a & 0x3ff];
    return 0xff;
}

void VccCore::memWrite(uint16_t a, uint8_t d)
{
    if (a >= 0x4000 && a < 0x6000) m_s.ram[a & 0x3ff] = d;
}

void VccCore::flushDisplay()
{
    uint64_t dt = m_time - m_lastFlush;
    m_lastFlush = m_time;
    if (!dt) return;
    uint8_t sel = m_s.ledSelect >> 2 & 0xf;
    uint8_t data = uint8_t((m_s.segData & 0x7f) | (m_s.ledSelect << 7 & 0x80));
    for (int d = 0; d < 4; d++) {
        if (!(sel >> d & 1)) continue;
        for (int s = 0; s < 8; s++) if (data >> s & 1) m_acc[d * 8 + s] += dt;
    }
}

void VccCore::updateDisplay() { flushDisplay(); }

void VccCore::runFrame()
{
    const uint64_t perFrame = CPU_HZ / 60;
    const uint64_t end = m_frameStart + perFrame;
    m_lastFlush = m_time;
    int32_t sum = 0, n = 0;
    double next = 0, perSample = double(CPU_HZ) / AUDIO_HZ;
    uint64_t base = m_time;
    while (m_time < end) {
        int cyc = 4;
        if (!m_inReset) {
            unsigned long before = m_z80.cyc;
            z80_step(&m_z80);
            cyc = int(m_z80.cyc - before);
            if (cyc <= 0) cyc = 4;
        }
        m_time += uint64_t(cyc);
        m_s.speechCarry += cyc;
        while (m_s.speechCarry >= SPEECH_DIV) {
            m_s.speechCarry -= SPEECH_DIV;
            m_speech.clock();
        }
        sum += m_speech.output() - 7; n++;
        while (double(m_time - base) >= next + perSample) {
            next += perSample;
            int v = n ? sum * 3072 / n : 0;
            m_audio.push_back(int16_t(std::clamp(v, -32767, 32767)));
            sum = 0; n = 0;
        }
    }
    flushDisplay();
    uint64_t span = m_time - m_frameStart;
    m_frameStart = m_time;

    std::memset(m_out, 0, sizeof(m_out));
    for (int i = 0; i < 64; i++) m_out[i] = m_s.board.piece[i];
    m_out[64] = m_s.board.hand;
    m_out[65] = uint32_t(m_s.board.handPos + 1);
    // the digits are lit in short strobes (well under 1% duty), not every frame:
    // a digit keeps what it last showed for a few frames
    (void)span;
    for (int d = 0; d < 4; d++) {
        uint8_t m = 0; uint64_t mx = 0;
        for (int sg = 0; sg < 8; sg++) mx = std::max(mx, m_acc[d * 8 + sg]);
        for (int sg = 0; sg < 8; sg++) if (mx && m_acc[d * 8 + sg] * 3 >= mx) m |= uint8_t(1 << sg);
        if (mx) { m_held[d] = m; m_age[d] = 0; }
        else if (m_age[d] < 255) m_age[d]++;
        uint8_t show = m_age[d] < 12 ? m_held[d] : 0;
        for (int sg = 0; sg < 8; sg++) m_out[130 + d * 8 + sg] = (show >> sg & 1) ? 255 : 0;
    }
    std::memset(m_acc, 0, sizeof(m_acc));
    if (m_audio.size() > 44100) m_audio.erase(m_audio.begin(), m_audio.end() - 4410);
}

size_t VccCore::readAudio(int16_t* out, size_t maxSamples)
{
    size_t n = std::min(maxSamples, m_audio.size());
    std::memcpy(out, m_audio.data(), n * sizeof(int16_t));
    m_audio.erase(m_audio.begin(), m_audio.begin() + long(n));
    return n;
}

namespace { constexpr uint32_t MAGIC = 0x31434356; }  // 'VCC1'

bool VccCore::saveState(std::vector<uint8_t>& out)
{
    out.clear();
    auto put = [&](const void* p, size_t n) { const uint8_t* b = (const uint8_t*)p; out.insert(out.end(), b, b + n); };
    uint32_t magic = MAGIC;
    put(&magic, 4);
    put(&m_s, sizeof(m_s));
    put(&m_ppi.s, sizeof(m_ppi.s));
    put(&m_speech.s, sizeof(m_speech.s));
    put(&m_z80, sizeof(m_z80));
    return true;
}

bool VccCore::loadState(const std::vector<uint8_t>& in)
{
    if (in.size() != 4 + sizeof(m_s) + sizeof(m_ppi.s) + sizeof(m_speech.s) + sizeof(m_z80)) return false;
    uint32_t magic; std::memcpy(&magic, in.data(), 4);
    if (magic != MAGIC) return false;
    const uint8_t* p = in.data() + 4;
    std::memcpy(&m_s, p, sizeof(m_s)); p += sizeof(m_s);
    std::memcpy(&m_ppi.s, p, sizeof(m_ppi.s)); p += sizeof(m_ppi.s);
    std::memcpy(&m_speech.s, p, sizeof(m_speech.s)); p += sizeof(m_speech.s);
    auto keep = m_z80;
    std::memcpy(&m_z80, p, sizeof(m_z80));
    m_z80.read_byte = keep.read_byte; m_z80.write_byte = keep.write_byte;
    m_z80.port_in = keep.port_in; m_z80.port_out = keep.port_out; m_z80.userdata = keep.userdata;
    return true;
}
