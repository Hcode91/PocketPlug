// PocketPlug - Acorn Archimedes family (Arculator)
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "../../core_api.h"
#include <string>
#include <vector>

class ArchiCore : public Core {
public:
    explicit ArchiCore(const std::string& id);
    ~ArchiCore() override;

    bool load(const std::vector<RomFile>& roms, std::string& error) override;
    void reset() override;
    void runFrame() override;
    const uint32_t* frameBuffer() const override { return fb_.data(); }
    int frameWidth() const override { return kW; }
    int frameHeight() const override { return kH; }
    double refreshRate() const override { return 60.0; }
    int sampleRate() const override { return kRate; }
    size_t readAudio(int16_t* out, size_t maxSamples) override;

    // mouse buttons: 1 Select (left), 2 Menu (middle), 4 Adjust (right)
    void setInputs(uint32_t pressed) override;
    // stick 9: mouse movement in pixels
    void setAnalog(int stick, float x, float y) override;
    // PS/2 set-2 codes, 0x90+ for the E0 keys (as the A7000 core)
    void keyEvent(int code, bool down) override;
    // option 1: operating system, as an index into the machine's list
    void setOption(int id, int value) override;

    // drive 0 / 1: floppies (.adf ...), drive 2: a file copied into HostFS
    bool mediaInsert(int drive, const std::string& path, std::string& error) override;
    void mediaEject(int drive) override;
    int mediaActivity() override;

    bool saveState(std::vector<uint8_t>& out) override { (void)out; return false; }
    bool loadState(const std::vector<uint8_t>& in) override { (void)in; return false; }

    void video(const uint32_t* px, int stride, int w, int h, int dblscan);
    void sound(const int16_t* stereo, int frames);

    static const int kW = 800, kH = 600, kRate = 44100;

private:
    bool writeRom(int os, std::string& error);
    void writeConfig(int os);
    std::string hostfsDir() const { return dataDir_ + "/hostfs"; }

    std::string id_;
    int model_ = -1;
    int os_ = -1;
    bool loaded_ = false;
    std::string dataDir_;
    std::vector<RomFile> roms_;
    std::vector<uint32_t> fb_;
    std::vector<int16_t> audio_;
    double resamplePos_ = 0;
    double runAcc_ = 0;
    float mx_ = 0, my_ = 0;
    uint32_t buttons_ = 0;
    int busy_ = 0;
};
