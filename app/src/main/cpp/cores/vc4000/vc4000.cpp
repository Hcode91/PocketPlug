// PocketPlug - Interton VC 4000 family core (1292 Advanced Programmable Video System etc.)
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Port of MAME's VC 4000 driver: Signetics 2636 PVI video/sound, keypads,
// analog joysticks and cartridge mapping. Video logic follows MAME's
// vc4000_v.cpp line by line; scheduling and glue are rewritten for this app.

#include "vc4000.h"
#include <cstring>
#include <cmath>
#include <algorithm>

static uint8_t bus_r(void* c, uint16_t a) { return static_cast<Vc4000Core*>(c)->read(a); }
static void bus_w(void* c, uint16_t a, uint8_t d) { static_cast<Vc4000Core*>(c)->write(a, d); }
static int bus_sense(void* c) { return static_cast<Vc4000Core*>(c)->vsync(); }

// 2636 register layout (MAME's vc4000_video_t union)
enum {
    R_SPRITE_SIZES = 0xc0, R_SPRITE_COLORS1 = 0xc1, R_SPRITE_COLORS2 = 0xc2, R_SCORE_CONTROL = 0xc3,
    R_BACKGROUND = 0xc6, R_SOUND = 0xc7, R_BCD0 = 0xc8, R_BCD1 = 0xc9,
    R_BG_COLLISION = 0xca, R_SPRITE_COLLISION = 0xcb,
    R_GRID = 0x80, R_GRID_CONTROL = 0xa8
};
enum { S_X1 = 10, S_X2 = 11, S_Y1 = 12, S_Y2 = 13 };

static const uint32_t kPalette[8] = {
    0xff000000, // black
    0xff0000af, // blue
    0xff00af00, // green
    0xff00ffff, // cyan
    0xffff0000, // red
    0xffff00ff, // magenta
    0xffc8c800, // yellow
    0xffc8c8c8, // white
};

#define STICKCENTRE (105)
#define STICKLOW    (20)
#define STICKHIGH   (225)

Vc4000Core::Vc4000Core()
{
    for (int i = 0; i < 0x20; i++) {
        uint8_t s = 0;
        if ((i & 0x3) == 0x3) s |= 0x20;
        if ((i & 0x5) == 0x5) s |= 0x10;
        if ((i & 0x9) == 0x9) s |= 0x08;
        if ((i & 0x6) == 0x6) s |= 0x04;
        if ((i & 0xa) == 0xa) s |= 0x02;
        if ((i & 0xc) == 0xc) s |= 0x01;
        m_sprite_collision_tab[i] = s;
        uint8_t b = 0;
        if ((i & 0x11) == 0x11) b |= 0x80;
        if ((i & 0x12) == 0x12) b |= 0x40;
        if ((i & 0x14) == 0x14) b |= 0x20;
        if ((i & 0x18) == 0x18) b |= 0x10;
        m_background_collision_tab[i] = b;
    }
    m_cpu.setBus(this, bus_r, bus_w, bus_sense);
    m_cpu.setIntackVector(0x03);
    memset(m_frame, 0, sizeof(m_frame));
}

