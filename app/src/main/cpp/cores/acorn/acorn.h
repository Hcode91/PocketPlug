// PocketPlug - Acorn A7000 / A7000+ (RISC OS)
// SPDX-License-Identifier: GPL-2.0-or-later
//
// The machine is RPCEmu (GPL-2+, Sarah Walker, Peter Howkins, Matthew Howkins
// and contributors) in ./rpcemu, interpreter build, driven by rpcemu_glue.c.
// RPCEmu keeps its state in globals: one of these at a time.

#pragma once
#include "../../core_api.h"
#include <mutex>

class AcornCore : public Core {
public:
    enum Model { kA7000, kA7000Plus };
    explicit AcornCore(Model m);
    ~AcornCore() override;

    bool load(const std::vector<RomFile>& roms, std::string& error) override;
    void reset() override;
    void runFrame() override;
    const uint32_t* frameBuffer() const override { return fb_.data(); }
    int frameWidth() const override { return kW; }
    int frameHeight() const override { return kH; }
    double refreshRate() const override { return 60.0; }
    int sampleRate() const override { return kRate; }
    size_t readAudio(int16_t* out, size_t maxSamples) override;
    // bits 0-2: SELECT (left), MENU (middle), ADJUST (right) mouse buttons
    void setInputs(uint32_t pressed) override;
    // stick 9: relative mouse motion in screen pixels (accumulated)
    void setAnalog(int stick, float x, float y) override;
    // keys: PS/2 set-2 codes 0x01-0x83; 0x90+ the E0-prefixed keys (see acorn.cpp)
    void keyEvent(int code, bool down) override;
    // drive 0: floppy (.adf); drive 2: copy a file into HostFS
    bool mediaInsert(int drive, const std::string& path, std::string& error) override;
    void mediaEject(int drive) override;
    int mediaActivity() override;
    // RPCEmu has no save states
    bool saveState(std::vector<uint8_t>& out) override { (void)out; return false; }
    bool loadState(const std::vector<uint8_t>& in) override { (void)in; return false; }

    static const int kW = 800, kH = 600;
    static const int kRate = 44100;

    // called back from the glue
    void video(const uint32_t* buf, int xs, int ys, int yl, int yh, int dbl, int hx, int hy);
    void sound(uint32_t rate, const int16_t* st, uint32_t frames);

    std::string hostfsDir() const { return dataDir_ + "/hostfs"; }

private:
    Model model_;
    bool loaded_ = false;
    std::string dataDir_;
    std::vector<uint32_t> fb_;
    std::vector<int16_t> audio_;
    double resamplePos_ = 0;
    uint32_t buttons_ = 0, sentButtons_ = 0;
    float mx_ = 0, my_ = 0;
    std::vector<int> keyEvents_;     // code | 0x1000 when down
    int busy_ = 0;
};
