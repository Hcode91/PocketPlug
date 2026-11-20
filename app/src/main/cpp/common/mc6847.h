// PocketPlug - Motorola MC6847 VDG (NTSC)
// SPDX-License-Identifier: BSD-3-Clause
//
// Port of MAME's mc6847_base_device / mc6847_friend_device
// (src/devices/video/mc6847.cpp, 0.264; copyright-holders Nathan Woods).
//
// Time is counted in VDG clocks. The machine calls hsyncOn() every 228 clocks
// (the end of a line, when MAME records it) and fireFsync() when
// fsyncDue() comes round; mode changes from the CPU go through changeMode(),
// which first records the part of the current line already drawn, as MAME's
// video_flush() does. render() draws the 372 x 243 picture with MAME's
// "standard" NTSC artifacting.

#pragma once
#include <cstdint>

class MC6847 {
public:
    enum : uint8_t {
        MODE_AG = 0x80, MODE_GM2 = 0x40, MODE_GM1 = 0x20, MODE_GM0 = 0x10,
        MODE_CSS = 0x08, MODE_AS = 0x04, MODE_INTEXT = 0x02, MODE_INV = 0x01,
    };
    static const int WIDTH = 372, HEIGHT = 243;

    struct Host {
        virtual ~Host() = default;
        virtual uint8_t vdgInput(uint16_t address) = 0;
        virtual void vdgFsync(int) {}
        virtual int64_t vdgNow() = 0;       // the current time, in VDG clocks
    };

    struct Line { uint8_t mode[32], data[32], count; };
    struct State {
        Line lines[192];
        uint8_t mode, fixedMode;
        uint16_t videoAddress;
        uint16_t physical, logical;
        uint8_t zone, fieldSync, hsync, recording;
        int32_t partialClocks;
        int64_t lastHsyncOn;
        int64_t fsyncAt;        // -1: none pending
        uint8_t fsyncValue;
    } s;

    explicit MC6847(Host* h);
    void setFixedMode(uint8_t m) { m_fixedMode = m; }
    void reset();

    // mode inputs (MAME's ag_w, gm0_w, as_w, inv_w, css_w ...)
    void changeMode(uint8_t bits, int state);

    void hsyncOn(int64_t now);
    int64_t fsyncDue() const { return s.fsyncAt; }
    void fireFsync();
    // true once per frame, when the screen reaches its vertical blank
    bool frameReady() { bool r = m_frameReady; m_frameReady = false; return r; }
    void render(uint32_t* out);

private:
    Host* m_h;
    uint8_t m_fixedMode = 0;
    bool m_frameReady = false;
    uint8_t m_textInverse[64 * 12];
    struct Entry { const uint8_t* font; uint8_t charMask, shift1, mask1; uint16_t base0, base1; };
    Entry m_entries[128];
    uint32_t m_expanded[128];
    uint32_t m_savedC0 = 0, m_savedC1 = 0;

    void nextScanline();
    void recordFull(int scanline);
    void recordPartial(int scanline, int32_t startClock, int32_t endClock);
    void recordBody(uint8_t mode, int scanline, int32_t startPos, int32_t endPos);
    template<int sampleCount, int yres> void recordRes(int scanline, int32_t startPos, int32_t endPos);
    void videoFlush();
    uint8_t borderValue(uint8_t mode) const;
    uint32_t emit(uint8_t mode, const uint8_t* data, int length, uint32_t* pixels, int y);
    void updateArtifactColors(uint32_t c0, uint32_t c1);
};
