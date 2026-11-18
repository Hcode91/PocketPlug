// PocketPlug - Intel 4004 CPU with 4001 ROM / 4002 RAM addressing
// SPDX-License-Identifier: BSD-3-Clause
//
// The first microprocessor: 4-bit accumulator and carry, sixteen 4-bit index
// registers, a 12-bit program counter with a three-level return stack. One
// instruction cycle is eight clocks; the two-word instructions (JCN, FIM,
// JUN, JMS, ISZ) and FIN take two.
//
// I/O goes through the address latched by SRC, as on the real chips:
//   4002 RAM data   (bank << 8) | src
//   4002 status     (bank << 6 | src >> 2) & 0x1fc | status number
//   4001 ROM ports  (bank << 8) | src            (the ROM chip is src >> 4)
//   4002 RAM ports  (bank << 2 | src >> 6)
// where bank is the 3-bit value set by DCL.

#pragma once
#include <cstdint>
#include <cstring>

class I4004Bus {
public:
    virtual ~I4004Bus() {}
    virtual uint8_t romRead(uint16_t addr) = 0;               // program memory
    virtual uint8_t romPortRead(uint16_t addr) = 0;           // RDR
    virtual void    romPortWrite(uint16_t addr, uint8_t v) = 0; // WRR
    virtual void    ramPortWrite(uint8_t addr, uint8_t v) = 0;  // WMP
    virtual bool    testPin() = 0;                            // TEST input (true = high)
};

class I4004 {
public:
    explicit I4004(I4004Bus* bus) : bus_(bus) { reset(); }

    void reset()
    {
        a = c = 0;
        memset(r, 0, sizeof r);
        memset(stack, 0, sizeof stack);
        sp = 0;
        src = 0;
        bank = 0;
    }

    // runs one instruction, returns the instruction cycles it took (1 or 2)
    int step()
    {
        uint8_t op = fetch();
        uint8_t opr = op >> 4, opa = op & 0x0f;
        switch (opr) {
        case 0x0:   // NOP (the other 0x0n codes act as NOP on the 4004)
            return 1;
        case 0x1: { // JCN
            uint8_t arg = fetch();
            bool jump = ((opa & 1) && !bus_->testPin()) || ((opa & 2) && c) || ((opa & 4) && !a);
            if (bool(opa & 8) != jump) pcPage(arg);
            return 2;
        }
        case 0x2:
            if (opa & 1) { src = pair(opa >> 1); return 1; }   // SRC
            pair(opa >> 1) = fetch();                           // FIM
            return 2;
        case 0x3:
            if (opa & 1) { pcPage(pair(opa >> 1)); return 1; }  // JIN
            pair(opa >> 1) = bus_->romRead((pc() & 0xf00) | pair(0));   // FIN
            return 2;
        case 0x4: { // JUN
            uint8_t arg = fetch();
            pc() = uint16_t(((opa << 8) | arg) & 0xfff);
            return 2;
        }
        case 0x5: { // JMS
            uint8_t arg = fetch();
            sp = (sp + 1) & 3;
            pc() = uint16_t(((opa << 8) | arg) & 0xfff);
            return 2;
        }
        case 0x6: setReg(opa, reg(opa) + 1); return 1;     // INC
        case 0x7: { // ISZ
            uint8_t arg = fetch();
            uint8_t v = (reg(opa) + 1) & 0x0f;
            setReg(opa, v);
            if (v) pcPage(arg);
            return 2;
        }
        case 0x8: setAC(a + reg(opa) + c); return 1;                    // ADD
        case 0x9: setAC(a + (reg(opa) ^ 0x0f) + (c ^ 1)); return 1;     // SUB
        case 0xa: a = reg(opa); return 1;                               // LD
        case 0xb: { uint8_t t = a; a = reg(opa); setReg(opa, t); return 1; }  // XCH
        case 0xc: sp = (sp - 1) & 3; a = opa; return 1;                 // BBL
        case 0xd: a = opa; return 1;                                    // LDM
        case 0xe: io(opa); return 1;
        case 0xf:
            switch (opa) {
            case 0x0: a = 0; c = 0; break;                      // CLB
            case 0x1: c = 0; break;                             // CLC
            case 0x2: setAC(a + 1); break;                      // IAC
            case 0x3: c ^= 1; break;                            // CMC
            case 0x4: a ^= 0x0f; break;                         // CMA
            case 0x5: setAC((a << 1) | c); break;               // RAL
            case 0x6: { uint8_t o = a & 1; a = uint8_t((a >> 1) | (c << 3)); c = o; break; }  // RAR
            case 0x7: a = c; c = 0; break;                      // TCC
            case 0x8: setAC(a + 0x0f); break;                   // DAC
            case 0x9: a = 9 + c; c = 0; break;                  // TCS
            case 0xa: c = 1; break;                             // STC
            case 0xb:                                           // DAA
                if (c || a > 9) { uint8_t v = a + 6; a = v & 0x0f; if (v & 0x10) c = 1; }
                break;
            case 0xc: {                                         // KBP
                static const uint8_t kbp[16] = { 0, 1, 2, 15, 3, 15, 15, 15, 4, 15, 15, 15, 15, 15, 15, 15 };
                a = kbp[a];
                break;
            }
            case 0xd: bank = a & 7; break;                      // DCL
            default: break;
            }
            return 1;
        }
        return 1;
    }

