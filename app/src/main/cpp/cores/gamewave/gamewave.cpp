// PocketPlug - ZAPiT Game Wave (2005)
// SPDX-License-Identifier: GPL-2.0-or-later
#include "gamewave.h"
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <zlib.h>

extern "C" {
#include "lua/lua.h"
#include "lua/lualib.h"
#include "lua/lauxlib.h"
#include "mpeg2/config.h"
#include "mpeg2/mpeg2.h"
}
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_NO_SIMD
#include "minimp3/minimp3.h"

#ifdef __ANDROID__
#include <android/log.h>
#endif

static GameWaveCore* core(lua_State* L)
{
    lua_pushstring(L, "pocketplug.gamewave");
    lua_rawget(L, LUA_REGISTRYINDEX);
    GameWaveCore* c = (GameWaveCore*)lua_touserdata(L, -1);
    lua_pop(L, 1);
    return c;
}

static std::string lower(std::string s)
{
    for (auto& c : s) c = char(tolower((unsigned char)c));
    return s;
}

static int argi(lua_State* L, int i, int def = 0) { return lua_isnumber(L, i) ? int(lua_tonumber(L, i)) : def; }
static std::string args(lua_State* L, int i) { const char* s = lua_tostring(L, i); return s ? s : ""; }

void GameWaveCore::dbg(const char* fmt, ...)
{
    if (!m_debug) return;
    va_list ap;
    va_start(ap, fmt);
#ifdef __ANDROID__
    __android_log_vprint(ANDROID_LOG_DEBUG, "GameWave", fmt, ap);
#else
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
#endif
    va_end(ap);
}

// YCbCr (studio range) to ARGB
static uint32_t ycc(int y, int cb, int cr, int a = 255)
{
    int yy = (y - 16) * 298;
    int u = cb - 128, v = cr - 128;
    int r = (yy + 409 * v + 128) >> 8, g = (yy - 100 * u - 208 * v + 128) >> 8, b = (yy + 516 * u + 128) >> 8;
    r = std::clamp(r, 0, 255); g = std::clamp(g, 0, 255); b = std::clamp(b, 0, 255);
    return uint32_t(a) << 24 | uint32_t(r) << 16 | uint32_t(g) << 8 | uint32_t(b);
}

// ===========================================================================
// disc: ISO 9660 with Joliet names
// ===========================================================================
bool GameWaveCore::readFile(const FileEnt& f, std::vector<uint8_t>& out)
{
    out.resize(f.size);
    uint8_t raw[2352];
    for (uint32_t done = 0, s = 0; done < f.size; s++) {
        if (!m_cd.readRaw(f.lba + s, raw)) return false;
        uint32_t n = std::min<uint32_t>(2048, f.size - done);
        std::memcpy(out.data() + done, raw + 16, n);
        done += n;
    }
    return true;
}

bool GameWaveCore::scanDir(uint32_t lba, uint32_t size, const std::string& path, bool joliet, int depth)
{
    if (depth > 8) return false;
    std::vector<uint8_t> d;
    if (!readFile({ lba, size }, d)) return false;
    size_t i = 0;
    while (i < d.size()) {
        uint8_t n = d[i];
        if (n == 0) { i = (i / 2048 + 1) * 2048; continue; }
        if (i + n > d.size() || n < 34) break;
        const uint8_t* r = &d[i];
        i += n;
        uint32_t l = uint32_t(r[2]) | uint32_t(r[3]) << 8 | uint32_t(r[4]) << 16 | uint32_t(r[5]) << 24;
        uint32_t s = uint32_t(r[10]) | uint32_t(r[11]) << 8 | uint32_t(r[12]) << 16 | uint32_t(r[13]) << 24;
        uint8_t flags = r[25], nl = r[32];
        if (nl == 1 && (r[33] == 0 || r[33] == 1)) continue;
        std::string name;
        if (joliet) { for (int k = 0; k + 1 < nl; k += 2) name += char(r[33 + k + 1]); }
        else name.assign((const char*)r + 33, nl);
        size_t semi = name.find(';');
        if (semi != std::string::npos) name.resize(semi);
        std::string p = path + "/" + lower(name);
        if (flags & 2) scanDir(l, s, p, joliet, depth + 1);
        else m_files[p] = { l, s };
    }
    return true;
}

bool GameWaveCore::findFile(const std::string& path, FileEnt& out) const
{
    std::string p = lower(path);
    for (auto& c : p) if (c == '\\') c = '/';
    while (p.find("//") != std::string::npos) p.erase(p.find("//"), 1);
    if (p.rfind("./", 0) == 0) p.erase(0, 1);
    if (p.empty() || p[0] != '/') p = "/" + p;
    auto it = m_files.find(p);
    if (it == m_files.end()) return false;
    out = it->second;
    return true;
}

std::string GameWaveCore::resolve(int res, const std::string& name) const
{
    if (!name.empty() && name[0] == '/') return name;
    std::string dir = (res >= 1 && res <= int(m_res.size())) ? m_res[size_t(res - 1)] : "/";
    if (dir == ".") dir = "/";
    if (dir.empty() || dir.back() != '/') dir += "/";
    return dir + name;
}

static bool inflateAll(const uint8_t* src, size_t n, std::vector<uint8_t>& out, size_t expect)
{
    out.resize(expect ? expect : n * 8);
    z_stream z{};
    if (inflateInit(&z) != Z_OK) return false;
    z.next_in = const_cast<uint8_t*>(src);
    z.avail_in = uInt(n);
    for (;;) {
        z.next_out = out.data() + z.total_out;
        z.avail_out = uInt(out.size() - z.total_out);
        int r = inflate(&z, Z_NO_FLUSH);
        if (r == Z_STREAM_END) break;
        if (r != Z_OK && r != Z_BUF_ERROR) { inflateEnd(&z); return false; }
        if (z.avail_out == 0) out.resize(out.size() * 2);
        else if (r == Z_BUF_ERROR) break;
    }
    out.resize(z.total_out);
    inflateEnd(&z);
    return true;
}

// ===========================================================================
// resources
// ===========================================================================
int GameWaveCore::newTexture(int w, int h)
{
    int id = m_nextTex++;
    Texture& t = m_tex[id];
    t.w = w; t.h = h;
    t.px.assign(size_t(w * h), 0);
    return id;
}

// .zbm: 48-byte header (width at 0x10, height at 0x14, packed / unpacked sizes at 0x24 / 0x28),
// then zlib data of 16-bit pixels, A4 Y6 Cb3 Cr3, big-endian with each pair of pixels swapped
int GameWaveCore::loadTexture(const std::string& path)
{
    FileEnt f;
    std::vector<uint8_t> d, px;
    if (!findFile(path, f) || !readFile(f, d) || d.size() < 48) { dbg("texture %s missing", path.c_str()); return -1; }
    auto u32 = [&](size_t o) { return uint32_t(d[o]) | uint32_t(d[o + 1]) << 8 | uint32_t(d[o + 2]) << 16 | uint32_t(d[o + 3]) << 24; };
    int w = int(u32(0x10)), h = int(u32(0x14));
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096) return -1;
    if (!inflateAll(d.data() + 48, d.size() - 48, px, u32(0x28))) return -1;
    int id = newTexture(w, h);
    Texture& t = m_tex[id];
    size_t n = std::min(size_t(w * h), px.size() / 2);
    for (size_t i = 0; i < n; i++) {
        size_t src = (i ^ 1) * 2;                     // pairs are swapped
        if (src + 1 >= px.size()) break;
        uint16_t v = uint16_t(px[src] << 8 | px[src + 1]);
        int cr = (v & 7) << 5, cb = ((v >> 3) & 7) << 5, y = ((v >> 6) & 63) << 2, a = ((v >> 12) & 15) * 17;
        int Y = y, U = cb - 128, V = cr - 128;
        int r = Y + (45 * V) / 32, g = Y - (11 * U + 23 * V) / 32, b = Y + (113 * U) / 64;
        t.px[i] = uint32_t(a) << 24 | uint32_t(std::clamp(r, 0, 255)) << 16 | uint32_t(std::clamp(g, 0, 255)) << 8 | uint32_t(std::clamp(b, 0, 255));
    }
    return id;
}

