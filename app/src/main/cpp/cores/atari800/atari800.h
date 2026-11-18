// PocketPlug - Atari 8-bit computers (1200XL, 130XE)
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The machine is Atari800 (GPL-2+, the Atari800 development team), built as
// its "libatari800" library in ./a800. This class is the glue to PocketPlug's
// Core interface. Atari800 keeps its state in globals, so only one of these
// may exist at a time.

#pragma once
#include "../../core_api.h"

class Atari800Core : public Core {
public:
    enum Model { k1200XL, k130XE };
    explicit Atari800Core(Model m);
    ~Atari800Core() override;

    bool load(const std::vector<RomFile>& roms, std::string& error) override;
    void reset() override;
    void runFrame() override;
    const uint32_t* frameBuffer() const override { return fb_.data(); }
    int frameWidth() const override { return kW; }
    int frameHeight() const override { return kH; }
    double refreshRate() const override;
    int sampleRate() const override { return kRate; }
    size_t readAudio(int16_t* out, size_t maxSamples) override;
    // joystick 1: bit 0 up, 1 down, 2 left, 3 right, 4 fire
    void setInputs(uint32_t pressed) override;
    // keys: see Key below; 0..63 are POKEY keyboard codes
    void keyEvent(int code, bool down) override;
    // option 1: 1200XL BASIC cartridge (built-in Altirra BASIC) in / out
    void setOption(int id, int value) override;
    // drive 0: D1:, drive 1: D2:, drive 2: "load anything" (disk / program /
    // cartridge / cassette, detected from the file; the machine restarts),
    // eject 2 = pull the cartridge.
    bool mediaInsert(int drive, const std::string& path, std::string& error) override;
    bool mediaSave(int drive, const std::string& path) override;
    void mediaEject(int drive) override;
    // bit 0: disk drive busy, bit 4 / 5: 1200XL keyboard lights L1 / L2
    int mediaActivity() override;
    bool saveState(std::vector<uint8_t>& out) override;
    bool loadState(const std::vector<uint8_t>& in) override;

    enum Key {
        kShift = 64, kControl = 65, kStart = 66, kSelect = 67, kOption = 68,
        kBreak = 69, kReset = 70,
    };
    static const int kW = 336, kH = 240;
    static const int kRate = 44100;

    // debugging (desktop harness)
    uint16_t pc() const;

private:
    void render();
    int pickKey() const;

    Model model_;
    std::vector<uint32_t> fb_;
    std::vector<int16_t> audio_;
    std::string osPath_, basicPath_;
    bool loaded_ = false;
    bool keys_[72] = {};
    std::vector<int> held_;   // most recent last
    uint32_t joy_ = 0;
    int busy_ = 0;
};
