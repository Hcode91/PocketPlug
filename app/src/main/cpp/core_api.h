// PocketPlug - core interface
// SPDX-License-Identifier: BSD-3-Clause
//
// Every emulated system ("core") implements this interface. The Android
// side only ever talks to a Core, so adding a new machine means adding a
// new Core subclass plus a machine entry in the registry.

#pragma once
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

struct RomFile {
    std::string name;
    std::vector<uint8_t> data;
};

class Core {
public:
    virtual ~Core() {}

    // Load the machine's ROM files (already extracted). Returns false + error on failure.
    virtual bool load(const std::vector<RomFile>& roms, std::string& error) = 0;
    virtual void reset() = 0;
    virtual void runFrame() = 0;

    // Video: ARGB8888 frame (0xAARRGGBB)
    virtual const uint32_t* frameBuffer() const = 0;
    virtual int frameWidth() const = 0;
    virtual int frameHeight() const = 0;
    virtual double refreshRate() const = 0;

    // Audio: mono 16-bit samples produced since the last call
    virtual int sampleRate() const = 0;
    virtual size_t readAudio(int16_t* out, size_t maxSamples) = 0;

    // Input: bitmask of machine-specific buttons, 1 = pressed
    virtual void setInputs(uint32_t pressed) = 0;

    // Save states
    virtual bool saveState(std::vector<uint8_t>& out) = 0;
    virtual bool loadState(const std::vector<uint8_t>& in) = 0;
};

// Creates the core for a machine id (e.g. "110dance"); nullptr if unknown.
Core* createCore(const std::string& machineId);