// fonts: a .dat (names, the glyph sheet's file name at 0x200, then at 0x280 the glyph count,
// first code, last code, cell height, ? and per glyph its cell x0 y0 x1 y1 and advance,
// left, width, right) and the glyph sheet .zbm
int GameWaveCore::loadFont(const std::string& path)
{
    FileEnt f;
    std::vector<uint8_t> d;
    if (!findFile(path, f) || !readFile(f, d) || d.size() < 0x280 + 20) { dbg("font %s missing", path.c_str()); return -1; }
    auto i32 = [&](size_t o) { return o + 4 <= d.size() ? int(uint32_t(d[o]) | uint32_t(d[o + 1]) << 8 | uint32_t(d[o + 2]) << 16 | uint32_t(d[o + 3]) << 24) : 0; };
    std::string sheet((const char*)&d[0x200], strnlen((const char*)&d[0x200], 128));
    std::string dir = path.substr(0, path.rfind('/') + 1);
    Font fn;
    fn.count = i32(0x280); fn.first = i32(0x284); fn.cellH = i32(0x28c);
    for (int k = 0; k < fn.count; k++) {
        size_t o = 0x280 + 20 + size_t(k) * 32;
        if (o + 32 > d.size()) break;
        fn.g.push_back({ i32(o), i32(o + 4), i32(o + 8), i32(o + 12), i32(o + 16), i32(o + 20), i32(o + 24), i32(o + 28) });
    }
    fn.tex = loadTexture(dir + sheet);
    if (fn.tex < 0) return -1;
    int id = m_nextFont++;
    m_fonts[id] = fn;
    if (!m_builtinFont) m_builtinFont = id;
    return id;
}

int GameWaveCore::renderText(const std::string& s, int font, int w, int h, int align, uint32_t color)
{
    w = std::clamp(w, 1, 2048); h = std::clamp(h, 1, 2048);
    int tid = newTexture(w, h);
    auto fit = m_fonts.find(font);
    if (fit == m_fonts.end() && m_fonts.size()) fit = m_fonts.find(m_builtinFont);
    if (fit != m_fonts.end()) {
        Font& fn = fit->second;
        Texture& sheet = m_tex[fn.tex];
        Texture& t = m_tex[tid];
        // lines split on '\n'; each line laid out on its own
        std::vector<std::string> lines;
        size_t p = 0;
        while (true) { size_t e = s.find('\n', p); lines.push_back(s.substr(p, e == std::string::npos ? std::string::npos : e - p)); if (e == std::string::npos) break; p = e + 1; }
        int lineH = fn.cellH ? fn.cellH : 20;
        int top = lines.size() == 1 ? (h - lineH) / 2 : 0;
        for (size_t li = 0; li < lines.size(); li++) {
            const std::string& line = lines[li];
            int width = 0;
            for (unsigned char c : line) { int gi = int(c) - fn.first; if (gi >= 0 && gi < int(fn.g.size())) width += fn.g[size_t(gi)].adv; }
            int pen = align == 1 ? (w - width) / 2 : align == 2 ? w - width : 0;
            int y0 = top + int(li) * lineH;
            for (unsigned char c : line) {
                int gi = int(c) - fn.first;
                if (gi < 0 || gi >= int(fn.g.size())) continue;
                const Glyph& g = fn.g[size_t(gi)];
                // the ink sits at the cell's left edge; the left bearing moves it along
                for (int yy = 0; yy < g.y1 - g.y0; yy++)
                    for (int xx = 0; xx < g.width + 1 && xx < g.x1 - g.x0; xx++) {
                        int sx = g.x0 + xx, sy = g.y0 + yy, dx = pen + g.left + xx, dy = y0 + yy;
                        if (sx < 0 || sy < 0 || sx >= sheet.w || sy >= sheet.h || dx < 0 || dy < 0 || dx >= w || dy >= h) continue;
                        uint32_t sp = sheet.px[size_t(sy * sheet.w + sx)];
                        uint32_t a = sp >> 24;
                        if (!a) continue;
                        uint32_t& dp = t.px[size_t(dy * w + dx)];
                        if (a > (dp >> 24)) dp = (a << 24) | (color & 0xffffff);
                    }
                pen += g.adv;
            }
        }
    }
    return tid;
}

int GameWaveCore::loadSound(const std::string& path)
{
    FileEnt f;
    std::vector<uint8_t> d, pcm;
    if (!findFile(path, f) || !readFile(f, d) || d.size() < 20) { dbg("sound %s missing", path.c_str()); return -1; }
    uint32_t count = uint32_t(d[4]) | uint32_t(d[5]) << 8 | uint32_t(d[6]) << 16 | uint32_t(d[7]) << 24;
    if (!inflateAll(d.data() + 20, d.size() - 20, pcm, size_t(count) * 2)) return -1;
    int id = m_nextSound++;
    Sound& s = m_sounds[id];
    s.ch = 1; s.rate = 22050;
    s.pcm.resize(pcm.size() / 2);
    for (size_t i = 0; i < s.pcm.size(); i++) s.pcm[i] = int16_t(pcm[i * 2] << 8 | pcm[i * 2 + 1]);
    return id;
}

// ===========================================================================
// MPEG-2: stills and movies
// ===========================================================================
void GameWaveCore::yuvToVideo(const uint8_t* const* buf, int w, int h, int cw, int ch)
{
    m_video.assign(size_t(W * H), 0xff000000u);
    int ox = (W - std::min(w, W)) / 2, oy = (H - std::min(h, H)) / 2;
    for (int y = 0; y < std::min(h, H); y++) {
        const uint8_t* Y = buf[0] + y * w;
        const uint8_t* U = buf[1] + (y * ch / h) * cw;
        const uint8_t* V = buf[2] + (y * ch / h) * cw;
        uint32_t* d = &m_video[size_t((y + oy) * W + ox)];
        for (int x = 0; x < std::min(w, W); x++) d[x] = ycc(Y[x], U[x * cw / w], V[x * cw / w]);
    }
}

bool GameWaveCore::decodeStill(const std::vector<uint8_t>& es, std::vector<uint32_t>& rgb)
{
    mpeg2dec_t* d = mpeg2_init();
    if (!d) return false;
    const mpeg2_info_t* info = mpeg2_info(d);
    std::vector<uint8_t> buf(es);
    static uint8_t endCode[4] = { 0, 0, 1, 0xb7 };
    buf.insert(buf.end(), endCode, endCode + 4);
    mpeg2_buffer(d, buf.data(), buf.data() + buf.size());
    bool got = false, fed = false;
    std::vector<uint32_t> keep = m_video;
    for (int guard = 0; guard < 100000; guard++) {
        mpeg2_state_t st = mpeg2_parse(d);
        if (st == STATE_BUFFER) { if (fed) break; fed = true; mpeg2_buffer(d, endCode, endCode + 4); continue; }
        if ((st == STATE_SLICE || st == STATE_END || st == STATE_INVALID_END) && info->display_fbuf && info->sequence) {
            const mpeg2_sequence_t* q = info->sequence;
            yuvToVideo(info->display_fbuf->buf, int(q->width), int(q->height), int(q->chroma_width), int(q->chroma_height));
            rgb = m_video;
            got = true;
        }
        if (st == STATE_END || st == STATE_INVALID_END) break;
    }
    mpeg2_close(d);
    m_video = keep;
    return got;
}

