// PocketPlug - multiplexed LED / VFD / lamp matrix (BSD-3-Clause)
// SPDX-License-Identifier: BSD-3-Clause
//
// Port of the logic of MAME's pwm_display_device (hap): the game strobes rows
// (grids) and columns (plates/segments); every element's lit time is summed
// over a frame, blended 50/50 with the previous frame, and the element shows
// when that is above 1% (MAME's default brightness levels). Time is counted
// in host cycles.

#pragma once
#include <cstdint>
#include <cstring>
#include <algorithm>

class PwmDisplay {
public:
    static constexpr int MAXH = 32, MAXW = 64;

    void init(int height, int width) {
        std::memset(this, 0, sizeof(*this));
        m_h = height; m_w = width;
        m_level = 0.01f;
    }
    // the brightness an element must pass to show (MAME's set_bri_levels, default 1%)
    void setLevel(float level) { m_level = level; }
    // rowsel: which rows are selected; rowdata: the same data on every selected row
    void matrix(uint64_t now, uint64_t rowsel, uint64_t rowdata) {
        sync(now);
        uint64_t mask = m_w >= 64 ? ~0ull : ((1ull << m_w) - 1);
        m_rowsel = rowsel;
        for (int y = 0; y < m_h; y++) m_rowdata[y] = (rowsel >> y & 1) ? (rowdata & mask) : 0;
    }
    // per-row data (displays where each row's data is latched separately)
    void writeRow(uint64_t now, int y, uint64_t data) {
        sync(now);
        if (y >= 0 && y < m_h) m_rowdata[y] = data;
    }
    void sync(uint64_t now) {
        if (now <= m_last) return;
        uint32_t d = uint32_t(now - m_last);
        m_last = now;
        for (int y = 0; y < m_h; y++) {
            uint64_t r = m_rowdata[y];
            while (r) {
                int x = __builtin_ctzll(r);
                r &= r - 1;
                m_acc[y][x] += d;
            }
        }
    }
    // end of a frame of frameCycles host cycles: updates the levels
    void frame(uint64_t now, uint32_t frameCycles) {
        sync(now);
        double cutoff = 4.0 * m_level;   // 4 x the level, at most 1, as MAME
        if (cutoff > 1.0) cutoff = 1.0;
        for (int y = 0; y < m_h; y++)
            for (int x = 0; x < m_w; x++) {
                double b = m_bri[y][x] * 0.5 + (double(m_acc[y][x]) / frameCycles) * 0.5;
                if (b > cutoff) b = cutoff;
                m_bri[y][x] = float(b);
                m_acc[y][x] = 0;
            }
    }
    // the element is lit (MAME's output value 1)
    bool on(int y, int x) const { return m_bri[y][x] > m_level; }
    int height() const { return m_h; }
    int width() const { return m_w; }

private:
    int m_h, m_w;
    float m_level;
    uint64_t m_last;
    uint64_t m_rowsel;
    uint64_t m_rowdata[MAXH];
    uint32_t m_acc[MAXH][MAXW];
    float m_bri[MAXH][MAXW];
};
