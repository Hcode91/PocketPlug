// PocketPlug - Convergent NGEN (CP-001 / Burroughs B26) workstation
// SPDX-License-Identifier: BSD-3-Clause
//
// 80186 main board: 8259A, 8254, keyboard 8251A + MC6845 text video board,
// uPD7201 serial ports, and the X-Bus floppy/hard disk module (WD2797,
// WD1010, two 8253s). Interrupt wiring as found while booting BTOS 4.0:
//   8259 IR1 = serial, IR3 = hard disk (WD1010), IR5 = 80186 timer 0,
//   IR6 = keyboard UART (RxRDY | TxRDY), 80186 INT3 = floppy (WD2797).

#include "ngen.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>

namespace {
const int kCpuHz = 8000000;
const int kSlice = 128;                 // CPU clocks between device updates
const double kPitHz = 78125.0 / 4;      // main 8254 channel 0 clock
const double kPit12Hz = 14745600.0 / 12;
}

NgenCore::NgenCore()
    : cpu_(new I186(this)), mem_(0x100000, 0), fb_(kW * kH, 0xff000000), disk_(nullptr)
{
    fdc_.onIntrq = [this](bool v) { fdcIrq_ = v; updateInt3(); };
    fdc_.onDrq = [this](bool v) { cpu_->setDrq(1, v); };
    fdc_.onDataAccess = [this]() { fdcPit_.tick(0, 1); };
    hdc_.onIntrq = [this](bool v) { hdcIrq_ = v; pic_.setIrq(3, v); };
    hdc_.dmaTransfer = [this](bool toMem, uint8_t* data, size_t len) { return hdDma(toMem, data, len); };
    fdcPit_.onOut = [this](int ch, bool v) {
        if (ch == 0 && v) {
            // byte counter reached its terminal count: the module ends the
            // multi-sector transfer at the end of the current sector and the
            // counter stops (BTOS checks that it reads back exactly 0)
            fdc_.stopAfterSector();
            fdcPit_.c[0].frozen = true;
        }
    };
    pic_.onInt = [this](bool v) { cpu_->setInt(0, v); };
    pit_.onOut = [](int, bool) {};   // baud rate generators for the serial ports
    kbdUart_.onLines = [this](bool rx, bool tx) {
        kbIrqRx_ = rx; kbIrqTx_ = tx;
        pic_.setIrq(6, rx || tx);
    };
    kbdUart_.onTransmit = [this](uint8_t v) { kbCommand(v); };
    cpu_->setTmrInPeriod(0, 25600);   // 312.5 kHz refresh clock on TMRIN0 (8 MHz / 25.6)
}

bool NgenCore::load(const std::vector<RomFile>& roms, std::string& error)
{
    const std::vector<uint8_t>* lo = nullptr;
    const std::vector<uint8_t>* hi = nullptr;
    for (const RomFile& f : roms) {
        if (f.name == "72-00414_80186_cpu.bin") lo = &f.data;
        else if (f.name == "72-00415_80186_cpu.bin") hi = &f.data;
        else if (f.name == "72-00422_10mb_disk.bin") diskRom_ = f.data;
    }
    if (!lo || !hi || lo->size() < 0x1000 || hi->size() < 0x1000) {
        error = "The NGEN boot ROMs (72-00414 and 72-00415) are missing";
        return false;
    }
    for (int i = 0; i < 0x1000; i++) {
        mem_[0xfe000 + i * 2] = (*lo)[i];
        mem_[0xfe000 + i * 2 + 1] = (*hi)[i];
    }
    reset();
    return true;
}

bool NgenCore::insertDisk(const std::vector<uint8_t>& image, const std::string& name, std::string& error)
{
    std::unique_ptr<DiskImage> d(new DiskImage());
    bool ok = false;
    if (image.size() > 3 && image[0] == 'I' && image[1] == 'M' && image[2] == 'D') ok = d->loadImd(image);
    else if (image.size() == 77 * 2 * 16 * 256) ok = d->loadRaw(image, 77, 2, 16, 256);
    else if (image.size() == 80 * 2 * 16 * 256) ok = d->loadRaw(image, 80, 2, 16, 256);
    else if (image.size() == 40 * 2 * 8 * 512) ok = d->loadRaw(image, 40, 2, 8, 512);
    else if (image.size() == 80 * 2 * 9 * 512) ok = d->loadRaw(image, 80, 2, 9, 512);
    if (!ok) { error = "Unrecognised floppy image: " + name; return false; }
    d->path = name;
    if (disk_) {
        // a swap: the drive reads "not ready" for a moment, as when the door
        // is opened, so BTOS notices the change and drops its cached directories
        disk_.reset();
        fdc_.setDisk(nullptr);
        pendingDisk_ = std::move(d);
        pendingDiskFrames_ = 120;
    } else {
        disk_ = std::move(d);
        fdc_.setDisk(disk_.get());
    }
    return true;
}

