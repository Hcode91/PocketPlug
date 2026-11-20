// PocketPlug - Altair 8800b and 8800b Turnkey
// SPDX-License-Identifier: GPL-2.0-or-later
//
// An 8080 at 2 MHz with 64 KB of RAM, an 88-2SIO serial card (6850 at ports
// 10/11, also answering at 00/01 as in MAME's al8800bt) wired to a glass
// terminal, the 88-DCDD floppy controller (ports 08-0A, four 8" drives,
// 77 tracks x 32 sectors x 137 bytes, the SIMH .dsk layout) and the sense
// switches at port FF.
//
//  - front panel model (al8800b): the CPU starts stopped; the 16 address /
//    data switches, EXAMINE, DEPOSIT, RUN, STOP, SINGLE STEP, RESET and the
//    accumulator switches work as on the 8800b panel (behaviour built in;
//    the panel's microcode PROM isn't run). While running, the LEDs show
//    the bus averaged over the frame, so programs like Kill the Bit work.
//  - turnkey model (al8800bt): no panel; the turnkey monitor at FD00 and the
//    disk boot loader at FF00 (MAME's ROMs). START restarts at FD00.
//
// Frame: 80 x 27 status block.
//   rows 0-23   terminal characters: ASCII | 0x100 reverse | 0x200 bold | 0x400 underline
//   row 24      [0] cursor x, [1] cursor y, [2] running, [3] disk light,
//               [4] protect, [5] bell count
//   row 25      LED brightness 0-255: [0-15] A0-A15, [16-23] D0-D7,
//               [24] INTE [25] PROT [26] MEMR [27] INP [28] M1 [29] OUT
//               [30] HLTA [31] STACK [32] WO [33] INT [34] WAIT [35] HLDA
// Inputs: bits 0-15 the address/data switches; momentary bits (rising edge):
//   16 STOP, 17 RUN, 18 SINGLE STEP, 19 EXAMINE, 20 EXAMINE NEXT, 21 DEPOSIT,
//   22 DEPOSIT NEXT, 23 RESET, 24 CLR, 25 PROTECT, 26 UNPROTECT,
//   27 DISPLAY ACC (held), 28 LOAD ACC, 29 INPUT ACC, 30 OUTPUT ACC.
// keyEvent(code, down): terminal keys, code = ASCII (sent on key down).
// Media: drives 0-3 floppy images (.dsk); drive 8 loads a binary at 0000
// and runs it; drive 10 (front panel model) puts a boot PROM image at FF00
// and runs it.

#pragma once
#include "../../core_api.h"
#include "../../common/i8080.h"
#include <deque>
#include <string>
#include <vector>

class AltairCore : public Core {
public:
    explicit AltairCore(bool turnkey) : m_turnkey(turnkey) { m_out.assign(80 * 27, 0); }
    bool load(const std::vector<RomFile>& roms, std::string& error) override;
    void reset() override;
    void runFrame() override;
    const uint32_t* frameBuffer() const override { return m_out.data(); }
    int frameWidth() const override { return 80; }
    int frameHeight() const override { return 27; }
    double refreshRate() const override { return 60.0; }
    int sampleRate() const override { return 44100; }
    size_t readAudio(int16_t* out, size_t maxSamples) override;
    void setInputs(uint32_t pressed) override;
    void keyEvent(int code, bool down) override;
    void setOption(int id, int value) override;     // 0: terminal type (0 VT100, 1 ADM-3A)
    bool mediaInsert(int drive, const std::string& path, std::string& error) override;
    bool mediaSave(int drive, const std::string& path) override;
    void mediaEject(int drive) override;
    int mediaActivity() override { return m_diskLight > 0 ? 1 : 0; }
    bool saveState(std::vector<uint8_t>& out) override;
    bool loadState(const std::vector<uint8_t>& in) override;

private:
    static constexpr int CPU_HZ = 2000000;
    static constexpr int TRACKS = 77, SECTORS = 32, SECLEN = 137;

    static uint8_t rdThunk(void* c, uint16_t a, I8080::Access k) { return static_cast<AltairCore*>(c)->busRead(a, k); }
    static void wrThunk(void* c, uint16_t a, uint8_t d, I8080::Access k) { static_cast<AltairCore*>(c)->busWrite(a, d, k); }
    static uint8_t inThunk(void* c, uint8_t p) { return static_cast<AltairCore*>(c)->ioIn(p); }
    static void outThunk(void* c, uint8_t p, uint8_t d) { static_cast<AltairCore*>(c)->ioOut(p, d); }
    uint8_t busRead(uint16_t a, I8080::Access k);
    void busWrite(uint16_t a, uint8_t d, I8080::Access k);
    uint8_t peek(uint16_t a) const;
    void poke(uint16_t a, uint8_t d);
    uint8_t ioIn(uint8_t p);
    void ioOut(uint8_t p, uint8_t d);
    void tally(uint16_t a, uint8_t d, int status);
    void termPut(uint8_t c);
    void termScroll(int dir);
    void termCsi(uint8_t f);
    void termLf();
    void termClear(int r0, int c0, int r1, int c1);
    void diskFlush();
    void panelEdge(int bit);
    void runCycles(int cycles);
    void render();

    bool m_turnkey;
    uint8_t m_monRom[256], m_dblRom[256];
    I8080 m_cpu;
    std::vector<uint32_t> m_out;
    uint32_t m_inputs = 0;
    std::deque<uint8_t> m_keys;
    std::vector<uint8_t> m_disk[4];
    std::string m_diskPath[4];
    bool m_diskDirty[4] = {};
    int m_diskLight = 0;
    uint32_t m_audioDue = 0;
    uint32_t m_bellPhase = 0;

    // bus tallies for the panel LEDs while running
    uint32_t m_hitA[16] = {}, m_hitD[8] = {}, m_hitS[12] = {}, m_hitN = 0;

    struct State {
        uint8_t ram[0x10000];
        uint8_t running, protect, halted;
        uint16_t panelAddr;         // the address the panel examines when stopped
        // 6850
        uint8_t aciaCtrl, aciaRx, aciaRdrf, pad0;
        int32_t aciaTxBusy, aciaRxWait;
        // terminal
        uint8_t screen[24][80], attr[24][80];
        int32_t cx, cy, esc, escRow;
        int32_t vtMode;             // 0 VT100 / ANSI, 1 ADM-3A / Soroc
        int32_t par[8], npar, priv, curAttr, saveX, saveY, top, bottom;
        int32_t bell;               // samples of bell tone left
        uint32_t bellCount;
        // disk controller
        int32_t dsel;               // selected drive, -1 = none
        int32_t track[4], sector[4], byte[4], writeIdx[4];
        uint8_t flag[4], sectorTrue[4], pad1[2];
        uint8_t buf[4][SECLEN];
        uint8_t wbuf[SECLEN + 3], pad2;
    } m_s;
};
