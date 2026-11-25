// PocketPlug - Bit90 (port of the bit90 parts of MAME's coleco/coleco.cpp)
// SPDX-License-Identifier: BSD-3-Clause

#include "bit90.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>

namespace {
constexpr double Z80_HZ = 7159090.0 / 2;
constexpr int LINES = 313;                       // TMS9929A (PAL)
constexpr int CYCLES_PER_LINE = 228;
constexpr int AUDIO_HZ = 44100;
uint8_t rd(void* u, uint16_t a) { return static_cast<Bit90Core*>(u)->memRead(a); }
void wr(void* u, uint16_t a, uint8_t d) { static_cast<Bit90Core*>(u)->memWrite(a, d); }
uint8_t pin(z80* z, uint8_t p) { return static_cast<Bit90Core*>(z->userdata)->ioRead(p); }
void pout(z80* z, uint8_t p, uint8_t d) { static_cast<Bit90Core*>(z->userdata)->ioWrite(p, d); }
const RomFile* find(const std::vector<RomFile>& roms, const char* name)
{
    for (auto& r : roms) if (r.name == name) return &r;
    return nullptr;
}
}

Bit90Core::Bit90Core() : m_frame(W * H, 0xff000000u)
{
    std::memset(&m_s, 0, sizeof(m_s));
    std::memset(m_keys, 0, sizeof(m_keys));
}

bool Bit90Core::load(const std::vector<RomFile>& roms, std::string& error)
{
    const char* names[2][3] = { { "bit90b3.u2", "bit90b3.u4", "bit90b3.u3" }, { "d32522e.u2", "d32351e.u4", "d32521e.u3" } };
    bool any = false;
    for (int b = 0; b < 2; b++) {
        const RomFile* r0 = find(roms, names[b][0]); const RomFile* r1 = find(roms, names[b][1]); const RomFile* r2 = find(roms, names[b][2]);
        m_basic[b].clear(); m_banked[b].clear();
        if (!r0 || !r1 || !r2) continue;
        m_basic[b] = r0->data; m_basic[b].resize(0x2000, 0xff);
        m_banked[b] = r1->data; m_banked[b].resize(0x2000, 0xff);
        m_banked[b].insert(m_banked[b].end(), r2->data.begin(), r2->data.end()); m_banked[b].resize(0x4000, 0xff);
        any = true;
    }
    if (!any) { error = "Missing the BASIC ROMs (d32522e.u2, d32351e.u4, d32521e.u3 for 3.1)"; return false; }
    if (m_basic[m_bios].empty()) m_bios = m_basic[1].empty() ? 0 : 1;
    reset();
    return true;
}

void Bit90Core::setOption(int id, int value)
{
    if (id != 0) return;
    int b = value ? 1 : 0;
    if (m_basic[b].empty() || b == m_bios) return;
    m_bios = b;
    reset();
}

void Bit90Core::reset()
{
    std::memset(&m_s, 0, sizeof(m_s));
    std::memset(m_vdp.vram, 0, sizeof(m_vdp.vram));
    m_vdp.reset();
    m_psg.reset();
    z80_init(&m_z80);
    m_z80.read_byte = rd; m_z80.write_byte = wr; m_z80.port_in = pin; m_z80.port_out = pout; m_z80.userdata = this;
}

uint8_t Bit90Core::memRead(uint16_t a)
{
    if (a < 0x2000) return m_banked[m_bios][(m_s.bank << 13) | a];
    if (a < 0x4000) return m_basic[m_bios][a - 0x2000];
    if (a < 0x6000) return 0xff;                        // the expansion port
    if (a < 0x8000) return m_s.ram[a & 0x7ff];
    if (!m_cart.empty()) return m_cart[(a - 0x8000) % m_cart.size()];
    return m_s.xram[a - 0x8000];
}

void Bit90Core::memWrite(uint16_t a, uint8_t d)
{
    if (a >= 0x6000 && a < 0x8000) m_s.ram[a & 0x7ff] = d;
    else if (a >= 0x8000 && m_cart.empty()) m_s.xram[a - 0x8000] = d;
}

uint8_t Bit90Core::padRead(int n)
{
    if (n != 0) return 0x7f;
    uint8_t d = 0x7f;
    if (m_s.joyMode) {
        if (m_pad & 0x01) d &= ~0x01;
        if (m_pad & 0x08) d &= ~0x02;
        if (m_pad & 0x02) d &= ~0x04;
        if (m_pad & 0x04) d &= ~0x08;
        if (m_pad & 0x10) d &= ~0x40;
    } else {
        static const uint8_t code[12] = { 0x0a, 0x0d, 0x07, 0x0c, 0x02, 0x03, 0x0e, 0x05, 0x01, 0x0b, 0x06, 0x09 };
        for (int k = 0; k < 12; k++) if (m_pad >> (8 + k) & 1) { d = uint8_t((d & 0xf0) | code[k]); break; }
        if (m_pad & 0x20) d &= ~0x40;
    }
    return d;
}

uint8_t Bit90Core::ioRead(uint8_t a)
{
    switch (a & 0xe0) {
    case 0x80: m_s.bank = (a & 0x08) ? 1 : 0; return 0xff;          // 80h: u4 (monitor), 88h: u3
    case 0xa0: return (a & 1) ? m_vdp.registerRead() : m_vdp.vramRead();
    case 0xc0: return m_s.keySel < 8 ? uint8_t(~m_keys[m_s.keySel]) : 0xff;
    case 0xe0: return padRead((a >> 1) & 1);
    }
    return 0xff;
}

