// PocketPlug - read-only MAME CHD v5 reader
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Format and codecs follow MAME's chd.cpp / chdcodec.cpp / huffman.cpp
// (BSD-3-Clause, Aaron Giles). LZMA decoding is the LZMA SDK (public domain),
// FLAC is libFLAC (BSD-3-Clause, Xiph.Org), zlib is the system library.

#include "chd.h"
#include <cstring>
#include <zlib.h>

extern "C" {
#include "lzma/LzmaDec.h"
}
#define FLAC__NO_DLL
#include "flac/include/FLAC/stream_decoder.h"

namespace {

uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
uint64_t be64(const uint8_t* p) { return uint64_t(be32(p)) << 32 | be32(p + 4); }
constexpr uint32_t tag(const char* s) { return uint32_t(uint8_t(s[0])) << 24 | uint8_t(s[1]) << 16 | uint8_t(s[2]) << 8 | uint8_t(s[3]); }

// MSB-first bit reader; zeros past the end, as MAME's bitstream_in
struct Bits {
    const uint8_t* d; size_t n; uint64_t pos = 0;
    Bits(const uint8_t* d_, size_t n_) : d(d_), n(n_) {}
    uint32_t peek(int k) const {
        uint32_t v = 0;
        for (int i = 0; i < k; i++) {
            uint64_t b = pos + i;
            int bit = (b >> 3) < n ? (d[b >> 3] >> (7 - (b & 7))) & 1 : 0;
            v = (v << 1) | bit;
        }
        return v;
    }
    uint32_t read(int k) { uint32_t v = peek(k); pos += k; return v; }
    bool overflow() const { return pos > n * 8; }
};

// MAME's canonical huffman decoder (huffman_decoder<>)
struct Huff {
    int nc, mb;
    std::vector<uint8_t> nbits;
    std::vector<uint32_t> lut;     // symbol << 8 | length
    Huff(int numcodes, int maxbits) : nc(numcodes), mb(maxbits), nbits(numcodes, 0) {}
    bool canonical() {
        uint32_t histo[33] = {};
        for (int i = 0; i < nc; i++) { if (nbits[i] > 32) return false; histo[nbits[i]]++; }
        uint32_t cur = 0;
        for (int len = 32; len > 0; len--) { uint32_t next = (cur + histo[len]) >> 1; histo[len] = cur; cur = next; }
        lut.assign(size_t(1) << mb, 0);
        for (int i = 0; i < nc; i++) {
            int b = nbits[i];
            if (!b) continue;
            if (b > mb) return false;
            uint32_t code = histo[b]++;
            int sh = mb - b;
            uint32_t lo = code << sh, hi = (code + 1) << sh;
            if (hi > lut.size()) return false;
            for (uint32_t j = lo; j < hi; j++) lut[j] = uint32_t(i) << 8 | b;
        }
        return true;
    }
    int decode(Bits& bits) const {
        uint32_t e = lut[bits.peek(mb)];
        bits.pos += e & 0xff;
        return int(e >> 8);
    }
    bool importRle(Bits& bits) {
        int nb = mb >= 16 ? 5 : mb >= 8 ? 4 : 3;
        int cur = 0;
        while (cur < nc) {
            int v = bits.read(nb);
            if (v != 1) nbits[cur++] = uint8_t(v);
            else {
                v = bits.read(nb);
                if (v == 1) nbits[cur++] = 1;
                else {
                    int rep = bits.read(nb) + 3;
                    if (cur + rep > nc) return false;
                    while (rep--) nbits[cur++] = uint8_t(v);
                }
            }
        }
        return canonical();
    }
    bool importHuffman(Bits& bits) {
        Huff small(24, 6);
        small.nbits[0] = uint8_t(bits.read(3));
        int start = bits.read(3) + 1, count = 0;
        for (int i = 1; i < 24; i++) {
            if (i < start || count == 7) small.nbits[i] = 0;
            else { count = bits.read(3); small.nbits[i] = count == 7 ? 0 : uint8_t(count); }
        }
        if (!small.canonical()) return false;
        int temp = nc - 9, rlefull = 0;
        while (temp) { temp >>= 1; rlefull++; }
        int last = 0, cur = 0;
        while (cur < nc) {
            int v = small.decode(bits);
            if (v != 0) { last = v - 1; nbits[cur++] = uint8_t(last); }
            else {
                int c = bits.read(3) + 2;
                if (c == 7 + 2) c += bits.read(rlefull);
                while (c-- && cur < nc) nbits[cur++] = uint8_t(last);
            }
        }
        return canonical();
    }
};

void* lzAlloc(ISzAllocPtr, size_t size) { return malloc(size); }
void lzFree(ISzAllocPtr, void* p) { free(p); }
const ISzAlloc kLzAlloc = { lzAlloc, lzFree };

// MAME's flac hunks: an 'L' / 'B' byte, then FLAC frames with no stream
// header (16-bit stereo, 44.1 kHz, block size from the hunk size)
struct FlacCtx {
    std::vector<uint8_t> in; size_t inPos = 0;
    uint8_t* out; size_t outPos = 0, outMax; bool big;
};
FLAC__StreamDecoderReadStatus flacRead(const FLAC__StreamDecoder*, FLAC__byte b[], size_t* n, void* c)
{
    auto* x = static_cast<FlacCtx*>(c);
    size_t k = x->in.size() - x->inPos;
    if (k > *n) k = *n;
    memcpy(b, x->in.data() + x->inPos, k);
    x->inPos += k; *n = k;
    return k ? FLAC__STREAM_DECODER_READ_STATUS_CONTINUE : FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM;
}
FLAC__StreamDecoderWriteStatus flacWrite(const FLAC__StreamDecoder*, const FLAC__Frame* f, const FLAC__int32* const buf[], void* c)
{
    auto* x = static_cast<FlacCtx*>(c);
    for (unsigned i = 0; i < f->header.blocksize && x->outPos + 4 <= x->outMax; i++)
        for (int ch = 0; ch < 2; ch++) {
            int s = buf[ch][i] & 0xffff;
            if (x->big) { x->out[x->outPos++] = uint8_t(s >> 8); x->out[x->outPos++] = uint8_t(s); }
            else { x->out[x->outPos++] = uint8_t(s); x->out[x->outPos++] = uint8_t(s >> 8); }
        }
    return FLAC__STREAM_DECODER_WRITE_STATUS_CONTINUE;
}
void flacError(const FLAC__StreamDecoder*, FLAC__StreamDecoderErrorStatus, void*) {}

bool flacHunk(const uint8_t* src, uint32_t len, uint8_t* dst, uint32_t hunkBytes)
{
    if (len < 2 || (src[0] != 'L' && src[0] != 'B')) return false;
    FlacCtx x;
    x.out = dst; x.outMax = hunkBytes; x.big = src[0] == 'B';
    uint32_t bs = hunkBytes / 4;
    while (bs > 2048) bs /= 2;
    uint8_t h[0x2a] = { 'f', 'L', 'a', 'C', 0x80, 0, 0, 0x22 };
    h[8] = uint8_t(bs >> 8); h[9] = uint8_t(bs); h[10] = uint8_t(bs >> 8); h[11] = uint8_t(bs);
    uint32_t v = (44100u << 4) | (1u << 1);       // rate, 2 channels - 1
    h[0x12] = uint8_t(v >> 16); h[0x13] = uint8_t(v >> 8); h[0x14] = uint8_t(v); h[0x15] = 0xf0;  // 16 bits
    x.in.assign(h, h + sizeof(h));
    x.in.insert(x.in.end(), src + 1, src + len);
    FLAC__StreamDecoder* d = FLAC__stream_decoder_new();
    if (!d) return false;
    bool ok = FLAC__stream_decoder_init_stream(d, flacRead, nullptr, nullptr, nullptr, nullptr,
                                               flacWrite, nullptr, flacError, &x) == FLAC__STREAM_DECODER_INIT_STATUS_OK;
    while (ok && x.outPos < x.outMax && FLAC__stream_decoder_process_single(d))
        if (FLAC__stream_decoder_get_state(d) == FLAC__STREAM_DECODER_END_OF_STREAM) break;
    FLAC__stream_decoder_delete(d);
    return ok && x.outPos == x.outMax;
}

} // namespace