int GameWaveCore::loadIframe(const std::string& path)
{
    FileEnt f;
    std::vector<uint8_t> d;
    if (!findFile(path, f) || !readFile(f, d)) { dbg("iframe %s missing", path.c_str()); return -1; }
    std::vector<uint32_t> rgb;
    if (!decodeStill(d, rgb)) return -1;
    int id = m_nextIframe++;
    m_iframes[id] = std::move(rgb);
    return id;
}

void GameWaveCore::showIframe(int id)
{
    auto it = m_iframes.find(id);
    if (it == m_iframes.end()) return;
    if (m_movieState) stopMovie();
    m_video = it->second;
}

int GameWaveCore::loadMovie(const std::string& path)
{
    FileEnt f;
    if (!findFile(path, f)) { dbg("movie %s missing", path.c_str()); return -1; }
    int id = m_nextMovie++;
    m_movies[id].f = f;
    m_curMovie = id;
    m_movieLoop = false;                               // the scripts only ever turn looping on
    return id;
}

void GameWaveCore::playMovie()
{
    if (!m_movies.count(m_curMovie)) return;
    if (m_mpeg) mpeg2_close(m_mpeg);
    m_mpeg = mpeg2_init();
    if (!m_mp3dec) m_mp3dec = new mp3dec_t;
    mp3dec_init((mp3dec_t*)m_mp3dec);
    m_movPos = 0; m_movBuf.clear(); m_movBufPos = 0; m_esChunk.clear(); m_mp2.clear();
    m_movAudio.clear(); m_movEof = false; m_noPicture = 0;
    m_movieState = 1;
}

void GameWaveCore::stopMovie()
{
    m_movieState = 0;
    m_movAudio.clear();
    if (m_mpeg) { mpeg2_close(m_mpeg); m_mpeg = nullptr; }
}

// Pull one pack's worth of program stream: video PES to the decoder, audio PES (MPEG-1 layer II)
// to the audio decoder. False when the file is used up.
bool GameWaveCore::movieDemux()
{
    const Movie& mv = m_movies[m_curMovie];
    // keep at least 64 KB of stream buffered
    if (m_movBuf.size() - m_movBufPos < 131072 && m_movPos < mv.f.size) {
        m_movBuf.erase(m_movBuf.begin(), m_movBuf.begin() + long(m_movBufPos));
        m_movBufPos = 0;
        uint32_t n = std::min<uint32_t>(64 * 2048, mv.f.size - m_movPos);
        uint8_t raw[2352];
        uint32_t lba = mv.f.lba + m_movPos / 2048;
        for (uint32_t got = 0; got < n; lba++) {
            if (!m_cd.readRaw(lba, raw)) { m_movPos = mv.f.size; break; }
            uint32_t k = std::min<uint32_t>(2048, n - got);
            m_movBuf.insert(m_movBuf.end(), raw + 16, raw + 16 + k);
            got += k;
        }
        m_movPos = std::min(mv.f.size, m_movPos + n);
    }
    const uint8_t* b = m_movBuf.data();
    size_t n = m_movBuf.size();
    size_t i = m_movBufPos;
    while (i + 4 <= n && !(b[i] == 0 && b[i + 1] == 0 && b[i + 2] == 1 && b[i + 3] >= 0xb9)) i++;
    if (i + 6 > n) { m_movBufPos = n; return m_movPos < mv.f.size; }
    uint8_t sid = b[i + 3];
    if (sid == 0xb9) { m_movBufPos = i + 4; return false; }
    if (sid == 0xba) {                                 // pack header (MPEG-2: 14 bytes + stuffing)
        if (i + 14 > n) { m_movBufPos = i; return m_movPos < mv.f.size; }
        size_t len = (b[i + 4] & 0xc0) == 0x40 ? 14 + (b[i + 13] & 7) : 12;
        m_movBufPos = i + len;
        return true;
    }
    size_t len = size_t(b[i + 4]) << 8 | b[i + 5];
    if (i + 6 + len > n) {
        if (m_movPos >= mv.f.size) { m_movBufPos = n; return false; }
        m_movBufPos = i;                                // the rest arrives with the next refill
        return true;
    }
    const uint8_t* pes = b + i + 6;
    m_movBufPos = i + 6 + len;
    if (sid == 0xe0 || sid == 0xc0) {
        size_t hl = 0;
        if ((pes[0] & 0xc0) == 0x80) hl = 3 + pes[2];     // MPEG-2 PES header
        else {                                          // MPEG-1 style
            while (hl < len && pes[hl] == 0xff) hl++;
            if (hl < len && (pes[hl] & 0xc0) == 0x40) hl += 2;
            if (hl < len && (pes[hl] & 0xf0) == 0x20) hl += 5;
            else if (hl < len && (pes[hl] & 0xf0) == 0x30) hl += 10;
            else hl++;
        }
        if (hl < len) {
            if (sid == 0xe0) m_esChunk.insert(m_esChunk.end(), pes + hl, pes + len);
            else m_mp2.insert(m_mp2.end(), pes + hl, pes + len);
        }
    }
    return true;
}

void GameWaveCore::movieFrame()
{
    if (!m_movieState || !m_mpeg) return;
    const mpeg2_info_t* info = mpeg2_info(m_mpeg);
    bool shown = false;
    for (int guard = 0; guard < 4000 && !shown; guard++) {
        mpeg2_state_t st = mpeg2_parse(m_mpeg);
        if (st == STATE_BUFFER) {
            // hand libmpeg2 the next video bytes
            m_esChunk.clear();
            bool more = true;
            while (m_esChunk.size() < 4096 && (more = movieDemux())) {}
            if (m_esChunk.empty() && !more) {
                if (m_movEof) break;
                m_movEof = true;
                static const uint8_t endCode[4] = { 0, 0, 1, 0xb7 };   // copied, so const is fine
                m_esChunk.assign(endCode, endCode + 4);
            }
            mpeg2_buffer(m_mpeg, m_esChunk.data(), m_esChunk.data() + m_esChunk.size());
            continue;
        }
        if ((st == STATE_SLICE || st == STATE_END || st == STATE_INVALID_END) && info->display_fbuf && info->sequence) {
            const mpeg2_sequence_t* q = info->sequence;
            yuvToVideo(info->display_fbuf->buf, int(q->width), int(q->height), int(q->chroma_width), int(q->chroma_height));
            shown = true;
        }
        if (st == STATE_END || st == STATE_INVALID_END) { if (m_movEof) break; }
    }
    // audio: decode what has arrived
    size_t used = 0;
    mp3dec_frame_info_t fi;
    int16_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];
    while (m_mp2.size() - used > 4) {
        int n = mp3dec_decode_frame((mp3dec_t*)m_mp3dec, m_mp2.data() + used, int(m_mp2.size() - used), pcm, &fi);
        if (!fi.frame_bytes) break;
        if (fi.frame_offset + fi.frame_bytes > int(m_mp2.size() - used)) break;
        used += size_t(fi.frame_bytes);
        int ch = fi.channels ? fi.channels : 1;
        // resample to 48 kHz mono
        double step = double(fi.hz ? fi.hz : RATE) / RATE;
        for (double p = 0; p < n; p += step) {
            int k = int(p);
            int v = ch == 2 ? (pcm[k * 2] + pcm[k * 2 + 1]) / 2 : pcm[k];
            m_movAudio.push_back(int16_t(v));
        }
    }
    m_mp2.erase(m_mp2.begin(), m_mp2.begin() + long(used));
    while (m_movAudio.size() > RATE) m_movAudio.pop_front();
    if (shown) m_noPicture = 0;
    else if (++m_noPicture > 3 || m_movEof) {
        // finished
        if (m_movieLoop) { playMovie(); return; }
        m_movieState = 0;
        if (m_mpeg) { mpeg2_close(m_mpeg); m_mpeg = nullptr; }
    }
}