// Board type per known cartridge, from MAME's software list (hash/vc4000.xml).
// Unknown carts fall back to MAME's size heuristic for loose files.
enum { Vc4000Core_CART_STD, Vc4000Core_CART_ROM4K, Vc4000Core_CART_RAM1K, Vc4000Core_CART_CHESS2 };
static const struct { uint32_t crc; int type; } kKnownCarts[] = {
    { 0x5c7f11e0U, Vc4000Core_CART_STD }, // carraces
    { 0xec2a91b3U, Vc4000Core_CART_STD }, // blckjack
    { 0x682b876aU, Vc4000Core_CART_STD }, // paddleg
    { 0x468e4779U, Vc4000Core_CART_STD }, // tbattle
    { 0xe38e0c03U, Vc4000Core_CART_STD }, // mathema1
    { 0x66c5975eU, Vc4000Core_CART_STD }, // mathema2
    { 0xf3d37699U, Vc4000Core_CART_STD }, // airseaba
    { 0xea8f717fU, Vc4000Core_CART_STD }, // memory1
    { 0x4d3f9cfbU, Vc4000Core_CART_STD }, // intel1
    { 0xbdd652b7U, Vc4000Core_CART_RAM1K }, // wintersp
    { 0xaaea290fU, Vc4000Core_CART_STD }, // hippodro
    { 0x31978ad4U, Vc4000Core_CART_STD }, // hunting
    { 0xe9c53288U, Vc4000Core_CART_CHESS2 }, // chess
    { 0x55011f0aU, Vc4000Core_CART_RAM1K }, // motox
    { 0x8ab96827U, Vc4000Core_CART_STD }, // intel2
    { 0x52b8c6e7U, Vc4000Core_CART_STD }, // intel3
    { 0x0ab80f3eU, Vc4000Core_CART_STD }, // circus
    { 0x922c9f0dU, Vc4000Core_CART_ROM4K }, // boxing
    { 0xb1c31f9aU, Vc4000Core_CART_STD }, // outersc
    { 0xd68da80aU, Vc4000Core_CART_STD }, // memory2
    { 0x619c07c9U, Vc4000Core_CART_STD }, // reversi
    { 0x1b948aebU, Vc4000Core_CART_CHESS2 }, // chess2
    { 0xadd99c6eU, Vc4000Core_CART_STD }, // pinball
    { 0x05af8228U, Vc4000Core_CART_STD }, // soccer
    { 0xa47fe1bfU, Vc4000Core_CART_STD }, // bowling
    { 0x7b868473U, Vc4000Core_CART_RAM1K }, // draughts
    { 0xd399ce07U, Vc4000Core_CART_RAM1K }, // golf
    { 0x11dd7f0dU, Vc4000Core_CART_RAM1K }, // cockpit
    { 0xa640a330U, Vc4000Core_CART_STD }, // metropol
    { 0x08209d98U, Vc4000Core_CART_STD }, // solitair
    { 0x50fd6a18U, Vc4000Core_CART_STD }, // casino
    { 0x9497204aU, Vc4000Core_CART_STD }, // invaders
    { 0xa46c2f9eU, Vc4000Core_CART_STD }, // superinv
    { 0x08cd8135U, Vc4000Core_CART_RAM1K }, // backgamm
    { 0x27cce96fU, Vc4000Core_CART_RAM1K }, // mnstrman
    { 0xb3ed1129U, Vc4000Core_CART_STD }, // hyperspa
    { 0x306e37bbU, Vc4000Core_CART_RAM1K }, // suprspac
};

static uint32_t crc32(const uint8_t* p, size_t n)
{
    uint32_t c = 0xffffffffu;
    for (size_t i = 0; i < n; i++) {
        c ^= p[i];
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1)));
    }
    return ~c;
}

bool Vc4000Core::load(const std::vector<RomFile>& roms, std::string& error)
{
    if (roms.empty() || roms[0].data.empty()) { error = "No cartridge"; return false; }
    m_cart = roms[0].data;
    if (m_cart.size() > 0x1800) { error = "Cartridge is larger than 6K - not a VC 4000 family cartridge"; return false; }
    // same size heuristic as MAME's slot for loose files
    if (m_cart.size() > 0x1000) m_type = CART_CHESS2;
    else if (m_cart.size() > 0x0800) m_type = CART_RAM1K;
    else m_type = CART_STD;
    uint32_t crc = crc32(m_cart.data(), m_cart.size());
    for (auto& k : kKnownCarts)
        if (k.crc == crc) m_type = (CartType)k.type;
    reset();
    return true;
}

