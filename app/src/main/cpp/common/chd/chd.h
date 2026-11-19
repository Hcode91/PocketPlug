// PocketPlug - read-only MAME CHD v5 reader (hard-disk / SD card images)
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Reads the hunk map once, then decompresses hunks on demand (lzma, zlib,
// huff and flac, as MAME's chdman writes them) into a small cache, so a
// large card image never has to be unpacked to storage. Parent CHDs are not
// supported.
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

class ChdFile {
public:
    ~ChdFile();
    // false with a reason in err when the file isn't a CHD v5 this can read
    bool open(const std::string& path, std::string& err);
    static bool isChd(FILE* f);
    uint64_t size() const { return m_logical; }
    // reads len bytes at offset; false past the end or on a bad hunk
    bool read(uint64_t offset, uint8_t* dst, size_t len);

private:
    struct Entry { uint8_t type; uint64_t offset; uint32_t length; };
    bool hunk(uint32_t index, uint8_t* dst, int depth = 0);
    bool decode(uint32_t codec, const uint8_t* src, uint32_t len, uint8_t* dst);

    FILE* m_f = nullptr;
    uint64_t m_logical = 0;
    uint32_t m_hunkBytes = 0;
    uint32_t m_codecs[4] = {};
    std::vector<Entry> m_map;
    std::vector<uint8_t> m_comp;
    // tiny cache: the SD controller reads 512-byte sectors in order
    static const int CACHE = 8;
    struct Slot { uint32_t hunk = 0xffffffff; uint32_t age = 0; std::vector<uint8_t> data; } m_cache[CACHE];
    uint32_t m_clock = 0;
};
