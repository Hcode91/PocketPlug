#include "cdimage.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <sys/stat.h>
#include <zlib.h>

static std::string lower(std::string s) { for (auto& c : s) c = char(std::tolower((unsigned char)c)); return s; }
static std::string dirOf(const std::string& p) { size_t k = p.find_last_of('/'); return k == std::string::npos ? "" : p.substr(0, k + 1); }
static uint64_t fileSize(const std::string& p) { struct stat st; return stat(p.c_str(), &st) == 0 ? uint64_t(st.st_size) : 0; }

void CdImage::close()
{
    for (auto& f : m_files) if (f.second) fclose(f.second);
    m_files.clear();
    m_tracks.clear();
}

FILE* CdImage::fileFor(const std::string& name)
{
    for (auto& f : m_files) if (f.first == name) return f.second;
    FILE* f = fopen(name.c_str(), "rb");
    m_files.push_back({ name, f });
    return f;
}

uint32_t CdImage::totalFrames() const
{
    return m_tracks.empty() ? 0 : m_tracks.back().start + m_tracks.back().frames;
}

int CdImage::trackAt(uint32_t lba) const
{
    for (int i = numTracks() - 1; i >= 0; i--) if (lba >= m_tracks[size_t(i)].start) return i;
    return 0;
}

static void lbaToMsf(uint32_t lba, uint8_t* o)
{
    lba += 150;
    auto bcd = [](uint32_t v) { return uint8_t((v / 10) << 4 | (v % 10)); };
    o[0] = bcd(lba / 75 / 60); o[1] = bcd(lba / 75 % 60); o[2] = bcd(lba % 75);
}

bool CdImage::readRaw(uint32_t lba, uint8_t* out)
{
    std::memset(out, 0, 2352);
    if (m_tracks.empty()) return false;
    const Track& t = m_tracks[size_t(trackAt(lba))];
    if (lba >= t.start + t.frames) return false;
    FILE* f = fileFor(t.file);
    if (!f) return false;
    uint64_t off = t.fileOffset + uint64_t(lba - t.start) * uint64_t(t.sectorSize);
    if (fseeko(f, off_t(off), SEEK_SET)) return false;
    if (t.sectorSize == 2048) {
        // build the raw sector around the user data: sync, address, mode 1
        static const uint8_t sync[12] = { 0, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0 };
        std::memcpy(out, sync, 12);
        lbaToMsf(lba, out + 12);
        out[15] = 1;
        return fread(out + 16, 1, 2048, f) == 2048;
    }
    return fread(out, 1, 2352, f) == 2352;
}

bool CdImage::parseCue(const std::string& cuePath, const std::string& text, std::string& error)
{
    std::string dir = dirOf(cuePath), file;
    uint32_t fileBase = 0;          // first sector of the current file
    struct Pending { Track t; uint32_t idx1 = 0; bool have = false; };
    std::vector<Pending> tracks;
    size_t p = 0;
    while (p < text.size()) {
        size_t e = text.find('\n', p);
        if (e == std::string::npos) e = text.size();
        std::string line = text.substr(p, e - p);
        p = e + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        size_t b = line.find_first_not_of(" \t");
        if (b == std::string::npos) continue;
        line = line.substr(b);
        std::string kw = lower(line.substr(0, line.find(' ')));
        if (kw == "file") {
            size_t q1 = line.find('"'), q2 = line.rfind('"');
            std::string name = (q1 != std::string::npos && q2 > q1) ? line.substr(q1 + 1, q2 - q1 - 1) : line.substr(5, line.rfind(' ') - 5);
            // close the length of the previous file's last track
            file = dir + name;
            if (!fileSize(file)) {
                // try a case-insensitive match in the folder
                error = "Missing track file: " + name;
                return false;
            }
            if (!tracks.empty()) {
                Pending& last = tracks.back();
                uint64_t sz = fileSize(last.t.file);
                last.t.frames = uint32_t((sz - last.t.fileOffset) / uint64_t(last.t.sectorSize));
                fileBase = last.t.start + last.t.frames;
            }
        } else if (kw == "track") {
            Pending pt;
            std::string mode = lower(line.substr(line.rfind(' ') + 1));
            pt.t.audio = mode == "audio";
            pt.t.sectorSize = (mode == "mode1/2048") ? 2048 : 2352;
            pt.t.file = file;
            tracks.push_back(pt);
        } else if (kw == "index" && !tracks.empty()) {
            int n = 0, m = 0, s = 0, f = 0;
            if (sscanf(line.c_str() + 5, "%d %d:%d:%d", &n, &m, &s, &f) == 4 && n == 1) {
                Pending& cur = tracks.back();
                uint32_t rel = uint32_t((m * 60 + s) * 75 + f);
                cur.t.fileOffset = uint64_t(rel) * uint64_t(cur.t.sectorSize);
                cur.t.start = fileBase + rel;
                cur.have = true;
                // the previous track in the same file ends here
                if (tracks.size() > 1) {
                    Pending& prev = tracks[tracks.size() - 2];
                    if (prev.t.file == cur.t.file) prev.t.frames = cur.t.start - prev.t.start;
                }
            }
        }
    }
    if (tracks.empty()) { error = "The .cue lists no tracks"; return false; }
    Pending& last = tracks.back();
    last.t.frames = uint32_t((fileSize(last.t.file) - last.t.fileOffset) / uint64_t(last.t.sectorSize));
    for (auto& t : tracks) if (t.have) m_tracks.push_back(t.t);
    if (m_tracks.empty()) { error = "The .cue has no INDEX 01"; return false; }
    return true;
}

