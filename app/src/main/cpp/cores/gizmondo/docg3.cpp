// PocketPlug - M-Systems DiskOnChip G3. See docg3.h.
// SPDX-License-Identifier: BSD-3-Clause
// Based on MAME's docg3.cpp (copyright-holders: Tim Schuerewegen).

#include "docg3.h"

DocG3::DocG3()
{
    m_data[0].resize(size_t(PLANES) * BLOCKS * PAGES * PAGE_BYTES);
    m_data[1].resize(size_t(PLANES) * BLOCKS * PAGES * 16);
    m_data[2].resize(size_t(BLOCKS) * 8);
    erase();
}

void DocG3::erase()
{
    std::fill(m_data[0].begin(), m_data[0].end(), 0xff);
    std::fill(m_data[1].begin(), m_data[1].end(), 0x00);
    std::fill(m_data[2].begin(), m_data[2].end(), 0xff);
}

uint8_t DocG3::readData()
{
    uint8_t d = 0;
    if (m_test == 0) {
        if (m_xfer >= PAGE_BYTES) { m_xfer -= PAGE_BYTES; m_plane++; }
        uint32_t o = off1() + m_xfer;
        d = o < m_data[0].size() ? m_data[0][o] : 0xff;
    } else if (m_test == 1) {
        if (m_xfer < 0x20) {
            uint32_t o = off1() + PAGE_BYTES + (DATA - 16) + m_xfer;
            d = o < m_data[0].size() ? m_data[0][o] : 0xff;
        } else if (m_xfer < 0x28) {
            uint32_t o = off3() + m_xfer - 0x20;
            d = o < m_data[2].size() ? m_data[2][o] : 0xff;
        } else d = 0xff;
    }
    m_xfer++;
    return d;
}

uint16_t DocG3::sec1Read()
{
    if (m_sec2[0x1b] & 0x40) { m_sec2[0x1b] &= ~0x40; return readData(); }
    uint16_t lo = readData();
    return uint16_t(lo | readData() << 8);
}

void DocG3::writeData(uint8_t v)
{
    if (m_test == 3) {
        if (m_xfer >= PAGE_BYTES) { m_xfer -= PAGE_BYTES; m_plane++; }
        if (m_xfer == 0) {
            static const uint8_t st[16] = { 0, 0, 0x27 };
            uint32_t o = off2();
            if (o + 16 <= m_data[1].size()) std::memcpy(&m_data[1][o], st, 16);
        }
        uint32_t o = off1() + m_xfer;
        if (o < m_data[0].size()) m_data[0][o] = v;
    }
    m_xfer++;
}

void DocG3::sec1Write(uint16_t v)
{
    if (m_sec2[0x1b] & 0x40) { m_sec2[0x1b] &= ~0x40; writeData(uint8_t(v)); return; }
    writeData(uint8_t(v));
    writeData(uint8_t(v >> 8));
}

uint8_t DocG3::status(uint32_t o)
{
    if (o == 0x42) {
        if (m_block == 0 && m_page == 0 && m_size == 0x27) return 0x07;
        uint32_t a = off2() + 2;
        uint8_t d = a < m_data[1].size() ? m_data[1][a] : 0;
        if (!(m_sec2[0x41] & 0x10)) d &= ~0x20;
        if (!(m_sec2[0x41] & 0x08)) d &= ~0x80;
        if (m_test) d = 0;
        return d;
    }
    if (m_test) return 0;
    uint32_t a = off2() + (o - 0x40);
    return a < m_data[1].size() ? m_data[1][a] : 0;
}

uint8_t DocG3::read8(uint32_t off)
{
    off &= 0x7ff;
    switch (off) {
    case 0x0e: return 0x81;
    case 0x14: return 0x01;
    case 0x22: return 0x85;
    case 0x38: return m_device == 0 ? 0x19 : 0x38;
    case 0x42: case 0x46: case 0x48: case 0x49: case 0x4a: case 0x4b:
    case 0x4c: case 0x4d: case 0x4e: case 0x4f:
        return status(off);
    }
    return m_sec2[off];
}

