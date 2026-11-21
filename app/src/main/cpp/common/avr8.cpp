// PocketPlug - Atmel AVR8 (ATmega88) CPU. See avr8.h.
// SPDX-License-Identifier: BSD-3-Clause

#include "avr8.h"

namespace {
enum : uint8_t { FC = 1, FZ = 2, FN = 4, FV = 8, FS = 16, FH = 32, FT = 64, FI = 128 };
enum : uint16_t {
    PINB = 0x23, PORTB = 0x25, PINC = 0x26, PORTC = 0x28, PIND = 0x29, PORTD = 0x2b,
    TIFR2 = 0x37, EECR = 0x3f, EEDR = 0x40, EEARL = 0x41, EEARH = 0x42,
    TIMSK2 = 0x70, TCCR2A = 0xb0, TCCR2B = 0xb1, TCNT2 = 0xb2, OCR2A = 0xb3, OCR2B = 0xb4,
};
const int kT2Prescale[8] = { 0, 1, 8, 32, 64, 128, 256, 1024 };
bool unhandled(uint16_t a)
{
    return (a >= 0x44 && a <= 0x48) || a == 0x6e || a == 0x35 ||       // timer 0
           (a >= 0x80 && a <= 0x8b) || a == 0x6f || a == 0x36 ||       // timer 1
           (a >= 0x78 && a <= 0x7e) || (a >= 0xc0 && a <= 0xc6) ||     // ADC, USART
           (a >= 0x4c && a <= 0x4e) || (a >= 0xb8 && a <= 0xbd) ||     // SPI, TWI
           a == 0x68 || a == 0x69 || a == 0x3d || a == 0x3b || a == 0x6b || a == 0x6c || a == 0x6d; // ext / pin-change ints
}
}

void AVR8::reset()
{
    uint8_t ee[0x200]; std::memcpy(ee, s.eeprom, sizeof(ee));
    uint64_t cyc = s.cycles;
    std::memset(&s, 0, sizeof(s));
    std::memcpy(s.eeprom, ee, sizeof(ee));
    s.cycles = cyc;
    setSp(0x4ff);
    s.pc = 0;
}

uint8_t AVR8::rd(uint16_t a)
{
    if (a >= sizeof(s.data)) return 0;
    switch (a) {
    case PINB: return portIn(PB);
    case PINC: return portIn(PC);
    case PIND: return portIn(PD);
    default: break;
    }
    if (unknownIo && unhandled(a)) unknownIo(a, false, s.data[a]);
    return s.data[a];
}

void AVR8::wr(uint16_t a, uint8_t d)
{
    if (a >= sizeof(s.data)) return;
    switch (a) {
    case PORTB: s.data[a] = d; portOut(PB, d); return;
    case PORTC: s.data[a] = d; portOut(PC, d); return;
    case PORTD: s.data[a] = d; portOut(PD, d); return;
    case PINB: s.data[PORTB] ^= d; portOut(PB, s.data[PORTB]); return;   // writing PINx toggles PORTx
    case PINC: s.data[PORTC] ^= d; portOut(PC, s.data[PORTC]); return;
    case PIND: s.data[PORTD] ^= d; portOut(PD, s.data[PORTD]); return;
    case TIFR2: s.data[a] &= uint8_t(~d & 7); return;                      // write 1 to clear
    case EECR: {
        uint16_t ea = uint16_t((s.data[EEARL] | s.data[EEARH] << 8) & 0x1ff);
        if (d & 1) s.data[EEDR] = s.eeprom[ea];                             // EERE
        if ((d & 2) && (s.data[EECR] & 4)) s.eeprom[ea] = s.data[EEDR];   // EEPE after EEMPE
        s.data[a] = uint8_t(d & 0x3c);                                     // EEPE / EERE read back 0
        return;
    }
    default: break;
    }
    if (unknownIo && unhandled(a)) unknownIo(a, true, d);
    s.data[a] = d;
}

