// PocketPlug - SunPlus SPG2xx (SPG24x) "TV game on a chip"
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Port of MAME's SPG2xx devices: spg2xx.cpp, spg2xx_io.cpp, spg2xx_sysdma.cpp,
// spg2xx_video.cpp, spg_renderer.cpp (Ryan Holtz, David Haywood), spg2xx_audio.cpp
// (Ryan Holtz, Jonathan Gevaryahu), imaadpcm.cpp (Andrew Gardner, Aaron Giles),
// all BSD-3-Clause, and the spg2xx_wiwi.cpp driver code for the 265-in-1
// handheld (Ryan Holtz, David Haywood, BSD-3-Clause). The u'nSP CPU is GPL-2.0+.
// Device code keeps MAME's names and structure; MAME's framework is replaced by
// spg_lite.h.

#pragma once
#include <cstdlib>
#include "../../core_api.h"
#include "spg_lite.h"
#include "unsp.h"
#include <memory>
#include <string>

typedef uint32_t offs_t;
class Spg2xxCore;

// what the sound device needs from the chip it sits in (the SPG2xx core, the GPL162xx core)
struct SpgAudioHost {
    virtual uint16_t audio_read_word(uint32_t address) = 0;
    virtual void audio_irq(int state) = 0;
    virtual void audio_ch_irq(int state) = 0;
    virtual SpgMachine& audio_machine() = 0;
    virtual ~SpgAudioHost() = default;
};

// ---------------------------------------------------------------------------
class ima_adpcm_state
{
public:
    ima_adpcm_state() { compute_tables(); reset(); }
    void reset();
    int16_t clock(uint8_t nibble);
    void save();
    void restore();
    int32_t m_signal, m_step, m_loop_signal, m_loop_step;
    bool m_saved;
private:
    static const int8_t s_index_shift[8];
    static int s_diff_lookup[89 * 16];
    static void compute_tables();
    static bool s_tables_computed;
};

// ---------------------------------------------------------------------------
class spg_renderer_device
{
public:
    void device_start();
    void device_reset();
    void draw_sprites(uint32_t scanline, int priority, uint32_t spritegfxdata_addr, uint16_t *paletteram, uint16_t *spriteram, int sprlimit);
    void draw_page(uint32_t scanline, int priority, uint16_t tilegfxdata_addr, uint16_t *scrollregs, uint16_t *tilemapregs, uint16_t *paletteram, uint16_t *scrollram, uint32_t which);
    void new_line();
    void apply_saturation_and_fade(uint32_t *dst);

    void set_video_reg_1c(uint16_t val) { m_video_regs_1c = val; update_vcmp_table(); }
    void set_video_reg_1d(uint16_t val) { m_video_regs_1d = val; update_vcmp_table(); }
    void set_video_reg_1e(uint16_t val) { m_video_regs_1e = val; update_vcmp_table(); }
    void set_video_reg_2a(uint16_t val) { m_video_regs_2a = val; }
    void set_video_reg_30(uint16_t val) { if (m_video_regs_30 != val) m_brightness_or_saturation_dirty = true; m_video_regs_30 = val; }
    void set_video_reg_3c(uint16_t val) { if (m_video_regs_3c != val) m_brightness_or_saturation_dirty = true; m_video_regs_3c = val; }
    uint16_t get_video_reg_1c() { return m_video_regs_1c; }
    uint16_t get_video_reg_1d() { return m_video_regs_1d; }
    uint16_t get_video_reg_1e() { return m_video_regs_1e; }
    uint16_t get_video_reg_2a() { return m_video_regs_2a; }
    uint16_t get_video_reg_30() { return m_video_regs_30; }
    uint16_t get_video_reg_3c() { return m_video_regs_3c; }
    void set_video_reg_42(uint16_t val) { m_video_regs_42 = val; }
    uint16_t get_video_reg_42() { return m_video_regs_42; }

    Spg2xxCore* m_soc = nullptr;
    uint16_t read_word(uint32_t a);

    template<bool Blend, bool FlipX>
    void draw_tilestrip(uint32_t screenwidth, uint32_t drawwidthmask, uint32_t tile_h, uint32_t tile_w, uint32_t tilegfxdata_addr, uint32_t tile, uint32_t tile_scanline, int drawx, bool flip_y, uint32_t palette_offset, const uint32_t nc_bpp, const uint32_t bits_per_row, const uint32_t words_per_tile, uint16_t *paletteram, uint8_t blendlevel);
    void draw_tilestrip(uint32_t screenwidth, uint32_t drawwidthmask, bool blend, bool flip_x, uint32_t tile_h, uint32_t tile_w, uint32_t tilegfxdata_addr, uint32_t tile, uint32_t tile_scanline, int drawx, bool flip_y, uint32_t palette_offset, const uint32_t nc_bpp, const uint32_t bits_per_row, const uint32_t words_per_tile, uint16_t *paletteram, uint8_t blendlevel);
    void draw_sprite(uint32_t scanline, int priority, uint32_t spritegfxdata_addr, uint32_t base_addr, uint16_t *paletteram, uint16_t *spriteram);
    void draw_linemap(uint32_t scanline, int priority, uint32_t tilegfxdata_addr, uint16_t *scrollregs, uint16_t *tilemapregs, uint16_t *paletteram);
    uint8_t mix_channel(uint8_t a, uint8_t b, uint8_t alpha) { return ((0x20 - alpha) * a + alpha * b) >> 5; }
    void update_vcmp_table();
    void update_palette_lookup();