namespace {
bool readFile(const std::string& path, std::vector<uint8_t>& out)
{
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    out.assign(std::istreambuf_iterator<char>(f), {});
    return true;
}
bool writeFile(const std::string& path, const std::vector<uint8_t>& data)
{
    std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
        if (!f) return false;
    }
    return std::rename(tmp.c_str(), path.c_str()) == 0;
}
}

bool NgenCore::mediaInsert(int drive, const std::string& path, std::string& error)
{
    std::vector<uint8_t> data;
    if (drive == 0) {
        if (!readFile(path, data)) { error = "Can't read " + path; return false; }
        size_t slash = path.find_last_of('/');
        return insertDisk(data, slash == std::string::npos ? path : path.substr(slash + 1), error);
    }
    if (drive == 1) {
        if (!path.empty()) readFile(path, data);   // a missing file means a new, unformatted drive
        return attachHardDisk(data, error);
    }
    error = "No such drive";
    return false;
}

bool NgenCore::mediaSave(int drive, const std::string& path)
{
    DiskImage* d = drive == 0 ? disk() : drive == 1 ? hd_.get() : nullptr;
    if (!d || !d->dirty) return false;
    if (!writeFile(path, drive == 0 ? d->saveImd() : d->saveHd())) return false;
    d->dirty = false;
    return true;
}

void NgenCore::mediaEject(int drive)
{
    if (drive == 0) ejectDisk();
}

int NgenCore::mediaActivity()
{
    // bits 0-1: floppy / hard disk busy; bits 8-15: keyboard LEDs
    return (fdcBusyFrames_ > 0 ? 1 : 0) | (hdcBusyFrames_ > 0 ? 2 : 0) | (kbLeds_ << 8);
}

void NgenCore::ejectDisk()
{
    disk_.reset();
    pendingDisk_.reset();
    fdc_.setDisk(nullptr);
}

bool NgenCore::attachHardDisk(const std::vector<uint8_t>& image, std::string& error)
{
    std::unique_ptr<DiskImage> d(new DiskImage());
    if (!image.empty() && !d->loadHd(image)) { error = "Unrecognised hard disk image"; return false; }
    hd_ = std::move(d);
    hdc_.setDisk(hd_.get());
    return true;
}

void NgenCore::reset()
{
    for (uint32_t a = 0; a < 0xfe000; a++) mem_[a] = 0;
    cpu_->reset();
    pic_.reset();
    pit_.reset();
    fdcPit_.reset();
    hdcPit_.reset();
    kbdUart_.reset();
    fdc_.reset();
    fdc_.setDisk(disk_.get());
    hdc_.setDisk(hd_.get());
    hdc_.reset();
    pacs_ = mpcs_ = 0;
    periphBase_ = 0xf800;
    periphInMem_ = false;
    modBase_ = 0;
    xbusCurrent_ = 0;
    periph141_ = control_ = spk_ = 0;
    crtcAddr_ = 0;
    memset(crtc_, 0, sizeof(crtc_));
    fdcControl_ = hdcControl_ = diskPage_ = 0;
    memset(dmaPage_, 0, sizeof(dmaPage_));
    memset(&dma_, 0, sizeof(dma_));
    dma_.mask = 0x0f;
    memset(sioReg_, 0, sizeof(sioReg_));
    sioPtr_[0] = sioPtr_[1] = 0;
    fdcIrq_ = hdcIrq_ = false;
    kbIrqRx_ = kbIrqTx_ = false;
    kbEcho_ = false;
    kbLeds_ = 0;
    memset(keyMap_, 0, sizeof(keyMap_));
    kbPending_.clear();
    kbDelayUs_ = 0;
    cycleFrac_ = 0;
    pitAcc_ = pit12Acc_ = fdcPitAcc_ = 0;
    frameCount_ = 0;
}

// ------------------------------------------------------------------ memory

uint8_t NgenCore::memRead(uint32_t addr)
{
    addr &= 0xfffff;
    if (periphInMem_ && addr >= periphBase_ && addr < periphBase_ + 0x400) {
        uint32_t off = (addr - periphBase_) >> 1;
        uint16_t v = periphRead(off, false, addr & 1);
        return uint8_t(v);
    }
    return mem_[addr];
}

void NgenCore::memWrite(uint32_t addr, uint8_t v)
{
    addr &= 0xfffff;
    if (periphInMem_ && addr >= periphBase_ && addr < periphBase_ + 0x400) {
        periphWrite((addr - periphBase_) >> 1, v, false, addr & 1);
        return;
    }
    if (addr >= 0xfe000) return;   // boot ROM
    mem_[addr] = v;
}