// ===========================================================================
// overlays
// ===========================================================================
void GameWaveCore::updateAnims()
{
    int now = nowMs();
    for (auto& [id, o] : m_ovl) {
        for (size_t k = 0; k < o.anims.size();) {
            Anim& a = o.anims[k];
            bool done = false;
            if (now < a.start) { k++; continue; }
            if (a.kind == 1) {
                int t = a.dur > 0 ? std::min(now - a.start, a.dur) : a.dur;
                double f = a.dur > 0 ? double(t) / a.dur : 1.0;
                o.x = a.x0 + int((a.x1 - a.x0) * f);
                o.y = a.y0 + int((a.y1 - a.y0) * f);
                done = f >= 1.0;
            } else if (a.kind == 2) {
                double f = a.dur > 0 ? std::min(1.0, double(now - a.start) / a.dur) : 1.0;
                o.alpha = float(a.mode == 1 ? f : 1.0 - f);
                done = f >= 1.0;
            } else if (a.kind == 3) {
                // steps: {1, frame, ms} show a frame; {3, step, 0} go to a step; {2,..} stop
                for (int guard = 0; guard < 64; guard++) {
                    if (a.step < 0 || a.step >= int(a.steps.size())) { done = true; break; }
                    Step& s = a.steps[size_t(a.step)];
                    if (s.cmd == 1) {
                        if (s.a >= 0 && s.a < int(o.tex.size())) o.frame = s.a;
                        if (now - a.stepStart < s.b) break;
                        a.stepStart += std::max(s.b, 1);
                        if (now - a.stepStart > 1000) a.stepStart = now;
                        a.step++;
                    } else if (s.cmd == 3) a.step = s.a;
                    else { done = true; break; }
                }
            }
            if (done) o.anims.erase(o.anims.begin() + long(k));
            else k++;
        }
    }
}

void GameWaveCore::compose()
{
    m_frameBuf = m_video;
    if (m_frameBuf.size() != size_t(W * H)) m_frameBuf.assign(size_t(W * H), 0xff000000u);
    if (m_osdShown) {
        std::vector<std::pair<int, int>> order;
        for (auto& [id, o] : m_ovl) if (o.visible && o.alpha > 0.0f && !o.tex.empty()) order.push_back({ o.z, id });
        std::stable_sort(order.begin(), order.end());
        for (auto& [z, id] : order) {
            Overlay& o = m_ovl[id];
            int tid = o.tex[size_t(std::clamp(o.frame, 0, int(o.tex.size()) - 1))];
            auto it = m_tex.find(tid);
            if (it == m_tex.end()) continue;
            const Texture& t = it->second;
            int ga = int(o.alpha * 256);
            for (int y = 0; y < t.h; y++) {
                int dy = o.y + y;
                if (dy < 0 || dy >= H) continue;
                const uint32_t* s = &t.px[size_t(y * t.w)];
                uint32_t* d = &m_frameBuf[size_t(dy * W)];
                for (int x = 0; x < t.w; x++) {
                    int dx = o.x + x;
                    if (dx < 0 || dx >= W) continue;
                    uint32_t p = s[x];
                    int a = int(p >> 24) * ga >> 8;
                    if (!a) continue;
                    uint32_t q = d[dx];
                    if (a >= 255) { d[dx] = p | 0xff000000u; continue; }
                    int r = (int((p >> 16) & 255) * a + int((q >> 16) & 255) * (255 - a)) / 255;
                    int g = (int((p >> 8) & 255) * a + int((q >> 8) & 255) * (255 - a)) / 255;
                    int b = (int(p & 255) * a + int(q & 255) * (255 - a)) / 255;
                    d[dx] = 0xff000000u | uint32_t(r) << 16 | uint32_t(g) << 8 | uint32_t(b);
                }
            }
        }
    }
    // 720 x 480 DVD pixels shown as 640 x 480 square pixels
    for (int y = 0; y < OUT_H; y++) {
        const uint32_t* s = &m_frameBuf[size_t(y * W)];
        uint32_t* d = &m_out[size_t(y * OUT_W)];
        for (int x = 0; x < OUT_W; x++) d[x] = s[x * W / OUT_W];
    }
}

void GameWaveCore::mixAudio()
{
    int n = RATE / 30;
    m_audioOut.assign(size_t(n), 0);
    for (int i = 0; i < n; i++) {
        int v = 0;
        if (!m_movAudio.empty()) { v += m_movAudio.front(); m_movAudio.pop_front(); }
        m_audioOut[size_t(i)] = int16_t(std::clamp(v, -32768, 32767));
    }
    for (size_t k = 0; k < m_voices.size();) {
        Voice& vc = m_voices[k];
        auto it = m_sounds.find(vc.snd);
        bool end = it == m_sounds.end();
        if (!end) {
            const Sound& s = it->second;
            double step = double(s.rate) / RATE;
            for (int i = 0; i < n; i++) {
                size_t p = size_t(vc.pos);
                if (p >= s.pcm.size()) { if (vc.loop && !s.pcm.empty()) { vc.pos = 0; p = 0; } else { end = true; break; } }
                int v = m_audioOut[size_t(i)] + s.pcm[p];
                m_audioOut[size_t(i)] = int16_t(std::clamp(v, -32768, 32767));
                vc.pos += step;
            }
        }
        if (end) m_voices.erase(m_voices.begin() + long(k));
        else k++;
    }
}

size_t GameWaveCore::readAudio(int16_t* out, size_t maxSamples)
{
    size_t n = std::min(maxSamples, m_audioOut.size());
    std::memcpy(out, m_audioOut.data(), n * 2);
    m_audioOut.erase(m_audioOut.begin(), m_audioOut.begin() + long(n));
    return n;
}

// ===========================================================================
// script scheduling
// ===========================================================================
int GameWaveCore::yieldFor(lua_State* L, WaitKind kind, int arg)
{
    m_wait = kind;
    m_waitArg = arg;
    m_yielded = true;
    return lua_yield(L, 0);
}

bool GameWaveCore::poll(lua_State*, WaitKind, int)
{
    // a script spinning on a status call gets one look per frame after a few tries
    return ++m_polls > 16;
}

int GameWaveCore::pushKey(lua_State* L)
{
    if (m_keyQ.empty()) { lua_pushnumber(L, 255); lua_pushnumber(L, 255); lua_pushnumber(L, nowMs()); return 3; }
    KeyEv e = m_keyQ.front();
    m_keyQ.pop_front();
    lua_pushnumber(L, e.key); lua_pushnumber(L, e.remote); lua_pushnumber(L, e.time);
    return 3;
}

