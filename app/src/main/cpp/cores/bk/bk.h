// PocketPlug - Elektronika BK-0010 / BK-0010.01 / BK-0011 / BK-0011M
// SPDX-License-Identifier: BSD-3-Clause
//
// Port of MAME's ussr/bk.cpp + bk_m.cpp (Miodrag Milanovic, Sergey Svishchev), 1801vp014.cpp
// (keyboard controller) and vm1timer.cpp, all BSD-3-Clause, on the K1801VM1 core in t11.h.
//
// keyEvent(code, down): code 0-127 = the KOI-7 character the key sends (13 is taken as ВВОД 012), 0x80 = STOP (the HALT key), 0x81 = АР2 (the alternative-register
// shift: keys pressed with it go to vector 0274).
// mediaInsert(2, path): a .bin program (start address, length, data) is loaded into memory
// and started, like MAME's quickload plus "S" in the monitor.
// setOption(0, v): 1 = colour monitor (default), 0 = black-and-white.
// The floppy controller's ROM is mapped where the sets have one, but there are no drives yet.

#pragma once
#include "../../core_api.h"
#include "t11.h"
#include <string>
#include <vector>

class BkCore : public Core, public t11_device::Bus
{
public:
    enum Model { BK0010, BK001001, BK0010FD, BK0011, BK0011M };
    explicit BkCore(Model m);
    bool load(const std::vector<RomFile>& roms, std::string& error) override;
    void reset() override;
    void runFrame() override;
    const uint32_t* frameBuffer() const override { return m_frame.data(); }
    int frameWidth() const override { return 512; }
    int frameHeight() const override { return 256; }
    double refreshRate() const override { return m_eleven ? 4000000.0 / 81920.0 : 3000000.0 / 61440.0; }
    int sampleRate() const override { return 44100; }
    size_t readAudio(int16_t* out, size_t maxSamples) override;
    void setInputs(uint32_t) override {}
    void keyEvent(int code, bool down) override;
    void setOption(int id, int value) override { if (id == 0) m_colour = value != 0; }
    bool mediaInsert(int drive, const std::string& path, std::string& error) override;
    bool saveState(std::vector<uint8_t>&) override { return false; }
    bool loadState(const std::vector<uint8_t>&) override { return false; }

    // CPU bus
    uint16_t read_word(uint16_t a) override;
    void write_word(uint16_t a, uint16_t d) override;
    uint8_t read_byte(uint16_t a) override;
    void write_byte(uint16_t a, uint8_t d) override;
    int iack() override;
    void reset_out() override;

private:
    Model m_model;
    bool m_eleven;
    uint32_t m_clock;
    t11_device m_cpu;

    // memory
    uint16_t m_lowram[0x4000] = {};          // 000000-037777 (BK0010) / pages 0-7 of 16 KB (BK0011)
    std::vector<uint16_t> m_pages;            // BK0011: 8 x 8192 words; BK0010: page 0 = video RAM
    std::vector<uint16_t> m_fdram;            // BK0010FD: 120000-157777
    std::vector<uint8_t> m_rom;               // the "maincpu" region
    std::vector<uint8_t> m_diskRom;           // the floppy controller's ROM at 160000
    int m_view1 = -1, m_view2 = -1;           // BK0011 bank views (-1 = unselected)
    uint16_t* ramPtr(int share) { return &m_pages[size_t(share) * 0x2000]; }
    bool readMem(uint16_t a, uint16_t& v);
    bool writeMem(uint16_t a, uint16_t d, uint16_t mask);
    bool ioRead(uint16_t a, uint16_t& v);
    bool ioWrite(uint16_t a, uint16_t d, uint16_t mask);
    void busError() { m_cpu.pulse_input_line(t11_device::BUS_ERROR); }

    // system registers
    uint16_t m_scroll = 01330;
    uint8_t m_sel1 = 0;
    uint8_t m_misc = 0100;
    bool m_colour = true;
    int m_videoPage = 0;
    int m_stopDisabled = 0;
    int m_dac = 0;
    void sel1_w(uint16_t data);
    void bk11_sel1_w(uint16_t data, uint16_t mask);
    void bk11m_sel1_w(uint16_t data, uint16_t mask);

    // keyboard (K1801VP014)
    uint16_t m_kbdState = 0100, m_keyCode = 0, m_keyVector = 060;
    bool m_rxrdy = false, m_ar2 = false;
    int m_keysDown = 0;
    std::vector<std::pair<int, bool>> m_keyQueue;
    int m_keyHold = 0;
    int m_haltFrames = 0;
    void keyMake(int code);
    void updateVirq();

    // timer (K1801VM1 internal)
    uint16_t m_tLimit = 0, m_tCounter = 0, m_tCsr = 0;
    int m_tDivisor = 0;     // 0 = stopped
    int64_t m_tAcc = 0;
    int m_tReloadIn = -1;
    void timerAdvance(int cycles);

    // output
    std::vector<uint32_t> m_frame;
    std::vector<int16_t> m_samples;
    void render();
};
