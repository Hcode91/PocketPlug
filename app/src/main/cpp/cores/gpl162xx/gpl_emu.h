// PocketPlug - the bits of MAME's framework the GPL162xx device ports use (on top of spg_lite.h)
// SPDX-License-Identifier: BSD-3-Clause
#pragma once
#include "../spg2xx/spg_lite.h"
#include "../spg2xx/unsp.h"
#include <cstdint>
#include <cstring>
#include <cmath>
#include <memory>
#include <algorithm>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef uint32_t offs_t;

#ifndef LOGMASKED
#define LOGMASKED(...) do {} while (0)
#endif
#define LOG(...) do {} while (0)
#define ATTR_COLD
#define save_item(...) do {} while (0)
#define save_pointer(...) do {} while (0)

namespace gpl {

struct rectangle {
    int min_x = 0, max_x = 0, min_y = 0, max_y = 0;
    rectangle() = default;
    rectangle(int x0, int x1, int y0, int y1) : min_x(x0), max_x(x1), min_y(y0), max_y(y1) {}
    int left() const { return min_x; }
    int right() const { return max_x; }
    int top() const { return min_y; }
    int bottom() const { return max_y; }
    void set(int x0, int x1, int y0, int y1) { min_x = x0; max_x = x1; min_y = y0; max_y = y1; }
};

struct bitmap_rgb32 {
    uint32_t* base = nullptr;
    int rowpixels = 0;
    uint32_t& pix(int y, int x = 0) { return base[size_t(y) * rowpixels + x]; }
};

// a 16-bit word address space
struct address_space {
    virtual uint16_t read_word(uint32_t a) = 0;
    virtual void write_word(uint32_t a, uint16_t d) = 0;
    virtual ~address_space() = default;
};

}  // namespace gpl