void GameWaveCore::resume()
{
    if (!m_co || m_dead) return;
    int nargs = 0;
    switch (m_wait) {
    case WAIT_SLEEP: if (nowMs() < m_wakeMs) return; break;
    case WAIT_KEY: if (m_keyQ.empty()) return; nargs = pushKey(m_co); break;
    case WAIT_POLL_KEY: nargs = pushKey(m_co); break;
    case WAIT_POLL_MOVIE: lua_pushnumber(m_co, m_movieState); nargs = 1; break;
    case WAIT_POLL_TIME: lua_pushnumber(m_co, nowMs()); nargs = 1; break;
    case WAIT_POLL_ANIM: { auto it = m_ovl.find(m_waitArg); lua_pushnumber(m_co, it == m_ovl.end() ? 0 : int(it->second.anims.size())); nargs = 1; break; }
    case WAIT_POLL_VIS: { auto it = m_ovl.find(m_waitArg); lua_pushnumber(m_co, it != m_ovl.end() && it->second.visible ? 1 : 0); nargs = 1; break; }
    default: break;
    }
    m_wait = WAIT_NONE;
    m_yielded = false;
    m_polls = 0;
    int r = lua_resume(m_co, nargs);
    if (r != 0) {
        const char* msg = lua_tostring(m_co, -1);
        dbg("script error: %s", msg ? msg : "?");
#ifdef __ANDROID__
        __android_log_print(ANDROID_LOG_WARN, "GameWave", "script error: %s", msg ? msg : "?");
#endif
        m_dead = true;
        return;
    }
    if (!m_yielded) { dbg("script finished"); m_dead = true; }
}

// ===========================================================================
// Lua libraries
// ===========================================================================
#define GW GameWaveCore* c = core(L)

// ---- gl ----
static int gl_SelectOSDMode(lua_State*) { return 0; }
static int gl_BeginScene(lua_State*) { return 0; }
static int gl_EndScene(lua_State*) { return 0; }
static int gl_LoadTexture(lua_State* L) { GW; lua_pushnumber(L, c->loadTexture(c->resolve(argi(L, 1), args(L, 2)))); return 1; }
static int gl_CreateEmptyTexture(lua_State* L) { GW; lua_pushnumber(L, c->newTexture(argi(L, 1, 1), argi(L, 2, 1))); return 1; }
static int gl_FreeTexture(lua_State* L) { GW; c->m_tex.erase(argi(L, 1)); return 0; }
static int gl_SetTextureAlphaLevel(lua_State*) { return 0; }
static int gl_CreateOverlayFromTexture(lua_State* L)
{
    GW;
    int id = c->m_nextOvl++;
    GameWaveCore::Overlay& o = c->m_ovl[id];
    o.tex.push_back(argi(L, 1));
    lua_pushnumber(L, id);
    return 1;
}
static GameWaveCore::Overlay* ovl(lua_State* L, int i = 1)
{
    GW;
    auto it = c->m_ovl.find(argi(L, i, -1));
    return it == c->m_ovl.end() ? nullptr : &it->second;
}
static int gl_FreeOverlay(lua_State* L) { GW; c->m_ovl.erase(argi(L, 1)); return 0; }
static int gl_AddTextureToOverlay(lua_State* L) { if (auto o = ovl(L)) o->tex.push_back(argi(L, 2)); return 0; }
static int gl_RemoveTextureFromOverlay(lua_State* L)
{
    if (auto o = ovl(L)) {
        auto it = std::find(o->tex.begin(), o->tex.end(), argi(L, 2));
        if (it != o->tex.end() && o->tex.size() > 1) o->tex.erase(it);
        o->frame = std::min(o->frame, int(o->tex.size()) - 1);
    }
    return 0;
}
static int gl_SetTextureActiveFrame(lua_State* L) { if (auto o = ovl(L)) o->frame = std::clamp(argi(L, 2), 0, std::max(0, int(o->tex.size()) - 1)); return 0; }
static int gl_SetParameters(lua_State* L)
{
    if (auto o = ovl(L)) { o->x = argi(L, 2); o->y = argi(L, 3); o->z = argi(L, 4); o->visible = argi(L, 5) != 0; }
    return 0;
}
static int gl_SetZorder(lua_State* L) { if (auto o = ovl(L)) o->z = argi(L, 2); return 0; }
static int gl_SetVisibility(lua_State* L) { if (auto o = ovl(L)) o->visible = argi(L, 2) != 0; return 0; }
static int gl_SetPosition(lua_State* L) { if (auto o = ovl(L)) { o->x = argi(L, 2); o->y = argi(L, 3); } return 0; }
static int gl_GetZorder(lua_State* L) { auto o = ovl(L); lua_pushnumber(L, o ? o->z : 0); return 1; }
static int gl_GetVisibility(lua_State* L)
{
    GW;
    if (c->poll(L, GameWaveCore::WAIT_POLL_VIS)) return c->yieldFor(L, GameWaveCore::WAIT_POLL_VIS, argi(L, 1));
    auto o = ovl(L);
    lua_pushnumber(L, o && o->visible ? 1 : 0);
    return 1;
}
static int gl_GetPosition(lua_State* L) { auto o = ovl(L); lua_pushnumber(L, o ? o->x : 0); lua_pushnumber(L, o ? o->y : 0); return 2; }
static int gl_GetSize(lua_State* L)
{
    GW;
    auto o = ovl(L);
    int w = 0, h = 0;
    if (o && !o->tex.empty()) { auto it = c->m_tex.find(o->tex[0]); if (it != c->m_tex.end()) { w = it->second.w; h = it->second.h; } }
    lua_pushnumber(L, w); lua_pushnumber(L, h);
    return 2;
}
static int gl_ClearOSD(lua_State* L) { GW; for (auto& [id, o] : c->m_ovl) o.visible = false; return 0; }
static int gl_Show(lua_State* L) { GW; c->m_osdShown = argi(L, 1, 1) != 0; return 0; }
static int gl_HasAnimations(lua_State* L)
{
    GW;
    if (c->poll(L, GameWaveCore::WAIT_POLL_ANIM)) return c->yieldFor(L, GameWaveCore::WAIT_POLL_ANIM, argi(L, 1));
    auto o = ovl(L);
    lua_pushnumber(L, o ? int(o->anims.size()) : 0);
    return 1;
}
static int gl_DeleteAllAnimations(lua_State* L) { if (auto o = ovl(L)) o->anims.clear(); return 0; }
static int gl_AddPositionAnimation(lua_State* L)
{
    if (auto o = ovl(L)) {
        GameWaveCore::Anim a;
        a.kind = 1; a.x0 = argi(L, 2); a.y0 = argi(L, 3); a.x1 = argi(L, 4); a.y1 = argi(L, 5);
        a.start = argi(L, 6); a.dur = argi(L, 7);
        o->anims.push_back(a);
    }
    return 0;
}
static int gl_AddAlphaAnimation(lua_State* L)
{
    if (auto o = ovl(L)) {
        GameWaveCore::Anim a;
        a.kind = 2; a.start = argi(L, 2); a.mode = argi(L, 3); a.dur = argi(L, 4);
        o->anims.push_back(a);
    }
    return 0;
}
static int gl_AddVisibilityAnimation(lua_State* L) { if (auto o = ovl(L)) o->visible = argi(L, 2) != 0; return 0; }
static int gl_CreateTextureAnimation(lua_State* L)
{
    GW;
    auto o = ovl(L);
    if (!o || !lua_istable(L, 3)) return 0;
    GameWaveCore::Anim a;
    a.kind = 3;
    a.start = c->nowMs(); a.stepStart = a.start;
    int n = luaL_getn(L, 3);
    for (int i = 1; i <= n; i++) {
        lua_rawgeti(L, 3, i);
        if (lua_istable(L, -1)) {
            GameWaveCore::Step s;
            lua_rawgeti(L, -1, 1); s.cmd = argi(L, -1); lua_pop(L, 1);
            lua_rawgeti(L, -1, 2); s.a = argi(L, -1); lua_pop(L, 1);
            lua_rawgeti(L, -1, 3); s.b = argi(L, -1); lua_pop(L, 1);
            a.steps.push_back(s);
        }
        lua_pop(L, 1);
    }
    o->anims.push_back(a);
    return 0;
}
static int gl_BlitOverlay(lua_State*) { return 0; }
static int gl_SetClipInfo(lua_State*) { return 0; }