// Reads the zip's central directory; stored entries are used in place, compressed ones are
// unpacked once into "<zip>.d/". Returns the .cue text in cue (empty: none in the zip).
bool CdImage::unzip(const std::string& zipPath, std::string& cue, std::string& error)
{
    FILE* f = fopen(zipPath.c_str(), "rb");
    if (!f) { error = "Can't open " + zipPath; return false; }
    uint64_t size = fileSize(zipPath);
    // end of central directory (also ZIP64)
    std::vector<uint8_t> tail(size_t(std::min<uint64_t>(size, 0x10000 + 22)));
    fseeko(f, off_t(size - tail.size()), SEEK_SET);
    if (fread(tail.data(), 1, tail.size(), f) != tail.size()) { fclose(f); error = "Can't read the zip"; return false; }
    auto u16 = [](const uint8_t* p) { return uint32_t(p[0] | p[1] << 8); };
    auto u32 = [](const uint8_t* p) { return uint32_t(p[0] | p[1] << 8 | p[2] << 16 | uint32_t(p[3]) << 24); };
    auto u64 = [&](const uint8_t* p) { return uint64_t(u32(p)) | uint64_t(u32(p + 4)) << 32; };
    long eocd = -1;
    for (long i = long(tail.size()) - 22; i >= 0; i--) if (u32(&tail[size_t(i)]) == 0x06054b50) { eocd = i; break; }
    if (eocd < 0) { fclose(f); error = "Not a zip"; return false; }
    uint64_t cdOff = u32(&tail[size_t(eocd) + 16]), cdCount = u16(&tail[size_t(eocd) + 10]);
    if (eocd >= 20 && u32(&tail[size_t(eocd) - 20]) == 0x07064b50) {
        uint64_t z64 = u64(&tail[size_t(eocd) - 12]);
        uint8_t r[56];
        fseeko(f, off_t(z64), SEEK_SET);
        if (fread(r, 1, 56, f) == 56 && u32(r) == 0x06064b50) { cdCount = u64(r + 32); cdOff = u64(r + 48); }
    }
    struct Entry { std::string name; uint32_t method; uint64_t csize, usize, local; };
    std::vector<Entry> entries;
    fseeko(f, off_t(cdOff), SEEK_SET);
    for (uint64_t i = 0; i < cdCount; i++) {
        uint8_t h[46];
        if (fread(h, 1, 46, f) != 46 || u32(h) != 0x02014b50) break;
        Entry e;
        e.method = u16(h + 10);
        e.csize = u32(h + 20); e.usize = u32(h + 24); e.local = u32(h + 42);
        uint32_t nl = u16(h + 28), xl = u16(h + 30), cl = u16(h + 32);
        e.name.resize(nl);
        if (fread(&e.name[0], 1, nl, f) != nl) break;
        std::vector<uint8_t> extra(xl);
        if (xl && fread(extra.data(), 1, xl, f) != xl) break;
        for (size_t k = 0; k + 4 <= extra.size();) {
            uint32_t id = u16(&extra[k]), len = u16(&extra[k + 2]);
            if (id == 1) {
                size_t q = k + 4;
                if (e.usize == 0xffffffffu && q + 8 <= extra.size()) { e.usize = u64(&extra[q]); q += 8; }
                if (e.csize == 0xffffffffu && q + 8 <= extra.size()) { e.csize = u64(&extra[q]); q += 8; }
                if (e.local == 0xffffffffu && q + 8 <= extra.size()) { e.local = u64(&extra[q]); q += 8; }
            }
            k += 4 + len;
        }
        fseeko(f, off_t(cl), SEEK_CUR);
        if (!e.name.empty() && e.name.back() != '/') entries.push_back(e);
    }
    std::string outDir = zipPath + ".d/";
    mkdir(outDir.c_str(), 0755);
    std::string cueName;
    // place of each entry's data: in the zip (stored) or an unpacked file
    for (auto& e : entries) {
        std::string base = e.name.substr(e.name.find_last_of('/') + 1);
        std::string ext = lower(base.substr(base.find_last_of('.') + 1));
        uint8_t lh[30];
        fseeko(f, off_t(e.local), SEEK_SET);
        if (fread(lh, 1, 30, f) != 30 || u32(lh) != 0x04034b50) continue;
        uint64_t dataAt = e.local + 30 + u16(lh + 26) + u16(lh + 28);
        if (ext == "cue") {
            std::vector<char> buf(size_t(e.usize));
            if (e.method == 0) { fseeko(f, off_t(dataAt), SEEK_SET); if (fread(buf.data(), 1, buf.size(), f) != buf.size()) continue; }
            else {
                std::vector<uint8_t> in(size_t(e.csize));
                fseeko(f, off_t(dataAt), SEEK_SET);
                if (fread(in.data(), 1, in.size(), f) != in.size()) continue;
                uLongf n = uLongf(buf.size());
                z_stream z{}; inflateInit2(&z, -MAX_WBITS);
                z.next_in = in.data(); z.avail_in = uInt(in.size()); z.next_out = (Bytef*)buf.data(); z.avail_out = uInt(n);
                inflate(&z, Z_FINISH); inflateEnd(&z);
            }
            cue.assign(buf.begin(), buf.end());
            cueName = base;
            continue;
        }
        std::string out = outDir + base;
        if (fileSize(out) == e.usize) continue;     // unpacked before
        if (e.method != 8 && e.method != 0) continue;
        FILE* o = fopen((out + ".part").c_str(), "wb");
        if (!o) { fclose(f); error = "Can't unpack the zip (no space?)"; return false; }
        z_stream z{}; inflateInit2(&z, -MAX_WBITS);
        std::vector<uint8_t> in(1 << 20), ob(1 << 20);
        uint64_t left = e.csize;
        fseeko(f, off_t(dataAt), SEEK_SET);
        int r = Z_OK;
        if (e.method == 0) {                         // stored: a plain copy
            while (left) {
                size_t k = size_t(std::min<uint64_t>(left, in.size()));
                if (fread(in.data(), 1, k, f) != k || fwrite(in.data(), 1, k, o) != k) break;
                left -= k;
            }
            r = left ? Z_ERRNO : Z_STREAM_END;
        }
        while (r != Z_STREAM_END && r != Z_ERRNO) {
            if (!z.avail_in && left) {
                size_t k = size_t(std::min<uint64_t>(left, in.size()));
                if (fread(in.data(), 1, k, f) != k) break;
                left -= k; z.next_in = in.data(); z.avail_in = uInt(k);
            }
            z.next_out = ob.data(); z.avail_out = uInt(ob.size());
            r = inflate(&z, Z_NO_FLUSH);
            if (r != Z_OK && r != Z_STREAM_END) break;
            if (fwrite(ob.data(), 1, ob.size() - z.avail_out, o) != ob.size() - z.avail_out) { r = Z_ERRNO; break; }
            if (!z.avail_in && !left && r != Z_STREAM_END && z.avail_out) break;
        }
        inflateEnd(&z);
        fclose(o);
        if (r != Z_STREAM_END) { remove((out + ".part").c_str()); fclose(f); error = "Can't unpack " + base + " (no space?)"; return false; }
        rename((out + ".part").c_str(), out.c_str());
    }
    fclose(f);
    if (cue.empty()) {
        // no .cue: one track from the single image in the zip
        for (auto& e : entries) {
            std::string base = e.name.substr(e.name.find_last_of('/') + 1);
            std::string ext = lower(base.substr(base.find_last_of('.') + 1));
            if (ext == "bin" || ext == "iso" || ext == "img")
                cue = "FILE \"" + base + "\" BINARY\n  TRACK 01 " + (ext == "iso" ? "MODE1/2048" : "MODE1/2352") + "\n    INDEX 01 00:00:00\n";
        }
    }
    return true;
}

bool CdImage::open(const std::string& path, std::string& error)
{
    close();
    std::string ext = lower(path.substr(path.find_last_of('.') + 1));
    if (ext == "zip") {
        std::string cue;
        if (!unzip(path, cue, error)) return false;
        if (cue.empty()) { error = "No disc image (.cue/.bin/.iso) in the zip"; return false; }
        std::string dir = path + ".d/";
        return parseCue(dir + "disc.cue", cue, error);
    }
    if (ext == "cue") {
        FILE* f = fopen(path.c_str(), "rb");
        if (!f) { error = "Can't open " + path; return false; }
        std::string text;
        char buf[4096]; size_t n;
        while ((n = fread(buf, 1, sizeof buf, f)) > 0) text.append(buf, n);
        fclose(f);
        return parseCue(path, text, error);
    }
    // a bare image: one data track
    Track t;
    t.file = path;
    t.sectorSize = (ext == "iso" || fileSize(path) % 2352) ? 2048 : 2352;
    t.frames = uint32_t(fileSize(path) / uint64_t(t.sectorSize));
    if (!t.frames) { error = "The disc image is empty"; return false; }
    m_tracks.push_back(t);
    return true;
}
