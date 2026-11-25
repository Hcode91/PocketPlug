#include <cmath>
#include <tuple>
#include <map>
#include <deque>
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
    static constexpr double PITCH = 1.6e-6, VEL = 0.952, R0 = 0.025, RLIMIT = 0.0225, RLEADIN = 0.023, SLED_SPEED = 0.004;
    double rad = 0.0325;                    // metres from the centre
    long discSectors = 271506;              // set from the image size
    int tracking = 0, clv = 0;              // $2x bits 3-2, $Ex low nibble
    int jumpCount = 0;                      // $7x
    int traverseTarget = 0; double traverseFrom = 0;
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
    // Each jump covers a little more than one groove so the boot ROM's pause/fine seek converges:
    // it allows ~14 frames of play between a kick and its next position check.
    double jumpScale = getenv("JUMPSCALE") ? atof(getenv("JUMPSCALE")) : 1.6;
    void jump(int tracks) { sled_update(); rad = std::max(RLIMIT, rad + tracks * PITCH * (tracks == 1 || tracks == -1 ? jumpScale : 1.0)); if (getenv("VFSUB")) printf("jump %d -> lba %.0f\n", tracks, lba_at(rad)); }
    static u8 bcd(int v) { return u8((v / 10) << 4 | (v % 10)); }
    static void msf(long lba, u8* m) { lba += 150; if (lba < 0) lba += 450000; m[0] = bcd(int(lba / 4500)); m[1] = bcd(int(lba / 75 % 60)); m[2] = bcd(int(lba % 75)); }
    bool spinning() { return clv != 0 && clv != 8 && tracking == 1; }
    // ---- minimal command list interpreter: 2D space = RAM at chip register 0x98 (VRAM_BASE as fallback), block-linear, RGB555 ----
    static constexpr u32 VRAM_BASE = 0x10048000;
    u32 gfxReg[256] = {};
    // The 2D space is block-linear: 64-byte x 8-row blocks, 32 blocks per block row; base = chip register 0x98.
    u16* vram_px(int x, int y) { u32 base = regs.count(0xa8000098) && regs[0xa8000098] ? regs[0xa8000098] : VRAM_BASE; if (x < 0 || y < 0 || x >= 1024) return nullptr; u32 xb = u32(x) * 2, a = base + (u32(y) / 8) * 16384 + (xb / 64) * 512 + (u32(y) % 8) * 64 + xb % 64; if (a < 0x10000000 || a + 2 > 0x10000000 + sdram.size()) return nullptr; return (u16*)&sdram[a - 0x10000000]; }
    u32 rd32(u32 a) { u32 w = 0; if (a >= 0x10000000 && a + 4 <= 0x10000000 + sdram.size()) memcpy(&w, &sdram[a - 0x10000000], 4); return w; }
    u32 fillPos = 0, fillSize = 0, fillColour = 0;
    std::map<u32, int> seenSub;
    void d4_scan(u32 a, int n, int depth) {
        for (int k = 0; k < n; k++) {
            u32 w = rd32(a + 4 * k);
            if (depth == 0 && k > 0 && (w & 0xff0000ff) == 0x14000001) break;
            if ((w >> 24) == 0xd4 && (rd32(a + 4 * k + 12) >> 24) == 0x0e)
                printf("upload %08x pos %08x size %08x %08x src %08x end %08x at %08x t=%llu\n", w, rd32(a + 4 * k + 4), rd32(a + 4 * k + 8), rd32(a + 4 * k + 12), rd32(a + 4 * k + 16), rd32(a + 4 * k + 20), a + 4 * k, cycles / 150000);
            if (w == 0x09000000 && depth < 3) { u32 t = rd32(a + 4 * k + 4); if (t >= 0x10000000 && t < 0x12000000) d4_scan(t, 64, depth + 1); k++; }
        }
    }
    // ---- experimental 3D renderer (VFRENDER=ms: rasterise lists from that time on; see NOTES "first menu render") ----
    // Vertex floats: 1 sign, 6 exponent (bias 31), 9 mantissa. Projection constants are guesses.
    struct RnState { float M[9] = {1,0,0,0,1,0,0,0,1}, T[3] = {0,0,0}, S[3] = {1,1,1}; bool ident = false; u32 tex = 0, fmt = 0, pal = 0, vb = 0, cap = 0, org = 0, clip0 = 0, clip1 = 0x000ef1ff; } rn;
    std::vector<float> rnZ; long long rnTris = 0, rnFrames = 0;
    static float rn_f(u16 h) { int e = (h >> 9) & 63; if (!e) return 0; float v = (1 + (h & 511) / 512.0f) * ldexpf(1.0f, e - 31); return (h & 0x8000) ? -v : v; }
    static u16 rn_enc(float f) { if (f == 0 || f != f) return 0; u16 s = f < 0 ? 0x8000 : 0; f = fabsf(f); int e; float m = frexpf(f, &e); /* f = m * 2^e, m in [0.5,1) */ int ex = e - 1 + 31; if (ex <= 0) return 0; if (ex > 63) ex = 63; int man = int((m * 2 - 1) * 512); if (man > 511) man = 511; return s | u16(ex << 9) | u16(man); }
    u8* rn_byte(int xb, int y) { u32 base = regs.count(0xa8000098) && regs[0xa8000098] ? regs[0xa8000098] : VRAM_BASE; if (xb < 0 || y < 0 || xb >= 2048) return nullptr; u32 a = base + (u32(y) / 8) * 16384 + (u32(xb) / 64) * 512 + (u32(y) % 8) * 64 + u32(xb) % 64; if (a < 0x10000000 || a >= 0x10000000 + sdram.size()) return nullptr; return &sdram[a - 0x10000000]; }
    struct RnV { float x, y, z, u, v; };
    void rn_tri(const RnV& a, const RnV& b, const RnV& c) {
        const float F = 420.0f, NEARZ = 40.0f;
        if (a.z < NEARZ || b.z < NEARZ || c.z < NEARZ) return;
        int cx0 = rn.clip0 & 0xfff, cy0 = (rn.clip0 >> 12) & 0xfff, cx1 = rn.clip1 & 0xfff, cy1 = (rn.clip1 >> 12) & 0xfff;
        float ox = rn.org ? float(rn.org & 0xfff) : cx0 + 256.0f, oy = rn.org ? float((rn.org >> 12) & 0xfff) : cy0 + 120.0f;   // 81 = projection centre
        float X[3] = { ox + F * a.x / a.z, ox + F * b.x / b.z, ox + F * c.x / c.z }, Y[3] = { oy + 0.47f * F * a.y / a.z, oy + 0.47f * F * b.y / b.z, oy + 0.47f * F * c.y / c.z };
        float iz[3] = { 1 / a.z, 1 / b.z, 1 / c.z }, U[3] = { a.u * iz[0], b.u * iz[1], c.u * iz[2] }, V[3] = { a.v * iz[0], b.v * iz[1], c.v * iz[2] };
        float den = (Y[1] - Y[2]) * (X[0] - X[2]) + (X[2] - X[1]) * (Y[0] - Y[2]); if (fabsf(den) < 1e-6f) return;
        int x0 = std::max(cx0, (int)floorf(std::min({X[0], X[1], X[2]}))), x1 = std::min(cx1, (int)ceilf(std::max({X[0], X[1], X[2]})));
        int y0 = std::max(cy0, (int)floorf(std::min({Y[0], Y[1], Y[2]}))), y1 = std::min(cy1, (int)ceilf(std::max({Y[0], Y[1], Y[2]})));
        if (x1 < x0 || y1 < y0) return;
        int tw = 8 << (rn.fmt & 7), th = 8 << ((rn.fmt >> 3) & 7);
        int tx = (rn.tex & 0xfff) * 2, ty = (rn.tex >> 12) & 0xfff, px = (rn.pal & 0xfff) * 2, py = (rn.pal >> 12) & 0xfff;
        if (ty >= 2048) ty -= 240; if (py >= 2048) py -= 240;      // rows 2048+ sit 240 rows lower in memory
        if (rnZ.empty()) rnZ.assign(1024 * 1024, 1e30f);
        rnTris++;
        for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
            float gx = x + 0.5f, gy = y + 0.5f;
            float l0 = ((Y[1] - Y[2]) * (gx - X[2]) + (X[2] - X[1]) * (gy - Y[2])) / den, l1 = ((Y[2] - Y[0]) * (gx - X[2]) + (X[0] - X[2]) * (gy - Y[2])) / den, l2 = 1 - l0 - l1;
            if (l0 < 0 || l1 < 0 || l2 < 0) continue;
            float w = l0 * iz[0] + l1 * iz[1] + l2 * iz[2], z = 1 / w;
            float& zb = rnZ[(y & 1023) * 1024 + (x & 1023)]; if (z >= zb) continue;
            float u = (l0 * U[0] + l1 * U[1] + l2 * U[2]) * z, v = 1 - (l0 * V[0] + l1 * V[1] + l2 * V[2]) * z;
            int iu = ((int)floorf(u * tw) % tw + tw) % tw, iv = ((int)floorf(v * th) % th + th) % th;
            u8* t = rn_byte(tx + iu, ty + iv); if (!t || !*t) continue;                 // palette entry 0 = transparent
            u8* p = rn_byte(px + 2 * *t, py); if (!p) continue;
            if (u16* d = vram_px(x, y)) { *d = u16(p[0] | p[1] << 8); zb = z; }
        }
    }
    void rn_sublist(u32 a, bool draw) {
        for (int guard = 0; guard < 4000; guard++) {
            u32 w = rd32(a), op = w >> 24;
            if (op == 0x0a) { rn.vb = rd32(a + 4); a += 8; continue; }
            if (op != 0x40 && op != 0x44 && op != 0x6f) return;
            u32 fl = (w >> 16) & 0xff, n = w & 0xffff; bool indexed = fl & 0x40;
            if (indexed && !rn.vb) return;
            std::vector<RnV> vs(n); std::vector<u32> src(n);
            for (u32 i = 0; i < n; i++) {
                u32 va = indexed ? rn.vb + 4 * u32(u16(rd32(a + 4 + (i / 2) * 4) >> ((i & 1) * 16))) : a + 4 + 20 * i;
                src[i] = va;
                u32 w0 = rd32(va), w1 = rd32(va + 4), w3 = rd32(va + 12);
                float p[3] = { rn_f(u16(w0)), rn_f(u16(w0 >> 16)), rn_f(u16(w1)) }, q[3];
                if (rn.ident) { for (int j = 0; j < 3; j++) q[j] = p[j] * 4096.0f; }
                else for (int j = 0; j < 3; j++) q[j] = (rn.M[j * 3] * p[0] * rn.S[0] + rn.M[j * 3 + 1] * p[1] * rn.S[1] + rn.M[j * 3 + 2] * p[2] * rn.S[2]) * 4096.0f + rn.T[j];
                vs[i] = { q[0], q[1], -q[2], rn_f(u16(w3)), rn_f(u16(w3 >> 16)) };
            }
            if (rn.cap && !indexed) {                       // 0C: write the transformed vertices back instead of drawing
                for (u32 i = 0; i < n; i++) {
                    u32 o = rn.cap + 20 * i - 0x10000000; if (rn.cap < 0x10000000 || o + 20 > sdram.size()) break;
                    memcpy(&sdram[o], &sdram[src[i] - 0x10000000], 20);
                    u16 h[3] = { rn_enc(vs[i].x / 4096.0f), rn_enc(vs[i].y / 4096.0f), rn_enc(-vs[i].z / 4096.0f) };
                    memcpy(&sdram[o], h, 6);
                    u16 vv = rn_enc(1 - vs[i].v); memcpy(&sdram[o + 14], &vv, 2);     // empirical: captured pieces need v the other way up
                }
            } else if (draw) {
                if (op == 0x44) { for (u32 i = 0; i + 2 < n; i += 3) rn_tri(vs[i], vs[i + 1], vs[i + 2]); }
                else for (u32 i = 0; i + 2 < n; i++) rn_tri(vs[i], vs[i + 1], vs[i + 2]);
            }
            a += 4 + (indexed ? 4 * ((n + 1) / 2) : 20 * n);
        }
    }
    void gfx_render(u32 la, bool draw) {
        for (int k = 1; k < 4096; k++) {
            u32 w = rd32(la + 4 * k), op = w >> 24;
            if ((w & 0xff0000ff) == 0x14000001) break;
            if (op == 0x20) {
                u32 r = (w >> 16) & 0xff, n = w & 0xffff, nw = (n + 1) / 2; int16_t h[16] = {};
                for (u32 i = 0; i < n && i < 16; i++) h[i] = int16_t(rd32(la + 4 * (k + 1 + i / 2)) >> ((i & 1) * 16));
                if (r == 0x40 && n >= 9) { rn.ident = u16(h[0]) == 0x3e00; for (int i = 0; i < 9; i++) rn.M[i] = h[i] / 4096.0f; }
                if (r == 0x4c && n >= 3) for (int i = 0; i < 3; i++) rn.T[i] = h[i];
                if (r == 0x3c && n >= 3) for (int i = 0; i < 3; i++) rn.S[i] = u16(h[0]) == 0x3e00 ? 1.0f : h[i] / 4096.0f;
                k += nw; continue;
            }
            if (op == 0xd3) { if (rd32(la + 4 * (k + 1)) == 0 && draw) { std::fill(rnZ.begin(), rnZ.end(), 1e30f); rnFrames++; } k += w & 0xffff; continue; }
            if (op == 0x82) rn.clip0 = w & 0xffffff;
            if (op == 0x81) rn.org = w & 0xffffff;
            if (op == 0x83) rn.clip1 = w & 0xffffff;
            if (op == 0x87) rn.tex = w & 0xffffff;
            if (op == 0x86) rn.fmt = w & 0xffffff;
            if (op == 0x8f) rn.pal = w & 0xffffff;
            if (op == 0x0a) { rn.vb = rd32(la + 4 * (k + 1)); k++; continue; }
            if (op == 0x0c) { rn.cap = rd32(la + 4 * (k + 1)); k++; continue; }
            if (op == 0x10) { k++; continue; }
            if (op == 0x09) { rn_sublist(rd32(la + 4 * (k + 1)), draw); rn.cap = 0; k++; continue; }
        }
    }
    void gfx_run(u32 la) {
        if (const char* re = getenv("VFRENDER")) gfx_render(la, cycles / 150000 >= (u64)atoll(re));
        if (getenv("VFD4")) d4_scan(la + 4, 4096, 0);
        if (const char* fe = getenv("VFFRAME")) {   // VFFRAME=ms: dump lists and their sublists for 40 ms from that time
            u64 t = cycles / 150000, t0 = (u64)atoll(fe);
            if (t >= t0 && t < t0 + 40) {
                printf("L %08x:", la);
                for (int k = 0; k < 64; k++) { u32 w = rd32(la + 4 * k); printf(" %08x", w); if (k > 0 && (w & 0xff0000ff) == 0x14000001) break; }
                printf("\n");
                for (int k = 1; k < 64; k++) { u32 w = rd32(la + 4 * k); if ((w & 0xff0000ff) == 0x14000001) break;
                    if (w == 0x09000000) { u32 tg = rd32(la + 4 * k + 4); printf("  S %08x:", tg); for (int j = 0; j < 40; j++) printf(" %08x", rd32(tg + 4 * j)); printf("\n"); k++; } }
            }
        }
        for (int k = 1; k < 4096; k++) {
            u32 w = rd32(la + 4 * k), op = w >> 24;
            if ((w & 0xff0000ff) == 0x14000001) break;
            if (op == 0x20) { u32 r = (w >> 16) & 0xff, n = w & 0xffff; for (u32 i = 0; i < n; i++) gfxReg[(r + i) & 0xff] = rd32(la + 4 * (k + 1 + i)); k += n; continue; }
            if (op == 0xd3) { u32 n = w & 0xffff; fillPos = rd32(la + 4 * (k + 1)); fillSize = rd32(la + 4 * (k + 2)); fillColour = rd32(la + 4 * (k + 3)); k += n; continue; }
            if (op == 0x96) {            // fill the rectangle set by D3
                int x0 = fillPos & 0xffff, y0 = fillPos >> 16, w2 = fillSize & 0xffff, h2 = fillSize >> 16;
                for (int y = y0; y < y0 + h2; y++) for (int x = x0; x < x0 + w2; x++) if (u16* p = vram_px(x, y)) *p = u16(fillColour);
                continue;
            }
            if (op == 0x09) { k += 1; continue; }   // primitive record address follows
        }
    }
    void gfx_int() { int_set(8, (regs[0xa8000014] & 0x3f) && (regs[0xa8000010] & 1)); }
    void dma_int() { int_set(12, (regs[0xbc00001c] & regs[0xbc000004]) != 0); }
    void cd_int() { int_set(14, (regs[0xc4000064] & regs[0xc4000068]) != 0); }
    // ---- CD-ROM decoder: 4 slots of 1 KB at 0x3C (8 status bytes, then the raw sector start) ----
    FILE* disc = nullptr;
    int slotRd = 0, slotCount = 0;
    bool ringStopped = false, endPending = false, resetPending = false;
    bool decoding = false, matched = false;
    void read_raw(long lba, u8* out) {
        memset(out, 0, 2352);
        if (disc && lba >= 0 && lba < discSectors) { fseek(disc, lba * 2352L, SEEK_SET); fread(out, 1, 2352, disc); }
    }
    void decoder_update() { regs[0xc4000070] = u32(slotCount) << 2 | u32(slotRd & 3); }
    // Ring mode: 2048 user bytes per sector at the write pointer 0x24, wrapping inside 0x28..0x2C;
    // 0x64 bit 16 once the pointer passes 0x80; 0x20 = sectors left (bit 24 = until stopped).
    u32 ring_sector(long lba) {   // returns where the sector's user data went (0 = not written)
        u32 cnt = regs[0xc4000020];
        if (!(cnt & 0x1000000) && (cnt & 0xffffff) == 0) return 0;
        if (ringStopped) return 0;                    // halted at the mark (0x84 bit 0)
        u8 raw[2352]; read_raw(lba, raw);
        u32 wp = regs[0xc4000024], lo = regs[0xc4000028], hi = regs[0xc400002c] + 4, mark = regs[0xc4000080];
        if (wp >= hi) wp = lo;
        bool last = (regs[0xc4000084] & 1) && wp == mark;   // stop mark: the sector starting there is the last one
        u32 start = wp;
        for (int i = 0; i < 2048; i += 4) {
            if (wp >= hi) wp = lo;
            if (wp >= 0x10000000 && wp + 4 <= 0x10000000 + sdram.size()) memcpy(&sdram[wp - 0x10000000], raw + 16 + i, 4);
            wp += 4;
            if (wp == mark && !(regs[0xc4000084] & 1)) regs[0xc4000064] |= 1u << 16;
        }
        if (last) { ringStopped = true; regs[0xc4000064] |= 1u << 16 | 1u << 4; }
        if (wp >= hi) wp = lo;
        regs[0xc4000024] = wp;
        if (!(cnt & 0x1000000)) { regs[0xc4000020] = cnt - 1; if (((cnt - 1) & 0xffffff) == 0) regs[0xc4000064] |= 1u << 4; }
        if (getenv("VFSUB")) printf("ring lba %ld -> wp %08x\n", lba, wp);
        return start;
    }
    void sector_arrived(long lba) {
        if (endPending) { endPending = false; regs[0xc4000064] |= 1u << 4; }
        if ((regs[0xc4000040] & 5) != 5) { decoding = false; return; }
        if (!decoding) { decoding = true; matched = (regs[0xc400001c] >> 24) & 1; }
        if (!matched) {
            u8 m[3]; msf(lba, m);
            if ((regs[0xc400001c] & 0xffffff) != u32(m[0] << 16 | m[1] << 8 | m[2])) return;
            matched = true;
        }
        bool ring = !(regs[0xc4000040] & 0x80);      // bit 7 clear: user data by DMA into the ring
        u32 dataAt = 0;
        if (ring) { if (ringStopped) return; dataAt = ring_sector(lba); if (!dataAt) return; }
        if (slotCount >= 4) return;                  // header buffer full: the entry is lost
        int slot = (slotRd + slotCount) & 3;
        u32 base = regs[0xc400003c] + u32(slot) * 0x400;
        u8 raw[2352]; read_raw(lba, raw);
        if (base >= 0x10000000 && base + 0x400 <= 0x10000000 + sdram.size()) {
            memset(&sdram[base - 0x10000000], 0, 8);
            sdram[base - 0x10000000] = 1;                // status word: 1 = sector decoded without error
            memcpy(&sdram[base - 0x10000000 + 4], &dataAt, 4);   // word 1: where the user data went (ring mode)
            // slot: status, data address, sync + header (raw 0..15), then in ring mode the EDC and the
            // P/Q parity (raw 2064..2351) that the software ECC (0x10072128) uses; slot mode keeps the sector start
            u8* sl = &sdram[base - 0x10000000];
            memset(sl + 8, 0, 0x400 - 8);
            if (ring) { memcpy(sl + 8, raw, 16); memcpy(sl + 0x18, raw + 2064, 288); }
            else memcpy(sl + 8, raw, 0x400 - 8);
        }
        slotCount++; decoder_update();
        regs[0xc4000064] |= (ring ? 0 : 1u << 4) | 1u << 12;
        if (getenv("VFSUB")) printf("sector lba %ld -> slot %d (%d queued)\n", lba, slot, slotCount);
    }
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
        if (lba >= 0 && lba < discSectors) {   // CD-ROM decoder: header of the sector passing by
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
        if (addr == 0xb) { sled_update(); traverseTarget = bits >= 24 ? int(v >> (bits - 20)) & 0xffff : 0; traverseFrom = rad;   // $Bx: traverse monitor count
            if (getenv("VFSUB")) printf("traverse %d tracks from lba %.0f\n", traverseTarget, lba_at(rad)); }
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
        case 0xb: return fabs(rad - traverseFrom) / PITCH < traverseTarget;    // COMP (active low): stays high until the count is reached
        case 0xe: return !(clv == 0xa && cycles - brakeAt > 45000000ull);   // braking: drops once the disc has slowed
        default: return sens;
        }
    }
    u32 gpioD = getenv("GPIOD") ? u32(strtoul(getenv("GPIOD"), 0, 16)) : 5;           // last written value per I/O address
    std::set<u32> seen;
    int logLimit = 4000, logged = 0;
    VF() {
        if (const char* dp = getenv("DISC")) { disc = fopen(dp, "rb"); if (disc) { fseek(disc, 0, SEEK_END); discSectors = ftell(disc) / 2352; } }
        regs[0x9009008c] = 0x1230ffab; sdram.assign(64u << 20, 0); sram.assign(64 << 10, 0); }
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
        if (getenv("VFB8") && (a >> 24) == 0xb8 && k[0] == 'W') { printf("%s %08x = %08x  pc %08x t=%llu\n", k, a, v, pc, cycles / 150000); return; }
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
            case 0x24: { int c = current(g); if (c >= 0 && (!getenv("VFCLAIMFROM") || cycles / 150000 >= (u64)atoll(getenv("VFCLAIMFROM")))) claims[c]++; if (c < 0) c = 0; prev_limit[g] = prio_limit[g]; prio_limit[g] = prio[c]; return c; }
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

    // ---- sound chip (0xB0000000): 64 voices x 0x40; guessed timing model ----
    bool sndOn[64] = {}; u64 sndAt[64] = {};
    bool snd_busy(int v) {
        if (!sndOn[v]) return false;
        u32 b = 0xb0000000 + v * 0x40, len = regs[b + 4] - regs[b], pitch = regs[b + 0x10] & 0x1fff;
        if (!pitch) pitch = 0x1000;
        if (regs[b + 12] & 0x1e0) return true;      // looping voice: busy until keyed off
        double rate = (getenv("SNDRATE") ? atof(getenv("SNDRATE")) : 44100.0) * pitch / 4096.0;   // bytes per second
        if (double(cycles - sndAt[v]) / 150e6 * rate >= double(len)) { sndOn[v] = false; if (getenv("VFSND")) printf("snd voice %d ended t=%llu\n", v, cycles / 150000); }
        return sndOn[v];
    }
    // ---- controller ports: two 16550 UARTs (0x90020000 = IRQ 1, 0x90030000 = IRQ 2, guessed from the game's mask) ----
    // Packet: FF, 01, L, then L raw bytes packed 2 -> 3 (each byte carries 0x40), then the 16-bit sum of 1 + L + raw bytes packed the same way.
    std::deque<u8> padRx[2]; std::vector<u8> padTx[2];
    void pad_int() { for (int p = 0; p < 2; p++) int_set(1 + p, !padRx[p].empty() && (regs[0x90020004 + p * 0x10000] & 1)); }
    void pad_send(int p, std::vector<u8> raw) {
        if (raw.size() & 1) raw.push_back(0);
        u32 sum = 1 + u32(raw.size()); for (u8 b : raw) sum += b;
        std::vector<u8> all = raw; all.push_back(u8(sum)); all.push_back(u8(sum >> 8));
        padRx[p].push_back(0xff); padRx[p].push_back(1); padRx[p].push_back(u8(raw.size()));
        for (size_t i = 0; i < all.size(); i += 2) { u8 r0 = all[i], r1 = all[i + 1]; padRx[p].push_back(0x40 | (r0 & 0xf)); padRx[p].push_back(0x40 | (r0 >> 4) | ((r1 & 3) << 4)); padRx[p].push_back(0x40 | (r1 >> 2)); }
        pad_int();
    }
    // VFPADKEY="ms:mask,ms:mask,..." = button mask (hex) from each time on; records go out on change and every 500 ms once the probe was answered.
    bool padLive = false; u64 padLast = 0; u32 padMask = 0xffffffff; std::vector<std::pair<u64, u32>> padScript; bool padScriptInit = false;
    void pad_tick() {
        if (!padLive) return;
        if (!padScriptInit) { padScriptInit = true; if (const char* e = getenv("VFPADKEY")) { while (*e) { u64 t = strtoull(e, (char**)&e, 10); if (*e != ':') break; u32 m = u32(strtoul(e + 1, (char**)&e, 16)); padScript.push_back({ t, m }); if (*e == ',') e++; } } }
        u64 t = cycles / 150000; u32 m = 0;
        for (auto& ps : padScript) if (t >= ps.first) m = ps.second;
        if (m == padMask && t - padLast < 500) return;
        if (m != padMask) printf("pad0 -> buttons %04x t=%llu\n", m, t);
        padMask = m; padLast = t;
        u64 rec = (u64(0x80) << 4) | (u64(0x80) << 13) | (u64(m & 0xffff) << 22);
        std::vector<u8> raw(6); for (int i = 0; i < 6; i++) raw[i] = u8(rec >> (8 * i));
        pad_send(0, raw);
    }
    void pad_tx(int p, u8 b) {
        auto& t = padTx[p];
        if (b == 0xff) t.clear();
        t.push_back(b);
        if (t[0] != 0xff) { t.clear(); return; }
        if (t.size() < 3) return;
        size_t need = 3 + ((t[2] + 1) / 2) * 3 + 3;
        if (t.size() < need) return;
        std::vector<u8> raw;
        for (size_t i = 3; i + 2 < need - 3 + 3 && i + 2 < t.size(); i += 3) { raw.push_back((t[i] & 0xf) | ((t[i + 1] << 4) & 0xf0)); raw.push_back(((t[i + 1] >> 4) & 3) | ((t[i + 2] << 2) & 0xfc)); }
        raw.resize(t[2]);
        printf("pad%d <-", p); for (u8 r : raw) printf(" %02x", r); printf("  t=%llu\n", cycles / 150000);
        if (getenv("VFPAD") && raw.size() == 2 && raw[0] == 0x40) { pad_send(p, { u8(atoi(getenv("VFPAD"))), 0 }); if (p == 0 && getenv("VFPADKEY")) padLive = true; printf("pad%d -> type %d\n", p, atoi(getenv("VFPAD"))); }
        t.clear();
    }
    u32 io_read(u32 a) {
        if (a >= 0xb0000000 && a < 0xb0001000 && (a & 0x3f) == 0x34) return snd_busy((a >> 6) & 63) ? 0x10000 : 0;
        if (a >= 0xb8000800 && a < 0xb8001000) { u32 v; memcpy(&v, &sram[a - 0xb8000000], 4); return v; }
        if ((a >> 24) == 0xdc) { u32 v = int_read(a); log("R", a, v); return v; }
        u32 dev = a >> 16;
        if (dev == 0x9001 || dev == 0x900c) { u32 v = timer_read(a); log("R", a, v); return v; }
        if (a == 0xc4000018) { u32 v = drive_sens() ? 3 : 0; log("R", a, v); return v; }
        if (a == 0xb8000000) { u32 v = lcdStatus; log("R", a, v); return v; }
        if (a == 0xa4000000) { float f; u32 w = regs[a]; memcpy(&f, &w, 4); u32 v = u32(int32_t(f)); log("R", a, v); return v; }   // math unit: float -> integer
        // 0x40: bits 0 (decoder on), 2 (transfer on), 7-8 (mode) read back as written; bits 3-6 are
        // status (3-4 = error, 5-6 = locked) as the driver's health check 0x100629B4 expects
        if (a == 0xc4000040) { u32 v = (regs[a] & ~0x7du) | (spinning() ? 0x65 : 0); log("R", a, v); return v; }
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
        if ((a & 0xfffeffff) == 0x90020014) return 0x60 | (padRx[(a >> 16) & 1].empty() ? 0 : 1);   // 16550 LSR: transmitter empty, data ready
        if ((a & 0xfffeffff) == 0x90020000 && !(regs[(a & 0xffff0000) | 0x0c] & 0x80)) { auto& q = padRx[(a >> 16) & 1]; u32 v = 0; if (!q.empty()) { v = q.front(); q.pop_front(); } pad_int(); return v; }
        if ((a & 0xfffeffff) == 0x90020008) return padRx[(a >> 16) & 1].empty() ? 0xc1 : 0xc4;        // IIR: receive data available
        log("R", a, v);
        return v;
    }
    std::string line;
    std::map<u32, u64> regionCount; std::map<u32, u32> regionLast;
    void io_write(u32 a, u32 v) {
        if (a >= 0xa800008c && a <= 0xa80000a4 && getenv("VFBASE")) printf("gfxreg %08x = %08x pc %08x t=%llu\n", a, v, cpu->r[15], cycles / 150000);
        if (getenv("VFREG") && cycles / 150000 > (u64)atoll(getenv("VFREG"))) {
            u64& c = regionCount[(a >> 24) == 0xa8 || (a >> 24) == 0xa4 ? a : a & 0xfffff000];
            if (!c) printf("region %08x first write %08x = %08x pc %08x t=%llu\n", a & 0xfffff000, a, v, cpu->r[15], cycles / 150000);
            c++; regionLast[(a >> 24) == 0xa8 || (a >> 24) == 0xa4 ? a : a & 0xfffff000] = v;
        }
        if (a == 0xb0001010 || a == 0xb0001014 || a == 0xb0001018 || a == 0xb000101c) {
            int base = (a & 4) ? 32 : 0; bool on = a < 0xb0001018;
            for (int i = 0; i < 32; i++) if (v >> i & 1) {
                sndOn[base + i] = on; sndAt[base + i] = cycles;
                if (getenv("VFSND")) { u32 b = 0xb0000000 + (base + i) * 0x40; printf("snd key %s voice %d %08x..%08x loop %08x ctrl %x pitch %x pc %08x t=%llu\n", on ? "on" : "off", base + i, regs[b], regs[b + 4], regs[b + 8], regs[b + 12], regs[b + 16], cpu->r[15], cycles / 150000); }
            }
        }
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
            pad_tx((a >> 16) & 1, u8(v));
            if (getenv("VFUART")) printf("tx%d %02x pc %08x t=%llu\n", (a >> 16) & 1, v & 0xff, cpu->r[15], cycles / 150000);
            char ch = char(v);
            if (ch == '\n') { printf("[uart%d] %s\n", (a >> 16) & 1 ? 1 : 0, line.c_str()); line.clear(); }
            else if (ch != '\r') line += ch;
            return;
        }
        if (a == 0xc4000064) { regs[a] &= ~v; cd_int(); log("W", a, v); return; }
        if (a == 0xc4000068) { regs[a] = v; cd_int(); log("W", a, v); return; }
        if (a == 0xc4000080 || a == 0xc4000024) ringStopped = false;
        if (a == 0xc4000024) { slotRd = slotCount = 0; decoder_update(); decoding = false; }   // a new ring read starts with an empty header buffer
        // ---- DMA engine 0xBC000000: channel n at 0x100 + n*16 (control: count in bits 0-9, words;
        // source; destination). 0x10 bit n starts it, 0x1C = done (write 1 to clear), 0x04 = IRQ 12 enable.
        if ((a >> 24) == 0xbc) {
            if (a == 0xbc00001c) { regs[a] &= ~v; dma_int(); log("W", a, v); return; }
            regs[a] = v; log("W", a, v);
            if (a == 0xbc000004) dma_int();
            if (a == 0xbc000010) for (int ch = 0; ch < 8; ch++) if (v >> ch & 1) {
                u32 base = 0xbc000100 + ch * 16, ctl = regs[base], src = regs[base + 4], dst = regs[base + 8], n = (ctl & 0x3ff) * 4;
                for (u32 i = 0; i < n; i += 4) {
                    if (src + i < 0x10000000 || src + i + 4 > 0x10000000 + sdram.size() || dst + i < 0x10000000 || dst + i + 4 > 0x10000000 + sdram.size()) break;
                    memcpy(&sdram[dst + i - 0x10000000], &sdram[src + i - 0x10000000], 4);
                }
                if (getenv("VFDMA")) printf("dma ch%d %08x -> %08x %u bytes t=%llu\n", ch, src, dst, n, cycles / 150000);
                regs[0xbc00000c] &= ~(1u << ch); regs[0xbc00001c] |= 1u << ch; dma_int();
            }
            return;
        }
        // ---- graphics engine 0xA8000000 (stub): a command list at 0x28 starts with 0x00 = 1; report it
        // finished at once (code 3 in bits 16-31), pending in 0x14 (write 1 to clear), IRQ 8 when 0x10 bit 0
        if ((a >> 24) == 0xa8) {
            if (a == 0xa8000014) { regs[a] &= ~v; gfx_int(); log("W", a, v); return; }
            if (a == 0xa8000000) {                     // 1 = run the list at 0x28, 3 = reset; status code 3 = busy, 0 = idle
                if ((v & 3) == 1) {
                    if (getenv("VFGFX")) {
                        u32 la = regs[0xa8000028];
                        printf("gfx list %08x t=%llu:", la, cycles / 150000);
                        for (int k = 0; k < 256 && la + 4 * k + 4 <= 0x10000000 + sdram.size() && la >= 0x10000000; k++) {
                            u32 w; memcpy(&w, &sdram[la + 4 * k - 0x10000000], 4); printf(" %08x", w);
                            if ((w & 0xff0000ff) == 0x14000001) break;
                        }
                        printf("\n");
                    }
                    gfx_run(regs[0xa8000028]);
                    regs[0xa8000014] |= 0xfd08; gfx_int();   // the list's closing 0x1400FD01 packet: interrupt with ID 0xFD
                }
                regs[a] = 0; log("W", a, v); return;
            }
            regs[a] = v; if (a == 0xa8000010) gfx_int(); log("W", a, v); return;
        }
        if (a == 0x900a0008 && (v & 1)) { resetPending = true; log("W", a, v); return; }   // chip reset (RAM and 0x8FFF0000 kept)
        if (a == 0xc4000074) { if ((v & 3) && slotCount) { slotRd = (slotRd + 1) & 3; slotCount--; decoder_update(); } log("W", a, v); return; }
        if (a == 0xc4000040) {
            if (!(v & 4)) {
                decoding = false;
                if ((regs[a] & 4) && !(v & 0x80)) { regs[0xc4000064] |= 1u << 4; cd_int(); }   // stopping a ring transfer: confirm with "ended"
            }
            if ((v & 4) && !(regs[a] & 4)) { slotRd = slotCount = 0; decoder_update(); }   // transfer start empties the header buffer
        }
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
        if (p) { cpu.hle_base = 0x10000000; cpu.hle_size = 0x02000000; }
        cpu.hle_watch_pc = 0x10010994;   // OS idle task: a pure counting loop, so sleep until the next interrupt
        cpu.hle_hook = [](ARM926* c, u32 pc, void*) -> bool {
            if (pc == 0x10010994 && !getenv("NOIDLE")) { c->halt(); return true; }
            static long long trFrom = getenv("VFTRACE") ? atoll(getenv("VFTRACE")) : -1, trTo = getenv("VFTRACETO") ? atoll(getenv("VFTRACETO")) : 1LL << 60;
            if (trFrom >= 0) {   // VFTRACE=ms[,VFTRACETO=ms]: first execution of each PC in the window
                long long t = (long long)(gvf->cycles / 150000);
                static std::set<u32> seenPc;
                if (!getenv("VFFLOW") && t >= trFrom && t < trTo && seenPc.insert(pc).second) printf("tr %08x t=%lld\n", pc, t);
            }
            static u32 flLo = 0, flHi = 0, flPrev = 0; static bool flInit = false;
            if (!flInit) { flInit = true; if (const char* e = getenv("VFFLOW")) { flLo = u32(strtoul(e, (char**)&e, 16)); if (*e == ',') flHi = u32(strtoul(e + 1, nullptr, 16)); } }
            if (flHi && pc >= flLo && pc < flHi) {   // VFFLOW=lo,hi (+VFTRACE window): every non-sequential PC in the range
                long long t = (long long)(gvf->cycles / 150000);
                if (t >= trFrom && t < trTo && pc != flPrev + 4) printf("fl %08x r0 %08x t=%lld\n", pc, c->r[0], t);
                flPrev = pc;
            }
            if (watch.count(pc)) {
                printf("watch %08x lr %08x r0 %08x r1 %08x r2 %08x r3 %08x t=%llu", pc, c->r[14], c->r[0], c->r[1], c->r[2], c->r[3], gvf->cycles / 150000);
                if (const char* pk = getenv("VFPEEK")) {   // VFPEEK=addr,...: memory words shown at each hit
                    while (*pk) { u32 a = u32(strtoul(pk, (char**)&pk, 16)); u32 w = 0; if (a >= 0x10000000 && a < 0x14000000) memcpy(&w, &gvf->sdram[a - 0x10000000], 4); printf(" [%08x]=%08x", a, w); if (*pk == ',') pk++; else break; }
                }
                if (getenv("VFWBT")) { u32 sp = c->r[13]; printf(" bt:"); for (int k = 0; k < 80; k++) { u32 w, a = sp + k * 4; if (a < 0x10000000 || a >= 0x14000000) break; memcpy(&w, &gvf->sdram[a - 0x10000000], 4); if ((w >= 0x10010000 && w < 0x100b0000) || (w >= 0x104b8000 && w < 0x10646000) || (w >= 0x10999000 && w < 0x10ae0000)) printf(" %08x", w); } }
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
        vf.pad_tick();
        if (cpu.r[15] >= 0x10999d74 && cpu.r[15] <= 0x10999d84 && !getenv("NOIDLE")) cpu.halt();   // the game OS's idle task
        if (vf.resetPending) {
            vf.resetPending = false;
            printf("chip reset at t=%llu (0x8fff0004 = %x)\n", vf.cycles / 150000, vf.regs[0x8fff0004]);
            u32 keep = vf.regs[0x8fff0004];
            cpu.reset();
            vf.regs[0x8fff0004] = keep;
            if (vf.regs[0x900b0000] == 3) vf.regs[0x900a000c] = 2;   // suspended (0x900B0000 = 3): wake-up reason for the ROM
        }
        if (i % 546 == 0) vf.vblank();        // ~60 Hz
        hot[cpu.r[15] & ~0xffu]++;
        if (getenv("VFSAMPLE") && i % 3 == 0 && vf.cycles / 150000 >= (u64)atoll(getenv("VFSAMPLE")) && (cpu.r[15] < 0x10010900 || cpu.r[15] >= 0x10010a00) && !(cpu.r[15] >= 0x10999d74 && cpu.r[15] <= 0x10999d88)) {
            printf("sample t=%llu pc %08x lr %08x", vf.cycles / 150000, cpu.r[15], cpu.r[14]);
            if (getenv("VFSTACK")) { u32 sp = cpu.r[13]; for (int k = 0; k < 96; k++) { u32 w, a = sp + k * 4; if (a < 0x10000000 || a >= 0x14000000) break; memcpy(&w, &vf.sdram[a - 0x10000000], 4); if (w >= 0x10010000 && w < 0x100b0000) printf(" %08x", w); } }
            printf("\n");
        }
    }
    if (getenv("VFRENDER")) {      // write the displayed 512x240 buffer as fb.ppm
        u32 base = vf.regs[0xa8000098] ? vf.regs[0xa8000098] : 0x10048000, off = vf.regs[0xb8000160] - base;
        int y0 = int(off / 16384) * 8 + 240, x0 = int((off % 16384) / 512) * 32;   // the shown buffer is the render target 240 rows below the display base (empirical)
        FILE* o = fopen("fb.ppm", "wb"); fprintf(o, "P6 512 240 255\n");
        for (int y = 0; y < 240; y++) for (int x = 0; x < 512; x++) { u16* p = vf.vram_px(x0 + x, y0 + y); u16 v = p ? *p : 0; u8 c[3] = { u8((v & 31) << 3), u8(((v >> 5) & 31) << 3), u8(((v >> 10) & 31) << 3) }; fwrite(c, 1, 3, o); }
        fclose(o);
        o = fopen("vram.ppm", "wb"); fprintf(o, "P6 1024 720 255\n");
        for (int y = 0; y < 720; y++) for (int x = 0; x < 1024; x++) { u16* p = vf.vram_px(x, y); u16 v = p ? *p : 0; u8 c[3] = { u8((v & 31) << 3), u8(((v >> 5) & 31) << 3), u8(((v >> 10) & 31) << 3) }; fwrite(c, 1, 3, o); }
        fclose(o); printf("clip %06x %06x\n", vf.rn.clip0, vf.rn.clip1);
        printf("render: %lld triangles, %lld frames, display %08x = (%d,%d)\n", vf.rnTris, vf.rnFrames, vf.regs[0xb8000160], x0, y0);
    }
    if (getenv("DUMP")) { FILE* o = fopen("ram.bin", "wb"); fwrite(vf.sdram.data(), 1, 32 << 20, o); fclose(o); }
    for (auto& [k, v] : vf.regionCount) printf("region %08x writes %llu last %08x\n", k, (unsigned long long)v, vf.regionLast[k]);
    for (int k = 0; k < 32; k++) if (vf.claims[k]) printf("irq %d x%lu\n", k, vf.claims[k]);
    printf("active %08x status %08x mask %08x\n", vf.active, vf.status, vf.mask[0]);
    printf("regs:"); for (int k = 0; k < 16; k++) printf(" r%d=%08x", k, cpu.r[k]); printf(" cpsr=%08x\n", cpu.cpsr);
    printf("pc %08x insns %llu\n", cpu.r[15], (unsigned long long)cpu.insn_count);
    std::vector<std::pair<int, u32>> h; for (auto& [k, v] : hot) h.push_back({ v, k });
    std::sort(h.rbegin(), h.rend()); for (int i = 0; i < 8 && i < (int)h.size(); i++) printf("hot %08x x%d\n", h[i].second, h[i].first);
}
