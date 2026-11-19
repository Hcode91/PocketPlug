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
    // hard-disk geometry from the 'GDDD' metadata (CYLS:..,HEADS:..,SECS:..)
    bool diskGeometry(int& cyls, int& heads, int& secs);
    // a metadata entry's text, by its four-letter tag ("CHT2" track 1, "DVD " ...)
    bool metadata(const char* tag4, std::string& out, int which = 0);
    uint32_t hunkBytes() const { return m_hunkBytes; }

private:
    struct Entry { uint8_t type; uint64_t offset; uint32_t length; };
    bool hunk(uint32_t index, uint8_t* dst, int depth = 0);
    bool decode(uint32_t codec, const uint8_t* src, uint32_t len, uint8_t* dst);
    bool decodeCd(uint32_t codec, const uint8_t* src, uint32_t len, uint8_t* dst);

    FILE* m_f = nullptr;
    uint64_t m_logical = 0;
    uint64_t m_metaOff = 0;
    uint32_t m_hunkBytes = 0;
    uint32_t m_codecs[4] = {};
    std::vector<Entry> m_map;
    std::vector<uint8_t> m_comp;
    // tiny cache: the SD controller reads 512-byte sectors in order
    static const int CACHE = 8;
    struct Slot { uint32_t hunk = 0xffffffff; uint32_t age = 0; std::vector<uint8_t> data; } m_cache[CACHE];
    uint32_t m_clock = 0;
};

// The start of the disc inside a CD / DVD / GD-ROM CHD, for identifying it:
// sectors 0-17 laid out like an image file (2048-byte sectors for MODE1 /
// MODE2_FORM1 and DVDs, 2352-byte raw sectors otherwise), so the ISO 9660
// volume descriptor and the boot sector sit where they would in a .iso / .bin.
// kind: "cd", "dvd", "gd" or "" (not a disc). False if it can't be read.
bool chdDiscHead(const std::string& path, std::vector<uint8_t>& out, std::string& kind);
