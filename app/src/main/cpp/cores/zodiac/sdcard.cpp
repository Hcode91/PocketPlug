#include "sdcard.h"
#include <algorithm>
#include <cctype>
#include <cstring>

// ---------------------------------------------------------------------------
// the card
// ---------------------------------------------------------------------------
void SdCard::reset()
{
    m_state = IDLE; m_app = false; m_rca = 0; m_blockLen = 512;
    dataOut.clear(); writeLeft = 0; multiWrite = multiRead = false;
}

uint32_t SdCard::status() const
{
    uint32_t st = uint32_t(m_state == DIS ? STBY : m_state) << 9;
    if (m_state == TRAN || m_state == STBY) st |= 1u << 8;     // READY_FOR_DATA
    if (m_app) st |= 1u << 5;                                  // APP_CMD
    return st;
}

static void put32(std::vector<uint8_t>& r, uint32_t v)
{
    r = { uint8_t(v >> 24), uint8_t(v >> 16), uint8_t(v >> 8), uint8_t(v) };
}

// CSD version 1.0: capacity = (C_SIZE + 1) << (C_SIZE_MULT + 2) << READ_BL_LEN
void SdCard::csd(std::vector<uint8_t>& r) const
{
    uint64_t bytes = image.size();
    int blLen = 9, mult = 7;
    uint64_t cs = bytes >> (blLen + mult + 2);
    while (cs > 4096) { blLen++; cs >>= 1; }
    uint32_t cSize = uint32_t(cs ? cs - 1 : 0);
    uint8_t b[16] = {};
    b[0] = 0x00;                 // CSD_STRUCTURE 0
    b[1] = 0x26;                 // TAAC
    b[2] = 0x00;                 // NSAC
    b[3] = 0x32;                 // TRAN_SPEED 25 MHz
    b[4] = 0x5f;                 // CCC (high)
    b[5] = uint8_t(0x50 | blLen); // CCC (low) | READ_BL_LEN
    b[6] = uint8_t(0x80 | (cSize >> 10));            // READ_BL_PARTIAL | C_SIZE[11:10]
    b[7] = uint8_t(cSize >> 2);
    b[8] = uint8_t((cSize & 3) << 6 | 0x3f);         // C_SIZE[1:0], VDD_R_CURR
    b[9] = uint8_t(0xfc | (mult >> 1));              // VDD_W_CURR, C_SIZE_MULT[2:1]
    b[10] = uint8_t((mult & 1) << 7 | 0x40 | 0x3f);  // C_SIZE_MULT[0], ERASE_BLK_EN, SECTOR_SIZE
    b[11] = 0x80;                                    // SECTOR_SIZE, WP_GRP_SIZE
    b[12] = uint8_t(0x0a | (blLen >> 2));            // R2W_FACTOR, WRITE_BL_LEN[3:2]
    b[13] = uint8_t((blLen & 3) << 6);               // WRITE_BL_LEN[1:0]
    b[14] = 0x00;
    b[15] = 0x01;
    r.assign(b, b + 16);
}

void SdCard::cid(std::vector<uint8_t>& r) const
{
    static const uint8_t b[16] = { 0x03, 'H', 'C', 'P', 'P', 'L', 'U', 'G', 0x10, 0x12, 0x34, 0x56, 0x78, 0x00, 0x41, 0x01 };
    r.assign(b, b + 16);
}

