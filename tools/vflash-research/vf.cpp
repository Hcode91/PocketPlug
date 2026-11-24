#include <cmath>
#include <tuple>
// V.Flash research harness: boot ROM on the ARM926 interpreter, logging I/O.
#include "cores/gpl32/arm926.h"
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>
using namespace didj;
struct VF : Bus {
    std::vector<u8> rom, sdram, sram, nv = std::vector<u8>(0x2000);
    ARM926* cpu = nullptr;
    std::map<u32, u32> regs;
    unsigned long long cycles = 0; u32 rtcBase = 0;
    int sens = getenv("SENS0") ? 0 : 1;
    // ---- display controller (0xB8000000): 0x00 status (write 1s to clear), 0x04 enable; IRQ 21 ----
    u32 lcdStatus = 0;
    void lcd_int() { int_set(21, lcdStatus != 0); }
    void vblank() { lcdStatus |= 2; lcd_int(); }
    // ---- CD drive (Sony servo chip behind the 0xC4000000 command port) ----
    u32 servoLast = 0;              // last command's address nibble selects what SENS reports
    int sledMode = 0;               // $2x low bits: 2 = move out, 3 = move in
    // ---- disc geometry: the pickup radius is the master state ----
    static constexpr double PITCH = 1.6e-6, VEL = 1.3, R0 = 0.025, RLIMIT = 0.0225, RLEADIN = 0.023, SLED_SPEED = 0.004;
    double rad = 0.0325;                    // metres from the centre
    long discSectors = 271506;              // set from the image size
    int tracking = 0, clv = 0;              // $2x bits 3-2, $Ex low nibble
    int jumpCount = 0;                      // $7x
    unsigned long long brakeAt = 0;
    unsigned long long sledAt = 0, subAt = 0;
    int leadInPtr = 0;
    bool mediaReported = false;
    static double lba_at(double r) { return 75.0 * M_PI * (r * r - R0 * R0) / (PITCH * VEL) - 150; }
    static double rad_at(double lba) { return sqrt(R0 * R0 + (lba + 150) / 75.0 * PITCH * VEL / M_PI); }
    void sled_update() {
        double dt = double(cycles - sledAt) / 150e6;
        sledAt = cycles;
        if (sledMode == 3) rad = std::max(RLIMIT, rad - dt * SLED_SPEED);
        else if (sledMode == 2) rad += dt * SLED_SPEED;
    }
    void jump(int tracks) { sled_update(); rad = std::max(RLIMIT, rad + tracks * PITCH); if (getenv("VFSUB")) printf("jump %d -> lba %.0f\n", tracks, lba_at(rad)); }
    static u8 bcd(int v) { return u8((v / 10) << 4 | (v % 10)); }
    static void msf(long lba, u8* m) { lba += 150; if (lba < 0) lba += 450000; m[0] = bcd(int(lba / 4500)); m[1] = bcd(int(lba / 75 % 60)); m[2] = bcd(int(lba % 75)); }
    bool spinning() { return clv != 0 && clv != 8 && tracking == 1; }
    void cd_int() { int_set(14, (regs[0xc4000064] & regs[0xc4000068]) != 0); }
    void sector_arrived(long lba) { (void)lba; }
    void subcode_tick() {
        if (cycles - subAt < 2000000) return;        // 75 frames per second at 150 MHz
        subAt = cycles;
        if (!spinning()) return;
        sled_update();
        if (rad < RLEADIN) return;                   // inside the lead-in: no pits, no subcode
        long lba = long(floor(lba_at(rad)));
        u8 q[10];
        if (lba < -150) {                            // lead-in: TOC pointers A0, A1, A2, 01, each three times
            int ptr = (leadInPtr++ / 3) % 4;
            u8 m[3]; msf(lba + 450000 - 150, m);
            q[0] = 0x41; q[1] = 0; q[3] = m[0]; q[4] = m[1]; q[5] = m[2]; q[6] = 0;
            if (ptr == 0) { q[2] = 0xa0; q[7] = 0x01; q[8] = 0x00; q[9] = 0; }
            else if (ptr == 1) { q[2] = 0xa1; q[7] = 0x01; q[8] = 0; q[9] = 0; }
            else if (ptr == 2) { q[2] = 0xa2; msf(discSectors, q + 7); }
            else { q[2] = 0x01; q[7] = 0x00; q[8] = 0x02; q[9] = 0x00; }
        } else if (lba >= discSectors) {             // lead-out
            q[0] = 0x41; q[1] = 0xaa; q[2] = 1; msf(lba - discSectors - 150, q + 3); q[6] = 0; msf(lba, q + 7);   // relative from lead-out start
        } else {
            q[0] = 0x41; q[1] = 1; q[2] = lba < 0 ? 0 : 1; msf(lba < 0 ? -lba - 150 : lba - 150, q + 3); q[6] = 0; msf(lba, q + 7);
        }
        regs[0xc4000054] = 1u << 16 | q[0] << 8 | q[1];
        regs[0xc4000058] = u32(q[2]) << 24 | q[3] << 16 | q[4] << 8 | q[5];
        regs[0xc400005c] = u32(q[6]) << 24 | q[7] << 16 | q[8] << 8 | q[9];
        regs[0xc4000064] |= 1u << 5;
        if ((regs[0xc4000068] & 0x4000) && !mediaReported && !getenv("NOMEDIA")) { regs[0xc4000064] |= 1u << 14; mediaReported = true; }   // media monitor: disc found (once per insertion)
        if ((regs[0xc4000060] >> 31) && lba >= 0 && lba < discSectors) {   // CD-ROM decoder: header of the sector passing by
            u8 m[3]; msf(lba, m);
            regs[0xc400004c] = 1u << 31 | m[0] << 16 | m[1] << 8 | m[2];
            static long long hdrFrom = getenv("HDRFROM") ? atoll(getenv("HDRFROM")) : -1;
            if (hdrFrom >= 0 && (long long)(cycles / 150000) >= hdrFrom) regs[0xc4000064] |= 1u << 15;
            sector_arrived(lba);
        }
        cd_int();
        rad = rad_at(lba_at(rad) + 1);              // the disc keeps turning: one frame further out
        if (getenv("VFSUB")) printf("subq %02x %02x %02x %02x:%02x:%02x %02x:%02x:%02x lba %ld t=%llu\n", q[0], q[1], q[2], q[3], q[4], q[5], q[7], q[8], q[9], lba, cycles / 150000);
    }
    void servo_cmd(int bits, u64 v, bool senseOnly = false) {
        sled_update();
        u32 addr = bits >= 4 ? u32(v >> (bits - 4)) & 15 : 0;
        servoLast = addr;
        if (bits == 8 && (v & 0xff) == 0x38) servoLast = 0x38;   // $38: SENS = auto-gain finished
        if (senseOnly) { if (getenv("VFSERVO")) printf("sense %02llx t=%llu\n", (unsigned long long)v, cycles / 150000); return; }   // 0x14 bit 2: only selects what SENS shows
        if (addr == 2) {
            sledMode = v & 3; tracking = (v >> 2) & 3;
            if (tracking == 2) jump(1); else if (tracking == 3) jump(-1);      // single track kicks
        }
        if (addr == 7) jumpCount = int(v >> 4) & 0xffff;
        if (addr == 4 && bits >= 8) {                // auto sequence
            int op = int(v >> (bits - 8)) & 15;
            switch (op) {
            case 0x8: jump(1); break;  case 0x9: jump(-1); break;
            case 0xa: jump(10); break; case 0xb: jump(-10); break;
            case 0xc: jump(2 * jumpCount); break; case 0xd: jump(-2 * jumpCount); break;
            case 0xe: jump(jumpCount); break; case 0xf: jump(-jumpCount); break;
            }
        }
        if (addr == 0xe && bits >= 8) { clv = int(v >> (bits - 8)) & 15; if (clv == 0xa) brakeAt = cycles; }
        if (getenv("VFBT")) { printf("bt:"); u32 sp = cpu->r[13]; for (int k = 0; k < 64; k++) { u32 w; u32 a = sp + k * 4; if (a < 0x10000000 || a >= 0x14000000) break; memcpy(&w, &sdram[a - 0x10000000], 4); if (w >= 0x10010000 && w < 0x100a0000) printf(" %08x", w); } printf("\n"); }
        if (getenv("VFSERVO")) printf("servo %d %09llx addr %x  lba %.0f mode %d lr %08x t=%llu\n", bits, (unsigned long long)v, addr, lba_at(rad), sledMode, cpu->r[14], cycles / 150000);
    }
    // $39 monitor readout: value shifted back out of the servo chip
    u32 servo_read(u64 cmd) {
        u32 v = getenv("RDVAL") ? u32(strtoul(getenv("RDVAL"), 0, 16)) : 0;
        u32 sel = u32(cmd >> 8) & 0xff;
        if ((cmd >> 16) == 0x39 && sel == 0x1f) v = 0xffa0;   // level checked by the offset sweep: target window -103..-89
        if (getenv("VFSERVO")) printf("read %09llx -> %x\n", (unsigned long long)cmd, v);
        return v;
    }
    bool drive_sens() {
        sled_update();
        switch (servoLast) {
        case 3: return rad <= RLIMIT;               // inner limit switch
        case 0x38: return true;                     // auto gain done
        case 0xe: return !(clv == 0xa && cycles - brakeAt > 45000000ull);   // braking: drops once the disc has slowed
        default: return sens;
        }
    }
    u32 gpioD = getenv("GPIOD") ? u32(strtoul(getenv("GPIOD"), 0, 16)) : 5;           // last written value per I/O address
    std::set<u32> seen;
    int logLimit = 4000, logged = 0;
    VF() { regs[0x9009008c] = 0x1230ffab; sdram.assign(64u << 20, 0); sram.assign(64 << 10, 0); }
    u8* page_ptr(u32 pa) override {
        if (pa < rom.size()) return &rom[pa & ~0xfffu];
        if (pa >= 0x10000000 && pa < 0x10000000 + sdram.size()) return &sdram[(pa - 0x10000000) & ~0xfffu];

        if (pa >= 0xf8000000 && pa < 0xf8002000) return &nv[(pa - 0xf8000000) & ~0xfffu];
        return nullptr;
    }
    bool page_readonly(u32 pa) override { return pa < rom.size(); }
    void log(const char* k, u32 a, u32 v) {
        u32 pc = cpu ? cpu->r[15] : 0;
        bool novel = seen.insert(a).second;
        static long long from = getenv("VFFROM") ? atoll(getenv("VFFROM")) : -1;   // ms: log each new (access, address, pc) after this time
        if (from >= 0) {
            static std::set<std::tuple<char, u32, u32>> keys;
            if ((long long)(cycles / 150000) >= from && keys.insert({ k[0], a, pc }).second)
                printf("%s %08x = %08x  pc %08x t=%llu\n", k, a, v, pc, cycles / 150000);
            return;
        }
        if (getenv("VFB8") && (a >> 24) == 0xb8 && k[0] == 'W') { printf("%s %08x = %08x  pc %08x\n", k, a, v, pc); return; }
        if (getenv("VFBC") && (a >> 24) == 0xbc) { printf("%s %08x = %08x  pc %08x t=%llu\n", k, a, v, pc, cycles / 150000); return; }
        if (getenv("VFC4") && (a >> 24) == 0xc4) { printf("%s %08x = %08x  pc %08x  t=%llu\n", k, a, v, pc, cycles / 150000); return; }
        if ((novel || getenv("VFALL")) && logged < logLimit) { logged++; printf("%s %08x = %08x  pc %08x\n", k, a, v, pc); }
    }
    // ---- interrupt controller (classic TI-Nspire / Zevio, after Firebird) ----
    u32 active = 0, noninverted = 0, sticky = 0, sticky_status = 0, raw_status = 0, status = 0;
    u32 mask[2] = {}, prio_limit[2] = { 8, 8 }, prev_limit[2] = {};
    u8 prio[32] = {};
    unsigned long claims[32] = {};
    int current(int f) { u32 ms = status & mask[f]; int lim = int(prio_limit[f]), cur = -1; for (int i = 0; i < 32; i++) if ((ms >> i & 1) && prio[i] < lim) { cur = i; lim = prio[i]; } return cur; }
    void int_update() {
        u32 prev = raw_status;
        raw_status = active ^ ~noninverted;
        sticky_status |= raw_status & ~prev;
        status = (raw_status & ~sticky) | (sticky_status & sticky);
        if (cpu) { cpu->set_irq(current(0) >= 0); cpu->set_fiq(current(1) >= 0); }
    }
    void int_set(int n, bool on) { if (on) active |= 1u << n; else active &= ~(1u << n); int_update(); }
    u32 int_read(u32 a) {
        int g = a >> 8 & 3;
        if (g < 2) {
            switch (a & 0xff) {
            case 0x00: return status & mask[g];
            case 0x04: return status;
            case 0x08: case 0x0c: return mask[g];
            case 0x20: { int c = current(g); return c < 0 ? 0 : c; }
            case 0x24: { int c = current(g); if (c >= 0) claims[c]++; if (c < 0) c = 0; prev_limit[g] = prio_limit[g]; prio_limit[g] = prio[c]; return c; }
            case 0x28: { int c = current(g); if (c < 0 && cpu) { if (g) cpu->set_fiq(false); else cpu->set_irq(false); } return prev_limit[g]; }
            case 0x2c: return prio_limit[g];
            }
        } else if (g == 2) { switch (a & 0xff) { case 0: return noninverted; case 4: return sticky; } return 0; }
        else if (!(a & 0x80)) return prio[a >> 2 & 31];
        return 0;
    }
    void int_write(u32 a, u32 v) {
        int g = a >> 8 & 3;
        if (g < 2) {
            switch (a & 0xff) {
            case 0x04: sticky_status &= ~v; break;
            case 0x08: mask[g] |= v; break;
            case 0x0c: mask[g] &= ~v; break;
            case 0x2c: prio_limit[g] = v & 15; break;
            }
        } else if (g == 2) { if ((a & 0xff) == 0) noninverted = v; else if ((a & 0xff) == 4) sticky = v; }
        else if (!(a & 0x80)) prio[a >> 2 & 31] = u8(v & 7);
        int_update();
    }
    // ---- timers: pairs at 90010000, 900C0000, 900D0000 ----
    struct Tmr { u32 value = 0, start = 0, divider = 0, control = 0x10; int ticks = 0; };
    struct Pair { Tmr t[2]; u32 compl_[6] = {}; u32 int_status = 0, int_mask = 0; } pairs[3];
    Pair* tp(u32 a) { return &pairs[(a >> 16) % 5 % 3]; }
    void tp_int(int i) { int_set(5 + i, pairs[i].int_status & pairs[i].int_mask); }
    void tp_advance(int i, int ticks) {
        Pair& p = pairs[i];
        for (int k = 0; k < 2; k++) {
            Tmr& t = p.t[k];
            if (t.control & 0x10) continue;
            int nt;
            for (nt = t.ticks + ticks; nt > int(t.divider); nt -= int(t.divider) + 1) {
                int c = t.control & 7;
                t.ticks = 0;
                if (c == 0 && t.value == 0) {}
                else if (c != 0 && c != 7 && t.value == p.compl_[c - 1]) t.value = t.start;
                else t.value += (t.control & 8) ? 1 : -1;
                if (k == 0) for (int q = 0; q < 6; q++) if (t.value == p.compl_[q]) { p.int_status |= 1u << q; tp_int(i); }
            }
            t.ticks = nt;
        }
    }
    u32 timer_read(u32 a) {
        Pair* p = tp(a);
        switch (a & 0x3f) {
        case 0x00: return p->t[0].value; case 0x04: return p->t[0].divider; case 0x08: return p->t[0].control;
        case 0x0c: return p->t[1].value; case 0x10: return p->t[1].divider; case 0x14: return p->t[1].control;
        case 0x18: case 0x1c: case 0x20: case 0x24: case 0x28: case 0x2c: return p->compl_[((a & 0x3f) - 0x18) >> 2];
        }
        return 0;
    }
    void timer_write(u32 a, u32 v) {
        Pair* p = tp(a);
        switch (a & 0x3f) {
        case 0x00: p->t[0].start = p->t[0].value = v; break; case 0x04: p->t[0].divider = v; break; case 0x08: p->t[0].control = v & 0x1f; break;
        case 0x0c: p->t[1].start = p->t[1].value = v; break; case 0x10: p->t[1].divider = v; break; case 0x14: p->t[1].control = v & 0x1f; break;
        case 0x18: case 0x1c: case 0x20: case 0x24: case 0x28: case 0x2c: p->compl_[((a & 0x3f) - 0x18) >> 2] = v; break;
        }
    }
    void tick32k() { tp_advance(0, 703); tp_advance(1, 114); }

