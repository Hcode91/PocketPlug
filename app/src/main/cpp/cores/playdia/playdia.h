// PocketPlug - Bandai Playdia (1994)
// SPDX-License-Identifier: BSD-3-Clause
//
// High-level disc player: the Playdia's discs are CD-XA streams of video pictures, XA ADPCM
// sound and F2 control sectors (jumps and button choices). Instead of running the console's
// two 8-bit chips, this plays the stream the way they do. Ported from PlaydiaEmu's core
// (Rust, BSD-3-Clause, Aloys): stream routing, F2 navigation, the AK8000 picture decoder and
// the XA ADPCM decoder.
#pragma once
#include "../../core_api.h"
#include "../hyperscan/cdimage.h"
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

class PlaydiaCore : public Core {
public:
    PlaydiaCore();
    bool load(const std::vector<RomFile>& roms, std::string& error) override;
    bool wantsRomPaths() const override { return true; }
    void reset() override;
    void runFrame() override;
    const uint32_t* frameBuffer() const override { return m_out.data(); }
    int frameWidth() const override { return 320; }
    int frameHeight() const override { return 240; }
    double refreshRate() const override { return 30.0; }
    int sampleRate() const override { return 44100; }
    size_t readAudio(int16_t* out, size_t maxSamples) override;
    void setInputs(uint32_t pressed) override { m_keys = pressed; }
    bool saveState(std::vector<uint8_t>&) override { return false; }
    bool loadState(const std::vector<uint8_t>&) override { return false; }

    enum Button : uint32_t {
        UP = 1u << 0, DOWN = 1u << 1, LEFT = 1u << 2, RIGHT = 1u << 3,
        A = 1u << 4, B = 1u << 5, START = 1u << 6, SELECT = 1u << 7,
    };

    static const int PIC_W = 248, PIC_H = 216;

private:
    struct Sector {
        uint8_t fileId = 0, channel = 0, submode = 0, coding = 0;
        const uint8_t* data = nullptr;
        size_t len = 0;
        uint32_t lba = 0;
    };
    bool handleSector(const Sector& s);           // true: stop reading this frame
    bool applyInteractive(uint32_t lba, const uint8_t* d, size_t n);
    bool seekLba(uint32_t lba);
    bool validTarget(uint32_t lba) const;
    void resumeAfter(uint32_t lba);
    int timeoutTarget() const;
    void videoFragment(const uint8_t* d, size_t n);
    void endPicture();
    void presentNext();
    void presentLatest();
    void blit(const std::vector<uint8_t>& rgb);
    void decodeAudio(const uint8_t* d, size_t n, uint8_t coding);

    CdImage m_cd;
    int m_track = -1;                             // the stream track
    uint32_t m_base = 0, m_sectors = 0;           // its first LBA and length
    uint32_t m_index = 0;                         // next sector in the track
    bool m_started = false;
    int m_phase = 0;

    // video
    std::vector<uint8_t> m_acc;
    bool m_accOverflow = false;
    std::deque<std::vector<uint8_t>> m_pending;
    std::vector<uint32_t> m_fb, m_out;

    // audio
    int m_prev[2] = {}, m_prev2[2] = {};
    std::vector<int16_t> m_pcm;                   // stereo at 44100

    // choices
    bool m_waiting = false;
    int m_dest[7] = {};                           // -1: none
    uint32_t m_waitFrames = 0;
    int m_timeoutDest = -1;

    uint32_t m_keys = 0, m_lastKeys = 0;
    int m_latch = -1, m_latchAge = 0;
    std::vector<uint8_t> m_raw;
};
