// PocketPlug - SVG screens (see svgscreen.h)
// SPDX-License-Identifier: BSD-3-Clause

#include "svgscreen.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>

#define NANOSVG_IMPLEMENTATION
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvg/nanosvg.h"
#include "nanosvg/nanosvgrast.h"

SvgScreen::~SvgScreen() {}

int SvgScreen::key(const std::string& name) const
{
    auto it = m_keys.find(name);
    return it == m_keys.end() ? -1 : it->second;
}

bool SvgScreen::load(const std::string& svg, int width, std::string& error)
{
    std::vector<char> text(svg.begin(), svg.end());
    text.push_back(0);
    NSVGimage* img = nsvgParse(text.data(), "px", 72);
    if (!img || img->width <= 0 || img->height <= 0) {
        if (img) nsvgDelete(img);
        error = "The screen artwork (SVG) could not be read";
        return false;
    }
    NSVGrasterizer* rast = nsvgCreateRasterizer();
    // width = the longer side's size in pixels
    const float scale = float(width) / std::max(img->width, img->height);
    m_w = std::max(1, int(std::lround(img->width * scale)));
    m_h = std::max(1, int(std::lround(img->height * scale)));

    // group the shapes by title
    std::vector<std::vector<NSVGshape*>> groups;
    m_keys.clear();
    for (NSVGshape* s = img->shapes; s; s = s->next) {
        if (!s->title[0]) continue;
        std::string t(s->title);
        auto it = m_keys.find(t);
        int k;
        if (it == m_keys.end()) { k = int(groups.size()); m_keys[t] = k; groups.emplace_back(); }
        else k = it->second;
        groups[k].push_back(s);
    }
    std::vector<uint8_t> origVisible;
    for (NSVGshape* s = img->shapes; s; s = s->next) origVisible.push_back(s->flags & NSVG_FLAGS_VISIBLE);

    // background: everything without a title
    for (NSVGshape* s = img->shapes; s; s = s->next) if (s->title[0]) s->flags &= ~NSVG_FLAGS_VISIBLE;
    std::vector<uint8_t> rgba(size_t(m_w) * m_h * 4, 0);
    nsvgRasterize(rast, img, 0, 0, scale, rgba.data(), m_w, m_h, m_w * 4);
    m_bg.resize(size_t(m_w) * m_h);
    for (size_t i = 0; i < m_bg.size(); i++) {
        uint32_t r = rgba[i * 4], g = rgba[i * 4 + 1], b = rgba[i * 4 + 2], a = rgba[i * 4 + 3];
        // straight alpha against black, as MAME
        m_bg[i] = 0xff000000u | (r * a / 255) << 16 | (g * a / 255) << 8 | (b * a / 255);
    }

    // each element alone, in its bounding box
    for (NSVGshape* s = img->shapes; s; s = s->next) s->flags &= ~NSVG_FLAGS_VISIBLE;
    m_patches.assign(groups.size(), Patch());
    for (size_t k = 0; k < groups.size(); k++) {
        float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f;
        for (NSVGshape* s : groups[k]) {
            float sw = s->strokeWidth;
            x0 = std::min(x0, s->bounds[0] - sw); y0 = std::min(y0, s->bounds[1] - sw);
            x1 = std::max(x1, s->bounds[2] + sw); y1 = std::max(y1, s->bounds[3] + sw);
            s->flags |= NSVG_FLAGS_VISIBLE;
        }
        Patch& p = m_patches[k];
        p.x = std::max(0, int(std::floor(x0 * scale)) - 1);
        p.y = std::max(0, int(std::floor(y0 * scale)) - 1);
        int px1 = std::min(m_w, int(std::ceil(x1 * scale)) + 2);
        int py1 = std::min(m_h, int(std::ceil(y1 * scale)) + 2);
        p.w = std::max(0, px1 - p.x);
        p.h = std::max(0, py1 - p.y);
        if (p.w > 0 && p.h > 0) {
            p.px.assign(size_t(p.w) * p.h, 0);
            nsvgRasterize(rast, img, float(-p.x), float(-p.y), scale, (unsigned char*)p.px.data(), p.w, p.h, p.w * 4);
        }
        for (NSVGshape* s : groups[k]) s->flags &= ~NSVG_FLAGS_VISIBLE;
    }
    size_t i = 0;
    for (NSVGshape* s = img->shapes; s; s = s->next, i++) if (origVisible[i]) s->flags |= NSVG_FLAGS_VISIBLE;
    nsvgDeleteRasterizer(rast);
    nsvgDelete(img);
    return true;
}

void SvgScreen::render(const uint8_t* levels, uint32_t* out) const
{
    std::memcpy(out, m_bg.data(), m_bg.size() * 4);
    for (size_t k = 0; k < m_patches.size(); k++) {
        uint32_t lv = levels[k];
        if (!lv) continue;
        const Patch& p = m_patches[k];
        for (int y = 0; y < p.h; y++) {
            const uint8_t* src = (const uint8_t*)&p.px[size_t(y) * p.w];
            uint32_t* dst = out + size_t(p.y + y) * m_w + p.x;
            for (int x = 0; x < p.w; x++, src += 4) {
                uint32_t a = src[3] * lv / 255;
                if (!a) continue;
                uint32_t d = dst[x];
                uint32_t r = ((d >> 16 & 0xff) * (255 - a) + src[0] * a) / 255;
                uint32_t g = ((d >> 8 & 0xff) * (255 - a) + src[1] * a) / 255;
                uint32_t b = ((d & 0xff) * (255 - a) + src[2] * a) / 255;
                dst[x] = 0xff000000u | r << 16 | g << 8 | b;
            }
        }
    }
}