void AVR8::timers(int n)
{
    int presc = kT2Prescale[s.data[TCCR2B] & 7];
    if (!presc) return;
    s.t2Count += n;
    while (s.t2Count >= presc) {
        s.t2Count -= presc;
        int wgm = (s.data[TCCR2A] & 3) | (s.data[TCCR2B] >> 1 & 4);
        uint8_t cnt = s.data[TCNT2];
        bool ctc = wgm == 2 || wgm == 7;
        uint8_t top = ctc ? s.data[OCR2A] : 0xff;
        if (cnt == s.data[OCR2A]) s.data[TIFR2] |= 2;
        if (cnt == s.data[OCR2B]) s.data[TIFR2] |= 4;
        if (cnt == top) {
            s.data[TCNT2] = 0;
            if (cnt == 0xff || wgm == 7) s.data[TIFR2] |= 1;
        } else s.data[TCNT2] = uint8_t(cnt + 1);
    }
}

bool AVR8::irq()
{
    if (!(sreg() & FI)) return false;
    uint8_t pend = s.data[TIFR2] & s.data[TIMSK2] & 7;
    if (!pend) return false;
    // lowest vector first: COMPA (7), COMPB (8), OVF (9)
    int vec; uint8_t bit;
    if (pend & 2) { vec = 7; bit = 2; } else if (pend & 4) { vec = 8; bit = 4; } else { vec = 9; bit = 1; }
    s.data[TIFR2] &= uint8_t(~bit);
    s.sleeping = 0;
    pushPc(s.pc);
    sreg() &= uint8_t(~FI);
    s.pc = uint16_t(vec);
    return true;
}

int AVR8::run(int cycles)
{
    int done = 0;
    while (done < cycles) {
        int n;
        if (!s.skipIrq && irq()) n = 4;
        else {
            s.skipIrq = 0;
            n = s.sleeping ? 1 : step();
        }
        timers(n);
        s.cycles += uint64_t(n);
        done += n;
    }
    return done;
}

bool AVR8::isTwoWord(uint16_t op) const
{
    return (op & 0xfe0f) == 0x9000 || (op & 0xfe0f) == 0x9200 || (op & 0xfe0e) == 0x940c || (op & 0xfe0e) == 0x940e;
}

