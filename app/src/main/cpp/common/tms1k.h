// PocketPlug - Texas Instruments TMS1000 / TMS1100 MCUs (BSD-3-Clause)
// SPDX-License-Identifier: BSD-3-Clause
//
// Port of MAME's cpu/tms1000 (Wilbert Pol, hap): 6 clocks per instruction,
// the microinstruction and output PLAs come from the ROM set (Berkeley
// .pla text files, as MAME's pla_device reads them).

#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

class Pla {
public:
    // Berkeley format text: ".i n", ".o n", terms "01-... 0110..."
    bool parse(const std::string& text, int inputs, int outputs);
    uint32_t read(uint32_t input) const { return input < m_cache.size() ? m_cache[input] : 0; }
private:
    std::vector<uint32_t> m_cache;
};

class TMS1K {
public:
    enum Type { TMS1000, TMS1100, TMS1200, TMS1300, TMS1070, TMS1270, TMS1170, TMS1370 };
    struct Io {
        virtual ~Io() {}
        virtual uint8_t readK() { return 0; }
        virtual void writeO(uint16_t o) { (void)o; }
        virtual void writeR(uint32_t r) { (void)r; }
    };

    void configure(Type t, Io* io);
    // micro: the microinstructions PLA; output: the output PLA (or table[] when given)
    bool setRom(const std::vector<uint8_t>& rom, const std::string& microPla, const std::string& outputPla, std::string& error);
    void setOutputTable(const uint16_t* table) { m_otable = table; }
    void reset();
    int run(int clocks);
    uint64_t cycles() const { return s.total; }
    uint32_t r() const { return s.r; }

    struct State {
        uint16_t pc, sr, pa, pb, ps, ca, cb, cs;
        uint8_t a, x, y;
        uint32_t r;
        uint16_t o, oIndex;
        uint8_t cki, c4, p, n, adderOut, carryIn, carryOut, status, statusLatch, eac, clatch, add, bl;
        uint8_t ramIn, damIn;
        int16_t ramOut;
        uint16_t ramAddress, romAddress;
        uint8_t opcode, subcycle;
        uint32_t micro;
        uint64_t fixed;
        uint8_t ram[256];
        uint64_t total;
    } s;

private:
    void execOne(int sub);
    void nextPc();
    void readOpcode();
    void writeOReg(uint8_t index);
    void setCki();

    Io* m_io = nullptr;
    Io m_null;
    Type m_type = TMS1000;
    int m_oPins = 8, m_rPins = 11, m_pcBits = 6, m_xBits = 2;
    uint32_t m_oMask = 0xff, m_rMask = 0x7ff, m_pcMask = 0x3f, m_xMask = 3;
    bool m_1100 = false;
    std::vector<uint8_t> m_rom;
    uint16_t m_romMask = 0x3ff;
    uint8_t m_ramMask = 0x3f;
    Pla m_mpla, m_opla;
    const uint16_t* m_otable = nullptr;
    uint32_t m_microDecode[256];
    uint64_t m_fixedDecode[256];
    int m_icount = 0;
};