    // 4002 RAM: 8 banks x 256 characters, and their status characters
    uint8_t ram[8 * 256];
    uint8_t status[0x200];

    uint8_t a, c;
    uint8_t r[8];          // index register pairs: R(2n) high nibble, R(2n+1) low
    uint16_t stack[4];     // the PC is stack[sp]
    uint8_t sp;
    uint8_t src, bank;

    uint16_t& pc() { return stack[sp]; }

private:
    I4004Bus* bus_;

    uint8_t fetch() { uint8_t v = bus_->romRead(pc()); pc() = (pc() + 1) & 0xfff; return v; }
    void pcPage(uint8_t lo) { pc() = uint16_t((pc() & 0xf00) | lo); }
    uint8_t& pair(int n) { return r[n & 7]; }
    uint8_t reg(int n) const { return (r[n >> 1] >> ((n & 1) ? 0 : 4)) & 0x0f; }
    void setReg(int n, int v)
    {
        uint8_t& p = r[n >> 1];
        if (n & 1) p = uint8_t((p & 0xf0) | (v & 0x0f));
        else p = uint8_t((p & 0x0f) | ((v & 0x0f) << 4));
    }
    void setAC(int v) { a = v & 0x0f; c = (v >> 4) & 1; }

    uint16_t ramAddr() const { return uint16_t((bank << 8) | src); }
    uint16_t statusAddr(int n) const { return uint16_t((((bank << 6) | (src >> 2)) & 0x1fc) | (n & 3)); }

    void io(uint8_t opa)
    {
        switch (opa) {
        case 0x0: ram[ramAddr()] = a; break;                                    // WRM
        case 0x1: bus_->ramPortWrite(uint8_t(((bank << 2) & 0x1c) | (src >> 6)), a); break;  // WMP
        case 0x2: bus_->romPortWrite(uint16_t((bank << 8) | src), a); break;     // WRR
        case 0x3: break;                                                        // WPM
        case 0x4: case 0x5: case 0x6: case 0x7: status[statusAddr(opa)] = a; break;  // WR0-3
        case 0x8: setAC(a + (ram[ramAddr()] ^ 0x0f) + (c ^ 1)); break;          // SBM
        case 0x9: a = ram[ramAddr()] & 0x0f; break;                             // RDM
        case 0xa: a = bus_->romPortRead(uint16_t((bank << 8) | src)) & 0x0f; break;  // RDR
        case 0xb: setAC(a + (ram[ramAddr()] & 0x0f) + c); break;                // ADM
        default: a = status[statusAddr(opa)] & 0x0f; break;                     // RD0-3
        }
    }
};
