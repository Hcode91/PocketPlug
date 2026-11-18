// PocketPlug - Intel 80186 CPU with its on-chip peripherals
// SPDX-License-Identifier: BSD-3-Clause
//
// Interpreter for the 8086 instruction set plus the 80186 additions, and the
// integrated interrupt controller (master mode, INT0 cascade), the three
// timers and the two DMA channels. Written for the Convergent NGEN.

#pragma once
#include <cstdint>
#include <vector>

class I186Bus {
public:
    virtual ~I186Bus() {}
    virtual uint8_t  memRead(uint32_t addr) = 0;
    virtual void     memWrite(uint32_t addr, uint8_t v) = 0;
    // I/O outside the peripheral control block. word = 16-bit access at an even port.
    virtual uint16_t ioRead(uint16_t port, bool word) = 0;
    virtual void     ioWrite(uint16_t port, uint16_t v, bool word) = 0;
    // INTA cycle for INT0 in cascade mode (vector from the external 8259)
    virtual uint8_t  intAck() = 0;
    // Chip select register writes (PACS = 0xffa4, MPCS = 0xffa8)
    virtual void     chipSelect(int reg, uint16_t v) { (void)reg; (void)v; }
    // A timer reached a terminal count (each phase in alternate mode)
    virtual void     timerOut(int which, int level) { (void)which; (void)level; }
};

class I186 {
public:
    explicit I186(I186Bus* bus) : bus_(bus) { reset(); }
    void reset();

    // Run for at least `cycles` CPU clocks. Returns the number actually run.
    int run(int cycles);

    // External lines
    void setInt(int which, bool level);     // INT0..INT3
    void setDrq(int which, bool level);     // DRQ0/DRQ1
    void tmrIn(int which);                  // rising edge on TMRIN0/1
    void nmi() { nmiPending_ = true; intRequested_ = true; }
    // Free-running clock on TMRIN0/1: one rising edge every periodMilli/1000 CPU clocks (0 = none)
    void setTmrInPeriod(int which, int64_t periodMilli) { tmrInPeriod_[which] = periodMilli; }

    uint64_t totalCycles() const { return total_; }

    // Registers (public for tracing/debugging)
    enum { AX, CX, DX, BX, SP, BP, SI, DI };
    enum { ES, CS, SS, DS };
    uint16_t r[8];
    uint16_t sr[4];
    uint16_t ip;
    uint16_t flags;
    bool halted;

    void saveState(std::vector<uint8_t>& out) const;
    bool loadState(const uint8_t*& p, const uint8_t* end);

private:
    I186Bus* bus_;
    uint64_t total_;
    int cyc_;            // cycles used by the current instruction

    // ---- decoding state ----
    int segOver_;        // -1 none
    int rep_;            // 0, 0xf2, 0xf3
    uint16_t opIp_;      // IP of the current instruction (with prefixes)
    // modrm
    uint8_t modrm_;
    bool modIsReg_;
    uint16_t eaOff_;
    int eaSeg_;

    // ---- memory helpers ----
    inline uint32_t lin(int seg, uint16_t off) const { return ((uint32_t(sr[seg]) << 4) + off) & 0xfffff; }
    uint8_t  rb(int seg, uint16_t off) { return bus_->memRead(lin(seg, off)); }
    uint16_t rw(int seg, uint16_t off) { return rb(seg, off) | (uint16_t(rb(seg, uint16_t(off + 1))) << 8); }
    void wb(int seg, uint16_t off, uint8_t v) { bus_->memWrite(lin(seg, off), v); }
    void ww(int seg, uint16_t off, uint16_t v) { wb(seg, off, uint8_t(v)); wb(seg, uint16_t(off + 1), uint8_t(v >> 8)); }
    uint8_t fetch8() { uint8_t v = rb(CS, ip); ip++; return v; }
    uint16_t fetch16() { uint16_t v = rw(CS, ip); ip += 2; return v; }
    void push(uint16_t v) { r[SP] -= 2; ww(SS, r[SP], v); }
    uint16_t pop() { uint16_t v = rw(SS, r[SP]); r[SP] += 2; return v; }

    // modrm
    void decodeModrm();
    uint8_t  getRM8();
    uint16_t getRM16();
    void setRM8(uint8_t v);
    void setRM16(uint16_t v);
    uint8_t& reg8(int n) { return n < 4 ? reinterpret_cast<uint8_t*>(&r[n])[0] : reinterpret_cast<uint8_t*>(&r[n - 4])[1]; }
    int regField() const { return (modrm_ >> 3) & 7; }

    // flags
    enum { CF = 0x0001, PF = 0x0004, AF = 0x0010, ZF = 0x0040, SF = 0x0080, TF = 0x0100, IF = 0x0200, DF = 0x0400, OF = 0x0800 };
    void setSZP8(uint8_t v);
    void setSZP16(uint16_t v);
    uint8_t  alu8(int op, uint8_t a, uint8_t b);
    uint16_t alu16(int op, uint16_t a, uint16_t b);
    uint8_t  inc8(uint8_t v, bool dec);
    uint16_t inc16(uint16_t v, bool dec);
    uint8_t  shift8(int op, uint8_t v, int count);
    uint16_t shift16(int op, uint16_t v, int count);
    bool cond(int c) const;

    void interrupt(uint8_t type);
    void execute();
    void grp3_8(); void grp3_16();
    void stringOp(uint8_t op);
    uint16_t portIn(uint16_t port, bool word);
    void portOut(uint16_t port, uint16_t v, bool word);

    // ---- peripheral control block ----
    uint16_t relocation_;
    uint16_t pcbRead(uint16_t off);
    void pcbWrite(uint16_t off, uint16_t v);
    bool inPcb(uint16_t port) const;

    // interrupt controller
    uint16_t icMask_, icPriMask_, icInServ_, icReq_, icTmrStat_;
    uint16_t icCon_[7];          // TMR, DMA0, DMA1, I0, I1, I2, I3
    bool extLine_[4];
    bool nmiPending_;
    int pendingSource();         // -1 none
    void checkInterrupts();
    bool intRequested_;

    // timers
    struct Timer { uint16_t count, maxA, maxB, control; int pre; } t_[3];
    int timerPre_;               // CPU clocks towards the next timer tick (CPU/4)
    void timerTick(int which);
    void timersAdvance(int cycles);

    // DMA
    struct Dma { uint32_t src, dst; uint16_t count, control; } d_[2];
    bool drq_[2];
    int dmaService();

    uint16_t chipSel_[5];
    bool inhibitInt_;
    bool dmaTimerReq_[2];
    int64_t tmrInPeriod_[2] = { 0, 0 };
    int64_t tmrInAcc_[2];
};
