// PocketPlug - V.R. Technology VT1682 core
// SPDX-License-Identifier: BSD-3-Clause
//
// Standalone port of MAME's VT1682 driver (src/mame/nintendo/vt1682.cpp and
// its io/uio/alu/timer devices), copyright David Haywood, BSD-3-Clause.
// See LICENSES/MAME-vt1682.txt.

#pragma once
#include "../../core_api.h"
#include "../../common/m6502.h"
#include <cstdint>
#include <vector>

struct Vt1682Config {
    bool pal = true;                 // PAL (26.601712 MHz) or NTSC (21.477272 MHz)
    int opcodeSwap = 0;              // 0 = none, 1 = D2/D7 swap, 2 = D5/D6 swap
    std::string romName;             // file inside the set
    uint32_t romSize = 0;
    uint32_t romCrc = 0;
};

class Vt1682Core : public Core {
public:
    explicit Vt1682Core(const Vt1682Config& cfg);

    bool load(const std::vector<RomFile>& roms, std::string& error) override;
    void reset() override;
    void runFrame() override;

    const uint32_t* frameBuffer() const override { return m_frame; }
    int frameWidth() const override { return 256; }
    int frameHeight() const override { return 240; }
    double refreshRate() const override { return m_refresh; }

    int sampleRate() const override { return 44100; }
    size_t readAudio(int16_t* out, size_t maxSamples) override;
    void setInputs(uint32_t pressed) override { m_inputs = pressed; }

    bool saveState(std::vector<uint8_t>& out) override;
    bool loadState(const std::vector<uint8_t>& in) override;

    // debugging (desktop runner)
    std::string debugInfo() const;

    // used by the bus trampolines
    uint8_t mainRead(uint16_t a);
    void mainWrite(uint16_t a, uint8_t d);
    uint8_t soundRead(uint16_t a);
    void soundWrite(uint16_t a, uint8_t d);

private:
    Vt1682Config m_cfg;
    std::vector<uint8_t> m_rom;      // 32MB address space (mirrored if the dump is smaller)
    uint32_t m_romMask = 0;

    M6502 m_maincpu, m_soundcpu;
    uint8_t m_optable[256];

    // ---- timing ----
    double m_master = 26601712.0;
    double m_refresh = 50.007;
    int m_lines = 312;
    double m_lineLen = 0;            // master ticks per scanline
    double m_mainTime = 0, m_soundTime = 0, m_now = 0;
    int m_mainDiv = 5;
    int m_vpos = 0;
    double m_lineStart = 0;

    // ---- audio ----
    double m_nextSample = 0, m_sampleStep = 0;
    std::vector<int16_t> m_audio;
    float m_hpPrevIn = 0, m_hpPrevOut = 0;

    uint32_t m_inputs = 0;
    uint32_t m_rng = 0x12345678;

    // ---- memories ----
    uint8_t m_mainram[0x1000];
    uint8_t m_soundShare[0x1000];
    uint8_t m_spriteram[0x800];
    uint8_t m_vram[0x2000];          // 0x0000-0x1fff (0x1c00-0x1fff is palette)

    // ---- video registers ----
    uint8_t m_2000, m_2001, m_2002_sprramaddr_2_0, m_2003_sprramaddr_10_3;
    uint8_t m_2005_vramaddr_7_0, m_2006_vramaddr_15_8;
    uint8_t m_201a_sp_segment_7_0, m_201b_sp_segment_11_8;
    uint8_t m_segment_7_0_bk[2], m_segment_11_8_bk[2];
    uint8_t m_main_control_bk[2], m_scroll_control_bk[2];
    uint8_t m_xscroll_7_0_bk[2], m_yscroll_7_0_bk[2];
    uint8_t m_200e_blend_pal_sel, m_200f_bk_pal_sel;
    uint8_t m_2008_lcd_vs_delay, m_2009_lcd_hs_delay_7_0, m_200a_lcd_fr_delay_7_0;
    uint8_t m_200d_misc_vregs2, m_200c_misc_vregs1, m_200b_misc_vregs0;
    uint8_t m_2018_spregs, m_2019_bkgain, m_2020_bk_linescroll, m_2021_lum_offset, m_2022_saturation_misc;
    uint8_t m_2023_lightgun_reset, m_2024_lightgun1_y, m_2025_lightgun1_x, m_2026_lightgun2_y, m_2027_lightgun2_x;
    uint8_t m_2031_red_dac, m_2032_green_dac, m_2033_blue_dac;
    uint8_t m_2028, m_2029, m_202a, m_202b, m_202e, m_2030;

    // ---- interrupts ----
    uint8_t m_scu_to_main_irq_active, m_timera_to_main_irq_active;
    uint16_t m_current_main_vector, m_new_main_vector;
    uint8_t m_timera_to_sound_irq_active, m_timerb_to_sound_irq_active;
    uint16_t m_current_sound_vector, m_new_sound_vector;

    // ---- system ----
    uint8_t m_prgbank1_r0, m_prgbank1_r1, m_210c_prgbank1_r2, m_2100_prgbank1_r3, m_2118_prgbank1_r4_r5;
    uint8_t m_2107_prgbank0_r0, m_2108_prgbank0_r1, m_2109_prgbank0_r2, m_210a_prgbank0_r3, m_prgbank0_r4, m_prgbank0_r5;
    uint8_t m_210b_misc_cs_prg0_bank_sel, m_2105_vt1682_2105_comr6_tvmodes, m_211c_regs_ext2421;
    uint8_t m_2122_dma_dt_addr_7_0, m_2123_dma_dt_addr_15_8, m_2124_dma_sr_addr_7_0, m_2125_dma_sr_addr_15_8;
    uint8_t m_2126_dma_sr_bank_addr_22_15, m_2128_dma_sr_bank_addr_24_23;
    uint8_t m_2106_enable_reg;
    uint8_t m_scpu_is_in_reset;
    uint8_t m_soundResetLine;        // CPU reset line state (held while 1)