void Vc4000Core::reset()
{
    memset(m_reg, 0, sizeof(m_reg));
    memset(m_cartRam, 0, sizeof(m_cartRam));
    memset(m_bitmap, 0, sizeof(m_bitmap));
    memset(m_objects, 0, sizeof(m_objects));
    static const int bases[4] = { 0x00, 0x10, 0x20, 0x40 };
    for (int i = 0; i < 4; i++) {
        Sprite& s = m_sprites[i];
        s = Sprite();
        s.base = bases[i];
        s.mask = 1 << i;
        s.size = 0;   // as at power-on in MAME, until register C0 is written
    }
    m_line = 0;
    m_sprite_collision_flags = m_background_collision_flags = 0;
    m_irq_pause = 0;
    m_cycleAcc = 0;
    m_sndReg = 0; m_sndSize = 0; m_sndPos = 0;
    m_sampleAcc = 0;
    m_audio.clear();
    m_tap = 0; m_tapFrames = 0; m_tapRead = false;
    m_latch = m_latchSeen = 0;
    m_cpu.reset();
}

// Some cartridges (Cockpit, for one) only look at the keypad in a short burst
// every second or so. A quick tap on a touch screen falls between those
// reads, so a newly pressed key is latched until the game has read it.
void Vc4000Core::setInputs(uint32_t pressed)
{
    const uint32_t keypadMask = (1u << 24) - 1;   // both players' keypads
    uint32_t fresh = pressed & ~m_inputs & keypadMask;
    if (fresh) { m_latch |= fresh; m_latchFrames = 0; }
    m_inputs = pressed;
}

bool Vc4000Core::tapInputs(uint32_t mask)
{
    mask &= (1u << Vc4000Bits::START) | (1u << Vc4000Bits::SELECT);
    if (!mask) return false;
    m_tap = mask;
    m_tapFrames = 0;
    m_tapRead = false;
    m_tapBcd[0] = m_reg[R_BCD0];
    m_tapBcd[1] = m_reg[R_BCD1];
    return true;
}

void Vc4000Core::setAnalog(int stick, float x, float y)
{
    if (stick < 0 || stick > 1) return;
    auto conv = [](float v) {
        v = std::max(-1.0f, std::min(1.0f, v));
        float r = v < 0 ? STICKCENTRE + v * (STICKCENTRE - STICKLOW) : STICKCENTRE + v * (STICKHIGH - STICKCENTRE);
        return (uint8_t)lrintf(r);
    };
    m_joyX[stick] = conv(x);
    m_joyY[stick] = conv(y);
}

/************************************************************************
 Memory map (address bus is 13 bits; A13/A14 not connected)
************************************************************************/

#ifdef PP_DEBUG
#include <cstdio>
#include <map>
#include <cstdlib>
static std::map<int, int> g_ioReads;
static std::map<int, int> g_pcs;
void vc4000_debug_dump()
{
    printf("IO reads:"); for (auto& kv : g_ioReads) printf(" %04x:%d", kv.first, kv.second); printf("\n");
    printf("PCs:"); for (auto& kv : g_pcs) if (kv.second > 50) printf(" %04x:%d", kv.first, kv.second); printf("\n");
    g_ioReads.clear(); g_pcs.clear();
}
#endif

uint8_t Vc4000Core::read(uint16_t a)
{
    a &= 0x1fff;
#ifdef PP_DEBUG
    if ((a & 0x17ff) >= 0x1680) g_ioReads[a]++;
    if (getenv("IOLOG") && (((a & 0x17ff) >= 0x1680 && (a & 0x17ff) < 0x1700) || (a & 0xff) == 0xcc || (a & 0xff) == 0xcd))
        printf("rd %04x pc=%04x line=%d\n", a, m_cpu.m_ppc, m_line);
    if (a == m_cpu.pc()) g_pcs[a]++;
    if (getenv("PCWATCH") && a == strtol(getenv("PCWATCH"), 0, 16) && a == m_cpu.pc()) printf("hit %04x line=%d\n", a, m_line);
#endif
    // cartridge
    switch (m_type) {
    case CART_STD:
        if (a < 0x0800) return a < m_cart.size() ? m_cart[a] : 0xff;
        break;
    case CART_ROM4K:
        if (a < 0x1000) return a < m_cart.size() ? m_cart[a] : 0xff;
        break;
    case CART_RAM1K:
        if (a < 0x1000) return a < m_cart.size() ? m_cart[a] : 0xff;
        if (a < 0x1600) return m_cartRam[(a - 0x1000) & 0x3ff];
        break;
    case CART_CHESS2:
        if (a < 0x1600) return a < m_cart.size() ? m_cart[a] : 0xff;
        if (a >= 0x1800 && a < 0x1c00) return m_cartRam[(a - 0x1800) & 0x3ff];
        break;
    }
    uint16_t m = a & 0x17ff;   // 0x1e80/0x1f00 mirror 0x1680/0x1700
    if (m >= 0x1680 && m < 0x1700) return key_r(m & 0x7f);
    if (m >= 0x1700 && m < 0x1800) return video_r(m & 0xff);
    return 0xff;
}