ChdFile::~ChdFile() { if (m_f) fclose(m_f); }

bool ChdFile::isChd(FILE* f)
{
    char sig[8];
    long at = ftell(f);
    bool ok = fseek(f, 0, SEEK_SET) == 0 && fread(sig, 1, 8, f) == 8 && memcmp(sig, "MComprHD", 8) == 0;
    fseek(f, at, SEEK_SET);
    return ok;
}

bool ChdFile::open(const std::string& path, std::string& err)
{
    m_f = fopen(path.c_str(), "rb");
    if (!m_f) { err = "can't open " + path; return false; }
    uint8_t h[124];
    if (fread(h, 1, sizeof(h), m_f) != sizeof(h) || memcmp(h, "MComprHD", 8) != 0) { err = "not a CHD file"; return false; }
    if (be32(h + 12) != 5) { err = "only CHD version 5 is supported (convert it with chdman)"; return false; }
    for (int i = 0; i < 4; i++) m_codecs[i] = be32(h + 16 + 4 * i);
    m_logical = be64(h + 32);
    uint64_t mapOff = be64(h + 40);
    m_metaOff = be64(h + 48);
    m_hunkBytes = be32(h + 56);
    for (int i = 0; i < 20; i++) if (h[104 + i]) { err = "CHDs with a parent aren't supported"; return false; }
    if (m_hunkBytes == 0 || m_hunkBytes > (1u << 20) || m_logical == 0) { err = "bad CHD header"; return false; }
    uint64_t nh = (m_logical + m_hunkBytes - 1) / m_hunkBytes;
    if (nh > (1u << 26)) { err = "CHD too large"; return false; }

    // compressed map: huffman-coded types, then lengths / offsets
    uint8_t mh[16];
    if (fseeko(m_f, off_t(mapOff), SEEK_SET) || fread(mh, 1, 16, m_f) != 16) { err = "bad CHD map"; return false; }
    uint32_t mapBytes = be32(mh);
    uint64_t firstOffs = uint64_t(mh[4]) << 40 | uint64_t(mh[5]) << 32 | be32(mh + 6);
    int lengthBits = mh[12], selfBits = mh[13], parentBits = mh[14];
    (void)parentBits;
    std::vector<uint8_t> raw(mapBytes);
    if (fread(raw.data(), 1, mapBytes, m_f) != mapBytes) { err = "bad CHD map"; return false; }
    Bits bits(raw.data(), raw.size());
    Huff dec(16, 8);
    if (!dec.importRle(bits)) { err = "bad CHD map"; return false; }
    std::vector<uint8_t> types(nh);
    int last = 0, rep = 0;
    for (uint64_t i = 0; i < nh; i++) {
        if (rep > 0) { types[i] = uint8_t(last); rep--; continue; }
        int v = dec.decode(bits);
        if (v == 7) { rep = 2 + dec.decode(bits); types[i] = uint8_t(last); }
        else if (v == 8) { rep = 2 + 16 + (dec.decode(bits) << 4); rep += dec.decode(bits); types[i] = uint8_t(last); }
        else { last = v; types[i] = uint8_t(v); }
    }
    m_map.resize(nh);
    uint64_t cur = firstOffs, lastSelf = 0;
    for (uint64_t i = 0; i < nh; i++) {
        Entry& e = m_map[i];
        switch (types[i]) {
        case 0: case 1: case 2: case 3:
            e = { types[i], cur, bits.read(lengthBits) }; bits.read(16); cur += e.length; break;
        case 4:                                            // stored
            bits.read(16); e = { 4, cur, m_hunkBytes }; cur += m_hunkBytes; break;
        case 5: lastSelf = bits.read(selfBits); e = { 5, lastSelf, 0 }; break;
        case 9: e = { 5, lastSelf, 0 }; break;
        case 10: e = { 5, ++lastSelf, 0 }; break;
        default: err = "CHDs with a parent aren't supported"; return false;
        }
        if (e.type == 5 && e.offset >= nh) { err = "bad CHD map"; return false; }
    }
    if (bits.overflow()) { err = "bad CHD map"; return false; }
    for (auto& s : m_cache) s.data.resize(m_hunkBytes);
    return true;
}