    uint8_t m_rgb5_to_rgb8[32];
    std::unique_ptr<uint32_t[]> m_rgb555_to_rgb888;
    std::unique_ptr<uint32_t[]> m_rgb555_to_rgb888_current;
    // state (saved)
    uint16_t m_video_regs_1c, m_video_regs_1d, m_video_regs_1e, m_video_regs_2a, m_video_regs_30, m_video_regs_3c, m_video_regs_42;
    uint32_t m_ycmp_table[480];
    bool m_brightness_or_saturation_dirty;
    uint16_t m_linebuf[320];
};

// ---------------------------------------------------------------------------
class spg2xx_video_device
{
public:
    void device_start();
    void device_reset();
    void screen_update(int min_y, int max_y, uint32_t* bitmap);
    void vblank(int state);
    uint16_t video_r(offs_t offset);
    void video_w(offs_t offset, uint16_t data);
    void check_video_irq();
    void screenpos_hit(int param);
    void do_sprite_dma(uint32_t len);

    Spg2xxCore* m_soc = nullptr;
    SpgScreen* m_screen = nullptr;
    spg_renderer_device* m_renderer = nullptr;
    uint16_t* m_scrollram = nullptr;
    uint16_t* m_hcompram = nullptr;
    uint16_t* m_paletteram = nullptr;
    uint16_t* m_spriteram = nullptr;
    emu_timer* m_screenpos_timer = nullptr;
    uint16_t m_video_regs[0x100];
};

// ---------------------------------------------------------------------------
class spg2xx_io_device
{
public:
    void device_start();
    void device_reset();
    uint16_t io_r(offs_t offset);
    void io_w(offs_t offset, uint16_t data);
    uint16_t io_extended_r(offs_t offset);
    void io_extended_w(offs_t offset, uint16_t data);

    enum
    {
        REG_IO_MODE, REG_IOA_DATA, REG_IOA_BUFFER, REG_IOA_DIR, REG_IOA_ATTRIB, REG_IOA_MASK,
        REG_IOB_DATA, REG_IOB_BUFFER, REG_IOB_DIR, REG_IOB_ATTRIB, REG_IOB_MASK,
        REG_IOC_DATA, REG_IOC_BUFFER, REG_IOC_DIR, REG_IOC_ATTRIB, REG_IOC_MASK,
        REG_TIMEBASE_SETUP, REG_TIMEBASE_CLEAR, REG_TIMERA_DATA, REG_TIMERA_CTRL, REG_TIMERA_ON, REG_TIMERA_IRQCLR,
        REG_TIMERB_DATA, REG_TIMERB_CTRL, REG_TIMERB_ON, REG_TIMERB_IRQCLR,
        REG_VERT_LINE = 0x1c,
        REG_SYSTEM_CTRL = 0x20, REG_INT_CTRL, REG_INT_CLEAR, REG_EXT_MEMORY_CTRL, REG_WATCHDOG_CLEAR, REG_ADC_CTRL, REG_ADC_PAD,
        REG_ADC_DATA, REG_SLEEP_MODE, REG_WAKEUP_SOURCE, REG_WAKEUP_TIME, REG_NTSC_PAL,
        REG_PRNG1 = 0x2c, REG_PRNG2, REG_FIQ_SEL, REG_DATA_SEGMENT,
        REG_UART_CTRL, REG_UART_STATUS, REG_UART_RESET, REG_UART_BAUD1, REG_UART_BAUD2, REG_UART_TXBUF, REG_UART_RXBUF, REG_UART_RXFIFO,
        REG_SPI_CTRL = 0x40, REG_SPI_TXSTATUS, REG_SPI_TXDATA, REG_SPI_RXSTATUS, REG_SPI_RXDATA, REG_SPI_MISC,
        REG_SIO_SETUP = 0x50, REG_SIO_STATUS, REG_SIO_ADDRL, REG_SIO_ADDRH, REG_SIO_DATA, REG_SIO_AUTO_TX_NUM,
        REG_I2C_CMD = 0x58, REG_I2C_STATUS, REG_I2C_ACCESS, REG_I2C_ADDR, REG_I2C_SUBADDR, REG_I2C_DATA_OUT, REG_I2C_DATA_IN, REG_I2C_MODE,
        REG_REGULATOR_CTRL = 0x60, REG_CLOCK_CTRL, REG_IO_DRIVE_CTRL
    };

    void check_extint_irq(int channel);
    void check_timers_irq();
    void check_data_irq();
    void check_external_irq();
    void check_hifreq_periodic_irq();
    void check_tmb_lofreq_key_irq();
    void check_all_irqs(const uint16_t changed);
    void timer_ab_tick(int param);
    void timer_c_tick(int param);
    template <int Which> void tmb_timer_tick(int param);
    void uart_transmit_tick(int param);
    void uart_receive_tick(int param);
    void system_timer_tick(int param);
    void rng_clock_tick(int param);
    void watchdog_tick(int param);
    void spi_tx_tick(int param);
    template <int Which> void adc_convert_tick(int param);
    uint16_t clock_rng(int which);
    void update_porta_special_modes() {}
    void update_portb_special_modes() {}
    void do_gpio(uint32_t offset, bool write);
    uint16_t do_special_gpio(uint32_t index, uint16_t mask);
    void update_timer_b_rate();
    void increment_timer_a();
    void set_spi_irq(bool set);
    void update_spi_irqs();
    void do_i2c();

    Spg2xxCore* m_soc = nullptr;
    SpgScreen* m_screen = nullptr;
    unsp_device* m_cpu = nullptr;
    SpgMachine& machine();
    uint32_t clock();