void Vc4000Core::write(uint16_t a, uint8_t d)
{
    a &= 0x1fff;
    if (m_type == CART_RAM1K && a >= 0x1000 && a < 0x1600) { m_cartRam[(a - 0x1000) & 0x3ff] = d; return; }
    if (m_type == CART_CHESS2 && a >= 0x1800 && a < 0x1c00) { m_cartRam[(a - 0x1800) & 0x3ff] = d; return; }
    uint16_t m = a & 0x17ff;
    if (m >= 0x1680 && m < 0x1700) return;  // sound control: unused
    if (m >= 0x1700 && m < 0x1800) video_w(m & 0xff, d);
}

uint8_t Vc4000Core::key_r(int offset)
{
    const uint32_t in = m_inputs | m_latch;
    auto col = [&](int base, int a, int b, int c, int d) -> uint8_t {
        uint8_t v = 0;
        if (in & (1u << (base + a))) v |= 0x80;
        if (in & (1u << (base + b))) v |= 0x40;
        if (in & (1u << (base + c))) v |= 0x20;
        if (in & (1u << (base + d))) v |= 0x10;
        // the game has now seen these keys
        m_latchSeen |= m_latch & ((1u << (base + a)) | (1u << (base + b)) | (1u << (base + c)) | (1u << (base + d)));
        return v;
    };
    using namespace Vc4000Bits;
    switch (offset & 0x0f) {
    case 0x08: return col(0, K1, K4, K7, KENTER);
    case 0x09: return col(0, K2, K5, K8, K0);
    case 0x0a: return col(0, K3, K6, K9, KCLEAR);
    case 0x0b: {
        uint32_t p = m_inputs | m_tap;
        if (m_tap) m_tapRead = true;
        return ((p >> START) & 1 ? 0x40 : 0) | ((p >> SELECT) & 1 ? 0x80 : 0);
    }
    case 0x0c: return col(P2, K1, K4, K7, KENTER);
    case 0x0d: return col(P2, K2, K5, K8, K0);
    case 0x0e: return col(P2, K3, K6, K9, KCLEAR);
    }
    return 0;
}

/************************************************************************
 2636 PVI registers
************************************************************************/

uint8_t Vc4000Core::video_r(int offset)
{
    uint8_t data = 0;
    if (offset > 0xcf) offset &= 0xcf;  // c0-cf is mirrored at d0-df, e0-ef, f0-ff
    switch (offset) {
    case 0xca:      // background-sprite collision (bit 7-4) and sprite finished (bit 3-0)
        data |= m_background_collision_flags;
        m_background_collision_flags = 0;
        if (m_sprites[3].finished) { data |= 1; m_sprites[3].finished = 0; }
        if (m_sprites[2].finished) { data |= 2; m_sprites[2].finished = 0; }
        if (m_sprites[1].finished) { data |= 4; m_sprites[1].finished = 0; }
        if (m_sprites[0].finished) { data |= 8; m_sprites[0].finished = 0; }
        break;
    case 0xcb:      // VRST flag (bit 6) and inter-sprite collision (bit 5-0)
        data = m_sprite_collision_flags | (m_reg[R_SPRITE_COLLISION] & 0xc0);
        m_sprite_collision_flags = 0;
        m_reg[R_SPRITE_COLLISION] &= 0xbf;
        break;
    case 0xcc:      // left joystick: flag output selects the axis
        data = m_cpu.flagOut() ? m_joyY[0] : m_joyX[0];
        break;
    case 0xcd:      // right joystick
        data = m_cpu.flagOut() ? m_joyY[1] : m_joyX[1];
        break;
    default:
        data = m_reg[offset];
        break;
    }
    return data;
}