bool ChdFile::decode(uint32_t codec, const uint8_t* src, uint32_t len, uint8_t* dst)
{
    if (codec == tag("lzma")) {
        // MAME: lc 3, lp 0, pb 2; the dictionary only has to cover a hunk
        uint8_t props[5] = { uint8_t((2 * 5 + 0) * 9 + 3) };
        uint32_t dict = m_hunkBytes < 4096 ? 4096 : m_hunkBytes;
        props[1] = uint8_t(dict); props[2] = uint8_t(dict >> 8); props[3] = uint8_t(dict >> 16); props[4] = uint8_t(dict >> 24);
        SizeT outLen = m_hunkBytes, inLen = len;
        ELzmaStatus st;
        int r = LzmaDecode(dst, &outLen, src, &inLen, props, 5, LZMA_FINISH_ANY, &st, &kLzAlloc);
        return r == SZ_OK && outLen == m_hunkBytes;
    }
    if (codec == tag("zlib")) {
        z_stream z = {};
        if (inflateInit2(&z, -15) != Z_OK) return false;
        z.next_in = const_cast<uint8_t*>(src); z.avail_in = len;
        z.next_out = dst; z.avail_out = m_hunkBytes;
        int r = inflate(&z, Z_FINISH);
        bool ok = (r == Z_STREAM_END || r == Z_OK || r == Z_BUF_ERROR) && z.avail_out == 0;
        inflateEnd(&z);
        return ok;
    }
    if (codec == tag("huff")) {
        Bits b(src, len);
        Huff h(256, 16);
        if (!h.importHuffman(b)) return false;
        for (uint32_t i = 0; i < m_hunkBytes; i++) dst[i] = uint8_t(h.decode(b));
        return !b.overflow();
    }
    if (codec == tag("flac")) return flacHunk(src, len, dst, m_hunkBytes);
    return false;                                         // cdlz / cdzl / avhu ... aren't for disks
}