bool SdCard::command(int cmd, uint32_t arg, std::vector<uint8_t>& resp)
{
    if (!present()) return false;
    bool app = m_app;
    m_app = false;
    if (app) {
        m_app = true;              // reported in the status of an application command
        struct Clear { bool& a; ~Clear() { a = false; } } clear{ m_app };
        switch (cmd) {
        case 6: put32(resp, status()); return true;                          // bus width
        case 13: put32(resp, status()); dataOut.assign(64, 0); return true;  // SD status
        case 22: put32(resp, status()); dataOut = { 0, 0, 0, 0 }; return true;
        case 23: put32(resp, status()); return true;
        case 41:                                                              // SD_SEND_OP_COND
            if (m_state == IDLE && (arg & 0x00ff8000)) m_state = READY;
            put32(resp, 0x80ff8000);                                          // powered up, 2.7-3.6 V
            return true;
        case 42: put32(resp, status()); return true;
        case 51: put32(resp, status()); dataOut = { 0x01, 0x25, 0x00, 0x00, 0, 0, 0, 0 }; return true;   // SCR: SD 1.1, 1 and 4 bit
        }
        // otherwise as an ordinary command
    }
    switch (cmd) {
    case 0: reset(); return true;
    case 2: if (m_state != READY) return false; m_state = IDENT; cid(resp); return true;
    case 3: m_rca = 0x4567; m_state = STBY; put32(resp, uint32_t(m_rca) << 16 | (status() & 0x1fff)); return true;
    case 7:
        if ((arg >> 16) == m_rca) { put32(resp, status()); m_state = TRAN; }
        else { m_state = STBY; return false; }        // deselect: no response
        return true;
    case 9: if ((arg >> 16) != m_rca) return false; csd(resp); return true;
    case 10: if ((arg >> 16) != m_rca) return false; cid(resp); return true;
    case 12:
        put32(resp, status());
        dataOut.clear(); multiRead = multiWrite = false; writeLeft = 0;
        m_state = TRAN;
        return true;
    case 13: if ((arg >> 16) != m_rca) return false; put32(resp, status()); return true;
    case 15: m_state = IDLE; return false;
    case 16: m_blockLen = arg ? arg : 512; put32(resp, status()); return true;
    case 17: case 18: {
        put32(resp, status());
        uint32_t addr = arg;
        dataOut.clear();
        if (uint64_t(addr) + m_blockLen > image.size()) { resp[0] |= 0x40; return true; }   // ADDRESS_ERROR
        dataOut.assign(image.begin() + addr, image.begin() + addr + m_blockLen);
        multiRead = cmd == 18;
        readAddr = addr + m_blockLen;
        return true;
    }
    case 24: case 25:
        put32(resp, status());
        if (uint64_t(arg) + m_blockLen > image.size()) { resp[0] |= 0x40; return true; }
        writeAddr = arg; writeLeft = m_blockLen; multiWrite = cmd == 25;
        return true;
    case 55: m_app = true; put32(resp, status() | 1u << 5); return true;
    }
    return false;       // CMD1 (MMC), CMD8 (SD 2.0) and the rest: no answer
}

void SdCard::moreRead()
{
    if (!multiRead || !dataOut.empty()) return;
    if (uint64_t(readAddr) + m_blockLen > image.size()) { multiRead = false; return; }
    dataOut.assign(image.begin() + readAddr, image.begin() + readAddr + m_blockLen);
    readAddr += m_blockLen;
}

void SdCard::writeData(const uint8_t* p, size_t n)
{
    while (n && writeLeft) {
        size_t k = std::min<size_t>(n, writeLeft);
        if (uint64_t(writeAddr) + k <= image.size()) { std::memcpy(&image[writeAddr], p, k); dirty = true; }
        writeAddr += uint32_t(k); writeLeft -= uint32_t(k); p += k; n -= k;
        if (!writeLeft && multiWrite) writeLeft = m_blockLen;
    }
}