void Vc4000Core::video_w(int offset, uint8_t data)
{
    if (offset > 0xcf) offset &= 0xcf;
    switch (offset) {
    case 0xc0:      // sprite sizes
        m_sprites[0].size = 1 << (data & 3);
        m_sprites[1].size = 1 << ((data >> 2) & 3);
        m_sprites[2].size = 1 << ((data >> 4) & 3);
        m_sprites[3].size = 1 << ((data >> 6) & 3);
        break;
    case 0xc1:
        m_sprites[0].scolor = ((~data >> 3) & 7);
        m_sprites[1].scolor = (~data & 7);
        break;
    case 0xc2:
        m_sprites[2].scolor = ((~data >> 3) & 7);
        m_sprites[3].scolor = (~data & 7);
        break;
    case 0xc7:      // sound
        m_reg[offset] = data;
        soundport_w(data);
        break;
    case 0xca:
        m_reg[offset] = data;
        m_background_collision_flags = data;
        break;
    case 0xcb:
        m_reg[offset] = data;
        m_sprite_collision_flags = data;
        break;
    default:        // includes score control, background, BCD digits
        m_reg[offset] = data;
        break;
    }
}

/************************************************************************
 Rendering (one scanline per call, as MAME's vc4000_video_line)
************************************************************************/

static const char led[20][12 + 1] = {
    "aaaabbbbcccc", "aaaabbbbcccc", "aaaabbbbcccc", "aaaabbbbcccc",
    "llll    dddd", "llll    dddd", "llll    dddd", "llll    dddd",
    "kkkkmmmmeeee", "kkkkmmmmeeee", "kkkkmmmmeeee", "kkkkmmmmeeee",
    "jjjj    ffff", "jjjj    ffff", "jjjj    ffff", "jjjj    ffff",
    "iiiihhhhgggg", "iiiihhhhgggg", "iiiihhhhgggg", "iiiihhhhgggg"
};

void Vc4000Core::draw_digit(int x, int y, int d, int line)
{
    static const int digit_to_segment[0x10] = { 0x0fff, 0x007c, 0x17df, 0x15ff, 0x1c7d, 0x1df7, 0x1ff7, 0x007f, 0x1fff, 0x1dff };
    int i = line;
    for (int j = 0; j < 12; j++) {
        if (led[i][j] != ' ' && (digit_to_segment[d] & (1 << (led[i][j] - 'a'))))
            m_bitmap[y + i][x + j] = ((m_reg[R_BACKGROUND] >> 4) & 7) ^ 7;
    }
}

void Vc4000Core::collision_plot(uint8_t* collision, uint8_t data, uint8_t color, int scale)
{
    for (int j = 0, m = 0x80; j < 8; j++, m >>= 1) {
        if (data & m)
            for (int i = 0; i < scale; i++, collision++) *collision |= color;
        else
            collision += scale;
    }
}

