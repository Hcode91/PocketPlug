// PocketPlug - M-Systems DiskOnChip G3 (64 MB), the Gizmondo's flash disk
// SPDX-License-Identifier: BSD-3-Clause
//
// Port of MAME's diskonchip_g3_device (src/mame/tigertel/docg3.cpp,
// copyright-holders: Tim Schuerewegen, BSD-3-Clause). The chip sits in the
// boot window at 0x0800-0x1fff: 0x0800 data port, 0x1000 registers.

#pragma once
#include <cstdint>
#include <cstring>
#include <vector>

class DocG3 {
public:
    static constexpr uint32_t PLANES = 2, BLOCKS = 1024, PAGES = 64, DATA = 512, EXTRA = 16;
    static constexpr uint32_t PAGE_BYTES = DATA + EXTRA;

    DocG3();
    void erase();                       // all blocks blank (0xff), status bytes zero
    // Raw page access for building an image: plane 0/1, block, page
    uint8_t* page(uint32_t plane, uint32_t block, uint32_t pg) {
        return &m_data[0][(((block * PAGES) + pg) * PLANES + plane) * PAGE_BYTES];
    }

    uint16_t sec1Read();                // 0x0800-0x0fff
    void sec1Write(uint16_t v);
    uint8_t read8(uint32_t off);        // off = 0x000-0x7ff within 0x1000
    uint16_t read16(uint32_t off);
    void write8(uint32_t off, uint8_t v);
    void write16(uint32_t off, uint16_t v);

    std::vector<uint8_t> m_data[3];

private:
    uint32_t off1() const { return (((m_block * PAGES) + m_page) * PLANES + m_plane) * PAGE_BYTES; }
    uint32_t off2() const { return (((m_block * PAGES) + m_page) * PLANES + m_plane) * 16; }
    uint32_t off3() const { return m_block * 8; }
    uint8_t readData();
    void writeData(uint8_t v);
    uint8_t status(uint32_t o);         // 0x1042-0x104f
    void cmd(uint8_t v);                // 0x1034
    void addr(uint8_t v);               // 0x1036
    void eraseBlock();
    void regWrite(uint32_t off);

    uint8_t m_sec2[0x800] = {};
    uint32_t m_d1036 = 0, m_d1036n = 0, m_xfer = 0;
    uint8_t m_device = 0;
    uint32_t m_block = 0, m_page = 0, m_plane = 0, m_size = 0;
    uint8_t m_test = 0;
    uint32_t m_addrCount = 0;
};