    // devcb stand-ins (implemented by the SoC/driver)
    void m_porta_out(int, uint16_t data, uint16_t mask);
    void m_portb_out(int, uint16_t data, uint16_t mask);
    void m_portc_out(int, uint16_t data, uint16_t mask);
    uint16_t m_porta_in(int, uint16_t mask);
    uint16_t m_portb_in(int, uint16_t mask);
    uint16_t m_portc_in(int, uint16_t mask);
    uint16_t m_adc_in(int line) { return 0x0fff; }
    void m_i2c_w(uint16_t, uint8_t) {}
    uint8_t m_i2c_r(uint16_t) { return 0; }
    void m_uart_tx(uint8_t) {}
    void m_spi_tx(int) {}
    void m_chip_sel(uint8_t) {}
    uint16_t m_pal_read_cb();
    void m_timer_irq_cb(int state);
    void m_uart_adc_irq_cb(int state);
    void m_external_irq_cb(int state);
    void m_ffreq_tmr1_irq_cb(int state);
    void m_ffreq_tmr2_irq_cb(int state);
    void m_fiq_vector_w(uint8_t data);

    // state (saved via the SoC)
    uint16_t m_io_regs[0x100];
    uint8_t m_uart_rx_fifo[8];
    uint8_t m_uart_rx_fifo_start, m_uart_rx_fifo_end, m_uart_rx_fifo_count;
    bool m_uart_rx_available, m_uart_rx_irq, m_uart_tx_irq;
    uint8_t m_spi_tx_fifo[16];
    uint8_t m_spi_tx_fifo_start, m_spi_tx_fifo_end, m_spi_tx_fifo_count, m_spi_tx_buf, m_spi_tx_bit;
    uint8_t m_spi_rx_fifo[16];
    uint8_t m_spi_rx_fifo_start, m_spi_rx_fifo_end, m_spi_rx_fifo_count, m_spi_rx_buf, m_spi_rx_bit;
    bool m_extint[2];
    uint16_t m_timer_a_preload, m_timer_b_preload, m_timer_b_divisor, m_timer_b_tick_rate;
    uint32_t m_2khz_divider, m_1khz_divider, m_4hz_divider;
    uint32_t m_uart_baud_rate;
    uint32_t m_spi_rate;
    uint8_t m_sio_bits_remaining;
    bool m_sio_writing;

    emu_timer *m_adc_timer[4];
    emu_timer *m_tmb1, *m_tmb2, *m_timer_src_ab, *m_timer_src_c, *m_4khz_timer;
    emu_timer *m_uart_tx_timer, *m_uart_rx_timer, *m_rng_timer, *m_watchdog_timer, *m_spi_tx_timer;
};

// ---------------------------------------------------------------------------
class spg2xx_sysdma_device
{
public:
    void device_reset() { memset(m_dma_regs, 0, sizeof(m_dma_regs)); }
    uint16_t dma_r(offs_t offset);
    void dma_w(offs_t offset, uint16_t data);
    void do_cpu_dma(uint32_t len);
    Spg2xxCore* m_soc = nullptr;
    uint16_t m_dma_regs[4];
};

// ---------------------------------------------------------------------------
class spg2xx_audio_device
{
public:
    void device_start();
    void device_reset();
    uint16_t audio_r(offs_t offset);
    void audio_w(offs_t offset, uint16_t data);
    uint16_t audio_ctrl_r(offs_t offset);
    void audio_ctrl_w(offs_t offset, uint16_t data);
    uint16_t audio_phase_r(offs_t offset);
    void audio_phase_w(offs_t offset, uint16_t data);

    // stream: produces samples up to the current machine time
    void stream_update();
    void sound_stream_update(int16_t* left, int16_t* right, int samples);
    static constexpr uint32_t STREAM_RATE = 281250 / 4;
    uint64_t m_stream_samples = 0;          // samples produced since power-on
    std::vector<int16_t> m_out;             // interleaved L/R at STREAM_RATE

    void irq_tick(int param);
    void audio_beat_tick(int param);
    void audio_rampdown_tick(const uint32_t channel);
    bool audio_envelope_tick(const uint32_t channel);
    uint32_t get_rampdown_frame_count(const uint32_t channel);
    uint32_t get_envclk_frame_count(const uint32_t channel);

