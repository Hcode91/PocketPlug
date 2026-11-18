// PocketPlug - Convergent NGEN support chips
// SPDX-License-Identifier: BSD-3-Clause
//
// Small, self-contained models of the standard parts on the NGEN boards:
// 8259A PIC, 8253/8254 PIT, 8251A USART, MC6845 CRTC registers,
// WD2797 floppy controller and a WD1010 hard disk controller.

#pragma once
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>

// ------------------------------------------------------------------ 8259A
class Pic8259 {
public:
    void reset();
    void write(int a0, uint8_t v);
    uint8_t read(int a0);
    void setIrq(int n, bool level);
    bool intPending() const;
    uint8_t acknowledge();
    std::function<void(bool)> onInt;
    // state
    uint8_t irr = 0, isr = 0, imr = 0xff, lines = 0;
    uint8_t icw2 = 0;
    bool level = false, aeoi = false, single = true, needIcw4 = false;
    int initStep = 0;       // 0 = operational, 2/3/4 = expecting ICWn
    bool readIsr = false, poll = false, specialMask = false;
private:
    int best() const;
    void update();
};

// ------------------------------------------------------------------ 8253/8254
class Pit8254 {
public:
    struct Counter {
        uint8_t mode = 0, rw = 3;
        uint16_t count = 0;       // programmed initial count
        uint32_t ce = 0;          // counting element (0..65535)
        bool loaded = false, pendingLoad = false, out = false;
        bool writeHi = false, readHi = false;
        bool latched = false; uint16_t latch = 0;
        bool frozen = false;      // clock gated off (NGEN disk module trick)
        bool done = false;        // one-shot modes: terminal count reached
        uint8_t lsbTmp = 0;
    } c[3];
    void reset();
    void write(int a, uint8_t v);
    uint8_t read(int a);
    void tick(int ch, uint32_t n);          // n clock edges
    std::function<void(int ch, bool level)> onOut;
private:
    void setOut(int ch, bool v);
};

// ------------------------------------------------------------------ 8251A
class Usart8251 {
public:
    void reset();
    void write(int a, uint8_t v);
    uint8_t read(int a);
    // time: advance in microseconds (fractional via accumulator)
    void advance(double us);
    // a byte from the line (keyboard)
    void receive(uint8_t v);
    std::function<void(uint8_t)> onTransmit;  // byte left the shift register
    std::function<void(bool rxrdy, bool txrdyPin)> onLines;
    double byteTimeUs = 11.0 * 1000000.0 / 19200.0;
    // state
    bool expectMode = true;
    uint8_t mode = 0, cmd = 0;
    bool txBufFull = false, txShifting = false;
    uint8_t txBuf = 0, txShift = 0;
    double txRemain = 0;
    std::vector<uint8_t> rxQueue;   // bytes still arriving on the line
    double rxRemain = 0;
    bool rxReady = false, overrun = false;
    uint8_t rxData = 0;
    void notify();
};

// ------------------------------------------------------------------ disk images
struct DiskImage {
    struct Track { int sectorSize = 256; std::map<int, std::vector<uint8_t>> sectors; std::vector<int> order; };
    std::map<int, Track> tracks;      // key = cyl * 16 + head
    int cylinders = 0, heads = 0;
    bool writeProtect = false;
    bool dirty = false;
    std::string path;
    bool loadImd(const std::vector<uint8_t>& d);
    bool loadRaw(const std::vector<uint8_t>& d, int cyls, int heads, int spt, int ssize);
    std::vector<uint8_t> saveImd() const;
    // hard disk container (any geometry): "NGENHD1\0" then tracks
    bool loadHd(const std::vector<uint8_t>& d);
    std::vector<uint8_t> saveHd() const;
    Track* track(int cyl, int head) { auto it = tracks.find(cyl * 16 + head); return it == tracks.end() ? nullptr : &it->second; }
};

// ------------------------------------------------------------------ WD2797
class Wd2797 {
public:
    void reset();
    void write(int reg, uint8_t v);
    uint8_t read(int reg);
    void advance(int us);
    void setDisk(DiskImage* d) { disk_ = d; }
    void setSide(int s) { side_ = s; }
    void setMotor(bool on) { motor_ = on; }
    // NGEN disk module: the byte counter reached its terminal count
    void stopAfterSector() { stopAfterSector_ = true; }
    std::function<void(bool)> onIntrq;
    std::function<void(bool)> onDrq;
    std::function<void()> onDataAccess;   // every data register read/write

    // state
    uint8_t status = 0, track = 0, sector = 1, data = 0, cmd = 0;
    int cyl = 0;              // head position
    bool intrq = false, drq = false;
    int cmdType = 0;
    int state = 0;            // command state machine
    int timer = 0;            // microseconds until the next event
    std::vector<uint8_t> buf; size_t bufPos = 0;
    bool stopAfterSector_ = false;
    bool writing = false;
    int indexUs = 0;          // rotation position
    bool forcedIndexIrq = false;
private:
    DiskImage* disk_ = nullptr;
    int side_ = 0;
    bool motor_ = true;
    void setIntrq(bool v);
    void setDrq(bool v);
    void startCommand(uint8_t c);
    void finish(uint8_t st);
    void typeIStatus();
    bool findSector(std::vector<uint8_t>** out);
};

// ------------------------------------------------------------------ WD1010
// Winchester controller. The NGEN disk module moves sector data through the
// main board 8237 (channel 3); dmaTransfer performs that copy in one go.
class Wd1010 {
public:
    void reset();
    void write(int reg, uint8_t v);
    uint8_t read(int reg);
    void advance(int us);
    void setDisk(DiskImage* d) { disk_ = d; status = d ? 0x50 : 0x00; }
    void setDiskKeepState(DiskImage* d) { disk_ = d; }
    std::function<void(bool)> onIntrq;
    std::function<bool(bool toMemory, uint8_t* data, size_t len)> dmaTransfer;
    uint8_t regs[8] = {};
    uint8_t status = 0x00, error = 0;
    bool intrq = false;
    uint8_t cmd = 0;
    int timer = 0;
    int curCyl = 0;
private:
    DiskImage* disk_ = nullptr;
    void execute();
    void done(uint8_t err);
    int cylReg() const { return regs[4] | ((regs[5] & 3) << 8); }
    int head() const { return regs[6] & 7; }
    int sizeBytes() const { static const int s[4] = { 256, 512, 1024, 128 }; return s[(regs[6] >> 5) & 3]; }
};