// ------------------------------------------------------------------ I/O decode

uint16_t NgenCore::ioRead(uint16_t port, bool word)
{
    if (port < 2) {
        // X-Bus module identification
        uint16_t v;
        if (xbusCurrent_ == 0) v = 0x1070;   // floppy / hard disk module
        else { cpu_->nmi(); v = 0x0080; }    // past the last module
        return word ? v : ((port & 1) ? (v >> 8) : (v & 0xff));
    }
    if (!periphInMem_ && port >= periphBase_ && port < periphBase_ + 0x400) {
        uint16_t v = periphRead((port - periphBase_) >> 1, word, port & 1);
        return word ? v : (v & 0xff);
    }
    if (modBase_ && port >= modBase_ && port < modBase_ + 0x100) {
        if (port & 1) return 0xff;
        uint8_t v = moduleRead((port - modBase_) >> 1);
        return word ? uint16_t(0xff00 | v) : v;
    }
    if (trace) fprintf(stderr, "IO read %04x (%s) @%04x:%04x\n", port, word ? "w" : "b", cpu_->sr[1], cpu_->ip);
    return word ? 0xffff : 0xff;
}

void NgenCore::ioWrite(uint16_t port, uint16_t v, bool word)
{
    if (port < 2) {
        // X-Bus module address select (high byte = I/O page)
        uint16_t addr = uint16_t((v & 0x00ff) << 8);
        if (xbusCurrent_ == 0) modBase_ = addr;
        else cpu_->nmi();
        xbusCurrent_++;
        return;
    }
    if (!periphInMem_ && port >= periphBase_ && port < periphBase_ + 0x400) {
        periphWrite((port - periphBase_) >> 1, v, word, port & 1);
        return;
    }
    if (modBase_ && port >= modBase_ && port < modBase_ + 0x100) {
        if (!(port & 1)) moduleWrite((port - modBase_) >> 1, uint8_t(v));
        return;
    }
    if (trace) fprintf(stderr, "IO write %04x=%04x (%s) @%04x:%04x\n", port, v, word ? "w" : "b", cpu_->sr[1], cpu_->ip);
}

void NgenCore::chipSelect(int reg, uint16_t v)
{
    if (reg == 0xffa4) pacs_ = v;
    else if (reg == 0xffa8) mpcs_ = v;
    else return;
    periphInMem_ = mpcs_ & 0x0040;
    periphBase_ = uint32_t(pacs_ & 0xffc0) << 4;
    if (!periphInMem_) periphBase_ &= 0xffff;
}

uint8_t NgenCore::intAck()
{
    uint8_t vec = pic_.acknowledge();
    // the 80186 timer 0 request is latched until acknowledged
    if ((vec & 7) == 5) pic_.setIrq(5, false);
    return vec;
}

void NgenCore::timerOut(int which, int level)
{
    (void)level;
    if (which == 0) pic_.setIrq(5, true);
}

void NgenCore::updateInt3()
{
    cpu_->setInt(3, fdcIrq_);
}

// ------------------------------------------------------------------ main board peripherals

void NgenCore::periphWrite(uint32_t off, uint16_t v, bool word, bool hiLane)
{
    if (hiLane && !word) return;   // the 8-bit chips sit on the low byte lane
    uint8_t b = uint8_t(v);
    switch (off) {
    case 0x00: case 0x01: case 0x02: case 0x03: case 0x04: case 0x05: case 0x06: case 0x07:
    case 0x08: case 0x09: case 0x0a: case 0x0b: case 0x0c: case 0x0d: case 0x0e: case 0x0f:
        dmaWrite(off, b);
        break;
    case 0x43: spk_ = v; break;
    case 0x80: case 0x81: case 0x82: case 0x83: dmaPage_[off & 3] = b; break;
    case 0xc0: xbusCurrent_ = 0; break;
    case 0x10c: pic_.write(0, b); break;
    case 0x10d: pic_.write(1, b); break;
    case 0x110: case 0x111: case 0x112: case 0x113: pit_.write(off - 0x110, b); break;
    case 0x141: periph141_ = v; break;
    case 0x144: crtcAddr_ = b & 0x1f; break;
    case 0x145: if (crtcAddr_ < 18) crtc_[crtcAddr_] = b; break;
    case 0x146: case 0x147: kbdUart_.write(off & 1, b); break;
    case 0x180: case 0x181: break;               // serial data (no line attached)
    case 0x182: case 0x183: sioWrite(off & 1, b); break;
    case 0x1a0: break;
    default:
        if (trace) fprintf(stderr, "periph write %03x=%04x\n", off, v);
        break;
    }
}