bool ChdFile::hunk(uint32_t index, uint8_t* dst, int depth)
{
    if (index >= m_map.size() || depth > 4) return false;
    const Entry& e = m_map[index];
    if (e.type == 5) return hunk(uint32_t(e.offset), dst, depth + 1);
    if (e.length > m_hunkBytes * 2 + 1024) return false;
    m_comp.resize(e.length);
    if (fseeko(m_f, off_t(e.offset), SEEK_SET) || fread(m_comp.data(), 1, e.length, m_f) != e.length) return false;
    if (e.type == 4) { memcpy(dst, m_comp.data(), m_hunkBytes); return true; }
    return decode(m_codecs[e.type], m_comp.data(), e.length, dst);
}

bool ChdFile::diskGeometry(int& cyls, int& heads, int& secs)
{
    uint64_t at = m_metaOff;
    for (int n = 0; at && n < 64; n++) {
        uint8_t mh[16];
        if (fseeko(m_f, off_t(at), SEEK_SET) || fread(mh, 1, 16, m_f) != 16) return false;
        uint32_t len = uint32_t(mh[5]) << 16 | mh[6] << 8 | mh[7];
        uint64_t next = be64(mh + 8);
        if (be32(mh) == tag("GDDD") && len < 256) {
            char s[256] = {};
            if (fread(s, 1, len, m_f) != len) return false;
            int bps = 512;
            return sscanf(s, "CYLS:%d,HEADS:%d,SECS:%d,BPS:%d", &cyls, &heads, &secs, &bps) >= 3;
        }
        at = next;
    }
    return false;
}

bool ChdFile::read(uint64_t offset, uint8_t* dst, size_t len)
{
    if (!m_f || offset + len > m_logical) return false;
    while (len) {
        uint32_t h = uint32_t(offset / m_hunkBytes), at = uint32_t(offset % m_hunkBytes);
        Slot* s = nullptr;
        for (auto& c : m_cache) if (c.hunk == h) { s = &c; break; }
        if (!s) {
            s = &m_cache[0];
            for (auto& c : m_cache) if (c.age < s->age) s = &c;
            s->hunk = 0xffffffff;
            if (!hunk(h, s->data.data())) return false;
            s->hunk = h;
        }
        s->age = ++m_clock;
        size_t k = m_hunkBytes - at;
        if (k > len) k = len;
        memcpy(dst, s->data.data() + at, k);
        dst += k; offset += k; len -= k;
    }
    return true;
}