void Vc4000Core::sprite_update(uint8_t* collision, Sprite* s)
{
    const uint8_t* d = &m_reg[s->base];
    if (m_line == 0) {
        s->y = d[S_Y1];
        s->state = 0;
        s->delay = 0;
        s->finished = 0;
    }
    s->finished_now = 0;
    if (m_line > END_LINE) return;

    switch (s->state) {
    case 0:
        if (m_line != s->y + 2) break;
        s->state++;
        [[fallthrough]];
    case 1: case 2: case 3: case 4: case 5: case 6: case 7: case 8: case 9: case 10:
        collision_plot(collision + d[S_X1], d[s->state - 1], s->mask, s->size);
        for (int j = 0, m = 0x80; j < 8; j++, m >>= 1) {
            if (d[s->state - 1] & m) {
                for (int i = 0; i < s->size; i++) {
                    m_objects[d[S_X1] + i + j * s->size] |= s->scolor;
                    m_objects[d[S_X1] + i + j * s->size] &= 7;
                }
            }
        }
        s->delay++;
        if (s->delay >= s->size) { s->delay = 0; s->state++; }
        if (s->state > 10) { s->finished = 1; s->finished_now = 1; }
        break;

    case 11:
        s->y = d[S_Y2];
        if (s->y == 255) s->y = 0;
        else s->y++;
        if (s->y > 252) break;
        s->delay = 0;
        s->state++;
        [[fallthrough]];
    case 12:
        if (s->y != 0) { s->y--; break; }
        s->state++;
        [[fallthrough]];
    case 13: case 14: case 15: case 16: case 17: case 18: case 19: case 20: case 21: case 22:
        collision_plot(collision + d[S_X2], d[s->state - 13], s->mask, s->size);
        for (int j = 0, m = 0x80; j < 8; j++, m >>= 1) {
            if (d[s->state - 13] & m) {
                for (int i = 0; i < s->size; i++) {
                    int offset = d[S_X2] + i + j * s->size;
                    if (offset < 256) {
                        m_objects[offset] |= s->scolor;
                        m_objects[offset] &= 7;
                    }
                }
            }
        }
        s->delay++;
        if (s->delay < s->size) break;
        s->delay = 0;
        s->state++;
        if (s->state < 23) break;
        s->finished = 1;
        s->finished_now = 1;
        s->state = 11;
        break;
    }
}

void Vc4000Core::draw_grid(uint8_t* collision)
{
    int line = m_line - 20;
    if (m_line >= SCR_H) return;

    memset(m_bitmap[m_line], m_reg[R_BACKGROUND] & 7, SCR_W);

    if (line < 0 || line >= 200) return;
    if (~m_reg[R_BACKGROUND] & 8) return;

    int i = (line / 20) * 2;
    if (line % 20 >= 2) i++;

    int k = m_reg[R_GRID_CONTROL + (i >> 2)];
    int w;
    switch (k >> 6) {
    default:
    case 0: case 2: w = 1; break;
    case 1: w = 2; break;
    case 3: w = 4; break;
    }
    switch (i & 3) {
    case 0: if (k & 1) w = 8; break;
    case 1:
        if ((line % 40) <= 10) { if (k & 2) w = 8; }
        else { if (k & 4) w = 8; }
        break;
    case 2: if (k & 8) w = 8; break;
    case 3:
        if ((line % 40) <= 30) { if (k & 0x10) w = 8; }
        else { if (k & 0x20) w = 8; }
        break;
    }
    int m = 0x80;
    for (int x = 30, j = 0; j < 16; j++, x += 8, m >>= 1) {
        if (m_reg[R_GRID + i * 2 + (j >> 3)] & m) {
            for (int l = 0; l < w; l++) collision[x + l] |= 0x10;
            for (int l = 0; l < w && x + l < SCR_W; l++) m_bitmap[m_line][x + l] = (m_reg[R_BACKGROUND] >> 4) & 7;
        }
        if (j == 7) m = 0x100;
    }
}

