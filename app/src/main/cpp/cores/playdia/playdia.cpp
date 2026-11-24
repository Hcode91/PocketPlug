// PocketPlug - Bandai Playdia (1994)
// SPDX-License-Identifier: BSD-3-Clause
//
// Ported from PlaydiaEmu (https://github.com/AloysHF/PlaydiaEmu, BSD-3-Clause, Aloys):
// player.rs, cd.rs, audio.rs, video.rs, video/ak8000.rs and video/structure.rs.
#include "playdia.h"
#include <algorithm>
#include <cstring>

static const int CHOICE_TIMEOUT_FRAMES = 300;         // ~10 s at 30 Hz
static const int LATCH_FRAMES = 60;
static const size_t VIDEO_PACKET_CAP = 256 * 1024;
static const int PICTURE_ROWS = 27;
static const int HEADER_BYTES = 36;

// ---------------------------------------------------------------------------
// AK8000 pictures: 27 rows of 31 macroblocks (four 4x4 luma + Cb + Cr blocks),
// run/level VLC coefficients, DC prediction, 4x4 inverse transform
// ---------------------------------------------------------------------------
namespace {

struct Entry { uint8_t bits, run; int16_t level; };

struct Code { uint16_t code; uint8_t bits, run; int16_t level; };
const Code CODES[63] = {
    {0b0000000010000, 13, 0, 25}, {0b0000000010001, 13, 5, 4}, {0b0000000010010, 13, 0, 24}, {0b0000000010011, 13, 0, 23},
    {0b0000000010100, 13, 3, 8}, {0b0000000010101, 13, 3, 7}, {0b0000000010110, 13, 3, 6}, {0b0000000010111, 13, 2, 8},
    {0b0000000011000, 13, 2, 7}, {0b0000000011001, 13, 2, 6}, {0b0000000011010, 13, 1, 9}, {0b0000000011011, 13, 1, 8},
    {0b0000000011100, 13, 1, 7}, {0b0000000011101, 13, 0, 22}, {0b0000000011110, 13, 0, 21}, {0b0000000011111, 13, 0, 20},
    {0b000000010000, 12, 9, 1}, {0b000000010001, 12, 8, 1}, {0b000000010010, 12, 7, 1}, {0b000000010011, 12, 5, 3},
    {0b000000010100, 12, 5, 2}, {0b000000010101, 12, 4, 5}, {0b000000010110, 12, 4, 4}, {0b000000010111, 12, 4, 3},
    {0b000000011000, 12, 3, 5}, {0b000000011001, 12, 3, 4}, {0b000000011010, 12, 2, 5}, {0b000000011011, 12, 1, 6},
    {0b000000011100, 12, 0, 19}, {0b000000011101, 12, 0, 18}, {0b000000011110, 12, 0, 17}, {0b000000011111, 12, 0, 16},
    {0b0000001000, 10, 3, 3}, {0b0000001001, 10, 2, 4}, {0b0000001010, 10, 2, 3}, {0b0000001011, 10, 1, 5},
    {0b0000001100, 10, 0, 15}, {0b0000001101, 10, 0, 14}, {0b0000001110, 10, 0, 13}, {0b0000001111, 10, 0, 12},
    {0b00000100, 8, 6, 1}, {0b00000101, 8, 4, 2}, {0b00000110, 8, 3, 2}, {0b00000111, 8, 1, 4},
    {0b0000100, 7, 5, 1}, {0b0000101, 7, 2, 2}, {0b0000110, 7, 0, 8}, {0b0000111, 7, 0, 7},
    {0b000100, 6, 4, 1}, {0b000101, 6, 1, 2}, {0b000110, 6, 0, 6}, {0b000111, 6, 0, 5},
    {0b00100100, 8, 1, 3}, {0b00100101, 8, 0, 11}, {0b00100110, 8, 0, 10}, {0b00100111, 8, 0, 9},
    {0b00101, 5, 3, 1}, {0b00110, 5, 2, 1}, {0b00111, 5, 0, 4},
    {0b1000, 4, 1, 1}, {0b1001, 4, 0, 3}, {0b101, 3, 0, 2}, {0b11, 2, 0, 1},
};

struct Lookup {
    Entry t[8192];
    Lookup()
    {
        for (int i = 0; i < 8192; i++) {
            t[i] = { 0, 0, 0 };
            if ((i >> 11) == 1) t[i].bits = 2;          // end of block
            if ((i >> 7) == 8) t[i].bits = 6;           // escape
            for (const Code& c : CODES)
                if ((i >> (13 - c.bits)) == c.code) t[i] = { c.bits, c.run, c.level };
        }
    }
};
const Lookup& lookup() { static Lookup l; return l; }

const int SCAN[16] = { 0, 1, 4, 8, 5, 2, 3, 6, 9, 12, 13, 10, 7, 11, 14, 15 };
const int64_t BASIS[4][4] = {
    { 8192, 8192, 8192, 8192 },
    { 10703, 4433, -4433, -10703 },
    { 8192, -8192, -8192, 8192 },
    { 4433, -10703, 10703, -4433 },
};

bool readBits(const uint8_t* d, size_t len, size_t off, int n, uint32_t& v)
{
    if (n > 32 || off + size_t(n) > len * 8) return false;
    v = 0;
    for (size_t p = off; p < off + size_t(n); p++) v = (v << 1) | ((d[p / 8] >> (7 - p % 8)) & 1);
    return true;
}

struct Reader {
    const uint8_t* d; size_t len; size_t pos = 0;
    bool read(int n, uint32_t& v) { if (!readBits(d, len, pos, n, v)) return false; pos += size_t(n); return true; }
    // 0: end of block, 1: symbol, -1: error
    int symbol(int& run, int& level)
    {
        size_t avail = std::min<size_t>(13, len * 8 > pos ? len * 8 - pos : 0);
        uint32_t prefix = 0;
        readBits(d, len, pos, int(avail), prefix);
        prefix <<= (13 - avail);
        const Entry& e = lookup().t[prefix];
        uint32_t v;
        if (e.bits == 0) return -1;
        if (e.bits == 2 && e.level == 0) { if (!read(2, v)) return -1; return 0; }
        if (e.bits == 6 && e.level == 0) {
            uint32_t r, raw;
            if (!read(6, v) || !read(4, r) || !read(10, raw)) return -1;
            run = int(r); level = int32_t(raw << 22) >> 22;
            return 1;
        }
        uint32_t sign;
        if (!read(e.bits, v) || !read(1, sign)) return -1;
        run = e.run; level = e.level * (sign ? -1 : 1);
        return 1;
    }
    bool coefficients(int out[16])
    {
        std::memset(out, 0, 16 * sizeof(int));
        int position = 0;
        while (position < 16) {
            int run = 0, level = 0;
            int r = symbol(run, level);
            if (r < 0) return false;
            if (r == 0) break;
            position += run;
            if (position >= 16) return false;
            out[SCAN[position]] = level;
            position++;
        }
        return true;
    }
};

void inverse(const int c[16], const uint8_t q[16], uint8_t factor, int px[16])
{
    int64_t mid[4][4] = {};
    for (int v = 0; v < 4; v++)
        for (int x = 0; x < 4; x++)
            for (int u = 0; u < 4; u++) {
                int i = v * 4 + u;
                mid[v][x] += int64_t(c[i]) * int64_t(q[i]) * int64_t(factor) * BASIS[u][x];
            }
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++) {
            int64_t s = 0;
            for (int v = 0; v < 4; v++) s += mid[v][x] * BASIS[v][y];
            px[y * 4 + x] = int((s + (int64_t(1) << 33)) >> 34);
        }
}

