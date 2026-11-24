// PocketPlug - Acorn BBC Micro family (A, B, US B, B+, B+128, Master 128,
// Master Turbo, Master 512, Master ET, Master Compact) on B-em (GPL-2+, Tom
// Walker and contributors) in ./bem, driven by bem_glue.c. B-em keeps its
// state in globals: one of these at a time.
//
// Frame: 704 x 552 (the picture area with B-em's medium borders, scan lines
// doubled; interlaced teletext uses both fields), 50 Hz.
// keyEvent codes are BBC keyboard matrix codes (row << 4 | column, B-em's
// numbering); 0xff is BREAK. Disc drives 0 and 1 take .ssd / .dsd (and the
// Compact's .adf / .adl), drive 2 a tape (.uef / .csw). A path ending in
// "|boot" also starts it: SHIFT+BREAK for a disc in drive 0, BREAK and
// *TAPE / CHAIN"" typed in for a tape.
// Option 1: what the pad's directions press: 0 Z X : /, 1 the cursor keys.
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once
#include "../../core_api.h"
#include <string>
#include <vector>

class BbcCore : public Core {
public:
    explicit BbcCore(const std::string& id);
    bool load(const std::vector<RomFile>& roms, std::string& error) override;
    void reset() override;
    void runFrame() override;
    const uint32_t* frameBuffer() const override;
    int frameWidth() const override { return 704; }
    int frameHeight() const override { return 552; }
    double refreshRate() const override { return 50.0; }
    int sampleRate() const override { return 44100; }
    size_t readAudio(int16_t* out, size_t maxSamples) override;
    // bits 0-7: the joystick/keys pad (see bbc.cpp)
    void setInputs(uint32_t pressed) override;
    void keyEvent(int code, bool down) override;
    bool mediaInsert(int drive, const std::string& path, std::string& error) override;
    void mediaEject(int drive) override;
    int mediaActivity() override;
    bool saveState(std::vector<uint8_t>& out) override { (void)out; return false; }
    bool loadState(const std::vector<uint8_t>& in) override { (void)in; return false; }
    void setOption(int id, int value) override;
    static bool known(const std::string& id);

private:
    std::string id_, dataDir_;
    bool loaded_ = false;
    uint32_t pad_ = 0;
    int padMode_ = 0;
    // scripted key presses (autoboot / typing): code (0x100 = down), frames to wait after
    struct Step { int code; int wait; };
    std::vector<Step> script_;
    size_t scriptPos_ = 0;
    int scriptWait_ = 0;
    int frames_ = 0;
    void typeText(const char* s);
};