void Vc4000Core::video_line()
{
    uint8_t collision[400] = { 0 };

    m_line++;
    if (m_irq_pause) m_irq_pause++;
    if (m_line > 311) m_line = 0;

    if (m_line == 0) {
        m_background_collision_flags = 0;
        m_sprite_collision_flags = 0;
        m_reg[R_SPRITE_COLLISION] = 0;
    }

    if (m_irq_pause > 10) {
        m_cpu.setIrq(false);
        m_irq_pause = 0;
    }

    const int visLeft = VIS_X, visRight = VIS_X + VIS_W - 1;   // MAME loops left..right-1
    if (m_line <= END_LINE) {
        draw_grid(collision);

        for (int i = visLeft; i < visRight; i++) m_objects[i] = 8;

        for (int s = 0; s < 4; s++) sprite_update(collision, &m_sprites[s]);

        for (int i = visLeft; i < visRight; i++) {
            m_sprite_collision_flags |= m_sprite_collision_tab[collision[i] & 0x1f];
            m_background_collision_flags |= m_background_collision_tab[collision[i] & 0x1f];
            if (m_objects[i] < 8) m_bitmap[m_line][i] = m_objects[i];
        }

        int y = (m_reg[R_SCORE_CONTROL] & 1) ? 200 : 20;
        if (m_line >= y && m_line < y + 20) {
            int x = 60;
            draw_digit(x, y, m_reg[R_BCD0] >> 4, m_line - y);
            draw_digit(x + 16, y, m_reg[R_BCD0] & 0xf, m_line - y);
            if (m_reg[R_SCORE_CONTROL] & 2) x -= 16;
            draw_digit(x + 48, y, m_reg[R_BCD1] >> 4, m_line - y);
            draw_digit(x + 64, y, m_reg[R_BCD1] & 0xf, m_line - y);
        }
    }
    if (m_line == END_LINE) m_reg[R_SPRITE_COLLISION] |= 0x40;

    bool fin = m_sprites[0].finished_now || m_sprites[1].finished_now || m_sprites[2].finished_now || m_sprites[3].finished_now;
    if ((m_line == END_LINE || fin) && !m_irq_pause) {
        m_cpu.setIrq(true);
        m_irq_pause = 1;
    }
}

/************************************************************************
 Sound: square wave, frequency 7874 / (n + 1) Hz, off when n = 0
************************************************************************/

void Vc4000Core::soundport_w(uint8_t data)
{
    m_sndReg = data;
    m_sndPos = 0;
    m_sndSize = sampleRate() * (data + 1) / 7874;
}

void Vc4000Core::produceSamples()
{
    m_sampleAcc += sampleRate() / 16536.0;
    while (m_sampleAcc >= 1.0) {
        m_sampleAcc -= 1.0;
        float in = (m_sndReg && m_sndPos <= m_sndSize / 2) ? 0.5f : 0.0f;
        if (m_sndPos <= m_sndSize) m_sndPos++;
        if (m_sndPos > m_sndSize) m_sndPos = 0;
        float out = in - m_hpIn + 0.995f * m_hpOut;    // remove DC
        m_hpIn = in; m_hpOut = out;
        float s = out * 24000.0f;
        if (s > 32767) s = 32767;
        if (s < -32768) s = -32768;
        if (m_audio.size() < 44100) m_audio.push_back((int16_t)s);
    }
}

size_t Vc4000Core::readAudio(int16_t* out, size_t maxSamples)
{
    size_t n = std::min(maxSamples, m_audio.size());
    memcpy(out, m_audio.data(), n * sizeof(int16_t));
    m_audio.erase(m_audio.begin(), m_audio.begin() + n);
    return n;
}

/************************************************************************
 Frame: 312 lines; the CPU (3546875/4 Hz) runs between line interrupts
************************************************************************/

void Vc4000Core::runFrame()
{
    const double cyclesPerLine = (3546875.0 / 4.0) / 16536.0;
    for (int i = 0; i < 312; i++) {
        m_cycleAcc += cyclesPerLine;
        int c = (int)m_cycleAcc;
        m_cycleAcc -= c;
        m_cpu.run(c);
        video_line();
        produceSamples();
    }
    // A tapped button stays down until the game reacts - the game number shown
    // in the score digits changes - or, failing that, for at least one full
    // 32-frame poll cycle (Golf, Circus...) and until the game has read the
    // panel at least once (Cockpit only looks every ~72 frames).
    if (m_tap) {
        m_tapFrames++;
        bool reacted = m_reg[R_BCD0] != m_tapBcd[0] || m_reg[R_BCD1] != m_tapBcd[1];
        if (reacted || (m_tapFrames >= 32 && m_tapRead) || m_tapFrames >= 200) m_tap = 0;
    }
    // latched keypad presses the game read during this frame are released now
    m_latch &= ~m_latchSeen;
    m_latchSeen = 0;
    if (m_latch && ++m_latchFrames > 150) m_latch = 0;   // ~3 s

    for (int y = 0; y < VIS_H; y++) {
        uint32_t* dst = &m_frame[y * VIS_W];
        const uint8_t* src = &m_bitmap[y][VIS_X];
        for (int x = 0; x < VIS_W; x++) dst[x] = kPalette[src[x] & 7];
    }
}