    bool get_channel_enable(const offs_t channel) const { return m_audio_ctrl_regs[AUDIO_CHANNEL_ENABLE] & (1 << channel); }
    bool get_channel_status(const offs_t channel) const { return m_audio_ctrl_regs[AUDIO_CHANNEL_STATUS] & (1 << channel); }
    bool get_manual_envelope_enable(const offs_t channel) const { return m_audio_ctrl_regs[AUDIO_CHANNEL_ENV_MODE] & (1 << channel); }
    bool get_auto_envelope_enable(const offs_t channel) const { return !get_manual_envelope_enable(channel); }
    uint32_t get_envelope_clock(const offs_t channel) const;
    uint16_t get_vol_sel() const { return (m_audio_ctrl_regs[AUDIO_CONTROL] & AUDIO_CONTROL_VOLSEL_MASK) >> AUDIO_CONTROL_VOLSEL_SHIFT; }
    uint16_t get_wave_addr_high(const offs_t channel) const { return m_audio_regs[(channel << 4) | AUDIO_MODE] & AUDIO_WADDR_HIGH_MASK; }
    uint16_t get_loop_addr_high(const offs_t channel) const { return (m_audio_regs[(channel << 4) | AUDIO_MODE] & AUDIO_LADDR_HIGH_MASK) >> AUDIO_LADDR_HIGH_SHIFT; }
    uint16_t get_tone_mode(const offs_t channel) const { return (m_audio_regs[(channel << 4) | AUDIO_MODE] & AUDIO_TONE_MODE_MASK) >> AUDIO_TONE_MODE_SHIFT; }
    uint16_t get_16bit_bit(const offs_t channel) const { return (m_audio_regs[(channel << 4) | AUDIO_MODE] & AUDIO_16M_MASK) ? 1 : 0; }
    uint16_t get_adpcm_bit(const offs_t channel) const { return (m_audio_regs[(channel << 4) | AUDIO_MODE] & AUDIO_ADPCM_MASK) ? 1 : 0; }
    uint16_t get_volume(const offs_t channel) const { return m_audio_regs[(channel << 4) | AUDIO_PAN_VOL] & AUDIO_VOLUME_MASK; }
    uint16_t get_pan(const offs_t channel) const { return (m_audio_regs[(channel << 4) | AUDIO_PAN_VOL] & AUDIO_PAN_MASK) >> AUDIO_PAN_SHIFT; }
    uint16_t get_envelope_inc(const offs_t channel) const { return m_audio_regs[(channel << 4) | AUDIO_ENVELOPE0] & AUDIO_ENVELOPE_INC_MASK; }
    uint16_t get_envelope_sign_bit(const offs_t channel) const { return (m_audio_regs[(channel << 4) | AUDIO_ENVELOPE0] & AUDIO_ENVELOPE_SIGN_MASK) ? 1 : 0; }
    uint16_t get_envelope_target(const offs_t channel) const { return (m_audio_regs[(channel << 4) | AUDIO_ENVELOPE0] & AUDIO_ENVELOPE_TARGET_MASK) >> AUDIO_ENVELOPE_TARGET_SHIFT; }
    uint16_t get_repeat_period_bit(const offs_t channel) const { return (m_audio_regs[(channel << 4) | AUDIO_ENVELOPE0] & AUDIO_ENVELOPE_REPEAT_PERIOD_MASK) ? 1 : 0; }
    uint16_t get_edd(const offs_t channel) const { return m_audio_regs[(channel << 4) | AUDIO_ENVELOPE_DATA] & AUDIO_EDD_MASK; }
    uint16_t get_envelope_count(const offs_t channel) const { return (m_audio_regs[(channel << 4) | AUDIO_ENVELOPE_DATA] & AUDIO_ENVELOPE_COUNT_MASK) >> AUDIO_ENVELOPE_COUNT_SHIFT; }
    void set_edd(const offs_t channel, uint8_t edd) { m_audio_regs[(channel << 4) | AUDIO_ENVELOPE_DATA] = (m_audio_regs[(channel << 4) | AUDIO_ENVELOPE_DATA] & ~AUDIO_EDD_MASK) | edd; }
    void set_envelope_count(const offs_t channel, uint16_t count) { m_audio_regs[(channel << 4) | AUDIO_ENVELOPE_DATA] = get_edd(channel) | (count << AUDIO_ENVELOPE_COUNT_SHIFT); }
    uint16_t get_envelope_load(const offs_t channel) const { return m_audio_regs[(channel << 4) | AUDIO_ENVELOPE1] & AUDIO_ENVELOPE_LOAD_MASK; }
    uint16_t get_envelope_repeat_bit(const offs_t channel) const { return (m_audio_regs[(channel << 4) | AUDIO_ENVELOPE1] & AUDIO_ENVELOPE_RPT_MASK) ? 1 : 0; }
    uint16_t get_envelope_repeat_count(const offs_t channel) const { return (m_audio_regs[(channel << 4) | AUDIO_ENVELOPE1] & AUDIO_ENVELOPE_RPCNT_MASK) >> AUDIO_ENVELOPE_RPCNT_SHIFT; }
    void set_envelope_repeat_count(const offs_t channel, const uint16_t count) { m_audio_regs[(channel << 4) | AUDIO_ENVELOPE1] = (m_audio_regs[(channel << 4) | AUDIO_ENVELOPE1] & ~AUDIO_ENVELOPE_RPCNT_MASK) | ((count << AUDIO_ENVELOPE_RPCNT_SHIFT) & AUDIO_ENVELOPE_RPCNT_MASK); }
    uint16_t get_envelope_addr_high(const offs_t channel) const { return m_audio_regs[(channel << 4) | AUDIO_ENVELOPE_ADDR_HIGH] & AUDIO_EADDR_HIGH_MASK; }
    uint16_t get_audio_irq_enable_bit(const offs_t channel) const { return (m_audio_regs[(channel << 4) | AUDIO_ENVELOPE_ADDR_HIGH] & AUDIO_IRQ_EN_MASK) ? 1 : 0; }
    uint16_t get_audio_irq_addr(const offs_t channel) const { return (m_audio_regs[(channel << 4) | AUDIO_ENVELOPE_ADDR_HIGH] & AUDIO_IRQ_ADDR_MASK) >> AUDIO_IRQ_ADDR_SHIFT; }
    uint16_t get_envelope_eaoffset(const offs_t channel) const { return m_audio_regs[(channel << 4) | AUDIO_ENVELOPE_LOOP_CTRL] & AUDIO_EAOFFSET_MASK; }
    uint16_t get_rampdown_offset(const offs_t channel) const { return (m_audio_regs[(channel << 4) | AUDIO_ENVELOPE_LOOP_CTRL] & AUDIO_RAMPDOWN_OFFSET_MASK) >> AUDIO_RAMPDOWN_OFFSET_SHIFT; }
    void set_envelope_eaoffset(const offs_t channel, uint16_t eaoffset) { m_audio_regs[(channel << 4) | AUDIO_ENVELOPE_LOOP_CTRL] = (m_audio_regs[(channel << 4) | AUDIO_ENVELOPE_LOOP_CTRL] & ~AUDIO_RAMPDOWN_OFFSET_MASK) | (eaoffset & AUDIO_EAOFFSET_MASK); }
    uint16_t get_point_number(const offs_t channel) const { return (m_audio_regs[(channel << 4) | AUDIO_ADPCM_SEL] & AUDIO_POINT_NUMBER_MASK) >> AUDIO_POINT_NUMBER_SHIFT; }
    uint16_t get_adpcm36_bit(const offs_t channel) const { return (m_audio_regs[(channel << 4) | AUDIO_ADPCM_SEL] & AUDIO_ADPCM36_MASK) ? 1 : 0; }
    uint16_t get_phase_high(const offs_t channel) const { return m_audio_phase_regs[(channel << 4) | AUDIO_PHASE_HIGH] & AUDIO_PHASE_HIGH_MASK; }
    uint16_t get_phase_accum_high(const offs_t channel) const { return m_audio_phase_regs[(channel << 4) | AUDIO_PHASE_ACCUM_HIGH] & AUDIO_PHASE_ACCUM_HIGH_MASK; }
    uint16_t get_target_phase_high(const offs_t channel) const { return m_audio_phase_regs[(channel << 4) | AUDIO_TARGET_PHASE_HIGH] & AUDIO_TARGET_PHASE_HIGH_MASK; }
    uint16_t get_rampdown_clock(const offs_t channel) const { return m_audio_phase_regs[(channel << 4) | AUDIO_RAMP_DOWN_CLOCK] & AUDIO_RAMP_DOWN_CLOCK_MASK; }
    uint16_t get_phase_offset(const offs_t channel) const { return m_audio_phase_regs[(channel << 4) | AUDIO_PHASE_CTRL] & AUDIO_PHASE_OFFSET_MASK; }
    uint16_t get_phase_sign_bit(const offs_t channel) const { return (m_audio_phase_regs[(channel << 4) | AUDIO_PHASE_CTRL] & AUDIO_PHASE_SIGN_MASK) >> AUDIO_PHASE_SIGN_SHIFT; }
    uint16_t get_phase_time_step(const offs_t channel) const { return (m_audio_phase_regs[(channel << 4) | AUDIO_PHASE_CTRL] & AUDIO_PHASE_TIME_STEP_MASK) >> AUDIO_PHASE_TIME_STEP_SHIFT; }
    uint32_t get_phase(const offs_t channel) const { return ((uint32_t)get_phase_high(channel) << 16) | m_audio_phase_regs[(channel << 4) | AUDIO_PHASE]; }
    uint32_t get_phase_accum(const offs_t channel) const { return ((uint32_t)get_phase_accum_high(channel) << 16) | m_audio_phase_regs[(channel << 4) | AUDIO_PHASE_ACCUM]; }
    uint32_t get_target_phase(const offs_t channel) const { return ((uint32_t)get_target_phase_high(channel) << 16) | m_audio_phase_regs[(channel << 4) | AUDIO_TARGET_PHASE]; }
    uint32_t get_wave_addr(const offs_t channel) const { return ((uint32_t)get_wave_addr_high(channel) << 16) | m_audio_regs[(channel << 4) | AUDIO_WAVE_ADDR]; }
    uint32_t get_loop_addr(const offs_t channel) const { return ((uint32_t)get_loop_addr_high(channel) << 16) | m_audio_regs[(channel << 4) | AUDIO_LOOP_ADDR]; }
    uint32_t get_envelope_addr(const offs_t channel) const { return ((uint32_t)get_envelope_addr_high(channel) << 16) | m_audio_regs[(channel << 4) | AUDIO_ENVELOPE_ADDR]; }
    void inc_wave_addr(const offs_t channel) { set_wave_addr(channel, get_wave_addr(channel) + 1); }
    void set_wave_addr(const offs_t channel, uint32_t addr)
    {
        m_audio_regs[(channel << 4) | AUDIO_MODE] &= ~AUDIO_WADDR_HIGH_MASK;
        m_audio_regs[(channel << 4) | AUDIO_MODE] |= (addr >> 16) & AUDIO_WADDR_HIGH_MASK;
        m_audio_regs[(channel << 4) | AUDIO_WAVE_ADDR] = addr & 0xffff;
    }