    // ---- sound CPU regs ----
    uint8_t m_soundcpu_2118_dacleft_7_0, m_soundcpu_2119_dacleft_15_8;
    uint8_t m_soundcpu_211a_dacright_7_0, m_soundcpu_211b_dacright_15_8;
    uint16_t m_dacLeft, m_dacRight;

    // ---- ALUs (main / sound) ----
    uint8_t m_alu[2][8];

    // ---- timers: 0 = sound A, 1 = sound B, 2 = system ----
    struct Timer {
        uint8_t preload_7_0, preload_15_8, enable;
        double clock;        // timer clock in Hz
        double period;       // in master ticks, 0 = stopped
        double next;         // next expiry (master ticks)
    } m_timer[3];

    // ---- IO / UIO ----
    uint8_t m_210d_ioconfig, m_211e_adconfig, m_214c_ioefconfig, m_io_ab_o, m_io_cd_o, m_io_ef_o;
    uint8_t m_2129_uio_a_data, m_212a_uio_a_direction, m_212b_uio_a_attribute;
    uint8_t m_2149_uio_b_data, m_214a_uio_b_direction, m_214b_uio_b_attribute;

    // ---- render buffers ----
    uint8_t m_pal2_pri[256], m_pal1_pri[256], m_pal2_pix[256], m_pal1_pix[256];
    uint32_t m_frame[256 * 240];

    // helpers
    void resetState();
    void runUntil(double t);
    bool soundIdleSkip(double t);
    void checkTimer(int i, double now);
    void produceAudio(double now);
    void timerUpdate(int i);
    void timerEnableW(int i, uint8_t d);
    void timerIrq(int i, bool state);
    void soundcpuSetReset(bool asserted);
    void clear_sound_reset_line();
    uint8_t rand8();

    uint8_t romRead(uint32_t a) const { return m_rom[a & m_romMask]; }

    uint16_t get_spriteram_addr() { return (m_2002_sprramaddr_2_0 & 0x07) | (m_2003_sprramaddr_10_3 << 3); }
    void set_spriteram_addr(uint16_t addr) { m_2002_sprramaddr_2_0 = addr & 0x07; m_2003_sprramaddr_10_3 = addr >> 3; }
    void inc_spriteram_addr();
    uint16_t get_vram_addr() { return m_2005_vramaddr_7_0 | (m_2006_vramaddr_15_8 << 8); }
    void set_vram_addr(uint16_t addr) { m_2005_vramaddr_7_0 = addr & 0xff; m_2006_vramaddr_15_8 = addr >> 8; }
    uint8_t vramRead(uint16_t a) { return (a < 0x2000) ? m_vram[a] : 0; }
    void vramWrite(uint16_t a, uint8_t d) { if (a < 0x2000) m_vram[a] = d; }

    uint16_t get_dma_sr_addr() { return ((m_2124_dma_sr_addr_7_0) | (m_2125_dma_sr_addr_15_8 << 8)) & 0x7fff; }
    void set_dma_sr_addr(uint16_t addr);
    uint16_t get_dma_dt_addr() { return ((m_2122_dma_dt_addr_7_0) | (m_2123_dma_dt_addr_15_8 << 8)) & 0x7fff; }
    void set_dma_dt_addr(uint16_t addr);
    bool get_dma_sr_isext() { return m_2125_dma_sr_addr_15_8 & 0x80; }
    bool get_dma_dt_isext() { return m_2123_dma_dt_addr_15_8 & 0x80; }
    bool get_dma_dt_is_video();
    uint16_t get_dma_sr_bank_ddr() { return ((m_2126_dma_sr_bank_addr_22_15) | (m_2128_dma_sr_bank_addr_24_23 << 8)) & 0x3ff; }
    void do_dma_external_to_internal(int data, bool is_video);
    void do_dma_internal_to_internal(int data, bool is_video);
    void dma_trigger(uint8_t data);

    uint8_t translate_prg0select(uint8_t tp20_tp13);
    uint32_t translate_address_4000_to_7fff(uint16_t address);
    uint32_t translate_address_8000_to_ffff(uint16_t address);
    void set_210b(uint8_t data);
    void set_2106(uint8_t data);

    void update_main_interrupts();
    void update_sound_interrupts();

    uint8_t io_read(uint16_t a);
    void io_write(uint16_t a, uint8_t d);
    uint8_t alu_read(int which, int reg);
    void alu_write(int which, int reg, uint8_t d);

    // video
    void renderLine(int y);
    void setup_video_pages(int which, int tilesize, int vs, int hs, int y8, int x8, uint16_t* pagebases);
    int get_address_for_tilepos(int x, int y, int tilesize, uint16_t* pagebases);
    void draw_tile_pixline(int segment, int tile, int tileline, int x, int y, int palselect, int pal, int is16pix_high, int is16pix_wide, int bpp, int depth, int opaque, int flipx, int flipy);
    void draw_layer(int which, int opaque, int y);
    void draw_sprites(int y);
};