void Bit90Core::ioWrite(uint8_t a, uint8_t d)
{
    switch (a & 0xe0) {
    case 0x80: m_s.joyMode = 0; break;
    case 0xa0: if (a & 1) m_vdp.registerWrite(d); else m_vdp.vramWrite(d); break;
    case 0xc0: m_s.joyMode = 1; break;
    case 0xe0:
        if (a & 0x04) m_psg.write(d);                      // E4h: the SN76489A
        else m_s.keySel = (d >> 4) & 0x0f;                  // E0h: keyboard row (bits 0-1 the tape)
        break;
    }
}

void Bit90Core::keyEvent(int code, bool down)
{
    if (code < 0 || code >= 64) return;
    if (down) m_keys[code >> 3] |= uint8_t(1 << (code & 7));
    else m_keys[code >> 3] &= uint8_t(~(1 << (code & 7)));
}

void Bit90Core::runFrame()
{
    const double perSample = Z80_HZ / AUDIO_HZ;
    for (int line = 0; line < LINES; line++) {
        unsigned long end = m_z80.cyc + CYCLES_PER_LINE;
        while (m_z80.cyc < end) {
            unsigned long before = m_z80.cyc;
            z80_step(&m_z80);
            int n = int(m_z80.cyc - before); if (n <= 0) n = 4;
            for (int i = 0; i < n; i++) {
                m_audioAcc += m_psg.clock();
                m_audioN++;
                m_s.phase += 1.0;
                if (m_s.phase >= perSample) {
                    m_s.phase -= perSample;
                    double x = m_audioAcc / m_audioN;
                    m_audioAcc = 0; m_audioN = 0;
                    m_dcY = x - m_dcX + 0.995 * m_dcY; m_dcX = x;
                    m_audio.push_back(int16_t(std::max(-32000.0, std::min(32000.0, m_dcY))));
                }
            }
        }
        m_vdp.line(line);
        bool irq = m_vdp.irq();
        if (irq && !m_s.vdpIrq) z80_gen_nmi(&m_z80);
        m_s.vdpIrq = irq;
    }
    uint32_t back = Tms9928a::palette(m_vdp.regs[7] & 15);
    std::fill(m_frame.begin(), m_frame.end(), back);
    for (int y = 0; y < 192; y++) std::memcpy(&m_frame[(y + 8) * W + 8], &m_vdp.out[y * 256], 256 * 4);
    if (m_audio.size() > AUDIO_HZ / 4) m_audio.erase(m_audio.begin(), m_audio.end() - AUDIO_HZ / 10);
}

size_t Bit90Core::readAudio(int16_t* out, size_t maxSamples)
{
    size_t n = std::min(maxSamples, m_audio.size());
    std::memcpy(out, m_audio.data(), n * sizeof(int16_t));
    m_audio.erase(m_audio.begin(), m_audio.begin() + long(n));
    return n;
}

bool Bit90Core::mediaInsert(int drive, const std::string& path, std::string& error)
{
    if (drive != 2) { error = "The Bit90 only takes cartridges"; return false; }
    std::ifstream f(path, std::ios::binary);
    if (!f) { error = "Can't open the cartridge"; return false; }
    std::vector<uint8_t> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (d.empty() || d.size() > 0x8000) { error = "Not a ColecoVision cartridge (up to 32 KB)"; return false; }
    m_cart = d;
    reset();
    return true;
}

void Bit90Core::mediaEject(int drive)
{
    if (drive != 2) return;
    m_cart.clear();
    reset();
}

bool Bit90Core::saveState(std::vector<uint8_t>& out)
{
    out.clear();
    auto put = [&](const void* p, size_t n) { const uint8_t* b = (const uint8_t*)p; out.insert(out.end(), b, b + n); };
    uint32_t magic = 0x30395442;   // 'BT90'
    put(&magic, 4); put(&m_bios, sizeof(m_bios)); put(&m_s, sizeof(m_s)); put(&m_z80, sizeof(m_z80));
    put(&m_vdp, sizeof(m_vdp)); put(&m_psg.s, sizeof(m_psg.s));
    return true;
}

bool Bit90Core::loadState(const std::vector<uint8_t>& in)
{
    size_t need = 4 + sizeof(m_bios) + sizeof(m_s) + sizeof(m_z80) + sizeof(m_vdp) + sizeof(m_psg.s);
    if (in.size() != need) return false;
    const uint8_t* p = in.data();
    uint32_t magic; std::memcpy(&magic, p, 4); p += 4;
    if (magic != 0x30395442) return false;
    int bios; std::memcpy(&bios, p, sizeof(bios)); p += sizeof(bios);
    if (bios != m_bios) return false;
    z80 keep = m_z80;
    std::memcpy(&m_s, p, sizeof(m_s)); p += sizeof(m_s);
    std::memcpy(&m_z80, p, sizeof(m_z80)); p += sizeof(m_z80);
    m_z80.read_byte = keep.read_byte; m_z80.write_byte = keep.write_byte; m_z80.port_in = keep.port_in; m_z80.port_out = keep.port_out; m_z80.userdata = keep.userdata;
    std::memcpy(&m_vdp, p, sizeof(m_vdp)); p += sizeof(m_vdp);
    std::memcpy(&m_psg.s, p, sizeof(m_psg.s));
    return true;
}