// One picture to packed RGB (PIC_W x PIC_H x 3); false if the stream doesn't hold up.
bool decodePicture(const uint8_t* d, size_t len, std::vector<uint8_t>& rgb)
{
    const int W = PlaydiaCore::PIC_W, H = PlaydiaCore::PIC_H;
    uint32_t v;
    if (len < size_t(HEADER_BYTES) || !readBits(d, len, 0, 19, v) || v != 0x400) return false;
    int pictureType = (d[2] >> 2) & 7, qshift = d[2] & 3;
    uint8_t factor = d[3];
    if (pictureType != 1 || qshift != 0 || factor == 0) return false;
    const uint8_t* qL = d + 4;
    const uint8_t* qC = d + 20;
    Reader r{ d, len, size_t(HEADER_BYTES) * 8 };
    std::vector<int> planes[3] = { std::vector<int>(size_t(W * H)), std::vector<int>(size_t(W * H / 4)), std::vector<int>(size_t(W * H / 4)) };
    for (int row = 0; row < PICTURE_ROWS; row++) {
        if (!r.read(19, v) || v != ((0x20u << 5) | uint32_t(row + 1))) return false;
        int pred[3] = {};
        for (int mb = 0; mb < 31; mb++)
            for (int block = 0; block < 6; block++) {
                int comp = block < 4 ? 0 : block - 3;
                int c[16];
                if (!r.coefficients(c)) return false;
                c[0] += pred[comp];
                if (block == 0 || comp != 0) pred[comp] = c[0];
                int px[16];
                inverse(c, comp == 0 ? qL : qC, factor, px);
                int stride = comp == 0 ? W : W / 2;
                int x = comp == 0 ? mb * 8 + (block % 2) * 4 : mb * 4;
                int y = comp == 0 ? row * 8 + (block / 2) * 4 : row * 4;
                for (int py = 0; py < 4; py++)
                    std::memcpy(&planes[comp][size_t((y + py) * stride + x)], &px[py * 4], 4 * sizeof(int));
            }
    }
    if (!r.read(14, v) || v != 0x21) return false;
    size_t used = len;
    while (used > 0 && d[used - 1] == 0xff) used--;
    used *= 8;
    if (used < r.pos || used - r.pos > 15) return false;
    if (!r.read(int(used - r.pos), v) || v != 0) return false;
    rgb.resize(size_t(W * H * 3));
    uint8_t* o = rgb.data();
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            int64_t luma = planes[0][size_t(y * W + x)] + 128;
            int64_t cb = planes[1][size_t((y / 2) * (W / 2) + x / 2)], cr = planes[2][size_t((y / 2) * (W / 2) + x / 2)];
            int64_t R = luma + ((91881 * cr) >> 16), G = luma - ((22554 * cb + 46802 * cr) >> 16), B = luma + ((116130 * cb) >> 16);
            *o++ = uint8_t(std::clamp<int64_t>(R, 0, 255));
            *o++ = uint8_t(std::clamp<int64_t>(G, 0, 255));
            *o++ = uint8_t(std::clamp<int64_t>(B, 0, 255));
        }
    return true;
}