uint16_t NgenCore::periphRead(uint32_t off, bool word, bool hiLane)
{
    (void)word;
    if (hiLane) return 0xff;
    switch (off) {
    case 0x00: case 0x01: case 0x02: case 0x03: case 0x04: case 0x05: case 0x06: case 0x07:
    case 0x08: case 0x09: case 0x0a: case 0x0b: case 0x0c: case 0x0d: case 0x0e: case 0x0f:
        return dmaRead(off);
    case 0x43: return spk_;
    case 0x80: case 0x81: case 0x82: case 0x83: return dmaPage_[off & 3];
    case 0x10c: return pic_.read(0);
    case 0x10d: return pic_.read(1);
    case 0x110: case 0x111: case 0x112: case 0x113: return pit_.read(off - 0x110);
    case 0x141: return periph141_;
    case 0x144: return 0;
    case 0x145: return crtcAddr_ >= 14 && crtcAddr_ < 18 ? crtc_[crtcAddr_] : 0;
    case 0x146: case 0x147: return kbdUart_.read(off & 1);
    case 0x180: case 0x181: return 0;
    case 0x182: case 0x183: return sioRead(off & 1);
    case 0x1a0: return control_;
    }
    if (trace) fprintf(stderr, "periph read %03x\n", off);
    return 0xffff;
}

// uPD7201 (Z80-SIO style): just enough for the OS to program it.
// Channel A = off 0x182, channel B = off 0x183.
void NgenCore::sioWrite(int ch, uint8_t v)
{
    int reg = sioPtr_[ch];
    sioPtr_[ch] = 0;
    if (reg == 0) {
        sioPtr_[ch] = v & 7;
        sioReg_[ch][0] = v;
    } else sioReg_[ch][reg] = v;
}

uint8_t NgenCore::sioRead(int ch)
{
    int reg = sioPtr_[ch];
    sioPtr_[ch] = 0;
    if (reg == 2 && ch == 1) return sioReg_[1][2];   // interrupt vector (no status)
    if (reg == 1) return 0x01;                       // all sent
    return 0x2c;                                     // Tx empty, DCD, CTS
}

// ------------------------------------------------------------------ disk module

void NgenCore::moduleWrite(uint32_t off, uint8_t v)
{
    if (trace) fprintf(stderr, "%.4f MW %02x=%02x @%04x:%04x\n", cpu_->totalCycles() / 8e6, off, v, cpu_->sr[1], cpu_->ip);
    switch (off) {
    case 0x00: case 0x01: case 0x02: case 0x03:
        if (off == 0 && (v & 0xf0) != 0xd0) fdcBusyFrames_ = 12;
        if (trace && off < 3) fprintf(stderr, "%.3f FDC w%d=%02x\n", cpu_->totalCycles() / 8e6, off, v);
        fdc_.write(off, v); break;
    case 0x04:
        // bit 0 drive enable, bit 2 motor, bit 6 step clock, bit 7 /reset.
        // The side comes from the WD2797's SSO output (command U bit).
        fdcControl_ = v;
        if (!(v & 0x80)) fdc_.reset();
        break;
    case 0x05:
        // bit 2: the module streams its boot ROM into memory through 8237
        // channel 3, one ROM byte per 16-bit word (low byte)
        if ((v & 0x04) && !diskRom_.empty()) {
            const int ch = 3;
            size_t ptr = 0;
            while (!(dma_.mask & (1 << ch))) {
                uint32_t phys = (uint32_t(diskPage_ & 0x7f) << 17) | (uint32_t(dma_.addr[ch]) << 1);
                memWrite(phys, diskRom_[ptr % diskRom_.size()]);
                memWrite(phys + 1, 0xff);
                ptr++;
                if (dma_.mode[ch] & 0x20) dma_.addr[ch]--; else dma_.addr[ch]++;
                if (dma_.count[ch]-- == 0) { dma_.status |= 1 << ch; dma_.mask |= 1 << ch; }
            }
            v &= ~0x04;
        }
        hdcControl_ = v;
        break;
    case 0x07: diskPage_ = v; break;
    case 0x08: case 0x09: case 0x0a: case 0x0b: if (trace) fprintf(stderr, "FDCPIT w%d=%02x\n", off - 8, v); fdcPit_.write(off - 0x08, v); break;
    case 0x10: case 0x11: case 0x12: case 0x13: case 0x14: case 0x15: case 0x16: case 0x17:
        if (off == 0x17) hdcBusyFrames_ = 12;
        hdc_.write(off - 0x10, v); break;
    case 0x18: case 0x19: case 0x1a: case 0x1b: hdcPit_.write(off - 0x18, v); break;
    default:
        if (trace) fprintf(stderr, "module write %02x=%02x\n", off, v);
        break;
    }
}