// ---------------------------------------------------------------------------
// FAT16
// ---------------------------------------------------------------------------
namespace fat {

static inline uint16_t rd16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
static inline uint32_t rd32(const uint8_t* p) { return p[0] | p[1] << 8 | p[2] << 16 | uint32_t(p[3]) << 24; }
static inline void wr16(uint8_t* p, uint16_t v) { p[0] = uint8_t(v); p[1] = uint8_t(v >> 8); }
static inline void wr32(uint8_t* p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = uint8_t(v >> (i * 8)); }

static const uint32_t PART_START = 63;

std::vector<uint8_t> format(uint64_t bytes)
{
    uint32_t total = uint32_t(bytes / 512);
    std::vector<uint8_t> img(size_t(total) * 512, 0);
    uint32_t part = total - PART_START;
    uint32_t spc = 4;
    while (spc < 64 && part / spc > 65000) spc *= 2;
    const uint32_t reserved = 1, rootEntries = 512, rootSecs = rootEntries * 32 / 512;
    uint32_t fatSz = 1;
    for (int i = 0; i < 8; i++) {
        uint32_t clusters = (part - reserved - rootSecs - 2 * fatSz) / spc;
        fatSz = ((clusters + 2) * 2 + 511) / 512;
    }
    // MBR with one FAT16 (LBA) partition
    uint8_t* mbr = img.data();
    uint8_t* pe = mbr + 0x1be;
    pe[0] = 0x00;
    pe[1] = 1; pe[2] = 1; pe[3] = 0;                  // CHS start (head 1, sector 1)
    pe[4] = total > 65536 * 2 ? 0x06 : 0x04;
    pe[5] = 0xfe; pe[6] = 0xff; pe[7] = 0xff;         // CHS end: beyond CHS
    wr32(pe + 8, PART_START);
    wr32(pe + 12, part);
    mbr[510] = 0x55; mbr[511] = 0xaa;
    // boot sector
    uint8_t* bs = img.data() + PART_START * 512;
    bs[0] = 0xeb; bs[1] = 0x3c; bs[2] = 0x90;
    std::memcpy(bs + 3, "MSDOS5.0", 8);
    wr16(bs + 11, 512);
    bs[13] = uint8_t(spc);
    wr16(bs + 14, reserved);
    bs[16] = 2;
    wr16(bs + 17, rootEntries);
    if (part < 65536) wr16(bs + 19, uint16_t(part)); else wr32(bs + 32, part);
    bs[21] = 0xf8;
    wr16(bs + 22, uint16_t(fatSz));
    wr16(bs + 24, 63);
    wr16(bs + 26, 255);
    wr32(bs + 28, PART_START);
    bs[36] = 0x80;
    bs[38] = 0x29;
    wr32(bs + 39, 0x20031021);
    std::memcpy(bs + 43, "ZODIAC SD  ", 11);
    std::memcpy(bs + 54, "FAT16   ", 8);
    bs[510] = 0x55; bs[511] = 0xaa;
    for (int f = 0; f < 2; f++) {
        uint8_t* fatp = bs + (reserved + f * fatSz) * 512;
        wr16(fatp, 0xfff8); wr16(fatp + 2, 0xffff);
    }
    // volume label in the root folder
    uint8_t* root = bs + (reserved + 2 * fatSz) * 512;
    std::memcpy(root, "ZODIAC SD  ", 11);
    root[11] = 0x08;
    return img;
}

namespace {
struct Vol {
    std::vector<uint8_t>& img;
    uint64_t base = 0;            // partition start (bytes)
    uint32_t spc = 0, reserved = 0, fatSz = 0, rootEntries = 0, clusters = 0;
    uint64_t fatAt = 0, rootAt = 0, dataAt = 0;
    explicit Vol(std::vector<uint8_t>& i) : img(i) {}

