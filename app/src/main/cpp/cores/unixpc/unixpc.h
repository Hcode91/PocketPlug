// PocketPlug - AT&T UNIX PC (PC 7300 / 3B1) desktop computer
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The machine itself is FreeBee by Phil Pemberton and contributors (GPL-3),
// on the Musashi 68010 core (MIT); both live in ./freebee, adapted to run
// without SDL. This class is the glue to PocketPlug's Core interface.

#pragma once
#include "../../core_api.h"
#include <cstdio>

class UnixPcCore : public Core {
public:
    UnixPcCore();
    ~UnixPcCore() override;
    bool load(const std::vector<RomFile>& roms, std::string& error) override;
    void reset() override;
    void runFrame() override;
    const uint32_t* frameBuffer() const override { return fb_.data(); }
    int frameWidth() const override { return kW; }
    int frameHeight() const override { return kH; }
    double refreshRate() const override { return 60.0; }
    int sampleRate() const override { return kRate; }
    size_t readAudio(int16_t* out, size_t maxSamples) override;
    // bits 0-2: mouse buttons (right, middle, left as on the real mouse)
    void setInputs(uint32_t pressed) override;
    void keyEvent(int code, bool down) override;
    // stick 9: relative mouse motion in screen pixels (accumulated)
    void setAnalog(int stick, float x, float y) override;
    bool mediaInsert(int drive, const std::string& path, std::string& error) override;
    bool mediaSave(int drive, const std::string& path) override;
    void mediaEject(int drive) override;
    int mediaActivity() override;
    bool saveState(std::vector<uint8_t>& out) override;
    bool loadState(const std::vector<uint8_t>& in) override;

    static const int kW = 720, kH = 348;
    static const int kRate = 44100;
    // a fresh drive: the 3B1 diagnostics disk calls this geometry "Miniscribe 64MB"
    static const int kHdCyl = 1024, kHdHeads = 8, kHdSpt = 17;

    // debugging (desktop harness)
    uint32_t pc() const;
    std::string screenDump() const;   // 1-bit screen as a PBM

private:
    void render();
    void runDma();
    void updateIrq();
    bool attachHardDisk(const std::string& path, std::string& error);
    void closeFloppy();

    std::vector<uint32_t> fb_;
    std::vector<uint8_t> rom14c_, rom15c_;
    std::vector<int16_t> audio_;
    bool loaded_ = false;

    // floppy: a raw image held in memory (.imd files are expanded on insert)
    std::vector<uint8_t> floppy_;
    FILE* floppyFp_ = nullptr;
    bool floppyDirty_ = false;
    std::string floppyPath_;
    // hard disk: written straight through to the file
    FILE* hdFp_ = nullptr;

    uint32_t clockCycles_ = 0;
    int fdcBusy_ = 0, hdcBusy_ = 0;
    uint32_t buttons_ = 0, lastButtons_ = 0;
    float mouseX_ = 0, mouseY_ = 0;
};