    enum
    {
        AUDIO_WAVE_ADDR = 0x000, AUDIO_MODE = 0x001, AUDIO_WADDR_HIGH_MASK = 0x003f, AUDIO_LADDR_HIGH_MASK = 0x0fc0, AUDIO_LADDR_HIGH_SHIFT = 6,
        AUDIO_TONE_MODE_MASK = 0x3000, AUDIO_TONE_MODE_SHIFT = 12, AUDIO_TONE_MODE_SW = 0, AUDIO_TONE_MODE_HW_ONESHOT = 1, AUDIO_TONE_MODE_HW_LOOP = 2,
        AUDIO_16M_MASK = 0x4000, AUDIO_ADPCM_MASK = 0x8000, AUDIO_LOOP_ADDR = 0x002, AUDIO_PAN_VOL = 0x003, AUDIO_PAN_VOL_MASK = 0x7f7f,
        AUDIO_VOLUME_MASK = 0x007f, AUDIO_PAN_MASK = 0x7f00, AUDIO_PAN_SHIFT = 8, AUDIO_ENVELOPE0 = 0x004, AUDIO_ENVELOPE_INC_MASK = 0x007f,
        AUDIO_ENVELOPE_SIGN_MASK = 0x0080, AUDIO_ENVELOPE_TARGET_MASK = 0x7f00, AUDIO_ENVELOPE_TARGET_SHIFT = 8, AUDIO_ENVELOPE_REPEAT_PERIOD_MASK = 0x8000,
        AUDIO_ENVELOPE_DATA = 0x005, AUDIO_ENVELOPE_DATA_MASK = 0xff7f, AUDIO_EDD_MASK = 0x007f, AUDIO_ENVELOPE_COUNT_MASK = 0xff00, AUDIO_ENVELOPE_COUNT_SHIFT = 8,
        AUDIO_ENVELOPE1 = 0x006, AUDIO_ENVELOPE_LOAD_MASK = 0x00ff, AUDIO_ENVELOPE_RPT_MASK = 0x0100, AUDIO_ENVELOPE_RPCNT_MASK = 0xfe00, AUDIO_ENVELOPE_RPCNT_SHIFT = 9,
        AUDIO_ENVELOPE_ADDR_HIGH = 0x007, AUDIO_EADDR_HIGH_MASK = 0x003f, AUDIO_IRQ_EN_MASK = 0x0040, AUDIO_IRQ_ADDR_MASK = 0xff80, AUDIO_IRQ_ADDR_SHIFT = 7,
        AUDIO_ENVELOPE_ADDR = 0x008, AUDIO_WAVE_DATA_PREV = 0x009, AUDIO_ENVELOPE_LOOP_CTRL = 0x00a, AUDIO_EAOFFSET_MASK = 0x01ff,
        AUDIO_RAMPDOWN_OFFSET_MASK = 0xfe00, AUDIO_RAMPDOWN_OFFSET_SHIFT = 9, AUDIO_WAVE_DATA = 0x00b, AUDIO_ADPCM_SEL = 0x00d,
        AUDIO_ADPCM_SEL_MASK = 0xfe00, AUDIO_POINT_NUMBER_MASK = 0x7e00, AUDIO_POINT_NUMBER_SHIFT = 9, AUDIO_ADPCM36_MASK = 0x8000,
    };
    enum
    {
        AUDIO_PHASE_HIGH = 0x000, AUDIO_PHASE_HIGH_MASK = 0x0007, AUDIO_PHASE_ACCUM_HIGH = 0x001, AUDIO_PHASE_ACCUM_HIGH_MASK = 0x0007,
        AUDIO_TARGET_PHASE_HIGH = 0x002, AUDIO_TARGET_PHASE_HIGH_MASK = 0x0007, AUDIO_RAMP_DOWN_CLOCK = 0x003, AUDIO_RAMP_DOWN_CLOCK_MASK = 0x0007,
        AUDIO_PHASE = 0x004, AUDIO_PHASE_ACCUM = 0x005, AUDIO_TARGET_PHASE = 0x006, AUDIO_PHASE_CTRL = 0x007, AUDIO_PHASE_OFFSET_MASK = 0x0fff,
        AUDIO_PHASE_SIGN_MASK = 0x1000, AUDIO_PHASE_SIGN_SHIFT = 12, AUDIO_PHASE_TIME_STEP_MASK = 0xe000, AUDIO_PHASE_TIME_STEP_SHIFT = 13,
        AUDIO_CHAN_OFFSET_MASK = 0xf0f,
    };
    enum
    {
        AUDIO_CHANNEL_ENABLE = 0x000, AUDIO_CHANNEL_ENABLE_MASK = 0xffff, AUDIO_MAIN_VOLUME = 0x001, AUDIO_MAIN_VOLUME_MASK = 0x007f,
        AUDIO_CHANNEL_FIQ_ENABLE = 0x002, AUDIO_CHANNEL_FIQ_ENABLE_MASK = 0xffff, AUDIO_CHANNEL_FIQ_STATUS = 0x003, AUDIO_CHANNEL_FIQ_STATUS_MASK = 0xffff,
        AUDIO_BEAT_BASE_COUNT = 0x004, AUDIO_BEAT_BASE_COUNT_MASK = 0x07ff, AUDIO_BEAT_COUNT = 0x005, AUDIO_BEAT_COUNT_MASK = 0x3fff,
        AUDIO_BIS_MASK = 0x4000, AUDIO_BIE_MASK = 0x8000, AUDIO_ENVCLK0 = 0x006, AUDIO_ENVCLK0_HIGH = 0x007, AUDIO_ENVCLK0_HIGH_MASK = 0xffff,
        AUDIO_ENVCLK1 = 0x008, AUDIO_ENVCLK1_HIGH = 0x009, AUDIO_ENVCLK1_HIGH_MASK = 0xffff, AUDIO_ENV_RAMP_DOWN = 0x00a, AUDIO_ENV_RAMP_DOWN_MASK = 0xffff,
        AUDIO_CHANNEL_STOP = 0x00b, AUDIO_CHANNEL_STOP_MASK = 0xffff, AUDIO_CHANNEL_ZERO_CROSS = 0x00c, AUDIO_CHANNEL_ZERO_CROSS_MASK = 0xffff,
        AUDIO_CONTROL = 0x00d, AUDIO_CONTROL_MASK = 0x9fe8, AUDIO_CONTROL_SATURATE_MASK = 0x8000, AUDIO_CONTROL_SOFTCH_MASK = 0x1000,
        AUDIO_CONTROL_COMPEN_MASK = 0x0800, AUDIO_CONTROL_NOHIGH_MASK = 0x0400, AUDIO_CONTROL_NOINT_MASK = 0x0200, AUDIO_CONTROL_EQEN_MASK = 0x0100,
        AUDIO_CONTROL_VOLSEL_MASK = 0x00c0, AUDIO_CONTROL_VOLSEL_SHIFT = 6, AUDIO_CONTROL_FOF_MASK = 0x0020, AUDIO_CONTROL_INIT_MASK = 0x0008,
        AUDIO_COMPRESS_CTRL = 0x00e, AUDIO_CHANNEL_STATUS = 0x00f, AUDIO_CHANNEL_STATUS_MASK = 0xffff,
        AUDIO_WAVE_IN_L = 0x010, AUDIO_WAVE_IN_R = 0x011, AUDIO_WAVE_OUT_L = 0x012, AUDIO_WAVE_OUT_R = 0x013,
        AUDIO_CHANNEL_REPEAT = 0x014, AUDIO_CHANNEL_REPEAT_MASK = 0xffff, AUDIO_CHANNEL_ENV_MODE = 0x015, AUDIO_CHANNEL_ENV_MODE_MASK = 0xffff,
        AUDIO_CHANNEL_TONE_RELEASE = 0x016, AUDIO_CHANNEL_TONE_RELEASE_MASK = 0xffff, AUDIO_CHANNEL_ENV_IRQ = 0x017, AUDIO_CHANNEL_ENV_IRQ_MASK = 0xffff,
        AUDIO_CHANNEL_PITCH_BEND = 0x018, AUDIO_CHANNEL_PITCH_BEND_MASK = 0xffff, AUDIO_SOFT_PHASE = 0x019, AUDIO_ATTACK_RELEASE = 0x01a,
        AUDIO_EQ_CUTOFF10 = 0x01b, AUDIO_EQ_CUTOFF10_MASK = 0x7f7f, AUDIO_EQ_CUTOFF32 = 0x01c, AUDIO_EQ_CUTOFF32_MASK = 0x7f7f,
        AUDIO_EQ_GAIN10 = 0x01d, AUDIO_EQ_GAIN10_MASK = 0x7f7f, AUDIO_EQ_GAIN32 = 0x01e, AUDIO_EQ_GAIN32_MASK = 0x7f7f
    };