// ---- text / font ----
static int font_Load(lua_State* L) { GW; lua_pushnumber(L, c->loadFont(c->resolve(argi(L, 1), args(L, 2)))); return 1; }
static int font_Free(lua_State*) { return 0; }
static int font_GetBuiltinFontID(lua_State* L) { GW; lua_pushnumber(L, c->m_builtinFont); return 1; }
static int text_Render(lua_State* L)
{
    GW;
    int top = lua_gettop(L);
    int align = top >= 9 ? argi(L, 9) : 0;
    uint32_t color = 0xffffffffu;
    if (top >= 13) color = ycc(argi(L, 11), argi(L, 12), argi(L, 13));
    int tex = c->renderText(args(L, 1), argi(L, 2), argi(L, 3, 100), argi(L, 4, 20), align, color);
    int oid = c->m_nextOvl++;
    c->m_ovl[oid].tex.push_back(tex);
    int id = c->m_nextText++;
    c->m_texts[id] = { tex, oid };
    lua_pushnumber(L, id);
    return 1;
}
static int text_GetOverlayId(lua_State* L) { GW; auto it = c->m_texts.find(argi(L, 1)); lua_pushnumber(L, it == c->m_texts.end() ? -1 : it->second.second); return 1; }
static int text_Remove(lua_State* L)
{
    GW;
    auto it = c->m_texts.find(argi(L, 1));
    if (it != c->m_texts.end()) { c->m_tex.erase(it->second.first); c->m_ovl.erase(it->second.second); c->m_texts.erase(it); }
    return 0;
}

// ---- iframe / movie ----
static int iframe_Load(lua_State* L) { GW; lua_pushnumber(L, c->loadIframe(c->resolve(argi(L, 1), args(L, 2)))); return 1; }
static int iframe_Show(lua_State* L) { GW; c->showIframe(argi(L, 1)); return 0; }
static int iframe_Unload(lua_State* L) { GW; c->m_iframes.erase(argi(L, 1)); return 0; }
// 0 when the movie is ready (the scripts assert on it); it becomes the one Play() starts
static int movie_Load(lua_State* L) { GW; lua_pushnumber(L, c->loadMovie(args(L, 1)) > 0 ? 0 : -1); return 1; }
static int movie_Play(lua_State* L) { GW; c->playMovie(); return 0; }
static int movie_Stop(lua_State* L) { GW; c->stopMovie(); return 0; }
static int movie_SetLoop(lua_State* L) { GW; c->m_movieLoop = argi(L, 1) != 0; return 0; }
static int movie_GetState(lua_State* L)
{
    GW;
    if (c->poll(L, GameWaveCore::WAIT_POLL_MOVIE)) return c->yieldFor(L, GameWaveCore::WAIT_POLL_MOVIE);
    lua_pushnumber(L, c->m_movieState);
    return 1;
}
static int movie_Unload(lua_State* L) { GW; c->m_movies.erase(argi(L, 1)); return 0; }

// ---- audio ----
static int audio_Load(lua_State* L) { GW; lua_pushnumber(L, c->loadSound(c->resolve(argi(L, 1), args(L, 2)))); return 1; }
static int audio_Play(lua_State* L)
{
    GW;
    int id = argi(L, 1);
    if (c->m_sounds.count(id)) c->m_voices.push_back({ id, 0.0, argi(L, 2) != 0 });
    return 0;
}
static int audio_Stop(lua_State* L)
{
    GW;
    int id = argi(L, 1);
    c->m_voices.erase(std::remove_if(c->m_voices.begin(), c->m_voices.end(), [&](const GameWaveCore::Voice& v) { return v.snd == id; }), c->m_voices.end());
    return 0;
}
static int audio_Unload(lua_State* L) { audio_Stop(L); GW; c->m_sounds.erase(argi(L, 1)); return 0; }

// ---- input ----
static int input_GetKey(lua_State* L)
{
    GW;
    if (c->poll(L, GameWaveCore::WAIT_POLL_KEY)) return c->yieldFor(L, GameWaveCore::WAIT_POLL_KEY);
    return c->pushKey(L);
}
static int input_WaitForKey(lua_State* L) { GW; return c->yieldFor(L, GameWaveCore::WAIT_KEY); }
static int input_ClearKeyQueue(lua_State* L);
static int input_EnableRemotes(lua_State* L) { GW; c->dbg("EnableRemotes"); c->m_remotesOn = true; return 0; }
static int input_DisableRemotes(lua_State* L) { GW; c->dbg("DisableRemotes"); c->m_remotesOn = false; return 0; }
static int input_SetMode(lua_State*) { return 0; }
static int input_SetRandomKeysTable(lua_State*) { return 0; }

// ---- time / math / engine / rm / zfile / log ----
static int time_GetRealTime(lua_State* L)
{
    GW;
    if (c->poll(L, GameWaveCore::WAIT_POLL_TIME)) return c->yieldFor(L, GameWaveCore::WAIT_POLL_TIME);
    lua_pushnumber(L, c->nowMs());
    return 1;
}
static int zmath_Mod(lua_State* L) { int a = argi(L, 1), b = argi(L, 2); lua_pushnumber(L, b ? ((a % b) + b) % b : 0); return 1; }
static int zmath_Rand(lua_State* L)
{
    GW;
    int lo = argi(L, 1), hi = argi(L, 2, lo + 1);
    c->m_rand = c->m_rand * 1103515245u + 12345u;
    int span = hi - lo;
    lua_pushnumber(L, span > 0 ? lo + int((c->m_rand >> 8) % uint32_t(span)) : lo);
    return 1;
}
static int zmath_RandSeed(lua_State* L) { GW; c->m_rand = uint32_t(argi(L, 1)) * 2654435761u + 1; return 0; }
static int engine_Version(lua_State* L) { GW; lua_pushstring(L, c->m_engineVersion.c_str()); return 1; }
static int engine_Mem(lua_State* L) { lua_pushnumber(L, 8 * 1024 * 1024); return 1; }
static int engine_Nop(lua_State*) { return 0; }
static int rm_OpenResource(lua_State* L) { GW; c->m_res.push_back(args(L, 1)); lua_pushnumber(L, int(c->m_res.size())); return 1; }
static int rm_CloseResource(lua_State*) { return 0; }
static int rm_LoadFile(lua_State* L)
{
    GW;
    GameWaveCore::FileEnt f;
    std::vector<uint8_t> d;
    if (!c->findFile(c->resolve(argi(L, 1), args(L, 2)), f) || !c->readFile(f, d)) { lua_pushnil(L); return 1; }
    lua_pushlstring(L, (const char*)d.data(), d.size());
    return 1;
}
static int zfile_OpenFile(lua_State* L)
{
    GW;
    GameWaveCore::FileEnt f;
    std::string p = c->resolve(argi(L, 3), args(L, 1));
    int id = -1;
    std::vector<uint8_t> d;
    if (c->findFile(p, f) && c->readFile(f, d)) { id = c->m_nextZFile++; c->m_zfiles[id].data = std::move(d); }
    lua_pushnumber(L, id);
    return 1;
}
static int zfile_GetFileSize(lua_State* L) { GW; auto it = c->m_zfiles.find(argi(L, 1)); lua_pushnumber(L, it == c->m_zfiles.end() ? 0 : int(it->second.data.size())); return 1; }
static int zfile_ReadBytes(lua_State* L)
{
    GW;
    auto it = c->m_zfiles.find(argi(L, 1));
    if (it == c->m_zfiles.end()) { lua_pushstring(L, ""); return 1; }
    size_t off = size_t(std::max(0, argi(L, 2))), n = size_t(std::max(0, argi(L, 3)));
    const auto& d = it->second.data;
    off = std::min(off, d.size()); n = std::min(n, d.size() - off);
    lua_pushlstring(L, (const char*)d.data() + off, n);
    return 1;
}
static int zfile_CloseFile(lua_State* L) { GW; c->m_zfiles.erase(argi(L, 1)); return 0; }
static int log_Print(lua_State* L) { GW; const char* s = lua_tostring(L, lua_gettop(L) >= 2 && lua_isstring(L, 2) && !lua_isnumber(L, 1) ? 1 : lua_gettop(L) >= 2 ? 2 : 1); if (s) c->dbg("lua: %s", s); return 0; }
static int log_Nop(lua_State*) { return 0; }
static int pointer_ToString(lua_State* L) { char b[32]; snprintf(b, sizeof b, "%p", lua_topointer(L, 1)); lua_pushstring(L, b); return 1; }

