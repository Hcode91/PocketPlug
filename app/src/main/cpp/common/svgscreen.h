// PocketPlug - SVG screens for VFD / LCD / LED handhelds (BSD-3-Clause)
// SPDX-License-Identifier: BSD-3-Clause
//
// Like MAME's screen_svg: the screen artwork is an SVG (shipped in the MAME
// ROM zip) whose shapes carry a <title> naming the output that lights them,
// e.g. "3.12" = display row 3, column 12 of the PWM matrix. Everything
// without a title is the always-visible background.
//
// load() rasterizes the background once and each named element once into a
// small patch (its bounding box) with nanosvg (zlib licence); render() then
// only composites the lit patches, scaled by their brightness.

#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

class SvgScreen {
public:
    ~SvgScreen();
    // svg: the file's text; width: the longer side in pixels (the other follows the SVG)
    bool load(const std::string& svg, int width, std::string& error);
    int width() const { return m_w; }
    int height() const { return m_h; }
    int keyCount() const { return int(m_patches.size()); }
    // index of the element named e.g. "3.12" (-1 = none in this SVG)
    int key(const std::string& name) const;
    // levels: one byte per key (0 = off, 255 = full); out: width x height ARGB
    void render(const uint8_t* levels, uint32_t* out) const;

private:
    struct Patch { int x = 0, y = 0, w = 0, h = 0; std::vector<uint32_t> px; };   // RGBA bytes, straight alpha
    int m_w = 0, m_h = 0;
    std::vector<uint32_t> m_bg;          // ARGB, opaque
    std::vector<Patch> m_patches;
    std::unordered_map<std::string, int> m_keys;
};