/************************************************************************
 Save states
************************************************************************/

bool Vc4000Core::saveState(std::vector<uint8_t>& out)
{
    out.clear();
    auto put = [&](const void* p, size_t n) { out.insert(out.end(), (const uint8_t*)p, (const uint8_t*)p + n); };
    uint32_t magic = 0x34435650; // 'PVC4'
    put(&magic, 4);
    put(m_cartRam, sizeof(m_cartRam)); put(m_reg, sizeof(m_reg)); put(m_sprites, sizeof(m_sprites));
    put(&m_line, sizeof(m_line)); put(&m_sprite_collision_flags, 1); put(&m_background_collision_flags, 1);
    put(m_objects, sizeof(m_objects)); put(&m_irq_pause, 1); put(m_bitmap, sizeof(m_bitmap));
    put(&m_cycleAcc, sizeof(m_cycleAcc)); put(&m_sndReg, 1); put(&m_sndSize, sizeof(int)); put(&m_sndPos, sizeof(int));
    put(&m_sampleAcc, sizeof(m_sampleAcc));
    put(&m_cpu.m_ppc, 2); put(&m_cpu.m_page, 2); put(&m_cpu.m_iar, 2); put(&m_cpu.m_ea, 2);
    put(&m_cpu.m_psl, 1); put(&m_cpu.m_psu, 1); put(&m_cpu.m_r, 1); put(m_cpu.m_reg, 7);
    put(&m_cpu.m_halt, 1); put(&m_cpu.m_ir, 1); put(m_cpu.m_ras, sizeof(m_cpu.m_ras));
    put(&m_cpu.m_irq_state, 1); put(&m_cpu.m_icount, sizeof(int));
    return true;
}

bool Vc4000Core::loadState(const std::vector<uint8_t>& in)
{
    size_t pos = 0;
    bool ok = true;
    auto get = [&](void* p, size_t n) { if (pos + n > in.size()) { ok = false; return; } memcpy(p, &in[pos], n); pos += n; };
    uint32_t magic = 0; get(&magic, 4);
    if (magic != 0x34435650) return false;
    get(m_cartRam, sizeof(m_cartRam)); get(m_reg, sizeof(m_reg)); get(m_sprites, sizeof(m_sprites));
    get(&m_line, sizeof(m_line)); get(&m_sprite_collision_flags, 1); get(&m_background_collision_flags, 1);
    get(m_objects, sizeof(m_objects)); get(&m_irq_pause, 1); get(m_bitmap, sizeof(m_bitmap));
    get(&m_cycleAcc, sizeof(m_cycleAcc)); get(&m_sndReg, 1); get(&m_sndSize, sizeof(int)); get(&m_sndPos, sizeof(int));
    get(&m_sampleAcc, sizeof(m_sampleAcc));
    get(&m_cpu.m_ppc, 2); get(&m_cpu.m_page, 2); get(&m_cpu.m_iar, 2); get(&m_cpu.m_ea, 2);
    get(&m_cpu.m_psl, 1); get(&m_cpu.m_psu, 1); get(&m_cpu.m_r, 1); get(m_cpu.m_reg, 7);
    get(&m_cpu.m_halt, 1); get(&m_cpu.m_ir, 1); get(m_cpu.m_ras, sizeof(m_cpu.m_ras));
    get(&m_cpu.m_irq_state, 1); get(&m_cpu.m_icount, sizeof(int));
    m_audio.clear();
    return ok;
}