uint8_t NgenCore::moduleRead(uint32_t off)
{
    uint8_t v = moduleReadRaw(off);
    if (trace && off != 3) fprintf(stderr, "%.4f MR %02x=%02x @%04x:%04x\n", cpu_->totalCycles() / 8e6, off, v, cpu_->sr[1], cpu_->ip);
    return v;
}

uint8_t NgenCore::moduleReadRaw(uint32_t off)
{
    switch (off) {
    case 0x00: case 0x01: case 0x02: case 0x03: { uint8_t r = fdc_.read(off); if (trace && off == 0) fprintf(stderr, "%.3f FDC st=%02x\n", cpu_->totalCycles() / 8e6, r); return r; }
    case 0x04: return fdcControl_;
    case 0x05: return hdcControl_;
    case 0x08: case 0x09: case 0x0a: case 0x0b: return fdcPit_.read(off - 0x08);
    case 0x10: case 0x11: case 0x12: case 0x13: case 0x14: case 0x15: case 0x16: case 0x17:
        return hdc_.read(off - 0x10);
    case 0x18: case 0x19: case 0x1a: case 0x1b: return hdcPit_.read(off - 0x18);
    }
    if (trace) fprintf(stderr, "module read %02x\n", off);
    return 0xff;
}

// ------------------------------------------------------------------ 8237 (hard disk data path)

void NgenCore::dmaWrite(int reg, uint8_t v)
{
    if (trace) fprintf(stderr, "8237 w%x=%02x\n", reg, v);
    if (reg < 8) {
        int ch = reg >> 1;
        uint16_t& base = (reg & 1) ? dma_.baseCount[ch] : dma_.baseAddr[ch];
        uint16_t& cur = (reg & 1) ? dma_.count[ch] : dma_.addr[ch];
        if (!dma_.ff) base = (base & 0xff00) | v; else base = uint16_t((base & 0x00ff) | (v << 8));
        cur = base;
        dma_.ff = !dma_.ff;
        return;
    }
    switch (reg) {
    case 0x8: dma_.command = v; break;
    case 0xa: if (v & 4) dma_.mask |= 1 << (v & 3); else dma_.mask &= ~(1 << (v & 3)); break;
    case 0xb: dma_.mode[v & 3] = v; break;
    case 0xc: dma_.ff = false; break;
    case 0xd: dma_.ff = false; dma_.mask = 0x0f; dma_.status = 0; break;
    case 0xe: dma_.mask = 0; break;
    case 0xf: dma_.mask = v & 0x0f; break;
    }
}

uint8_t NgenCore::dmaRead(int reg)
{
    if (reg < 8) {
        int ch = reg >> 1;
        uint16_t v = (reg & 1) ? dma_.count[ch] : dma_.addr[ch];
        uint8_t r = dma_.ff ? uint8_t(v >> 8) : uint8_t(v);
        dma_.ff = !dma_.ff;
        return r;
    }
    if (reg == 8) { uint8_t s = dma_.status; dma_.status &= 0xf0; return s; }
    return 0xff;
}

// The disk module moves 16-bit words between the WD1010 sector buffer and
// system RAM through 8237 channel 3; the page comes from the module's
// extended address register (word address << 1 | page << 17).
bool NgenCore::hdDma(bool toMemory, uint8_t* data, size_t len)
{
    const int ch = 3;
    if (dma_.mask & (1 << ch)) return false;
    size_t words = len / 2;
    for (size_t i = 0; i < words; i++) {
        uint32_t phys = (uint32_t(diskPage_ & 0x7f) << 17) | (uint32_t(dma_.addr[ch]) << 1);
        if (toMemory) { memWrite(phys, data[i * 2]); memWrite(phys + 1, data[i * 2 + 1]); }
        else { data[i * 2] = memRead(phys); data[i * 2 + 1] = memRead(phys + 1); }
        if (dma_.mode[ch] & 0x20) dma_.addr[ch]--; else dma_.addr[ch]++;
        if (dma_.count[ch]-- == 0) {
            dma_.status |= 1 << ch;
            if (dma_.mode[ch] & 0x10) { dma_.addr[ch] = dma_.baseAddr[ch]; dma_.count[ch] = dma_.baseCount[ch]; }
            else { dma_.mask |= 1 << ch; return i + 1 == words; }
        }
    }
    return true;
}

// ------------------------------------------------------------------ keyboard (HLE)

