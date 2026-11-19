// Stand-in for the bits of MAME's emu.h that the G65816 core uses.
#pragma once
#include <cstdint>
#include <cstddef>
#include <cassert>

#ifndef ATTR_COLD
#define ATTR_COLD
#endif

struct G65816Bus {
    virtual ~G65816Bus() = default;
    virtual uint8_t read(uint32_t a) = 0;
    virtual void write(uint32_t a, uint8_t d) = 0;
    virtual uint8_t vector(uint32_t a) = 0;      // offset in the $ffe0-$ffff vector space
};

#ifndef CLEAR_LINE
#define CLEAR_LINE 0
#define ASSERT_LINE 1
#endif
#ifndef HOLD_LINE
#define HOLD_LINE 2
#endif
enum { STATE_GENPC = -1, STATE_GENPCBASE = -2 };
