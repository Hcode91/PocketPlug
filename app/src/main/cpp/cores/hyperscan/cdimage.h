// PocketPlug - CD images for the HyperScan: .cue/.bin, single .bin (raw 2352-byte
// sectors), .iso (2048-byte sectors), or a .zip holding any of those (its tracks are
// unpacked once into files beside the zip).
// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

class CdImage {
public:
    struct Track {
        bool audio = false;
        uint32_t start = 0;        // first sector (INDEX 01), counted from the start of track 1
        uint32_t frames = 0;
        std::string file;          // image file holding it
        uint64_t fileOffset = 0;   // byte offset of its first sector in that file
        int sectorSize = 2352;     // 2352 raw, 2048 cooked data (MODE1/2048, .iso)
    };

    ~CdImage() { close(); }
    bool open(const std::string& path, std::string& error);
    void close();
    bool loaded() const { return !m_tracks.empty(); }

    int numTracks() const { return int(m_tracks.size()); }
    const Track& track(int i) const { return m_tracks[size_t(i)]; }
    uint32_t totalFrames() const;
    int trackAt(uint32_t lba) const;
    // 0x14 for data, 0x10 for audio (MAME's adr/control as the servo's Q subchannel uses it)
    uint8_t adrControl(int t) const { return m_tracks[size_t(t)].audio ? 0x10 : 0x14; }
    // A whole 2352-byte sector (sync, header and data for data tracks; samples for audio).
    bool readRaw(uint32_t lba, uint8_t* out);

private:
    bool parseCue(const std::string& cuePath, const std::string& text, std::string& error);
    bool unzip(const std::string& zipPath, std::string& cue, std::string& error);
    FILE* fileFor(const std::string& name);
    std::vector<Track> m_tracks;
    std::vector<std::pair<std::string, FILE*>> m_files;
};