void NgenCore::kbCommand(uint8_t v)
{
    // BTOS 4.0 keyboard initialisation: 0x92 -> 0xC0, 0x8C -> 0xF0, 0x9E enters a
    // loopback test in which every byte must be echoed until 0x92 ends it.
    // 0xAx / 0xBx set the keyboard LEDs.
    if (kbEcho_ && v != 0x92) { kbPending_.push_back(v); return; }
    switch (v) {
    case 0x92: kbEcho_ = false; kbPending_.push_back(0xc0); break;
    case 0x8c: kbPending_.push_back(0xf0); break;
    case 0x9e: kbEcho_ = true; break;
    default:
        if ((v & 0xf0) == 0xa0) kbLeds_ = uint8_t((kbLeds_ & 0xf0) | (v & 0x0f));
        else if ((v & 0xf0) == 0xb0) kbLeds_ = uint8_t((kbLeds_ & 0x0f) | ((v & 0x0f) << 4));
        if (trace) fprintf(stderr, "KB cmd %02x\n", v);
        break;
    }
}

void NgenCore::keyEvent(int code, bool down)
{
    // The keyboard reports the whole set of keys held down whenever it
    // changes: one byte per key (key number 0-0x7f), bit 7 set on the last
    // byte; 0xC0 means no keys are down.
    code &= 0x7f;
    uint8_t bit = uint8_t(1 << (code & 7));
    bool was = keyMap_[code >> 3] & bit;
    if (down == was) return;
    if (down) keyMap_[code >> 3] |= bit; else keyMap_[code >> 3] &= ~bit;
    std::vector<uint8_t> rep;
    for (int k = 0; k < 128; k++) if (keyMap_[k >> 3] & (1 << (k & 7))) rep.push_back(uint8_t(k));
    if (rep.empty()) rep.push_back(0x40);
    rep.back() |= 0x80;
    kbPending_.insert(kbPending_.end(), rep.begin(), rep.end());
}

// ------------------------------------------------------------------ timing

void NgenCore::advanceDevices(int cycles)
{
    double us = cycles / 8.0;
    // main 8254: channel 0 at 19.53 kHz, channels 1/2 at 1.2288 MHz (baud rates)
    pitAcc_ += cycles * kPitHz / kCpuHz;
    if (pitAcc_ >= 1) { uint32_t n = uint32_t(pitAcc_); pitAcc_ -= n; pit_.tick(0, n); }
    pit12Acc_ += cycles * kPit12Hz / kCpuHz;
    if (pit12Acc_ >= 1) { uint32_t n = uint32_t(pit12Acc_); pit12Acc_ -= n; pit_.tick(1, n); pit_.tick(2, n); }
    // disk module 8253 channels 1/2 at 1 MHz
    fdcPitAcc_ += us;
    if (fdcPitAcc_ >= 1) { uint32_t n = uint32_t(fdcPitAcc_); fdcPitAcc_ -= n; fdcPit_.tick(1, n); fdcPit_.tick(2, n); hdcPit_.tick(2, n * 2); }
    cycleFrac_ += cycles;
    int whole = int(cycleFrac_ / 8);
    if (whole > 0) { cycleFrac_ -= int64_t(whole) * 8; fdc_.advance(whole); hdc_.advance(whole); }
    kbdUart_.advance(us);
    if (!kbPending_.empty()) {
        kbDelayUs_ += us;
        if (kbDelayUs_ >= 200) {
            kbDelayUs_ = 0;
            kbdUart_.receive(kbPending_.front());
            kbPending_.erase(kbPending_.begin());
        }
    }
}

void NgenCore::runFrame()
{
    int target = kCpuHz / 60;
    int done = 0;
    while (done < target) {
        int n = cpu_->run(kSlice);
        advanceDevices(n);
        done += n;
    }
    frameCount_++;
    if (fdc_.status & 0x01) fdcBusyFrames_ = 12; else if (fdcBusyFrames_ > 0) fdcBusyFrames_--;
    if (hdc_.status & 0x80) hdcBusyFrames_ = 12; else if (hdcBusyFrames_ > 0) hdcBusyFrames_--;
    if (pendingDisk_ && --pendingDiskFrames_ <= 0) {
        disk_ = std::move(pendingDisk_);
        fdc_.setDisk(disk_.get());
    }
    render();
}

size_t NgenCore::readAudio(int16_t* out, size_t maxSamples)
{
    size_t n = 735;
    if (n > maxSamples) n = maxSamples;
    for (size_t i = 0; i < n; i++) out[i] = 0;
    return n;
}

// ------------------------------------------------------------------ video