bool isVideoPadding(const uint8_t* d, size_t n)
{
    if (n != 2048 || d[0] != 0xf3) return false;
    for (size_t i = 3; i < n; i++) if (d[i] != 0xff) return false;
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// player
// ---------------------------------------------------------------------------
PlaydiaCore::PlaydiaCore()
{
    m_fb.assign(size_t(PIC_W * PIC_H), 0xff000000u);
    m_out.assign(320 * 240, 0xff000000u);
    m_raw.resize(2352);
}

bool PlaydiaCore::load(const std::vector<RomFile>& roms, std::string& error)
{
    std::string disc;
    for (auto& r : roms) if (!r.path.empty()) disc = r.path;
    if (disc.empty()) { error = "Pick a Playdia disc (.zip of its .cue and .bin)"; return false; }
    if (!m_cd.open(disc, error)) return false;
    // the stream is the largest track; it must be raw (2352-byte CD-XA sectors)
    m_track = -1;
    for (int t = 0; t < m_cd.numTracks(); t++)
        if (m_track < 0 || m_cd.track(t).frames > m_cd.track(m_track).frames) m_track = t;
    if (m_track < 0 || m_cd.track(m_track).sectorSize != 2352) { error = "The disc image has no raw CD-XA track (a .bin with 2352-byte sectors is needed)"; return false; }
    m_base = m_cd.track(m_track).start;
    m_sectors = m_cd.track(m_track).frames;
    reset();
    return true;
}

void PlaydiaCore::reset()
{
    m_index = 0; m_started = false; m_phase = 0;
    m_acc.clear(); m_accOverflow = false; m_pending.clear();
    std::fill(m_fb.begin(), m_fb.end(), 0xff000000u);
    std::fill(m_out.begin(), m_out.end(), 0xff000000u);
    m_prev[0] = m_prev[1] = m_prev2[0] = m_prev2[1] = 0;
    m_pcm.clear();
    m_waiting = false; m_waitFrames = 0; m_timeoutDest = -1; m_latch = -1; m_latchAge = 0; m_lastKeys = 0;
    for (int& d : m_dest) d = -1;
}

static int choiceSlot(uint32_t k)
{
    // Slot order worked out from the discs' own prompts ("Push B" takes slot 1, a two-room
    // menu moves right with slot 2 and back left with slot 3, "Cancel A" is slot 6):
    // default (Start), B, Right, Left, Down, Up, A
    if (k & PlaydiaCore::B) return 1;
    if (k & PlaydiaCore::RIGHT) return 2;
    if (k & PlaydiaCore::LEFT) return 3;
    if (k & PlaydiaCore::DOWN) return 4;
    if (k & PlaydiaCore::UP) return 5;
    if (k & PlaydiaCore::A) return 6;
    if (k & (PlaydiaCore::START | PlaydiaCore::SELECT)) return 0;
    return -1;
}

void PlaydiaCore::runFrame()
{
    if (m_track < 0) return;
    // a press that lands while a menu's video loop is replaying counts for the next choice
    uint32_t fresh = m_keys & ~m_lastKeys;
    m_lastKeys = m_keys;
    if (fresh && choiceSlot(fresh) >= 0) { m_latch = choiceSlot(fresh); m_latchAge = 0; }
    else if (m_latch >= 0 && ++m_latchAge > LATCH_FRAMES) m_latch = -1;
    if (m_waiting) {
        bool done = false;
        int slot = choiceSlot(m_keys);
        if (slot < 0 || m_dest[slot] < 0) slot = m_latch;
        m_latch = -1;
        if (slot >= 0 && m_dest[slot] >= 0 && seekLba(uint32_t(m_dest[slot]))) { m_waiting = false; m_waitFrames = 0; done = true; }
        if (!done) {
            if (++m_waitFrames >= uint32_t(CHOICE_TIMEOUT_FRAMES)) {
                int t = timeoutTarget();
                if (t >= 0 && seekLba(uint32_t(t))) { m_waiting = false; m_waitFrames = 0; }
                else m_waitFrames = CHOICE_TIMEOUT_FRAMES;
            }
            if (m_waiting) return;
        }
    }
    // skip the lead-in / TOC until the stream content (file 1) starts
    if (!m_started) {
        m_started = true;
        for (uint32_t i = 0; i < m_sectors; i++) {
            if (!m_cd.readRaw(m_base + i, m_raw.data())) break;
            if (m_raw[16] == 1) { m_index = i; break; }
        }
    }
    // 75 sectors a second: 2 and 3 in turn at 30 frames a second
    int n = 2 + m_phase;
    m_phase ^= 1;
    for (int k = 0; k < n; k++) {
        if (m_index >= m_sectors) break;
        uint32_t lba = m_base + m_index;
        if (!m_cd.readRaw(lba, m_raw.data())) break;
        m_index++;
        const uint8_t* raw = m_raw.data();
        Sector s;
        s.fileId = raw[16]; s.channel = raw[17]; s.submode = raw[18]; s.coding = raw[19];
        s.data = raw + 24;
        s.len = (s.submode & 0x20) ? 2324 : 2048;
        s.lba = lba;
        if (handleSector(s)) break;
    }
    presentNext();
    // 320 x 240 out (the picture is 248 x 216, shown 4:3)
    for (int y = 0; y < 240; y++) {
        const uint32_t* src = &m_fb[size_t(y * PIC_H / 240 * PIC_W)];
        uint32_t* dst = &m_out[size_t(y * 320)];
        for (int x = 0; x < 320; x++) dst[x] = src[x * PIC_W / 320];
    }
}

bool PlaydiaCore::handleSector(const Sector& s)
{
    if (s.submode & 0x04) { decodeAudio(s.data, s.len, s.coding); return false; }
    if (s.channel == 0 && (s.submode & 0x08) && s.len) {
        switch (s.data[0]) {
        case 0xf1: videoFragment(s.data, s.len); return false;
        case 0xf2:
            if (s.submode & 0x01) {                  // interactive command
                videoFragment(s.data, s.len);
                endPicture();
                presentLatest();
                return applyInteractive(s.lba, s.data, s.len);
            }
            videoFragment(s.data, s.len);            // frame end
            endPicture();
            return false;
        case 0xf3:
            if (isVideoPadding(s.data, s.len)) return false;
            m_acc.clear(); m_accOverflow = false; m_pending.clear();   // scene reset
            return false;
        default: return false;
        }
    }
    if (s.fileId == 0x61) { videoFragment(s.data, s.len); return false; }
    if (s.fileId == 0x62) { decodeAudio(s.data, s.len, s.coding); return false; }
    return false;
}

// F1 sectors carry picture bytes after the marker; F2 (frame end / command) after 0x23 bytes.
void PlaydiaCore::videoFragment(const uint8_t* d, size_t n)
{
    size_t off = d[0] == 0xf1 ? 1 : d[0] == 0xf2 ? 0x23 : 0;
    if (!off) return;
    size_t end = std::min<size_t>(n, 2048);
    if (end <= off) return;
    if (d[0] == 0xf2 && m_acc.empty()) return;       // a frame end with nothing before it
    if (m_acc.size() + (end - off) <= VIDEO_PACKET_CAP && !m_accOverflow) m_acc.insert(m_acc.end(), d + off, d + end);
    else m_accOverflow = true;
}

void PlaydiaCore::endPicture()
{
    bool starts = m_acc.size() >= 44 && m_acc[0] == 0x00 && m_acc[1] == 0x80 && m_acc[2] == 0x04;
    if (starts && !m_accOverflow) {
        std::vector<uint8_t> rgb;
        if (decodePicture(m_acc.data(), m_acc.size(), rgb)) m_pending.push_back(std::move(rgb));
    }
    m_acc.clear();
    m_accOverflow = false;
}

void PlaydiaCore::blit(const std::vector<uint8_t>& rgb)
{
    for (size_t i = 0; i < size_t(PIC_W * PIC_H); i++)
        m_fb[i] = 0xff000000u | uint32_t(rgb[i * 3]) << 16 | uint32_t(rgb[i * 3 + 1]) << 8 | rgb[i * 3 + 2];
}

void PlaydiaCore::presentNext()
{
    if (m_pending.empty()) return;
    blit(m_pending.front());
    m_pending.pop_front();
}

void PlaydiaCore::presentLatest()
{
    if (m_pending.empty()) return;
    blit(m_pending.back());
    m_pending.clear();
}

// F2 destinations are binary minute / second / five-sector-unit bytes
static int commandLba(const uint8_t* a)
{
    int v = int(a[0]) * 4500 + int(a[1]) * 75 + int(a[2]) * 5 - 150;
    return v < 0 ? -1 : v;
}

bool PlaydiaCore::validTarget(uint32_t lba) const { return lba >= m_base && lba - m_base < m_sectors; }

void PlaydiaCore::resumeAfter(uint32_t lba) { m_index = (lba >= m_base ? lba - m_base : 0) + 1; }

bool PlaydiaCore::seekLba(uint32_t lba)
{
    if (!validTarget(lba)) return false;
    m_index = lba - m_base;
    m_acc.clear(); m_accOverflow = false; m_pending.clear();
    return true;
}

int PlaydiaCore::timeoutTarget() const
{
    if (m_timeoutDest >= 0 && validTarget(uint32_t(m_timeoutDest))) return m_timeoutDest;
    if (m_waiting && m_dest[0] >= 0) return m_dest[0];
    return -1;
}

bool PlaydiaCore::applyInteractive(uint32_t lba, const uint8_t* d, size_t n)
{
    if (n < 31 || d[0] != 0xf2) return false;
    int dest[7];
    for (int i = 0; i < 7; i++) {
        int t = commandLba(d + 3 + i * 4);
        dest[i] = (t >= 0 && validTarget(uint32_t(t))) ? t : -1;
    }
    switch (d[1]) {
    case 0x44: case 0x50: {                          // button choice
        bool any = false;
        for (int t : dest) any |= t >= 0;
        if (!any) return false;
        resumeAfter(lba);
        std::memcpy(m_dest, dest, sizeof dest);
        m_waiting = true;
        m_waitFrames = 0;
        return true;
    }
    case 0x40: case 0x60: case 0x90: case 0xa0: {    // jump
        if (d[1] == 0xa0 && d[2] == 0xf0) return false;
        if (dest[0] < 0) return false;
        resumeAfter(lba);
        // a backward jump loops until any held button breaks out
        if (uint32_t(dest[0]) <= lba && m_keys) { m_acc.clear(); m_accOverflow = false; m_pending.clear(); return true; }
        seekLba(uint32_t(dest[0]));
        return true;
    }
    case 0x80:                                       // the fallback for a choice that times out
        m_timeoutDest = dest[0];
        return false;
    }
    return false;
}

// ---------------------------------------------------------------------------
// XA ADPCM: 18 sound groups of 128 bytes per sector, 37800 or 18900 Hz, to 44100 stereo
// ---------------------------------------------------------------------------
void PlaydiaCore::decodeAudio(const uint8_t* d, size_t n, uint8_t coding)
{
    static const int K0[4] = { 0, 60, 115, 98 }, K1[4] = { 0, 0, -52, -55 };
    if (n < 18 * 128) return;
    bool stereo = coding & 1, half = coding & 4;
    double rate = half ? 18900.0 : 37800.0;
    int16_t rawL[4096], rawR[4096];
    size_t count = 0;
    for (int sg = 0; sg < 18; sg++) {
        const uint8_t* g = d + sg * 128;
        for (int unit = 0; unit < 8; unit++) {
            uint8_t h = g[unit < 4 ? unit : unit + 4];
            int filter = (h >> 4) & 3, range = std::min(h & 0xf, 12);
            int ch = stereo ? (unit & 1) : 0;
            for (int s = 0; s < 28; s++) {
                uint8_t byte = g[16 + s * 4 + unit / 2];
                int nib = (unit & 1) ? (int8_t(byte) >> 4) : (int8_t(uint8_t(byte << 4)) >> 4);
                int out = (nib << (12 - range)) + (K0[filter] * m_prev[ch] + K1[filter] * m_prev2[ch] + 32) / 64;
                out = std::clamp(out, -32768, 32767);
                m_prev2[ch] = m_prev[ch];
                m_prev[ch] = out;
                size_t idx = stereo ? size_t((sg * 4 + unit / 2) * 28 + s) : size_t((sg * 8 + unit) * 28 + s);
                if (idx >= 4096) continue;
                if (stereo) { if (unit & 1) rawR[idx] = int16_t(out); else rawL[idx] = int16_t(out); }
                else rawL[idx] = rawR[idx] = int16_t(out);
                count = std::max(count, idx + 1);
            }
        }
    }
    size_t outCount = size_t(double(count) * 44100.0 / rate);
    for (size_t i = 0; i < outCount; i++) {
        double pos = double(i) * rate / 44100.0;
        size_t k = size_t(pos);
        double f = pos - double(k);
        int16_t l, r;
        if (k + 1 < count) { l = int16_t(rawL[k] * (1 - f) + rawL[k + 1] * f); r = int16_t(rawR[k] * (1 - f) + rawR[k + 1] * f); }
        else if (k < count) { l = rawL[k]; r = rawR[k]; }
        else break;
        m_pcm.push_back(l);
        m_pcm.push_back(r);
    }
    if (m_pcm.size() > 44100 * 4) m_pcm.erase(m_pcm.begin(), m_pcm.end() - 44100 * 2);
}

size_t PlaydiaCore::readAudio(int16_t* out, size_t maxSamples)
{
    // one frame of mono samples (1470 at 30 frames a second)
    size_t n = std::min<size_t>(maxSamples, 1470), i = 0;
    for (; i < n && (i * 2 + 1) < m_pcm.size(); i++) out[i] = int16_t((m_pcm[i * 2] + m_pcm[i * 2 + 1]) / 2);
    m_pcm.erase(m_pcm.begin(), m_pcm.begin() + long(std::min(m_pcm.size(), i * 2)));
    for (; i < n; i++) out[i] = 0;
    return n;
}