int AVR8::step()
{
    uint16_t op = fetch();
    uint8_t& f = sreg();
    auto setZNS = [&](uint8_t res) {
        f = uint8_t(f & ~(FZ | FN | FS));
        if (!res) f |= FZ;
        if (res & 0x80) f |= FN;
        if (((f & FN) != 0) != ((f & FV) != 0)) f |= FS;
    };
    auto add = [&](uint8_t a, uint8_t b, int c) -> uint8_t {
        uint8_t res = uint8_t(a + b + c);
        f = uint8_t(f & ~(FH | FV | FC));
        uint8_t carries = uint8_t((a & b) | (b & ~res) | (~res & a));
        if (carries & 0x08) f |= FH;
        if (carries & 0x80) f |= FC;
        if ((a & b & ~res & 0x80) || (~a & ~b & res & 0x80)) f |= FV;
        setZNS(res);
        return res;
    };
    auto sub = [&](uint8_t a, uint8_t b, int c, bool keepZ) -> uint8_t {
        uint8_t res = uint8_t(a - b - c);
        bool z = (f & FZ) != 0;
        f = uint8_t(f & ~(FH | FV | FC));
        uint8_t borrows = uint8_t((~a & b) | (b & res) | (res & ~a));
        if (borrows & 0x08) f |= FH;
        if (borrows & 0x80) f |= FC;
        if ((a & ~b & ~res & 0x80) || (~a & b & res & 0x80)) f |= FV;
        setZNS(res);
        if (keepZ && !z) f &= uint8_t(~FZ);
        return res;
    };
    auto logic = [&](uint8_t res) { f &= uint8_t(~FV); setZNS(res); return res; };
    auto skip = [&]() -> int { uint16_t nx = peek(s.pc); s.pc = uint16_t(s.pc + (isTwoWord(nx) ? 2 : 1)); return isTwoWord(nx) ? 2 : 1; };
    auto X = [&]() { return uint16_t(r(26) | r(27) << 8); };
    auto Y = [&]() { return uint16_t(r(28) | r(29) << 8); };
    auto Z = [&]() { return uint16_t(r(30) | r(31) << 8); };
    auto setPair = [&](int lo, uint16_t v) { r(lo) = uint8_t(v); r(lo + 1) = uint8_t(v >> 8); };

    int d5 = op >> 4 & 0x1f;
    int r5 = (op & 0xf) | (op >> 5 & 0x10);
    int d4 = 16 + (op >> 4 & 0xf);
    uint8_t K = uint8_t((op & 0xf) | (op >> 4 & 0xf0));

    switch (op >> 12) {
    case 0x0:
        switch (op >> 10 & 3) {
        case 0:
            switch (op >> 8 & 3) {
            case 0: return 1;  // NOP
            case 1: { int d = (op >> 4 & 0xf) * 2, rr = (op & 0xf) * 2; r(d) = r(rr); r(d + 1) = r(rr + 1); return 1; } // MOVW
            case 2: { // MULS
                int16_t res = int16_t(int8_t(r(d4)) * int8_t(r(16 + (op & 0xf))));
                setPair(0, uint16_t(res));
                f = uint8_t(f & ~(FC | FZ)); if (res & 0x8000) f |= FC; if (!res) f |= FZ;
                return 2;
            }
            default: { // MULSU, FMUL, FMULS, FMULSU
                int a = 16 + (op >> 4 & 7), b = 16 + (op & 7);
                int32_t res; bool frac = false;
                switch ((op >> 3 & 1) | (op >> 6 & 2)) {
                case 0: res = int8_t(r(a)) * int32_t(r(b)); break;                 // MULSU
                case 1: res = int32_t(r(a)) * r(b); frac = true; break;             // FMUL
                case 2: res = int8_t(r(a)) * int8_t(r(b)); frac = true; break;      // FMULS
                default: res = int8_t(r(a)) * int32_t(r(b)); frac = true; break;    // FMULSU
                }
                uint16_t u = uint16_t(res);
                f = uint8_t(f & ~(FC | FZ)); if (u & 0x8000) f |= FC;
                if (frac) u = uint16_t(u << 1);
                if (!u) f |= FZ;
                setPair(0, u);
                return 2;
            }
            }
        case 1: sub(r(d5), r(r5), f & FC, true); return 1;          // CPC
        case 2: r(d5) = sub(r(d5), r(r5), f & FC, true); return 1;  // SBC
        default: r(d5) = add(r(d5), r(r5), 0); return 1;           // ADD / LSL
        }
    case 0x1:
        switch (op >> 10 & 3) {
        case 0: if (r(d5) == r(r5)) return 1 + skip(); return 1;  // CPSE
        case 1: sub(r(d5), r(r5), 0, false); return 1;            // CP
        case 2: r(d5) = sub(r(d5), r(r5), 0, false); return 1;    // SUB
        default: r(d5) = add(r(d5), r(r5), f & FC); return 1;    // ADC / ROL
        }
    case 0x2:
        switch (op >> 10 & 3) {
        case 0: r(d5) = logic(r(d5) & r(r5)); return 1;  // AND
        case 1: r(d5) = logic(r(d5) ^ r(r5)); return 1;  // EOR
        case 2: r(d5) = logic(r(d5) | r(r5)); return 1;  // OR
        default: r(d5) = r(r5); return 1;                // MOV
        }
    case 0x3: sub(r(d4), K, 0, false); return 1;                // CPI
    case 0x4: r(d4) = sub(r(d4), K, f & FC, true); return 1;    // SBCI
    case 0x5: r(d4) = sub(r(d4), K, 0, false); return 1;        // SUBI
    case 0x6: r(d4) = logic(r(d4) | K); return 1;               // ORI
    case 0x7: r(d4) = logic(r(d4) & K); return 1;               // ANDI
    case 0x8: case 0xa: {   // LDD / STD (Y or Z + q)
        int q = (op & 7) | (op >> 7 & 0x18) | (op >> 8 & 0x20);
        uint16_t a = uint16_t(((op & 8) ? Y() : Z()) + q);
        if (op & 0x200) wr(a, r(d5)); else r(d5) = rd(a);
        return 2;
    }
    case 0x9:
        if ((op & 0xfc00) == 0x9000) {   // loads / stores
            bool st = op & 0x200;
            switch (op & 0xf) {
            case 0x0: { uint16_t a = fetch(); if (st) wr(a, r(d5)); else r(d5) = rd(a); return 2; }  // LDS / STS
            case 0x1: { uint16_t a = Z(); if (st) wr(a, r(d5)); else r(d5) = rd(a); setPair(30, uint16_t(a + 1)); return 2; }
            case 0x2: { uint16_t a = uint16_t(Z() - 1); setPair(30, a); if (st) wr(a, r(d5)); else r(d5) = rd(a); return 2; }
            case 0x4: case 0x5: case 0x6: case 0x7:
                if (st) return 1;   // XCH / LAS / LAC / LAT: not on the ATmega88
                {   // LPM / ELPM Rd, Z(+)
                    uint16_t z = Z();
                    uint16_t w = uint16_t(z >> 1) < m_romWords ? uint16_t(z >> 1) : 0;
                    r(d5) = m_rom[w * 2 + (z & 1)];
                    if (op & 1) setPair(30, uint16_t(z + 1));
                    return 3;
                }
            case 0x9: { uint16_t a = Y(); if (st) wr(a, r(d5)); else r(d5) = rd(a); setPair(28, uint16_t(a + 1)); return 2; }
            case 0xa: { uint16_t a = uint16_t(Y() - 1); setPair(28, a); if (st) wr(a, r(d5)); else r(d5) = rd(a); return 2; }
            case 0xc: { uint16_t a = X(); if (st) wr(a, r(d5)); else r(d5) = rd(a); return 2; }
            case 0xd: { uint16_t a = X(); if (st) wr(a, r(d5)); else r(d5) = rd(a); setPair(26, uint16_t(a + 1)); return 2; }
            case 0xe: { uint16_t a = uint16_t(X() - 1); setPair(26, a); if (st) wr(a, r(d5)); else r(d5) = rd(a); return 2; }
            case 0xf: if (st) push(r(d5)); else r(d5) = pop(); return 2;   // PUSH / POP
            default: return 1;
            }
        }
        if ((op & 0xfe00) == 0x9400) {
            switch (op & 0xf) {
            case 0x0: { uint8_t v = uint8_t(~r(d5)); f = uint8_t((f & ~FV) | FC); setZNS(v); r(d5) = v; return 1; }   // COM
            case 0x1: r(d5) = sub(0, r(d5), 0, false); return 1;                                                    // NEG
            case 0x2: r(d5) = uint8_t(r(d5) << 4 | r(d5) >> 4); return 1;                                            // SWAP
            case 0x3: { uint8_t v = uint8_t(r(d5) + 1); f &= uint8_t(~FV); if (v == 0x80) f |= FV; setZNS(v); r(d5) = v; return 1; } // INC
            case 0x5: case 0x6: case 0x7: {   // ASR, LSR, ROR
                uint8_t a = r(d5), v;
                if ((op & 0xf) == 5) v = uint8_t((a >> 1) | (a & 0x80));
                else if ((op & 0xf) == 6) v = uint8_t(a >> 1);
                else v = uint8_t((a >> 1) | ((f & FC) ? 0x80 : 0));
                f = uint8_t(f & ~(FC | FV | FZ | FN | FS));
                if (a & 1) f |= FC;
                if (!v) f |= FZ;
                if (v & 0x80) f |= FN;
                if (((f & FN) != 0) != ((f & FC) != 0)) f |= FV;
                if (((f & FN) != 0) != ((f & FV) != 0)) f |= FS;
                r(d5) = v;
                return 1;
            }
            case 0x8:
                if ((op & 0xff0f) == 0x9408) { int b = op >> 4 & 7; if (op & 0x80) f &= uint8_t(~(1 << b)); else { f |= uint8_t(1 << b); if (b == 7) s.skipIrq = 1; } return 1; } // BSET / BCLR
                switch (op) {
                case 0x9508: s.pc = popPc(); return 4;                                  // RET
                case 0x9518: s.pc = popPc(); f |= FI; s.skipIrq = 1; return 4;          // RETI
                case 0x9588: s.sleeping = 1; return 1;                                  // SLEEP
                case 0x95a8: return 1;                                                  // WDR
                case 0x95c8: case 0x95d8: { uint16_t z = Z(); uint16_t w = uint16_t(z >> 1) < m_romWords ? uint16_t(z >> 1) : 0; r(0) = m_rom[w * 2 + (z & 1)]; return 3; } // LPM
                default: return 1;                                                      // BREAK, SPM
                }
            case 0x9:
                if (op == 0x9409) { s.pc = Z(); return 2; }                     // IJMP
                if (op == 0x9509) { pushPc(s.pc); s.pc = Z(); return 3; }       // ICALL
                return 1;
            case 0xa: { uint8_t v = uint8_t(r(d5) - 1); f &= uint8_t(~FV); if (v == 0x7f) f |= FV; setZNS(v); r(d5) = v; return 1; } // DEC
            case 0xc: case 0xd: { uint16_t k = fetch(); s.pc = k; return 3; }                    // JMP
            case 0xe: case 0xf: { uint16_t k = fetch(); pushPc(s.pc); s.pc = k; return 4; }      // CALL
            default: return 1;
            }
        }
        if ((op & 0xfe00) == 0x9600) {   // ADIW / SBIW
            int d = 24 + (op >> 4 & 3) * 2;
            uint16_t k = uint16_t((op & 0xf) | (op >> 2 & 0x30));
            uint16_t a = uint16_t(r(d) | r(d + 1) << 8), res;
            f = uint8_t(f & ~(FC | FZ | FN | FV | FS));
            if (!(op & 0x100)) {
                res = uint16_t(a + k);
                if (~a & res & 0x8000) f |= FV;
                if (~res & a & 0x8000) f |= FC;
            } else {
                res = uint16_t(a - k);
                if (a & ~res & 0x8000) f |= FV;
                if (res & ~a & 0x8000) f |= FC;
            }
            if (!res) f |= FZ;
            if (res & 0x8000) f |= FN;
            if (((f & FN) != 0) != ((f & FV) != 0)) f |= FS;
            setPair(d, res);
            return 2;
        }
        if ((op & 0xfc00) == 0x9800) {   // CBI, SBIC, SBI, SBIS
            uint16_t a = uint16_t(0x20 + (op >> 3 & 0x1f));
            int b = op & 7;
            switch (op >> 8 & 3) {
            case 0: wr(a, uint8_t(rd(a) & ~(1 << b))); return 2;
            case 1: if (!(rd(a) >> b & 1)) return 1 + skip(); return 1;
            case 2: wr(a, uint8_t(rd(a) | 1 << b)); return 2;
            default: if (rd(a) >> b & 1) return 1 + skip(); return 1;
            }
        }
        {   // MUL
            uint16_t res = uint16_t(r(d5) * r(r5));
            setPair(0, res);
            f = uint8_t(f & ~(FC | FZ)); if (res & 0x8000) f |= FC; if (!res) f |= FZ;
            return 2;
        }
    case 0xb: {   // IN / OUT
        uint16_t a = uint16_t(0x20 + ((op & 0xf) | (op >> 5 & 0x30)));
        if (op & 0x800) wr(a, r(d5)); else r(d5) = rd(a);
        return 1;
    }
    case 0xc: { int k = op & 0xfff; if (k & 0x800) k -= 0x1000; s.pc = uint16_t(s.pc + k); return 2; }                   // RJMP
    case 0xd: { int k = op & 0xfff; if (k & 0x800) k -= 0x1000; pushPc(s.pc); s.pc = uint16_t(s.pc + k); return 3; }     // RCALL
    case 0xe: r(d4) = K; return 1;   // LDI
    default: {   // 0xf
        if (!(op & 0x800)) {   // BRBS / BRBC
            int b = op & 7;
            int k = op >> 3 & 0x7f; if (k & 0x40) k -= 0x80;
            bool set = (f >> b & 1) != 0;
            if (set == !(op & 0x400)) { s.pc = uint16_t(s.pc + k); return 2; }
            return 1;
        }
        int b = op & 7;
        switch (op >> 9 & 3) {
        case 0: r(d5) = uint8_t((r(d5) & ~(1 << b)) | ((f & FT) ? 1 << b : 0)); return 1;   // BLD
        case 1: f = uint8_t((f & ~FT) | ((r(d5) >> b & 1) ? FT : 0)); return 1;            // BST
        case 2: if (!(r(d5) >> b & 1)) return 1 + skip(); return 1;                          // SBRC
        default: if (r(d5) >> b & 1) return 1 + skip(); return 1;                            // SBRS
        }
    }
    }
}