    bool open()
    {
        if (img.size() < 1024) return false;
        auto bpbOk = [&](uint64_t at) {
            const uint8_t* b = &img[at];
            return (b[0] == 0xeb || b[0] == 0xe9) && rd16(b + 11) == 512 && b[13] && rd16(b + 22) && b[16];
        };
        if (bpbOk(0)) base = 0;
        else {
            uint64_t lba = rd32(&img[0x1be + 8]);
            if (lba * 512 + 512 > img.size() || !bpbOk(lba * 512)) return false;
            base = lba * 512;
        }
        const uint8_t* b = &img[base];
        spc = b[13]; reserved = rd16(b + 14); fatSz = rd16(b + 22); rootEntries = rd16(b + 17);
        uint32_t total = rd16(b + 19) ? rd16(b + 19) : rd32(b + 32);
        fatAt = base + uint64_t(reserved) * 512;
        rootAt = fatAt + uint64_t(b[16]) * fatSz * 512;
        dataAt = rootAt + uint64_t(rootEntries) * 32;
        clusters = uint32_t((total - (dataAt - base) / 512) / spc);
        if (clusters < 4085 || clusters >= 65525) return false;           // FAT16 only
        if (dataAt + uint64_t(clusters) * spc * 512 > img.size() + uint64_t(spc) * 512) return false;
        return true;
    }
    uint32_t clusterBytes() const { return spc * 512; }
    uint16_t fat(uint32_t c) const { return rd16(&img[fatAt + c * 2]); }
    void setFat(uint32_t c, uint16_t v)
    {
        wr16(&img[fatAt + c * 2], v);
        wr16(&img[fatAt + uint64_t(fatSz) * 512 + c * 2], v);   // second copy
    }
    uint8_t* cluster(uint32_t c) { return &img[dataAt + uint64_t(c - 2) * clusterBytes()]; }
    uint32_t freeCount() const { uint32_t n = 0; for (uint32_t c = 2; c < clusters + 2; c++) if (!fat(c)) n++; return n; }
    uint32_t alloc(uint32_t prev)
    {
        for (uint32_t c = 2; c < clusters + 2; c++)
            if (!fat(c)) {
                setFat(c, 0xffff);
                if (prev) setFat(prev, uint16_t(c));
                std::memset(cluster(c), 0, clusterBytes());
                return c;
            }
        return 0;
    }
    void freeChain(uint32_t c)
    {
        while (c >= 2 && c < 0xfff8) { uint32_t n = fat(c); setFat(c, 0); c = n; }
    }
    // The slots of a folder (0 = root): pointers to each 32-byte entry.
    std::vector<uint8_t*> slots(uint32_t dir)
    {
        std::vector<uint8_t*> s;
        if (!dir) { for (uint32_t i = 0; i < rootEntries; i++) s.push_back(&img[rootAt + i * 32]); return s; }
        for (uint32_t c = dir; c >= 2 && c < 0xfff8; c = fat(c))
            for (uint32_t i = 0; i < clusterBytes() / 32; i++) s.push_back(cluster(c) + i * 32);
        return s;
    }
};

std::string upper(std::string s) { for (auto& ch : s) ch = char(std::toupper((unsigned char)ch)); return s; }

// Long name of the entry at slots[i] (an 8.3 entry), from the LFN entries before it.
std::string longName(const std::vector<uint8_t*>& s, size_t i)
{
    std::string name;
    for (size_t k = i; k-- > 0;) {
        const uint8_t* e = s[k];
        if (e[11] != 0x0f || e[0] == 0xe5) break;
        static const int pos[13] = { 1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30 };
        for (int p : pos) {
            uint16_t ch = rd16(e + p);
            if (ch == 0 || ch == 0xffff) break;
            name += ch < 0x80 ? char(ch) : '_';
        }
        if (e[0] & 0x40) break;
    }
    if (!name.empty()) return name;
    const uint8_t* e = s[i];
    std::string b(reinterpret_cast<const char*>(e), 8), x(reinterpret_cast<const char*>(e + 8), 3);
    while (!b.empty() && b.back() == ' ') b.pop_back();
    while (!x.empty() && x.back() == ' ') x.pop_back();
    return x.empty() ? b : b + "." + x;
}

// Index of the 8.3 entry called name in the folder, or -1.
long find(Vol& v, uint32_t dir, const std::string& name)
{
    auto s = v.slots(dir);
    for (size_t i = 0; i < s.size(); i++) {
        const uint8_t* e = s[i];
        if (e[0] == 0) break;
        if (e[0] == 0xe5 || e[11] == 0x0f || (e[11] & 0x08)) continue;
        if (upper(longName(s, i)) == upper(name)) return long(i);
    }
    return -1;
}

uint8_t lfnSum(const uint8_t* sfn)
{
    uint8_t sum = 0;
    for (int i = 0; i < 11; i++) sum = uint8_t(((sum & 1) << 7) + (sum >> 1) + sfn[i]);
    return sum;
}

// A unique 8.3 name for name in the folder; lfn tells whether a long-name entry is needed.
void shortName(Vol& v, uint32_t dir, const std::string& name, uint8_t out[11], bool& lfn)
{
    std::string base = name, ext;
    size_t dot = name.rfind('.');
    if (dot != std::string::npos && dot > 0) { base = name.substr(0, dot); ext = name.substr(dot + 1); }
    auto clean = [](const std::string& s) {
        std::string r;
        for (char ch : s) {
            unsigned char c = (unsigned char)ch;
            if (c == ' ' || c == '.') continue;
            if (std::isalnum(c) || std::strchr("$%'-_@~`!(){}^#&", c)) r += char(std::toupper(c));
            else r += '_';
        }
        return r;
    };
    std::string b = clean(base), x = clean(ext).substr(0, 3);
    lfn = !(b.size() <= 8 && b == base && x == ext && !b.empty());
    auto taken = [&](const uint8_t* n) {
        for (auto* e : v.slots(dir)) {
            if (e[0] == 0) break;
            if (e[0] != 0xe5 && e[11] != 0x0f && !std::memcmp(e, n, 11)) return true;
        }
        return false;
    };
    for (int t = 0; t < 1000000; t++) {
        std::string bb = b.empty() ? "FILE" : b;
        if (t || lfn) { std::string tail = "~" + std::to_string(t + 1); bb = bb.substr(0, 8 - tail.size()) + tail; }
        std::memset(out, ' ', 11);
        std::memcpy(out, bb.data(), std::min<size_t>(8, bb.size()));
        std::memcpy(out + 8, x.data(), std::min<size_t>(3, x.size()));
        if (!taken(out)) return;
        if (!lfn && !t) lfn = true;
    }
}

// Room for n consecutive entries in the folder (growing a subfolder if needed); -1 if full.
long freeRun(Vol& v, uint32_t dir, size_t n)
{
    for (int attempt = 0; attempt < 2; attempt++) {
        auto s = v.slots(dir);
        size_t run = 0;
        for (size_t i = 0; i < s.size(); i++) {
            if (s[i][0] == 0 || s[i][0] == 0xe5) { if (++run == n) return long(i + 1 - n); }
            else run = 0;
        }
        if (!dir) return -1;
        uint32_t last = dir;
        while (v.fat(last) >= 2 && v.fat(last) < 0xfff8) last = v.fat(last);
        if (!v.alloc(last)) return -1;
    }
    return -1;
}

void stamp(uint8_t* e)
{
    uint16_t t = uint16_t(12 << 11), d = uint16_t((2004 - 1980) << 9 | 1 << 5 | 1);
    wr16(e + 14, t); wr16(e + 16, d); wr16(e + 18, d); wr16(e + 22, t); wr16(e + 24, d);
}

// Writes an entry (with long-name entries when needed) for name; returns the 8.3 entry.
uint8_t* addEntry(Vol& v, uint32_t dir, const std::string& name, uint8_t attr, uint32_t first, uint32_t size)
{
    uint8_t sfn[11];
    bool lfn;
    shortName(v, dir, name, sfn, lfn);
    size_t nl = lfn ? (name.size() + 12) / 13 : 0;
    long at = freeRun(v, dir, nl + 1);
    if (at < 0) return nullptr;
    auto s = v.slots(dir);
    uint8_t sum = lfnSum(sfn);
    for (size_t k = 0; k < nl; k++) {
        size_t seq = nl - k;                      // stored last part first
        uint8_t* e = s[size_t(at) + k];
        std::memset(e, 0, 32);
        e[0] = uint8_t(seq | (k == 0 ? 0x40 : 0));
        e[11] = 0x0f; e[13] = sum;
        static const int pos[13] = { 1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30 };
        for (int j = 0; j < 13; j++) {
            size_t ci = (seq - 1) * 13 + size_t(j);
            uint16_t ch = ci < name.size() ? uint16_t((unsigned char)name[ci]) : (ci == name.size() ? 0 : 0xffff);
            wr16(e + pos[j], ch);
        }
    }
    uint8_t* e = s[size_t(at) + nl];
    std::memset(e, 0, 32);
    std::memcpy(e, sfn, 11);
    e[11] = attr;
    stamp(e);
    wr16(e + 26, uint16_t(first));
    wr32(e + 28, size);
    return e;
}

void removeEntry(Vol& v, uint32_t dir, long i)
{
    auto s = v.slots(dir);
    s[size_t(i)][0] = 0xe5;
    for (size_t k = size_t(i); k-- > 0;) {
        if (s[k][11] != 0x0f || s[k][0] == 0xe5) break;
        bool last = s[k][0] & 0x40;
        s[k][0] = 0xe5;
        if (last) break;
    }
}
} // namespace

uint64_t freeBytes(const std::vector<uint8_t>& img)
{
    Vol v(const_cast<std::vector<uint8_t>&>(img));
    if (!v.open()) return 0;
    return uint64_t(v.freeCount()) * v.clusterBytes();
}

bool addFile(std::vector<uint8_t>& img, const std::string& path, const std::vector<uint8_t>& data, std::string& error)
{
    Vol v(img);
    if (!v.open()) { error = "The card isn't FAT16"; return false; }
    std::vector<std::string> parts;
    for (size_t p = 0; p < path.size();) {
        size_t q = path.find_first_of("/\\", p);
        if (q == std::string::npos) q = path.size();
        if (q > p) parts.push_back(path.substr(p, q - p));
        p = q + 1;
    }
    if (parts.empty()) { error = "No file name"; return false; }
    uint32_t dir = 0;
    for (size_t i = 0; i + 1 < parts.size(); i++) {
        long at = find(v, dir, parts[i]);
        if (at >= 0) {
            uint8_t* e = v.slots(dir)[size_t(at)];
            if (!(e[11] & 0x10)) { error = parts[i] + " is a file"; return false; }
            dir = rd16(e + 26);
            continue;
        }
        uint32_t c = v.alloc(0);
        if (!c) { error = "The card is full"; return false; }
        if (!addEntry(v, dir, parts[i], 0x10, c, 0)) { v.freeChain(c); error = "The folder is full"; return false; }
        uint8_t* d = v.cluster(c);
        std::memset(d, ' ', 11); d[0] = '.'; d[11] = 0x10; stamp(d); wr16(d + 26, uint16_t(c));
        d += 32;
        std::memset(d, ' ', 11); d[0] = '.'; d[1] = '.'; d[11] = 0x10; stamp(d); wr16(d + 26, uint16_t(dir));
        dir = c;
    }
    const std::string& name = parts.back();
    long old = find(v, dir, name);
    if (old >= 0) {
        uint8_t* e = v.slots(dir)[size_t(old)];
        if (e[11] & 0x10) { error = name + " is a folder"; return false; }
        v.freeChain(rd16(e + 26));
        removeEntry(v, dir, old);
    }
    uint32_t need = uint32_t((data.size() + v.clusterBytes() - 1) / v.clusterBytes());
    if (need > v.freeCount()) { error = "The card is full"; return false; }
    uint32_t first = 0, prev = 0;
    for (uint32_t k = 0; k < need; k++) {
        uint32_t c = v.alloc(prev);
        if (!first) first = c;
        size_t off = size_t(k) * v.clusterBytes();
        std::memcpy(v.cluster(c), data.data() + off, std::min<size_t>(v.clusterBytes(), data.size() - off));
        prev = c;
    }
    if (!addEntry(v, dir, name, 0x20, first, uint32_t(data.size()))) {
        v.freeChain(first);
        error = "The folder is full";
        return false;
    }
    return true;
}

} // namespace fat