static int time_Sleep(lua_State* L)
{
    GW;
    c->sleepUntil(c->nowMs() + std::max(0, argi(L, 1)));
    return c->yieldFor(L, GameWaveCore::WAIT_SLEEP);
}

static const luaL_reg gl_lib[] = {
    { "SelectOSDMode", gl_SelectOSDMode }, { "BeginScene", gl_BeginScene }, { "EndScene", gl_EndScene },
    { "LoadTexture", gl_LoadTexture }, { "CreateEmptyTexture", gl_CreateEmptyTexture }, { "FreeTexture", gl_FreeTexture },
    { "SetTextureAlphaLevel", gl_SetTextureAlphaLevel }, { "CreateOverlayFromTexture", gl_CreateOverlayFromTexture },
    { "FreeOverlay", gl_FreeOverlay }, { "AddTextureToOverlay", gl_AddTextureToOverlay },
    { "RemoveTextureFromOverlay", gl_RemoveTextureFromOverlay }, { "SetTextureActiveFrame", gl_SetTextureActiveFrame },
    { "SetParameters", gl_SetParameters }, { "SetClipInfo", gl_SetClipInfo }, { "SetZorder", gl_SetZorder },
    { "SetVisibility", gl_SetVisibility }, { "SetPosition", gl_SetPosition }, { "GetZorder", gl_GetZorder },
    { "GetVisibility", gl_GetVisibility }, { "GetPosition", gl_GetPosition }, { "GetSize", gl_GetSize },
    { "ClearOSD", gl_ClearOSD }, { "Show", gl_Show }, { "HasAnimations", gl_HasAnimations },
    { "DeleteAllAnimations", gl_DeleteAllAnimations }, { "AddPositionAnimation", gl_AddPositionAnimation },
    { "AddVisibilityAnimation", gl_AddVisibilityAnimation }, { "AddAlphaAnimation", gl_AddAlphaAnimation },
    { "CreateTextureAnimation", gl_CreateTextureAnimation }, { "BlitOverlay", gl_BlitOverlay },
    { nullptr, nullptr } };
static const luaL_reg font_lib[] = { { "Load", font_Load }, { "Free", font_Free }, { "GetBuiltinFontID", font_GetBuiltinFontID }, { nullptr, nullptr } };
static const luaL_reg text_lib[] = { { "Render", text_Render }, { "GetOverlayId", text_GetOverlayId }, { "Remove", text_Remove }, { nullptr, nullptr } };
static const luaL_reg iframe_lib[] = { { "Load", iframe_Load }, { "Show", iframe_Show }, { "Unload", iframe_Unload }, { nullptr, nullptr } };
static const luaL_reg movie_lib[] = { { "Load", movie_Load }, { "Play", movie_Play }, { "Stop", movie_Stop }, { "SetLoop", movie_SetLoop },
    { "GetState", movie_GetState }, { "Unload", movie_Unload }, { nullptr, nullptr } };
static const luaL_reg audio_lib[] = { { "Load", audio_Load }, { "Play", audio_Play }, { "Stop", audio_Stop }, { "Unload", audio_Unload }, { nullptr, nullptr } };
static const luaL_reg input_lib[] = { { "GetKey", input_GetKey }, { "WaitForKey", input_WaitForKey }, { "ClearKeyQueue", input_ClearKeyQueue },
    { "EnableRemotes", input_EnableRemotes }, { "DisableRemotes", input_DisableRemotes }, { "SetMode", input_SetMode },
    { "SetRandomKeysTable", input_SetRandomKeysTable }, { nullptr, nullptr } };
static const luaL_reg time_lib[] = { { "GetRealTime", time_GetRealTime }, { "Sleep", time_Sleep }, { nullptr, nullptr } };
static const luaL_reg zmath_lib[] = { { "Mod", zmath_Mod }, { "Rand", zmath_Rand }, { "RandSeed", zmath_RandSeed }, { nullptr, nullptr } };
static const luaL_reg engine_lib[] = { { "Version", engine_Version }, { "ZMM_GetTotalAllocMemory", engine_Mem }, { "ZMM_GetTotalFreeMemory", engine_Mem },
    { "ZMM_GetMaxFreeMemory", engine_Mem }, { "SetLineTraceMode", engine_Nop }, { nullptr, nullptr } };
static const luaL_reg rm_lib[] = { { "OpenResource", rm_OpenResource }, { "CloseResource", rm_CloseResource }, { "LoadFile", rm_LoadFile }, { "UnloadFile", log_Nop }, { nullptr, nullptr } };
static const luaL_reg zfile_lib[] = { { "OpenFile", zfile_OpenFile }, { "GetFileSize", zfile_GetFileSize }, { "ReadBytes", zfile_ReadBytes }, { "CloseFile", zfile_CloseFile }, { nullptr, nullptr } };
static const luaL_reg log_lib[] = { { "PrintLine", log_Print }, { "Log", log_Print }, { "PrintRaw", log_Print }, { "SetLevel", log_Nop }, { "DebugSetState", log_Nop }, { nullptr, nullptr } };
static const luaL_reg pointer_lib[] = { { "ToString", pointer_ToString }, { nullptr, nullptr } };

// ===========================================================================
// core
// ===========================================================================
GameWaveCore::GameWaveCore()
{
    m_video.assign(size_t(W * H), 0xff000000u);
    m_out.assign(size_t(OUT_W * OUT_H), 0xff000000u);
    m_debug = getenv("GWDBG") != nullptr;
}

GameWaveCore::~GameWaveCore()
{
    if (m_L) lua_close(m_L);
    if (m_mpeg) mpeg2_close(m_mpeg);
    delete (mp3dec_t*)m_mp3dec;
}