void NgenCore::render()
{
    int cols = crtc_[1] ? crtc_[1] : 80;
    int rows = crtc_[6] ? crtc_[6] : 25;
    int lines = (crtc_[9] & 0x1f) + 1;
    if (cols > 80) cols = 80;
    uint16_t start = uint16_t((crtc_[12] << 8) | crtc_[13]);
    uint16_t cursor = uint16_t((crtc_[14] << 8) | crtc_[15]);
    int curStart = crtc_[10] & 0x1f, curEnd = crtc_[11] & 0x1f;
    bool curBlinkOff = (crtc_[10] & 0x60) == 0x20;
    bool blinkPhase = (frameCount_ / 16) & 1;
    const uint32_t fg = 0xff33ff66, fgBright = 0xffaaffbb, bg = 0xff000000;
    std::fill(fb_.begin(), fb_.end(), bg);
    for (int row = 0; row < rows; row++) {
        for (int col = 0; col < cols; col++) {
            uint16_t addr = uint16_t(start + row * cols + col);
            uint32_t va = 0xf8000 + ((addr * 2) & 0x1fff);
            uint8_t ch = mem_[va];
            uint8_t attr = mem_[va + 1];
            bool reverse = attr & 0x01;
            bool underline = attr & 0x02;
            bool bright = attr & 0x04;
            bool blink = attr & 0x08;
            uint32_t on = bright ? fgBright : fg;
            for (int ra = 0; ra < lines; ra++) {
                int y = row * lines + ra;
                if (y >= kH) break;
                uint32_t fa = 0xfa000 + ((ch * 16 + ra) * 2 & 0x1fff);
                uint16_t bits = uint16_t(mem_[fa] | (mem_[fa + 1] << 8));
                if (blink && blinkPhase) bits = 0;
                if (underline && ra == lines - 1) bits = 0x1ff;
                if (reverse) bits = ~bits;
                if (addr == cursor && !curBlinkOff && ra >= curStart && ra <= curEnd && blinkPhase) bits = ~bits;
                uint32_t* p = &fb_[y * kW + col * 9];
                for (int z = 0; z < 9 && col * 9 + z < kW; z++)
                    p[z] = (bits & (0x100 >> z)) ? on : bg;
            }
        }
    }
}

std::string NgenCore::screenText() const
{
    int cols = crtc_[1] ? crtc_[1] : 80;
    int rows = crtc_[6] ? crtc_[6] : 25;
    if (cols > 80) cols = 80;
    uint16_t start = uint16_t((crtc_[12] << 8) | crtc_[13]);
    std::string s;
    for (int row = 0; row < rows; row++) {
        std::string line;
        for (int col = 0; col < cols; col++) {
            uint16_t addr = uint16_t(start + row * cols + col);
            uint8_t ch = mem_[0xf8000 + ((addr * 2) & 0x1fff)];
            line += (ch >= 32 && ch < 127) ? char(ch) : '.';
        }
        while (!line.empty() && (line.back() == ' ' || line.back() == '.')) line.pop_back();
        s += line + "\n";
    }
    return s;
}

// ------------------------------------------------------------------ save states

namespace {
struct W {
    std::vector<uint8_t>& o;
    template <typename T> void operator()(const T& v) { const uint8_t* p = reinterpret_cast<const uint8_t*>(&v); o.insert(o.end(), p, p + sizeof(T)); }
    void bytes(const std::vector<uint8_t>& v) { uint32_t n = uint32_t(v.size()); (*this)(n); o.insert(o.end(), v.begin(), v.end()); }
};
struct R {
    const uint8_t* p; const uint8_t* e; bool ok = true;
    template <typename T> void operator()(T& v) { if (e - p < (long)sizeof(T)) { ok = false; return; } memcpy(&v, p, sizeof(T)); p += sizeof(T); }
    void bytes(std::vector<uint8_t>& v) { uint32_t n = 0; (*this)(n); if (!ok || uint32_t(e - p) < n) { ok = false; return; } v.assign(p, p + n); p += n; }
};

template <typename IO> void picState(IO& io, Pic8259& p)
{
    io(p.irr); io(p.isr); io(p.imr); io(p.lines); io(p.icw2); io(p.level); io(p.aeoi); io(p.single);
    io(p.needIcw4); io(p.initStep); io(p.readIsr); io(p.poll); io(p.specialMask);
}
template <typename IO> void pitState(IO& io, Pit8254& p) { for (auto& k : p.c) io(k); }
template <typename IO> void uartState(IO& io, Usart8251& u)
{
    io(u.expectMode); io(u.mode); io(u.cmd); io(u.txBufFull); io(u.txShifting); io(u.txBuf); io(u.txShift);
    io(u.txRemain); io.bytes(u.rxQueue); io(u.rxRemain); io(u.rxReady); io(u.overrun); io(u.rxData);
}
template <typename IO> void fdcState(IO& io, Wd2797& f)
{
    io(f.status); io(f.track); io(f.sector); io(f.data); io(f.cmd); io(f.cyl); io(f.intrq); io(f.drq);
    io(f.cmdType); io(f.state); io(f.timer); io.bytes(f.buf); io(f.bufPos); io(f.stopAfterSector_);
    io(f.writing); io(f.indexUs); io(f.forcedIndexIrq);
}
template <typename IO> void hdcState(IO& io, Wd1010& h)
{
    io(h.regs); io(h.status); io(h.error); io(h.intrq); io(h.cmd); io(h.timer); io(h.curCyl);
}
const uint32_t kMagic = 0x4e47454e;   // 'NGEN'
const uint32_t kVersion = 2;
}