    struct adpcm36_state { uint16_t m_remaining; uint16_t m_header; int16_t m_prevsamp[2]; };

    void check_irqs(const uint16_t changed);
    uint16_t read_space(offs_t offset);
    void start_channel(const uint32_t channel);
    void stop_channel(const uint32_t channel);
    bool advance_channel(const uint32_t channel);
    bool fetch_sample(const uint32_t channel);
    void loop_channel(const uint32_t channel);
    uint16_t decode_adpcm36_nybble(const uint32_t channel, const uint8_t data);

    SpgAudioHost* m_soc = nullptr;
    SpgMachine& machine();
    void m_irq_cb(int state);
    void m_ch_irq_cb(int state);

    // state (saved)
    bool m_debug_samples, m_debug_rates;
    uint16_t m_audio_regs[0x200];
    uint16_t m_audio_phase_regs[0x200];
    uint16_t m_audio_ctrl_regs[0x400];
    uint8_t m_sample_shift[16];
    uint32_t m_sample_count[16];
    double m_channel_rate[16];
    double m_channel_rate_accum[16];
    uint32_t m_rampdown_frame[16];
    uint32_t m_envclk_frame[16];
    uint32_t m_envelope_addr[16];
    uint16_t m_audio_curr_beat_base_count;
    ima_adpcm_state m_adpcm[16];
    adpcm36_state m_adpcm36_state[16];