bool GameWaveCore::load(const std::vector<RomFile>& roms, std::string& error)
{
    std::string disc;
    for (auto& r : roms) if (!r.path.empty()) disc = r.path;
    if (disc.empty()) { error = "Pick a Game Wave disc (.iso, or a .zip holding one)"; return false; }
    if (!m_cd.open(disc, error)) return false;
    // volume descriptors: prefer Joliet (long names), else the primary
    uint8_t raw[2352];
    uint32_t rootLba = 0, rootSize = 0;
    bool joliet = false;
    for (uint32_t s = 16; s < 32; s++) {
        if (!m_cd.readRaw(s, raw)) break;
        const uint8_t* v = raw + 16;
        if (v[0] == 255) break;
        bool isJ = v[0] == 2 && v[88] == '%' && v[89] == '/' && (v[90] == '@' || v[90] == 'C' || v[90] == 'E');
        if (v[0] == 1 || isJ) {
            if (v[0] == 1 && joliet) continue;
            const uint8_t* r = v + 156;
            rootLba = uint32_t(r[2]) | uint32_t(r[3]) << 8 | uint32_t(r[4]) << 16 | uint32_t(r[5]) << 24;
            rootSize = uint32_t(r[10]) | uint32_t(r[11]) << 8 | uint32_t(r[12]) << 16 | uint32_t(r[13]) << 24;
            joliet = isJ;
        }
    }
    if (!rootLba) { error = "Not a DVD/CD image (no ISO 9660 file system)"; return false; }
    scanDir(rootLba, rootSize, "", joliet, 0);
    // gamewave.diz names the game's script (appfile=) and engine version
    FileEnt f;
    std::string appfile = "/data/game.zbc";
    std::vector<uint8_t> diz;
    if (findFile("/gamewave.diz", f) && readFile(f, diz)) {
        std::string t(diz.begin(), diz.end());
        auto val = [&](const char* key) { size_t p = t.find(key); if (p == std::string::npos) return std::string(); p += strlen(key); size_t e = t.find_first_of("\r\n", p); return t.substr(p, e - p); };
        if (!val("appfile=").empty()) appfile = val("appfile=");
        size_t plat = t.find("[platform]");
        if (plat != std::string::npos) { size_t p = t.find("version=", plat); if (p != std::string::npos) { size_t e = t.find_first_of("\r\n", p); m_engineVersion = t.substr(p + 8, e - p - 8); } }
    } else { error = "This isn't a Game Wave disc (gamewave.diz is missing)"; return false; }
    std::vector<uint8_t> zbc;
    if (!findFile(appfile, f) || !readFile(f, zbc) || zbc.size() < 16) { error = "The game's script " + appfile + " is missing"; return false; }
    // .zbc: "\x1bZCS\n\x1a" header, sizes at 8 and 12, zlib data from 0x10
    if (std::memcmp(zbc.data(), "\x1bZCS\x0a\x1a", 6) == 0) {
        uint32_t usize = uint32_t(zbc[8]) | uint32_t(zbc[9]) << 8 | uint32_t(zbc[10]) << 16 | uint32_t(zbc[11]) << 24;
        if (!inflateAll(zbc.data() + 16, zbc.size() - 16, m_script, usize)) { error = "Can't unpack the game's script"; return false; }
    } else m_script = zbc;
    reset();
    return true;
}

static int input_ClearKeyQueue(lua_State* L) { core(L)->clearKeys(); return 0; }

struct ChunkReader { const std::vector<uint8_t>* d; bool done; };
static const char* readChunk(lua_State*, void* ud, unsigned int* sz)
{
    ChunkReader* r = (ChunkReader*)ud;
    if (r->done) { *sz = 0; return nullptr; }
    r->done = true;
    *sz = (unsigned int)r->d->size();
    return (const char*)r->d->data();
}

void GameWaveCore::startScript()
{
    if (m_L) { lua_close(m_L); m_L = nullptr; m_co = nullptr; }
    m_L = lua_open();
    lua_State* L = m_L;
    luaopen_base(L); lua_settop(L, 0);
    luaopen_table(L); lua_settop(L, 0);
    luaopen_string(L); lua_settop(L, 0);
    luaopen_math(L); lua_settop(L, 0);
    luaopen_debug(L); lua_settop(L, 0);
    lua_pushstring(L, "pocketplug.gamewave");
    lua_pushlightuserdata(L, this);
    lua_rawset(L, LUA_REGISTRYINDEX);
    luaL_openlib(L, "gl", gl_lib, 0); lua_settop(L, 0);
    luaL_openlib(L, "font", font_lib, 0); lua_settop(L, 0);
    luaL_openlib(L, "text", text_lib, 0); lua_settop(L, 0);
    luaL_openlib(L, "iframe", iframe_lib, 0); lua_settop(L, 0);
    luaL_openlib(L, "movie", movie_lib, 0); lua_settop(L, 0);
    luaL_openlib(L, "audio", audio_lib, 0); lua_settop(L, 0);
    luaL_openlib(L, "input", input_lib, 0); lua_settop(L, 0);
    luaL_openlib(L, "time", time_lib, 0); lua_settop(L, 0);
    luaL_openlib(L, "zmath", zmath_lib, 0); lua_settop(L, 0);
    luaL_openlib(L, "engine", engine_lib, 0); lua_settop(L, 0);
    luaL_openlib(L, "rm", rm_lib, 0); lua_settop(L, 0);
    luaL_openlib(L, "zfile", zfile_lib, 0); lua_settop(L, 0);
    luaL_openlib(L, "log", log_lib, 0); lua_settop(L, 0);
    luaL_openlib(L, "pointer", pointer_lib, 0); lua_settop(L, 0);
    m_co = lua_newthread(L);
    m_coRef = luaL_ref(L, LUA_REGISTRYINDEX);          // keep the thread alive
    ChunkReader rd{ &m_script, false };
    if (lua_load(m_co, readChunk, &rd, "=game") != 0) {
        const char* msg = lua_tostring(m_co, -1);
        dbg("script load error: %s", msg ? msg : "?");
        m_dead = true;
        return;
    }
    m_dead = false;
    m_wait = WAIT_NONE;
}

void GameWaveCore::reset()
{
    m_tex.clear(); m_ovl.clear(); m_fonts.clear(); m_texts.clear(); m_sounds.clear(); m_voices.clear();
    m_iframes.clear(); m_movies.clear(); m_zfiles.clear(); m_res.clear();
    m_nextTex = m_nextOvl = m_nextFont = m_nextText = m_nextSound = m_nextIframe = m_nextMovie = m_nextZFile = 1;
    m_builtinFont = 0;
    m_osdShown = true; m_remotesOn = true; m_movieLoop = false;
    stopMovie();
    m_frame = 0; m_keyQ.clear(); m_lastKeys = 0;
    m_video.assign(size_t(W * H), 0xff000000u);
    m_audioOut.clear();
    m_started = false;
    m_dead = false;
}

void GameWaveCore::runFrame()
{
    if (!m_started) { m_started = true; startScript(); }
    // remote presses (one remote at a time; its colour in bits 24..26)
    uint32_t keys = m_keys & 0x1fffff;
    uint32_t fresh = keys & ~(m_lastKeys & 0x1fffff);
    int remote = int((m_keys >> REMOTE_SHIFT) & 7) + 1;
    if (remote > 6) remote = 1;
    for (int k = 0; k <= 20; k++)
        if (fresh & (1u << k)) {
            dbg("key %d remote %d%s", k, remote, m_remotesOn ? "" : " (remotes off)");
            if (m_remotesOn) m_keyQ.push_back({ k, remote, nowMs() });
        }
    while (m_keyQ.size() > 32) m_keyQ.pop_front();
    m_lastKeys = m_keys;

    resume();
    movieFrame();
    updateAnims();
    compose();
    mixAudio();
    m_frame++;
}
