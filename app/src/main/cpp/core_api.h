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
    std::string path;      // always set by the app; data is left empty for cores that map files themselves
};

class Core {
public:
    virtual ~Core() {}

    // Load the machine's ROM files (already extracted). Returns false + error on failure.
    virtual bool load(const std::vector<RomFile>& roms, std::string& error) = 0;
    // Very large sets (hundreds of MB) are memory-mapped by the core: the app
    // then only passes RomFile::path and leaves data empty.
    virtual bool wantsRomPaths() const { return false; }
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
    // A "tap" of console buttons: the core holds them down until the game has
    // read them once, then releases them. Returns false if the core doesn't
    // support it (the caller should then just hold the bits briefly itself).
    virtual bool tapInputs(uint32_t mask) { (void)mask; return false; }
    // Analog sticks: x/y in -1..1 (left/up negative). Ignored by cores without them.
    virtual void setAnalog(int stick, float x, float y) { (void)stick; (void)x; (void)y; }

    // Real-time machines (LCD pets): fast-forward the emulated clock by this
    // much wall time, silently. Returns false if the core doesn't do this.
    virtual bool advanceTime(double seconds) { (void)seconds; return false; }
    // Machines with a built-in real-time clock that follows the phone:
    // local time as seconds since 1970-01-01 00:00 (time zone applied).
    virtual void setWallClock(int64_t localSeconds) { (void)localSeconds; }
    // Machine-specific settings (e.g. 1 = gentle mode for the ocean pet).
    virtual void setOption(int id, int value) { (void)id; (void)value; }

    // Save states
    virtual bool saveState(std::vector<uint8_t>& out) = 0;
    virtual bool loadState(const std::vector<uint8_t>& in) = 0;
};

// Creates the core for a machine id (e.g. "110dance"); nullptr if unknown.
Core* createCore(const std::string& machineId);