    u32 io_read(u32 a) {
        if (a >= 0xb8000800 && a < 0xb8001000) { u32 v; memcpy(&v, &sram[a - 0xb8000000], 4); return v; }
        if ((a >> 24) == 0xdc) { u32 v = int_read(a); log("R", a, v); return v; }
        u32 dev = a >> 16;
        if (dev == 0x9001 || dev == 0x900c) { u32 v = timer_read(a); log("R", a, v); return v; }
        if (a == 0xc4000018) { u32 v = drive_sens() ? 3 : 0; log("R", a, v); return v; }
        if (a == 0xb8000000) { u32 v = lcdStatus; log("R", a, v); return v; }
        if (a == 0xc4000040) { u32 v = regs[a] | (spinning() ? 0x65 : 0); log("R", a, v); return v; }
        if (a == 0xb8000004) { u32 v = 0xffffffff; log("R", a, v); return v; }
        if (a == 0xb800208c || a == 0xb8002088) { u32 v = u32(cycles / 300) % 0x600; return v; }   // line counter
        if (a == 0xa1000010) { u32 v = 1; log("R", a, v); return v; }   // serial bus: transfer done
        if (a == 0x900d0018) { u32 v = gpioD; log("R", a, v); return v; }
        if (a == 0x90090000) return rtcBase + u32(cycles / 150000000ull);
        // V.Flash: timer pair status at 900A002C (pair 0) / 900A0030 (pair 1), masks at 0034 / 0038
        if (a == 0x900a002c || a == 0x900a0030) { u32 v = pairs[a == 0x900a0030].int_status; log("R", a, v); return v; }
        if (a == 0x900a0034 || a == 0x900a0038) { u32 v = pairs[a == 0x900a0038].int_mask; log("R", a, v); return v; }
        u32 v = regs.count(a) ? regs[a] : 0;
        // status bits that boot code spins on
        if (a == 0x900b0014) v |= 0x21;            // PLL locked, power key on
        if ((a & 0xfffeffff) == 0x90020014) return 0x60;          // 16550 LSR: transmitter empty
        log("R", a, v);
        return v;
    }
    std::string line;
    void io_write(u32 a, u32 v) {
        if (a == 0xb8000000) { lcdStatus &= ~v; lcd_int(); log("W", a, v); return; }
        if (a == 0xc4000014) {
            if ((v & 1) && !(regs[a] & 1)) {
                u64 cmd = u64(regs[0xc400000c]) << 32 | regs[0xc4000008];
                servo_cmd(int(regs[0xc4000000]), cmd, (v & 4) != 0);
                regs[0xc4000064] |= 1;                    // sent
                if (v & 6) { regs[0xc4000010] = servo_read(cmd); regs[0xc4000064] |= 2; }   // bit 1: completion flag, readback in 0x10
            }
            regs[a] = v & ~1u; log("W", a, v); return;
        }
        if (a >= 0xb8000800 && a < 0xb8001000) { memcpy(&sram[a - 0xb8000000], &v, 4); return; }
        if ((a >> 24) == 0xdc) { int_write(a, v); log("W", a, v); return; }
        { u32 dev = a >> 16;
          if (dev == 0x9001 || dev == 0x900c) { timer_write(a, v); log("W", a, v); return; }
          if (a == 0x90090008) { rtcBase = v - u32(cycles / 150000000ull); log("W", a, v); return; }
          if (a == 0x900a002c || a == 0x900a0030) { int i = a == 0x900a0030; pairs[i].int_status &= ~v; tp_int(i); log("W", a, v); return; }
          if (a == 0x900a0034 || a == 0x900a0038) { int i = a == 0x900a0038; pairs[i].int_mask = v & 0x3f; tp_int(i); log("W", a, v); return; } }
        if ((a & 0xfffeffff) == 0x90020000 && !(regs[(a & 0xffff0000) | 0x0c] & 0x80)) {
            char ch = char(v);
            if (ch == '\n') { printf("[uart%d] %s\n", (a >> 16) & 1 ? 1 : 0, line.c_str()); line.clear(); }
            else if (ch != '\r') line += ch;
            return;
        }
        if (a == 0xc4000064) { regs[a] &= ~v; cd_int(); log("W", a, v); return; }
        if (a == 0xc4000068) { regs[a] = v; cd_int(); log("W", a, v); return; }
        regs[a] = v; log("W", a, v);
    }
    u32 read32(u32 pa) override { u8* p = page_ptr(pa); if (p) { u32 v; memcpy(&v, p + (pa & 0xfff), 4); return v; } return io_read(pa); }
    bool lowSram(u32 pa) { return pa >= 0xb8000800 && pa < 0xb8001000; }
    u16 read16(u32 pa) override { if (lowSram(pa)) { u16 v; memcpy(&v, &sram[pa - 0xb8000000], 2); return v; } u8* p = page_ptr(pa); if (p) { u16 v; memcpy(&v, p + (pa & 0xfff), 2); return v; } return u16(io_read(pa & ~3u) >> ((pa & 2) * 8)); }
    u8 read8(u32 pa) override { if (lowSram(pa)) return sram[pa - 0xb8000000]; u8* p = page_ptr(pa); if (p) return p[pa & 0xfff]; return u8(io_read(pa & ~3u) >> ((pa & 3) * 8)); }
    void write32(u32 pa, u32 v) override { if (pa < rom.size()) return; u8* p = page_ptr(pa); if (p) { memcpy(p + (pa & 0xfff), &v, 4); return; } io_write(pa, v); }
    void write16(u32 pa, u16 v) override { if (lowSram(pa)) { memcpy(&sram[pa - 0xb8000000], &v, 2); return; } if (pa < rom.size()) return; u8* p = page_ptr(pa); if (p) { memcpy(p + (pa & 0xfff), &v, 2); return; } io_write(pa, v); }
    void write8(u32 pa, u8 v) override { if (lowSram(pa)) { sram[pa - 0xb8000000] = v; return; } if (pa < rom.size()) return; u8* p = page_ptr(pa); if (p) { p[pa & 0xfff] = v; return; } io_write(pa, v); }
};
int main(int argc, char** argv) {
    VF vf;
    FILE* f = fopen(argv[1], "rb"); vf.rom.resize(2 << 20); fread(vf.rom.data(), 1, vf.rom.size(), f); fclose(f);
    ARM926 cpu(&vf); vf.cpu = &cpu; cpu.reset();
    long steps = atol(argv[2]);
    // VFWATCH=addr,addr,...: print when execution reaches these addresses (with lr, r0-r2)
    static std::set<u32> watch; static VF* gvf = &vf;
    {
        const char* p = getenv("VFWATCH");
        while (p && *p) { watch.insert(u32(strtoul(p, (char**)&p, 16))); if (*p == ',') p++; else break; }
        if (p) { cpu.hle_base = 0x10000000; cpu.hle_size = 0x00200000; }
        cpu.hle_watch_pc = 0x10010994;   // OS idle task: a pure counting loop, so sleep until the next interrupt
        cpu.hle_hook = [](ARM926* c, u32 pc, void*) -> bool {
            if (pc == 0x10010994 && !getenv("NOIDLE")) { c->halt(); return true; }
            static long long trFrom = getenv("VFTRACE") ? atoll(getenv("VFTRACE")) : -1, trTo = getenv("VFTRACETO") ? atoll(getenv("VFTRACETO")) : 1LL << 60;
            if (trFrom >= 0) {   // VFTRACE=ms[,VFTRACETO=ms]: first execution of each PC in the window
                long long t = (long long)(gvf->cycles / 150000);
                static std::set<u32> seenPc;
                if (t >= trFrom && t < trTo && seenPc.insert(pc).second) printf("tr %08x t=%lld\n", pc, t);
            }
            if (watch.count(pc)) {
                printf("watch %08x lr %08x r0 %08x r1 %08x r2 %08x t=%llu", pc, c->r[14], c->r[0], c->r[1], c->r[2], gvf->cycles / 150000);
                if (const char* pk = getenv("VFPEEK")) {   // VFPEEK=addr,...: memory words shown at each hit
                    while (*pk) { u32 a = u32(strtoul(pk, (char**)&pk, 16)); u32 w = 0; if (a >= 0x10000000 && a < 0x14000000) memcpy(&w, &gvf->sdram[a - 0x10000000], 4); printf(" [%08x]=%08x", a, w); if (*pk == ',') pk++; else break; }
                }
                printf("\n");
            }
            return false;
        };
    }
    std::map<u32, int> hot;
    // ~150 MHz CPU; the 32 kHz timer clock every ~4578 cycles
    for (long i = 0; i < steps; i++) {
        cpu.run(4578);
        vf.cycles += 4578;
        vf.tick32k();
        vf.subcode_tick();
        if (i % 546 == 0) vf.vblank();        // ~60 Hz
        hot[cpu.r[15] & ~0xffu]++;
        if (getenv("VFSAMPLE") && i % 3 == 0 && (cpu.r[15] < 0x10010900 || cpu.r[15] >= 0x10010a00)) {
            printf("sample t=%llu pc %08x lr %08x", vf.cycles / 150000, cpu.r[15], cpu.r[14]);
            if (getenv("VFSTACK")) { u32 sp = cpu.r[13]; for (int k = 0; k < 96; k++) { u32 w, a = sp + k * 4; if (a < 0x10000000 || a >= 0x14000000) break; memcpy(&w, &vf.sdram[a - 0x10000000], 4); if (w >= 0x10010000 && w < 0x100b0000) printf(" %08x", w); } }
            printf("\n");
        }
    }
    if (getenv("DUMP")) { FILE* o = fopen("ram.bin", "wb"); fwrite(vf.sdram.data(), 1, 32 << 20, o); fclose(o); }
    for (int k = 0; k < 32; k++) if (vf.claims[k]) printf("irq %d x%lu\n", k, vf.claims[k]);
    printf("active %08x status %08x mask %08x\n", vf.active, vf.status, vf.mask[0]);
    printf("regs:"); for (int k = 0; k < 16; k++) printf(" r%d=%08x", k, cpu.r[k]); printf(" cpsr=%08x\n", cpu.cpsr);
    printf("pc %08x insns %llu\n", cpu.r[15], (unsigned long long)cpu.insn_count);
    std::vector<std::pair<int, u32>> h; for (auto& [k, v] : hot) h.push_back({ v, k });
    std::sort(h.rbegin(), h.rend()); for (int i = 0; i < 8 && i < (int)h.size(); i++) printf("hot %08x x%d\n", h[i].second, h[i].first);
}