uint16_t DocG3::read16(uint32_t off)
{
    off &= 0x7fe;
    switch (off) {
    case 0x00: return m_device == 0 ? 0x0200 : 0x008c;
    case 0x74: return m_device == 0 ? 0xfdff : 0x02b3;
    case 0x0e: case 0x14: case 0x22: case 0x38: case 0x42: case 0x46:
        return read8(off);
    case 0x48: case 0x4a: case 0x4c: case 0x4e:
        return uint16_t(status(off) | status(off + 1) << 8);
    }
    return uint16_t(m_sec2[off] | m_sec2[off + 1] << 8);
}

void DocG3::eraseBlock()
{
    static const uint8_t st[16] = { 0, 0, 0x87, 0, 0, 0, 0xce, 0, 0xcf, 0x72, 0xfc, 0x1b, 0xa9, 0xc7, 0xb9, 0 };
    if (m_block >= BLOCKS) return;
    for (uint32_t p = 0; p < PAGES; p++)
        for (uint32_t pl = 0; pl < PLANES; pl++) {
            m_page = p; m_plane = pl;
            std::memset(&m_data[0][off1()], 0xff, PAGE_BYTES);
            std::memcpy(&m_data[1][off2()], st, 16);
        }
}

void DocG3::cmd(uint8_t v)
{
    uint8_t sel = m_sec2[0x32];
    if (sel == 0x0e && v == 0x00) { m_test = 0; m_addrCount = 0; }
    else if (sel == 0x10 && v == 0x50) { m_test = 1; m_addrCount = 0; }
    else if (sel == 0x00 && v == 0xff) m_addrCount = 0;
    else if ((sel == 0x12 || sel == 0x27) && v == 0x60) { m_d1036 = 0; m_d1036n = 0; m_addrCount++; }
    else if (sel == 0x27 && v == 0xd0) eraseBlock();
    else if (sel == 0x31 && v == 0x71) m_test = 2;
    else if (sel == 0x1d && v == 0x80) { m_d1036 = 0; m_d1036n = 0; m_addrCount++; }
    else if (sel == 0x1d && v == 0x11) m_test = 3;
}

void DocG3::addr(uint8_t v)
{
    uint8_t c = m_sec2[0x34];
    if (c == 0x60) {
        m_d1036 |= uint32_t(v) << (8 * m_d1036n++);
        if (m_d1036n == 3) {
            uint32_t block = m_d1036 >> 7, pg = m_d1036 & 0x3f;
            if (m_addrCount == 1 && block < BLOCKS) { m_plane = 0; m_block = block; m_page = pg; }
        }
    } else if (c == 0x80) {
        m_d1036 |= uint32_t(v) << (8 * m_d1036n++);
        if (m_d1036n == 4) {
            uint32_t block = m_d1036 >> 15, pg = (m_d1036 >> 8) & 0x3f;
            if (m_addrCount == 1 && block < BLOCKS) { m_plane = 0; m_block = block; m_page = pg; m_xfer = 0; }
        }
    } else if (c == 0x05) {
        m_xfer = uint32_t(v) << 2;
    }
}

void DocG3::regWrite(uint32_t off)
{
    uint8_t v = m_sec2[off];
    switch (off) {
    case 0x0c: if ((v & 3) == 0) m_sec2[0x04] = 0; break;
    case 0x0a: m_device = v & 3; break;
    case 0x32:
        if (v == 0x12 || v == 0x27) { m_xfer = 0; m_plane = 0; m_block = 0; m_page = 0; }
        break;
    case 0x34: cmd(v); break;
    case 0x36: addr(v); break;
    }
}

void DocG3::write8(uint32_t off, uint8_t v)
{
    off &= 0x7ff;
    m_sec2[off] = v;
    regWrite(off);
}

void DocG3::write16(uint32_t off, uint16_t v)
{
    off &= 0x7fe;
    m_sec2[off] = uint8_t(v);
    m_sec2[off + 1] = uint8_t(v >> 8);
    if (off == 0x40) { m_size = v & 0x3ff; return; }
    regWrite(off);
}