bool NgenCore::saveState(std::vector<uint8_t>& out)
{
    out.clear();
    W w{out};
    w(kMagic); w(kVersion);
    std::vector<uint8_t> cpu;
    cpu_->saveState(cpu);
    w.bytes(cpu);
    std::vector<uint8_t> ram(mem_.begin(), mem_.begin() + 0xfe000);
    w.bytes(ram);
    picState(w, pic_); pitState(w, pit_); pitState(w, fdcPit_); pitState(w, hdcPit_);
    uartState(w, kbdUart_); fdcState(w, fdc_); hdcState(w, hdc_);
    w(pacs_); w(mpcs_); w(periphBase_); w(periphInMem_); w(modBase_); w(xbusCurrent_);
    w(periph141_); w(control_); w(spk_); w(crtcAddr_); w(crtc_); w(fdcControl_); w(hdcControl_); w(diskPage_);
    w(dmaPage_); w(dma_); w(sioReg_); w(sioPtr_); w(fdcIrq_); w(hdcIrq_); w(kbIrqRx_); w(kbIrqTx_);
    w(kbEcho_); w(kbLeds_); w(keyMap_); w.bytes(kbPending_); w(kbDelayUs_); w(cycleFrac_); w(pitAcc_); w(pit12Acc_); w(fdcPitAcc_);
    w(frameCount_);
    // the disks travel with the state so the OS's view of them stays consistent
    DiskImage* fd = disk();
    std::vector<uint8_t> fdImg = fd ? fd->saveImd() : std::vector<uint8_t>();
    std::vector<uint8_t> hdImg = hd_ ? hd_->saveHd() : std::vector<uint8_t>();
    w.bytes(fdImg); w.bytes(hdImg);
    std::vector<uint8_t> name;
    if (fd) name.assign(fd->path.begin(), fd->path.end());
    w.bytes(name);
    return true;
}

bool NgenCore::loadState(const std::vector<uint8_t>& in)
{
    R r{in.data(), in.data() + in.size()};
    uint32_t magic = 0, ver = 0;
    r(magic); r(ver);
    if (!r.ok || magic != kMagic || ver != kVersion) return false;
    std::vector<uint8_t> cpu, ram;
    r.bytes(cpu); r.bytes(ram);
    if (!r.ok || ram.size() != 0xfe000) return false;
    const uint8_t* cp = cpu.data();
    if (!cpu_->loadState(cp, cpu.data() + cpu.size())) return false;
    memcpy(mem_.data(), ram.data(), ram.size());
    picState(r, pic_); pitState(r, pit_); pitState(r, fdcPit_); pitState(r, hdcPit_);
    uartState(r, kbdUart_); fdcState(r, fdc_); hdcState(r, hdc_);
    r(pacs_); r(mpcs_); r(periphBase_); r(periphInMem_); r(modBase_); r(xbusCurrent_);
    r(periph141_); r(control_); r(spk_); r(crtcAddr_); r(crtc_); r(fdcControl_); r(hdcControl_); r(diskPage_);
    r(dmaPage_); r(dma_); r(sioReg_); r(sioPtr_); r(fdcIrq_); r(hdcIrq_); r(kbIrqRx_); r(kbIrqTx_);
    r(kbEcho_); r(kbLeds_); r(keyMap_); r.bytes(kbPending_); r(kbDelayUs_); r(cycleFrac_); r(pitAcc_); r(pit12Acc_); r(fdcPitAcc_);
    r(frameCount_);
    std::vector<uint8_t> fdImg, hdImg, name;
    r.bytes(fdImg); r.bytes(hdImg); r.bytes(name);
    if (!r.ok) return false;
    pendingDisk_.reset();
    if (!fdImg.empty()) {
        std::unique_ptr<DiskImage> d(new DiskImage());
        if (d->loadImd(fdImg)) { d->path.assign(name.begin(), name.end()); d->dirty = true; disk_ = std::move(d); }
    } else disk_.reset();
    if (!hdImg.empty()) {
        std::unique_ptr<DiskImage> d(new DiskImage());
        if (d->loadHd(hdImg)) { d->dirty = true; hd_ = std::move(d); }
    }
    hdc_.setDiskKeepState(hd_.get());
    fdc_.setDisk(disk_.get());
    return true;
}
