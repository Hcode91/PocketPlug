// PocketPlug - TI TMS5110 speech synthesizer + TMS6100 VSM: port of MAME's
// sound/tms5110.h and machine/tms6100.h
// license:BSD-3-Clause
// copyright-holders:Frank Palazzolo, Jarek Burczynski, Aaron Giles, Jonathan Gevaryahu, Couriersud, hap

#pragma once
#include <cstdint>
#include <functional>
#include <vector>

#undef TMS5110_PERFECT_INTERPOLATION_HACK

// TMS6100 voice synthesis memory (serial ROM)
class Tms6100 {
public:
    void setRom(const uint8_t* rom, uint32_t size) { m_rom = rom; m_rommask = size - 1; }
    void reset();
    void m0_w(int state) { m_m0 = state ? 1 : 0; }
    void m1_w(int state) { m_m1 = state ? 1 : 0; }
    void add_w(uint8_t data) { m_add = data & 0xf; }
    int data_line_r() const { return (m_data & 8) ? 1 : 0; }
    void clk_w(int state);
    struct State {
        uint32_t address; uint8_t sa, count, prev_cmd, prev_m, add, data;
        int m0, m1, clk;
    };
private:
    void handle_command(uint8_t cmd);
    const uint8_t* m_rom = nullptr;
    uint32_t m_rommask = 0;
public:
    // state (kept together for save states)
    uint32_t m_address = 0;
    uint8_t m_sa = 0, m_count = 0, m_prev_cmd = 0, m_prev_m = 0, m_add = 0, m_data = 0;
    int m_m0 = 0, m_m1 = 0, m_clk = 0;
};

// TMS5110A (clock / 80 = sample rate: 8 kHz at 640 kHz)
class Tms5110 {
public:
    static constexpr uint8_t CMD_RESET = 0, CMD_LOAD_ADDRESS = 2, CMD_OUTPUT = 4, CMD_SPKSLOW = 6,
        CMD_READ_BIT = 8, CMD_SPEAK = 10, CMD_READ_BRANCH = 12, CMD_TEST_TALK = 14;
    void init(Tms6100* vsm);
    void device_reset();
    void ctl_w(uint8_t data);
    uint8_t ctl_r();
    void pdc_w(int state);
    // generate size samples (8 kHz)
    void process(int16_t *buffer, unsigned int size);
    // called before each access to catch the samples up to the CPU
    std::function<void()> update = [] {};

    Tms6100* m_vsm = nullptr;
	uint8_t TALK_STATUS() const { return m_SPEN | m_TALKD; }

	uint8_t m_SPEN;             /* set on speak command, cleared on stop command or reset command */
	uint8_t m_TALK;             /* set on SPEN & RESETL4(pc12->pc0 transition), cleared on stop command or reset command */
	uint8_t m_TALKD;            /* TALK(TCON) value, latched every RESETL4 */

private:
	void new_int_write(uint8_t rc, uint8_t m0, uint8_t m1, uint8_t addr);
	void new_int_write_addr(uint8_t addr);
	uint8_t new_int_read();
	int read_bits(int count);
	void perform_dummy_read();
	int32_t lattice_filter();
	void PDC_set(int data);
	void parse_frame();

	uint8_t &OLD_FRAME_SILENCE_FLAG() { return m_OLDE; } // 1 if E=0, 0 otherwise.
	uint8_t &OLD_FRAME_UNVOICED_FLAG() { return m_OLDP; } // 1 if P=0 (unvoiced), 0 if voiced

	bool NEW_FRAME_STOP_FLAG() const { return m_new_frame_energy_idx == 0x0F; } // 1 if this is a stop (Energy = 0x0F) frame
	bool NEW_FRAME_SILENCE_FLAG() const { return m_new_frame_energy_idx == 0; } // ditto as above
	bool NEW_FRAME_UNVOICED_FLAG() const { return m_new_frame_pitch_idx == 0; } // ditto as above

	// internal state

	/* coefficient tables */
	int m_variant = 5;                /* Variant of the 5110 - see tms5110.h */

	/* coefficient tables */
	const struct tms5100_coeffs *m_coeff;

	/* these contain global status bits */
	uint8_t m_PDC;
	uint8_t m_CTL_pins;
	uint8_t m_state;

	/* Rom interface */
	uint32_t m_address;
	uint8_t  m_next_is_address;
	uint8_t  m_schedule_dummy_read;
	uint8_t  m_addr_bit;
	/* read byte */
	uint8_t  m_CTL_buffer;


	/* these contain data describing the current and previous voice frames */
	uint8_t m_OLDE;
	uint8_t m_OLDP;

	uint8_t m_new_frame_energy_idx;
	uint8_t m_new_frame_pitch_idx;
	uint8_t m_new_frame_k_idx[10];


	/* these are all used to contain the current state of the sound generation */
#ifndef TMS5110_PERFECT_INTERPOLATION_HACK
	int16_t m_current_energy;
	int16_t m_current_pitch;
	int16_t m_current_k[10];
#else
	uint8_t m_old_frame_energy_idx;
	uint8_t m_old_frame_pitch_idx;
	uint8_t m_old_frame_k_idx[10];
	uint8_t m_old_zpar;
	uint8_t m_old_uv_zpar;

	int32_t m_current_energy;
	int32_t m_current_pitch;
	int32_t m_current_k[10];
#endif

	uint16_t m_previous_energy; /* needed for lattice filter to match patent */

	uint8_t m_subcycle;         /* contains the current subcycle for a given PC: 0 is A' (only used on SPKSLOW mode on 51xx), 1 is A, 2 is B */
	uint8_t m_subc_reload;      /* contains 1 for normal speech, 0 when SPKSLOW is active */
	uint8_t m_PC;               /* current parameter counter (what param is being interpolated), ranges from 0 to 12 */
	/* NOTE: the interpolation period counts 1,2,3,4,5,6,7,0 for divide by 8,8,8,4,4,2,2,1 */
	uint8_t m_IP;               /* the current interpolation period */
	uint8_t m_inhibit;          /* If 1, interpolation is inhibited until the DIV1 period */
	uint8_t m_uv_zpar;          /* If 1, zero k5 thru k10 coefficients */
	uint8_t m_zpar;             /* If 1, zero ALL parameters. */
	uint8_t m_pitch_zero;       /* circuit 412; pitch is forced to zero under certain circumstances */
	uint16_t m_pitch_count;     /* pitch counter; provides chirp rom address */

	int32_t m_u[11];
	int32_t m_x[10];

	uint16_t m_RNG;             /* the random noise generator configuration is: 1 + x + x^3 + x^4 + x^13 TODO: no it isn't */
	int16_t m_excitation_data;

	/* The TMS51xx has two different ways of providing output data: the
	   analog speaker pins (which were usually used) and the Digital I/O pin.
	   The internal DAC used to feed the analog pins is only 8 bits, and has the
	   funny clipping/clamping logic, while the digital pin gives full 10 bit
	   resolution of the output data.
	   TODO: add a way to set/reset this other than the FORCE_DIGITAL define
	 */
	uint8_t m_digital_select;

	int32_t m_speech_rom_bitnum;
};