    emu_timer *m_audio_beat;
    emu_timer *m_channel_irq[16];
    static const uint32_t s_rampdown_frame_counts[8];
    static const uint32_t s_envclk_frame_counts[16];
};

// ---------------------------------------------------------------------------
// The SoC + the 265-in-1 board (spg2xx_game_marc250_state)
// ---------------------------------------------------------------------------
struct Spg2xxConfig {
    std::string romName;
    uint64_t romSize = 0;
    bool marc250 = true;          // marc250/265games security pulses + banking
    int board = 0;                // 0 = marc250, 1 = plain spg2xx_game_state (abltenni), 2 = ABL Kick Boxing,
                                  // 3 = JungleTac lexizeus/lexiseal pads (arcade3d),
                                  // 4 = Hasbro 93LC66 boards (backybbs: pad on port B, EEPROM on port A),
                                  // 5 = plain P1/P3 pads, active low (ban_krkk: two players' mat buttons)
                                  // 6 = pad on P2, active high, buttons 1-3 (barbpet)
    bool pal = true;              // 50 Hz and the PAL flag; NTSC boards run at 60 Hz
    bool decryptAcFf = false;     // init_ablkickb: bitswap + xor 0xfafa on every word
    bool decryptZeus = false;     // init_zeus (spg2xx_lexibook.cpp)
    std::vector<uint32_t> romPatches;   // word index | (value in a parallel list)
    std::vector<uint16_t> romPatchValues;
};

class Spg2xxCore : public Core, public unsp_device::Bus, public SpgAudioHost
{
public:
    uint16_t audio_read_word(uint32_t a) override { return read_word(a); }
    void audio_irq(int s) override { audioirq_w(s); }
    void audio_ch_irq(int s) override { audiochirq_w(s); }
    SpgMachine& audio_machine() override { return m_machine; }
    explicit Spg2xxCore(const Spg2xxConfig& c);
    ~Spg2xxCore() override;
    bool load(const std::vector<RomFile>& roms, std::string& error) override;
    bool wantsRomPaths() const override { return true; }
    void reset() override;
    void runFrame() override;
    const uint32_t* frameBuffer() const override { return m_out; }
    int frameWidth() const override { return 320; }
    int frameHeight() const override { return 240; }
    double refreshRate() const override { return m_cfg.pal ? 50.0 : 60.0; }
    int sampleRate() const override { return 44100; }
    size_t readAudio(int16_t* out, size_t maxSamples) override;
    // bits: Up, Down, Left, Right, Button 1, Button 2, Start
    void setInputs(uint32_t pressed) override { m_inputs = pressed; }
    bool saveState(std::vector<uint8_t>& out) override;
    bool loadState(const std::vector<uint8_t>& in) override;

