// SD card for the Zodiac's slot: a standard-capacity (byte-addressed) SD card holding a
// FAT16 image, plus the small FAT writer used to build that image and to copy games onto it.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

class SdCard {
public:
    std::vector<uint8_t> image;       // the whole card, a multiple of 512 bytes
    bool dirty = false;               // written since it was loaded or saved

    bool present() const { return !image.empty(); }
    void reset();

    // A command; returns false when the card doesn't answer (the host sees a time-out).
    // resp gets 4 bytes for short responses or 16 for CID/CSD.
    bool command(int cmd, uint32_t arg, std::vector<uint8_t>& resp);
    // After a data command: bytes the card sends (reads) or wants (writes).
    std::vector<uint8_t> dataOut;     // card -> host
    uint32_t writeAddr = 0, writeLeft = 0;   // host -> card: where the next bytes go
    bool multiWrite = false, multiRead = false;
    uint32_t readAddr = 0;            // next block of an open-ended multiple-block read
    void writeData(const uint8_t* p, size_t n);
    void moreRead();                  // refill dataOut for an open-ended read

private:
    enum State { IDLE, READY, IDENT, STBY, TRAN, DATA, RCV, PRG, DIS };
    State m_state = IDLE;
    bool m_app = false;
    uint16_t m_rca = 0;
    uint32_t m_blockLen = 512;
    uint32_t status() const;
    void csd(std::vector<uint8_t>& r) const;
    void cid(std::vector<uint8_t>& r) const;
};

// FAT16 image building: format a blank card and copy files onto it.
namespace fat {
// A new card image (MBR, one FAT16 partition) of the given size in bytes.
std::vector<uint8_t> format(uint64_t bytes);
// Copies a file onto the card at an absolute path ("/PALM/Launcher/Game.prc"), making the
// folders it needs and replacing a file of that name. False if the card is full or not FAT16.
bool addFile(std::vector<uint8_t>& img, const std::string& path, const std::vector<uint8_t>& data, std::string& error);
// Free bytes on the card (0 if it can't be read).
uint64_t freeBytes(const std::vector<uint8_t>& img);
}
