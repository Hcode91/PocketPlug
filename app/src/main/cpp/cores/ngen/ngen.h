// PocketPlug - Convergent NGEN (CP-001 / Burroughs B26) workstation
// SPDX-License-Identifier: BSD-3-Clause

#pragma once
#include "../../core_api.h"
#include "i186.h"
#include "ngen_dev.h"
#include <memory>
#include <cstdio>

class NgenCore : public Core, public I186Bus {
public:
    NgenCore();
    bool load(const std::vector<RomFile>& roms, std::string& error) override;
    void reset() override;
    void runFrame() override;
    const uint32_t* frameBuffer() const override { return fb_.data(); }
    int frameWidth() const override { return kW; }
    int frameHeight() const override { return kH; }
    double refreshRate() const override { return 60.0; }
    int sampleRate() const override { return 44100; }
    size_t readAudio(int16_t* out, size_t maxSamples) override;
    void setInputs(uint32_t pressed) override { (void)pressed; }
    void keyEvent(int code, bool down) override;
    bool mediaInsert(int drive, const std::string& path, std::string& error) override;
    bool mediaSave(int drive, const std::string& path) override;
    void mediaEject(int drive) override;
    int mediaActivity() override;
    bool saveState(std::vector<uint8_t>& out) override;
    bool loadState(const std::vector<uint8_t>& in) override;

    // floppy drive 0 (desktop harness / app disk picker)
    bool insertDisk(const std::vector<uint8_t>& image, const std::string& name, std::string& error);
    DiskImage* disk() { return disk_ ? disk_.get() : pendingDisk_.get(); }
    void ejectDisk();
    // winchester drive 0: an empty image is created when data is empty
    bool attachHardDisk(const std::vector<uint8_t>& image, std::string& error);
    DiskImage* hardDisk() { return hd_.get(); }

    // I186Bus
    uint8_t memRead(uint32_t addr) override;
    void memWrite(uint32_t addr, uint8_t v) override;
    uint16_t ioRead(uint16_t port, bool word) override;
    void ioWrite(uint16_t port, uint16_t v, bool word) override;
    uint8_t intAck() override;
    void chipSelect(int reg, uint16_t v) override;
    void timerOut(int which, int level) override;

    // debugging
    I186& cpu() { return *cpu_; }
    std::vector<uint8_t>& ram() { return mem_; }
    bool trace = false;
    std::string screenText() const;
    const uint8_t* crtcRegs() const { return crtc_; }
    std::string debugState() const {
        char b[256];
        snprintf(b, sizeof b, "PIC irr=%02x isr=%02x imr=%02x lines=%02x init=%d | UART cmd=%02x rxq=%zu rxrdy=%d txfull=%d | kbpend=%zu",
                 pic_.irr, pic_.isr, pic_.imr, pic_.lines, pic_.initStep, kbdUart_.cmd, kbdUart_.rxQueue.size(), kbdUart_.rxReady, kbdUart_.txBufFull, kbPending_.size());
        return b;
    }

    static const int kW = 720, kH = 348;

private:
    std::unique_ptr<I186> cpu_;
    std::vector<uint8_t> mem_;       // 1 MB address space (ROM at FE000)
    std::vector<uint8_t> diskRom_;
    std::vector<uint32_t> fb_;
    std::unique_ptr<DiskImage> disk_;
    std::unique_ptr<DiskImage> hd_;
    std::unique_ptr<DiskImage> pendingDisk_;
    int pendingDiskFrames_ = 0;
    int fdcBusyFrames_ = 0, hdcBusyFrames_ = 0;

    Pic8259 pic_;
    Pit8254 pit_;          // main board 8254
    Pit8254 fdcPit_;       // disk module 8253 (FDC byte counter + timers)
    Pit8254 hdcPit_;
    Usart8251 kbdUart_;
    Wd2797 fdc_;
    Wd1010 hdc_;

    // peripheral decode
    uint16_t pacs_ = 0, mpcs_ = 0;
    uint32_t periphBase_ = 0xf800;
    bool periphInMem_ = false;
    uint16_t modBase_ = 0;          // disk module I/O base (0 = not yet assigned)
    int xbusCurrent_ = 0;
    uint16_t periph141_ = 0, control_ = 0, spk_ = 0;
    uint8_t crtcAddr_ = 0, crtc_[18] = {};
    uint8_t fdcControl_ = 0, hdcControl_ = 0, diskPage_ = 0;
    uint8_t dmaPage_[4] = {};
    struct Dma8237 {
        uint16_t baseAddr[4], baseCount[4], addr[4], count[4];
        uint8_t mode[4], mask, status, command;
        bool ff;
    } dma_ = {};
    void dmaWrite(int reg, uint8_t v);
    uint8_t dmaRead(int reg);
    bool hdDma(bool toMemory, uint8_t* data, size_t len);
    uint8_t sioReg_[2][8] = {};
    int sioPtr_[2] = { 0, 0 };
    bool fdcIrq_ = false, hdcIrq_ = false;
    bool kbIrqRx_ = false, kbIrqTx_ = false;

    // keyboard (HLE)
    bool kbEcho_ = false;
    uint8_t kbLeds_ = 0;
    uint8_t keyMap_[16] = {};
    std::vector<uint8_t> kbPending_;
    double kbDelayUs_ = 0;

    // timing
    int64_t cycleFrac_ = 0;
    double pitAcc_ = 0, pit12Acc_ = 0, fdcPitAcc_ = 0;
    uint32_t frameCount_ = 0;

    // audio (speaker)
    std::vector<int16_t> audio_;

    void periphWrite(uint32_t off, uint16_t v, bool word, bool hiLane);
    uint16_t periphRead(uint32_t off, bool word, bool hiLane);
    void moduleWrite(uint32_t off, uint8_t v);
    uint8_t moduleRead(uint32_t off);
    uint8_t moduleReadRaw(uint32_t off);
    void updateInt3();
    void kbCommand(uint8_t v);
    void advanceDevices(int cycles);
    void render();
    void sioWrite(int ch, uint8_t v);
    uint8_t sioRead(int ch);
};