    // bus
    uint16_t read_word(uint32_t address) override;
    void write_word(uint32_t address, uint16_t data) override;
    inline uint16_t rom_word(uint32_t address) const
    {
        if (address >= 0x400000) return 0;
        uint64_t idx = (uint64_t)m_current_bank * 0x400000 + address;
        if (idx >= m_romWords) return 0;
        return m_rom[idx];
    }

    SpgMachine m_machine;
    SpgScreen m_screen;
    unsp_device m_cpu;
    spg2xx_io_device m_spg_io;
    spg2xx_video_device m_spg_video;
    spg_renderer_device m_renderer;
    spg2xx_sysdma_device m_spg_sysdma;
    spg2xx_audio_device m_spg_audio;

    // internal RAM and video memories
    uint16_t m_ram[0x2800];
    uint16_t m_scrollram[0x100];
    uint16_t m_hcompram[0x100];
    uint16_t m_paletteram[0x100];
    uint16_t m_spriteram[0x400];

    // spg2xx_device
    uint8_t m_fiq_vector = 0xff;
    void videoirq_w(int state) { if (m_fiq_vector == 0) m_cpu.set_state_unsynced(UNSP_FIQ_LINE, state); else m_cpu.set_state_unsynced(UNSP_IRQ0_LINE, state); }
    void timerirq_w(int state) { m_cpu.set_state_unsynced(UNSP_IRQ2_LINE, state); }
    void uartirq_w(int state) { m_cpu.set_state_unsynced(UNSP_IRQ3_LINE, state); }
    void audioirq_w(int state) { m_cpu.set_state_unsynced(UNSP_IRQ4_LINE, state); }
    void audiochirq_w(int state) { m_cpu.set_state_unsynced(UNSP_FIQ_LINE, state); }
    void extirq_w(int state) { m_cpu.set_state_unsynced(UNSP_IRQ5_LINE, state); }
    void ffreq1_w(int state) { m_cpu.set_state_unsynced(UNSP_IRQ6_LINE, state); }
    void ffreq2_w(int state) { m_cpu.set_state_unsynced(UNSP_IRQ7_LINE, state); }

    // driver (spg2xx_game_marc250_state)
    uint16_t porta_r();
    uint16_t portb_r();
    uint16_t portc_r();
    uint32_t m_cpuClock = 54000000;
    int board() const { return m_cfg.board; }
    bool isPal() const { return m_cfg.pal; }
    uint32_t m_rng = 1;
    // ABL's wireless sensor receiver on port B
    void sensorsFrame();
    void rxNext();
    void rxSend(int ch, uint8_t sensor, uint8_t reading, uint8_t count);
    uint32_t m_rxPrev = 0;
    bool m_rxStrobe = false, m_rxAcked = false, m_rxDrop = false, m_rxReady = false;
    uint8_t m_rxSwing = 0, m_rxMsg = 0, m_rxNext = 0, m_rxCode[8] = {}, m_rxBurst[8] = {};
    void porta_w(uint16_t data);
    void portb_w(uint16_t data);
    void switch_bank(uint32_t bank) { if ((int)bank != m_current_bank) m_current_bank = bank; }
    int m_current_bank = 0;
    uint16_t m_prev_porta = 0, m_prev_portb = 0;
    bool m_toggle = false, m_toggle2 = false;
    emu_timer* m_pulse_timer = nullptr;
    emu_timer* m_pulse_timer2 = nullptr;

    // screen timers
    emu_timer* m_vblank_begin_timer = nullptr;
    emu_timer* m_vblank_end_timer = nullptr;
    emu_timer* m_scanline0_timer = nullptr;
    emu_timer* m_sound_update_timer = nullptr;
    void vblank_begin();
    void vblank_end();
    void reset_partial_updates();
    bool update_partial(int scanline);
    bool m_frameDone = false;

    uint32_t m_inputs = 0;

    // HT93LC66 serial EEPROM (MAME eeprom_serial_93cxx, 16-bit, 256 words, no write delay)
    struct Eeprom93c66 {
        uint16_t mem[256];
        uint8_t state, cs, clk, di, locked, cmd, addr;
        uint16_t bits, accum;
        uint32_t shift;
        void reset() { state = 0; cs = clk = di = 0; locked = 1; bits = accum = 0; shift = 0; }
        int doRead() const { return state == 3 ? int(shift >> 31 & 1) : 1; }
        void csWrite(int v);
        void clkWrite(int v);
    } m_eeprom;

private:
    Spg2xxConfig m_cfg;
    uint16_t* m_rom = nullptr;
    uint64_t m_romWords = 0;
    void* m_map = nullptr;
    size_t m_mapLen = 0;
    std::vector<uint16_t> m_romCopy;
    uint32_t m_bitmap[320 * 240];
    uint32_t m_out[320 * 240];
    std::vector<int16_t> m_audio;
    double m_resampleAcc = 0;
    int32_t m_resampleSumL = 0, m_resampleN = 0;
    double m_dcPrevIn = 0, m_dcPrevOut = 0;
    void startDevices();
};
