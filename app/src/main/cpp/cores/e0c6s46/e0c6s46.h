// PocketPlug - Seiko Epson E0C6S46 / E0C6S48 MCU (Tamagotchi and friends)
// SPDX-License-Identifier: BSD-3-Clause
//
// Port of MAME's E0C6200 CPU core and E0C6S46 peripherals
// (src/devices/cpu/e0c6200/e0c6200.cpp, e0c6200op.cpp, e0c6s46.cpp;
// copyright hap, BSD-3-Clause). MAME's emu_timers are replaced by a single
// event scheduler counted in 1/65536 s units, which lets a halted CPU skip
// straight to its next event - fast enough to "catch up" days of real time.

#pragma once
#include <cstdint>
#include <vector>

class E0C6S46 {
public:
    // variant: 0 = E0C6S46 (6144x12 ROM, 640x4 RAM, 2x80x4 VRAM)
    //          1 = E0C6S48 (8192x12 ROM, 768x4 RAM, 2x102x4 VRAM)
    explicit E0C6S46(int variant = 0);

    /** Optional fast oscillator (Hz) the program can switch the CPU to; 0 = none. */
    void setOsc3(uint32_t hz) { m_osc3 = hz; }
    void setRom(const std::vector<uint16_t>& words) { m_rom = words; }
    void reset();                         // power-on reset (RAM cleared)

    // Advance emulation by `units` of 1/65536 s.
    void run(uint64_t units);

    // K0x/K1x input lines (line 0-7), state 1 = high
    void setInputLine(int line, int state);

    // buzzer output level (R43) and LCD
    int buzzer() const { return (m_port_r[4] >> 3 & 1) ? ((m_bz_pulse) ? 1 : 0) : 0; }
    int buzzerOut() const { return m_r43; }
    int lcdWidth() const { return m_vramSize * 4 / 8; }   // segments: 40 or 51
    // pixel at (com 0-15, seg), honouring the LCD control register
    int lcdPixel(int com, int seg) const;

    uint64_t now() const { return m_now; }

    // save state
    void save(std::vector<uint8_t>& out) const;
    bool load(const uint8_t*& p, const uint8_t* end);

    // when set, called on each buzzer level change with the time (units)
    void (*onBuzzer)(void* ctx, uint64_t when, int level) = nullptr;
    void* buzzerCtx = nullptr;

private:
    int m_variant;
    std::vector<uint16_t> m_rom;
    int m_romWords, m_ramSize, m_vramSize;

    uint8_t m_ram[0x300];
    uint8_t m_vram[2][0x66];

    // ---- CPU (E0C6200) ----
    uint16_t m_op = 0, m_prev_op = 0;
    uint8_t m_irq_vector = 0;
    bool m_possible_irq = false, m_halt = false, m_sleep = false;
    uint16_t m_pc = 0, m_prev_pc = 0, m_npc = 0, m_jpc = 0;
    uint8_t m_a = 0, m_b = 0, m_xp = 0, m_xh = 0, m_xl = 0, m_yp = 0, m_yh = 0, m_yl = 0, m_sp = 0, m_f = 0;
    int m_icount = 0;   // remaining CPU clocks in the current slice

    // ---- peripherals (E0C6S46) ----
    uint8_t m_irqflag[6], m_irqmask[6];
    uint8_t m_osc = 0, m_svd = 0, m_lcd_control = 0, m_lcd_contrast = 0;
    uint8_t m_port_r[5], m_r_dir = 0, m_port_p[4], m_p_dir = 0, m_p_pullup = 0, m_port_k[2], m_dfk0 = 0xf;
    int m_256_src_pulse = 0, m_watchdog_count = 0;
    uint8_t m_clktimer_count = 0;
    uint8_t m_stopwatch_on = 0;
    int m_swl_cur_pulse = 0, m_swl_slice = 0, m_swl_count = 0, m_swh_count = 0;
    uint8_t m_prgtimer_select = 0, m_prgtimer_on = 0;
    int m_prgtimer_src_pulse = 0, m_prgtimer_cur_pulse = 0;
    uint8_t m_prgtimer_count = 0, m_prgtimer_reload = 0;
    uint8_t m_bz_43_on = 0, m_bz_freq = 0, m_bz_envelope = 0, m_bz_envelope_count = 0, m_bz_duty_ratio = 0, m_bz_1shot_on = 0;
    bool m_bz_1shot_running = false;
    uint8_t m_bz_1shot_count = 0;
    int m_bz_pulse = 0;
    int m_r43 = 0;

    // ---- scheduler (units of 1/65536 s) ----
    static const uint64_t NEVER = ~0ull;
    uint64_t m_now = 0, m_target = 0, m_osc3Frac = 0;
    uint32_t m_osc3 = 0;
    uint64_t m_t256 = 0, m_tprg = NEVER, m_tbz = NEVER;

    // CPU
    void cpu_reset();
    void device_reset();
    void execute_one();
    void do_interrupt();
    bool check_interrupt();
    uint8_t data_r(uint16_t a);
    void data_w(uint16_t a, uint8_t d);
    uint8_t read_mx(); uint8_t read_my(); uint8_t read_mn();
    void write_mx(uint8_t d); void write_my(uint8_t d); void write_mn(uint8_t d);
    void push(uint8_t d); uint8_t pop(); void push_pc(); void pop_pc();
    void set_cf(uint8_t d); void set_zf(uint8_t d); void inc_x(); void inc_y();
    void do_branch(int cond = 1);
    uint8_t op_inc(uint8_t x); uint8_t op_dec(uint8_t x);
    uint8_t op_add(uint8_t x, uint8_t y, int decimal = 0); uint8_t op_adc(uint8_t x, uint8_t y, int decimal = 0);
    uint8_t op_sub(uint8_t x, uint8_t y, int decimal = 0); uint8_t op_sbc(uint8_t x, uint8_t y, int decimal = 0);
    uint8_t op_and(uint8_t x, uint8_t y); uint8_t op_or(uint8_t x, uint8_t y); uint8_t op_xor(uint8_t x, uint8_t y);
    uint8_t op_rlc(uint8_t x); uint8_t op_rrc(uint8_t x);

    // peripherals
    uint8_t io_r(uint8_t off);
    void io_w(uint8_t off, uint8_t d);
    void write_r(uint8_t port, uint8_t data);
    void write_r4_out();
    void core_256_cb();
    void clock_watchdog();
    void clock_clktimer();
    void clock_stopwatch();
    void clock_prgtimer();
    bool prgtimer_reset_prescaler();
    void prgtimer_cb();
    void schedule_buzzer();
    void buzzer_cb();
    void reset_buzzer();
    void clock_bz_1shot();
    void clock_bz_envelope();
    void reset_bz_envelope();
    void runEvents(uint64_t upTo);
    void addCycles(int cycles);
    uint64_t nextEvent() const;
};
