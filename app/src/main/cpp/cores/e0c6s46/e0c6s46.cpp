// PocketPlug - Seiko Epson E0C6S46 / E0C6S48 MCU
// SPDX-License-Identifier: BSD-3-Clause
//
// CPU core and peripherals ported from MAME (copyright hap, BSD-3-Clause):
// src/devices/cpu/e0c6200/e0c6200.cpp, e0c6200op.cpp, e0c6s46.cpp.
// Opcode handlers are unchanged; timers run on our own event scheduler.

#include "e0c6s46.h"
#include <cstring>
#include <algorithm>

enum { C_FLAG = 1, Z_FLAG = 2, D_FLAG = 4, I_FLAG = 8 };
enum { IRQREG_CLKTIMER = 0, IRQREG_STOPWATCH, IRQREG_PRGTIMER, IRQREG_SERIAL, IRQREG_INPUT0, IRQREG_INPUT1 };
#define BIT(x, n) (((x) >> (n)) & 1)

// one oscillator (32768 Hz) tick = 2 scheduler units
static const uint64_t UNITS_PER_TICK = 2;

/************************************************************************
 Opcode helpers (MAME e0c6200op.cpp)
************************************************************************/

// internal data memory read/write

// MX/MY

uint8_t E0C6S46::read_mx()
{
	uint16_t address = m_xp << 8 | m_xh << 4 | m_xl;
	return data_r(address) & 0xf;
}

uint8_t E0C6S46::read_my()
{
	uint16_t address = m_yp << 8 | m_yh << 4 | m_yl;
	return data_r(address) & 0xf;
}

void E0C6S46::write_mx(uint8_t data)
{
	uint16_t address = m_xp << 8 | m_xh << 4 | m_xl;
	data_w(address, data);
}

void E0C6S46::write_my(uint8_t data)
{
	uint16_t address = m_yp << 8 | m_yh << 4 | m_yl;
	data_w(address, data);
}

// Mn(RP)

uint8_t E0C6S46::read_mn()
{
	return data_r(m_op & 0xf) & 0xf;
}

void E0C6S46::write_mn(uint8_t data)
{
	data_w(m_op & 0xf, data);
}


// common stack ops

void E0C6S46::push(uint8_t data)
{
	data_w(--m_sp, data);
}

uint8_t E0C6S46::pop()
{
	return data_r(m_sp++) & 0xf;
}

void E0C6S46::push_pc()
{
	// the highest bit(bank bit) is not pushed onto the stack
	push(m_pc >> 8 & 0xf);
	push(m_pc >> 4 & 0xf);
	push(m_pc & 0xf);
}

void E0C6S46::pop_pc()
{
	// the highest bit(bank bit) is unchanged
	uint16_t bank = m_pc & 0x1000;
	m_pc = pop();
	m_pc |= pop() << 4;
	m_pc |= pop() << 8;
	m_pc |= bank;
}


// misc internal helpers

void E0C6S46::set_cf(uint8_t data)
{
	// set carry flag if bit 4 is set, reset otherwise
	m_f = (m_f & ~C_FLAG) | ((data & 0x10) ? C_FLAG : 0);
}

void E0C6S46::set_zf(uint8_t data)
{
	// set zero flag if 4-bit data is 0, reset otherwise
	m_f = (m_f & ~Z_FLAG) | ((data & 0xf) ? 0 : Z_FLAG);
}

void E0C6S46::inc_x()
{
	// increment X (XH.XL)
	m_xl++;
	m_xh = (m_xh + (m_xl >> 4 & 1)) & 0xf;
	m_xl &= 0xf;
}

void E0C6S46::inc_y()
{
	// increment Y (YH.YL)
	m_yl++;
	m_yh = (m_yh + (m_yl >> 4 & 1)) & 0xf;
	m_yl &= 0xf;
}

void E0C6S46::do_branch(int condition)
{
	// branch on condition
	if (condition)
		m_pc = m_jpc | (m_op & 0xff);
}




// common opcodes (simpler ones are handled directly)
// note: it is implied that all opcodes below except RRC take 7 clock cycles (5 already deducted)

// arithmetic instructions

uint8_t E0C6S46::op_inc(uint8_t x)
{
	// INC x: increment x (flags: C, Z)
	m_icount -= 2;
	x++;
	set_cf(x); set_zf(x);
	return x & 0xf;
}

uint8_t E0C6S46::op_dec(uint8_t x)
{
	// DEC x: decrement x (flags: C, Z)
	m_icount -= 2;
	x--;
	set_cf(x); set_zf(x);
	return x & 0xf;
}

uint8_t E0C6S46::op_add(uint8_t x, uint8_t y, int decimal)
{
	// ADD x,y: add y to x (flags: C, Z)
	m_icount -= 2;
	x += y;
	set_cf(x);

	// decimal correction
	if (m_f & decimal && x >= 10)
	{
		x -= 10;
		m_f |= C_FLAG;
	}

	set_zf(x);
	return x & 0xf;
}

uint8_t E0C6S46::op_adc(uint8_t x, uint8_t y, int decimal)
{
	// ADC x,y: add with carry y to x (flags: C, Z)
	return op_add(x, y + (m_f & 1), decimal);
}

uint8_t E0C6S46::op_sub(uint8_t x, uint8_t y, int decimal)
{
	// SUB x,y: subtract y from x (flags: C, Z)
	m_icount -= 2;
	x -= y;
	set_cf(x);

	// decimal correction (carry remains same)
	if (m_f & decimal && m_f & C_FLAG)
		x -= 6;

	set_zf(x);
	return x & 0xf;
}

uint8_t E0C6S46::op_sbc(uint8_t x, uint8_t y, int decimal)
{
	// SBC x,y: subtract with carry y from x (flags: C, Z)
	return op_sub(x, y + (m_f & 1), decimal);
}


// logical instructions

uint8_t E0C6S46::op_and(uint8_t x, uint8_t y)
{
	// AND x,y: logical AND x with y (flags: Z)
	m_icount -= 2;
	x &= y;
	set_zf(x);
	return x;
}

uint8_t E0C6S46::op_or(uint8_t x, uint8_t y)
{
	// OR x,y: logical OR x with y (flags: Z)
	m_icount -= 2;
	x |= y;
	set_zf(x);
	return x;
}

uint8_t E0C6S46::op_xor(uint8_t x, uint8_t y)
{
	// XOR x,y: exclusive-OR x with y (flags: Z)
	m_icount -= 2;
	x ^= y;
	set_zf(x);
	return x;
}

uint8_t E0C6S46::op_rlc(uint8_t x)
{
	// RLC x: rotate x left through carry (flags: C, Z)
	m_icount -= 2;
	x = (x << 1) | (m_f & 1);
	set_cf(x); set_zf(x);
	return x & 0xf;
}

uint8_t E0C6S46::op_rrc(uint8_t x)
{
	// RRC x: rotate x right through carry (flags: C, Z)
	// note: RRC only takes 5 clock cycles
	int c = x & 1;
	x = (x >> 1) | (m_f << 3 & 8);
	m_f = (m_f & ~C_FLAG) | c;
	set_zf(x);
	return x & 0xf;
}

/************************************************************************
 Opcodes (MAME e0c6200.cpp execute_one)
************************************************************************/

void E0C6S46::execute_one()
{
	switch (m_op & 0xf00)
	{
		// JP s: jump unconditional
		case 0x000:
			do_branch();
			break;

		// JP C,s: jump if carry
		case 0x200:
			do_branch(m_f & C_FLAG);
			break;

		// JP NC,s: jump if no carry
		case 0x300:
			do_branch(~m_f & C_FLAG);
			break;

		// JP Z,s: jump if zero
		case 0x600:
			do_branch(m_f & Z_FLAG);
			break;

		// JP NZ,s: jump if not zero
		case 0x700:
			do_branch(~m_f & Z_FLAG);
			break;

		// CALL s: call unconditional (on current bank)
		case 0x400:
			m_icount -= 2;
			push_pc();
			m_pc = (m_pc & 0x1000) | (m_jpc & 0x0f00) | (m_op & 0xff);
			break;

		// CALZ s: call zero page (on current bank)
		case 0x500:
			m_icount -= 2;
			push_pc();
			m_pc = (m_pc & 0x1000) | (m_op & 0xff);
			break;

		// RETD e: return from subroutine, then LBPX MX,e
		case 0x100:
			m_icount -= 7;
			pop_pc();
			[[fallthrough]];
		// LBPX MX,e: load memory with 8-bit immediate data, increment X by 2
		case 0x900:
			write_mx(m_op & 0xf); inc_x();
			write_mx(m_op >> 4 & 0xf); inc_x();
			break;

		// LD X,e: load X with 8-bit immediate data
		case 0xb00:
			m_xh = m_op >> 4 & 0xf;
			m_xl = m_op & 0xf;
			break;

		// LD Y,e: load Y with 8-bit immediate data
		case 0x800:
			m_yh = m_op >> 4 & 0xf;
			m_yl = m_op & 0xf;
			break;


		default:
			switch (m_op)
			{
		// LD r,q: load register with register
		case 0xec0: /* m_a = m_a; */ break;
		case 0xec1: m_a = m_b; break;
		case 0xec2: m_a = read_mx(); break;
		case 0xec3: m_a = read_my(); break;
		case 0xec4: m_b = m_a; break;
		case 0xec5: /* m_b = m_b; */ break;
		case 0xec6: m_b = read_mx(); break;
		case 0xec7: m_b = read_my(); break;
		case 0xec8: write_mx(m_a); break;
		case 0xec9: write_mx(m_b); break;
		case 0xeca: write_mx(read_mx()); break;
		case 0xecb: write_mx(read_my()); break;
		case 0xecc: write_my(m_a); break;
		case 0xecd: write_my(m_b); break;
		case 0xece: write_my(read_mx()); break;
		case 0xecf: write_my(read_my()); break;

		// LDPX r,q: LD r,q, then increment X
		case 0xee0: /* m_a = m_a; */ inc_x(); break;
		case 0xee1: m_a = m_b; inc_x(); break;
		case 0xee2: m_a = read_mx(); inc_x(); break;
		case 0xee3: m_a = read_my(); inc_x(); break;
		case 0xee4: m_b = m_a; inc_x(); break;
		case 0xee5: /* m_b = m_b; */ inc_x(); break;
		case 0xee6: m_b = read_mx(); inc_x(); break;
		case 0xee7: m_b = read_my(); inc_x(); break;
		case 0xee8: write_mx(m_a); inc_x(); break;
		case 0xee9: write_mx(m_b); inc_x(); break;
		case 0xeea: write_mx(read_mx()); inc_x(); break;
		case 0xeeb: write_mx(read_my()); inc_x(); break;
		case 0xeec: write_my(m_a); inc_x(); break;
		case 0xeed: write_my(m_b); inc_x(); break;
		case 0xeee: write_my(read_mx()); inc_x(); break;
		case 0xeef: write_my(read_my()); inc_x(); break;

		// LDPY r,q: LD r,q, then increment Y
		case 0xef0: /* m_a = m_a; */ inc_y(); break;
		case 0xef1: m_a = m_b; inc_y(); break;
		case 0xef2: m_a = read_mx(); inc_y(); break;
		case 0xef3: m_a = read_my(); inc_y(); break;
		case 0xef4: m_b = m_a; inc_y(); break;
		case 0xef5: /* m_b = m_b; */ inc_y(); break;
		case 0xef6: m_b = read_mx(); inc_y(); break;
		case 0xef7: m_b = read_my(); inc_y(); break;
		case 0xef8: write_mx(m_a); inc_y(); break;
		case 0xef9: write_mx(m_b); inc_y(); break;
		case 0xefa: write_mx(read_mx()); inc_y(); break;
		case 0xefb: write_mx(read_my()); inc_y(); break;
		case 0xefc: write_my(m_a); inc_y(); break;
		case 0xefd: write_my(m_b); inc_y(); break;
		case 0xefe: write_my(read_mx()); inc_y(); break;
		case 0xeff: write_my(read_my()); inc_y(); break;

		// LD Xphl/Yphl,r: load IX/IY with register
		case 0xe80: m_xp = m_a; break;
		case 0xe81: m_xp = m_b; break;
		case 0xe82: m_xp = read_mx(); break;
		case 0xe83: m_xp = read_my(); break;
		case 0xe84: m_xh = m_a; break;
		case 0xe85: m_xh = m_b; break;
		case 0xe86: m_xh = read_mx(); break;
		case 0xe87: m_xh = read_my(); break;
		case 0xe88: m_xl = m_a; break;
		case 0xe89: m_xl = m_b; break;
		case 0xe8a: m_xl = read_mx(); break;
		case 0xe8b: m_xl = read_my(); break;
		case 0xe90: m_yp = m_a; break;
		case 0xe91: m_yp = m_b; break;
		case 0xe92: m_yp = read_mx(); break;
		case 0xe93: m_yp = read_my(); break;
		case 0xe94: m_yh = m_a; break;
		case 0xe95: m_yh = m_b; break;
		case 0xe96: m_yh = read_mx(); break;
		case 0xe97: m_yh = read_my(); break;
		case 0xe98: m_yl = m_a; break;
		case 0xe99: m_yl = m_b; break;
		case 0xe9a: m_yl = read_mx(); break;
		case 0xe9b: m_yl = read_my(); break;

		// LD r,Xphl/Yphl: load register with IX/IY
		case 0xea0: m_a = m_xp; break;
		case 0xea1: m_b = m_xp; break;
		case 0xea2: write_mx(m_xp); break;
		case 0xea3: write_my(m_xp); break;
		case 0xea4: m_a = m_xh; break;
		case 0xea5: m_b = m_xh; break;
		case 0xea6: write_mx(m_xh); break;
		case 0xea7: write_my(m_xh); break;
		case 0xea8: m_a = m_xl; break;
		case 0xea9: m_b = m_xl; break;
		case 0xeaa: write_mx(m_xl); break;
		case 0xeab: write_my(m_xl); break;
		case 0xeb0: m_a = m_yp; break;
		case 0xeb1: m_b = m_yp; break;
		case 0xeb2: write_mx(m_yp); break;
		case 0xeb3: write_my(m_yp); break;
		case 0xeb4: m_a = m_yh; break;
		case 0xeb5: m_b = m_yh; break;
		case 0xeb6: write_mx(m_yh); break;
		case 0xeb7: write_my(m_yh); break;
		case 0xeb8: m_a = m_yl; break;
		case 0xeb9: m_b = m_yl; break;
		case 0xeba: write_mx(m_yl); break;
		case 0xebb: write_my(m_yl); break;

		// LD SPhl,r: load stackpointer with register
		case 0xfe0: m_sp = (m_sp & 0xf0) | m_a; break;
		case 0xfe1: m_sp = (m_sp & 0xf0) | m_b; break;
		case 0xfe2: m_sp = (m_sp & 0xf0) | read_mx(); break;
		case 0xfe3: m_sp = (m_sp & 0xf0) | read_my(); break;
		case 0xff0: m_sp = (m_sp & 0x0f) | m_a << 4; break;
		case 0xff1: m_sp = (m_sp & 0x0f) | m_b << 4; break;
		case 0xff2: m_sp = (m_sp & 0x0f) | read_mx() << 4; break;
		case 0xff3: m_sp = (m_sp & 0x0f) | read_my() << 4; break;

		// LD r,SPhl: load register with stackpointer
		case 0xfe4: m_a = m_sp >> 4 & 0xf; break;
		case 0xfe5: m_b = m_sp >> 4 & 0xf; break;
		case 0xfe6: write_mx(m_sp >> 4 & 0xf); break;
		case 0xfe7: write_my(m_sp >> 4 & 0xf); break;
		case 0xff4: m_a = m_sp & 0xf; break;
		case 0xff5: m_b = m_sp & 0xf; break;
		case 0xff6: write_mx(m_sp & 0xf); break;
		case 0xff7: write_my(m_sp & 0xf); break;

		// ADD r,q: add register to register (flags: C, Z)
		case 0xa80: m_a = op_add(m_a, m_a, D_FLAG); break;
		case 0xa81: m_a = op_add(m_a, m_b, D_FLAG); break;
		case 0xa82: m_a = op_add(m_a, read_mx(), D_FLAG); break;
		case 0xa83: m_a = op_add(m_a, read_my(), D_FLAG); break;
		case 0xa84: m_b = op_add(m_b, m_a, D_FLAG); break;
		case 0xa85: m_b = op_add(m_b, m_b, D_FLAG); break;
		case 0xa86: m_b = op_add(m_b, read_mx(), D_FLAG); break;
		case 0xa87: m_b = op_add(m_b, read_my(), D_FLAG); break;
		case 0xa88: write_mx(op_add(read_mx(), m_a, D_FLAG)); break;
		case 0xa89: write_mx(op_add(read_mx(), m_b, D_FLAG)); break;
		case 0xa8a: write_mx(op_add(read_mx(), read_mx(), D_FLAG)); break;
		case 0xa8b: write_mx(op_add(read_mx(), read_my(), D_FLAG)); break;
		case 0xa8c: write_my(op_add(read_my(), m_a, D_FLAG)); break;
		case 0xa8d: write_my(op_add(read_my(), m_b, D_FLAG)); break;
		case 0xa8e: write_my(op_add(read_my(), read_mx(), D_FLAG)); break;
		case 0xa8f: write_my(op_add(read_my(), read_my(), D_FLAG)); break;

		// ADC r,q: add with carry register to register (flags: C, Z)
		case 0xa90: m_a = op_adc(m_a, m_a, D_FLAG); break;
		case 0xa91: m_a = op_adc(m_a, m_b, D_FLAG); break;
		case 0xa92: m_a = op_adc(m_a, read_mx(), D_FLAG); break;
		case 0xa93: m_a = op_adc(m_a, read_my(), D_FLAG); break;
		case 0xa94: m_b = op_adc(m_b, m_a, D_FLAG); break;
		case 0xa95: m_b = op_adc(m_b, m_b, D_FLAG); break;
		case 0xa96: m_b = op_adc(m_b, read_mx(), D_FLAG); break;
		case 0xa97: m_b = op_adc(m_b, read_my(), D_FLAG); break;
		case 0xa98: write_mx(op_adc(read_mx(), m_a, D_FLAG)); break;
		case 0xa99: write_mx(op_adc(read_mx(), m_b, D_FLAG)); break;
		case 0xa9a: write_mx(op_adc(read_mx(), read_mx(), D_FLAG)); break;
		case 0xa9b: write_mx(op_adc(read_mx(), read_my(), D_FLAG)); break;
		case 0xa9c: write_my(op_adc(read_my(), m_a, D_FLAG)); break;
		case 0xa9d: write_my(op_adc(read_my(), m_b, D_FLAG)); break;
		case 0xa9e: write_my(op_adc(read_my(), read_mx(), D_FLAG)); break;
		case 0xa9f: write_my(op_adc(read_my(), read_my(), D_FLAG)); break;

		// ACPX MX,r: ADC MX,r, then increment X (flags: C, Z)
		case 0xf28: write_mx(op_adc(read_mx(), m_a, D_FLAG)); inc_x(); break;
		case 0xf29: write_mx(op_adc(read_mx(), m_b, D_FLAG)); inc_x(); break;
		case 0xf2a: write_mx(op_adc(read_mx(), read_mx(), D_FLAG)); inc_x(); break;
		case 0xf2b: write_mx(op_adc(read_mx(), read_my(), D_FLAG)); inc_x(); break;

		// ACPY MY,r: ADC MY,r, then increment Y (flags: C, Z)
		case 0xf2c: write_my(op_adc(read_my(), m_a, D_FLAG)); inc_y(); break;
		case 0xf2d: write_my(op_adc(read_my(), m_b, D_FLAG)); inc_y(); break;
		case 0xf2e: write_my(op_adc(read_my(), read_mx(), D_FLAG)); inc_y(); break;
		case 0xf2f: write_my(op_adc(read_my(), read_my(), D_FLAG)); inc_y(); break;

		// SUB r,q: subtract register from register (flags: C, Z)
		case 0xaa0: m_a = op_sub(m_a, m_a, D_FLAG); break;
		case 0xaa1: m_a = op_sub(m_a, m_b, D_FLAG); break;
		case 0xaa2: m_a = op_sub(m_a, read_mx(), D_FLAG); break;
		case 0xaa3: m_a = op_sub(m_a, read_my(), D_FLAG); break;
		case 0xaa4: m_b = op_sub(m_b, m_a, D_FLAG); break;
		case 0xaa5: m_b = op_sub(m_b, m_b, D_FLAG); break;
		case 0xaa6: m_b = op_sub(m_b, read_mx(), D_FLAG); break;
		case 0xaa7: m_b = op_sub(m_b, read_my(), D_FLAG); break;
		case 0xaa8: write_mx(op_sub(read_mx(), m_a, D_FLAG)); break;
		case 0xaa9: write_mx(op_sub(read_mx(), m_b, D_FLAG)); break;
		case 0xaaa: write_mx(op_sub(read_mx(), read_mx(), D_FLAG)); break;
		case 0xaab: write_mx(op_sub(read_mx(), read_my(), D_FLAG)); break;
		case 0xaac: write_my(op_sub(read_my(), m_a, D_FLAG)); break;
		case 0xaad: write_my(op_sub(read_my(), m_b, D_FLAG)); break;
		case 0xaae: write_my(op_sub(read_my(), read_mx(), D_FLAG)); break;
		case 0xaaf: write_my(op_sub(read_my(), read_my(), D_FLAG)); break;

		// SBC r,q: subtract with carry register from register (flags: C, Z)
		case 0xab0: m_a = op_sbc(m_a, m_a, D_FLAG); break;
		case 0xab1: m_a = op_sbc(m_a, m_b, D_FLAG); break;
		case 0xab2: m_a = op_sbc(m_a, read_mx(), D_FLAG); break;
		case 0xab3: m_a = op_sbc(m_a, read_my(), D_FLAG); break;
		case 0xab4: m_b = op_sbc(m_b, m_a, D_FLAG); break;
		case 0xab5: m_b = op_sbc(m_b, m_b, D_FLAG); break;
		case 0xab6: m_b = op_sbc(m_b, read_mx(), D_FLAG); break;
		case 0xab7: m_b = op_sbc(m_b, read_my(), D_FLAG); break;
		case 0xab8: write_mx(op_sbc(read_mx(), m_a, D_FLAG)); break;
		case 0xab9: write_mx(op_sbc(read_mx(), m_b, D_FLAG)); break;
		case 0xaba: write_mx(op_sbc(read_mx(), read_mx(), D_FLAG)); break;
		case 0xabb: write_mx(op_sbc(read_mx(), read_my(), D_FLAG)); break;
		case 0xabc: write_my(op_sbc(read_my(), m_a, D_FLAG)); break;
		case 0xabd: write_my(op_sbc(read_my(), m_b, D_FLAG)); break;
		case 0xabe: write_my(op_sbc(read_my(), read_mx(), D_FLAG)); break;
		case 0xabf: write_my(op_sbc(read_my(), read_my(), D_FLAG)); break;

		// SCPX MX,r: SBC MX,r, then increment X (flags: C, Z)
		case 0xf38: write_mx(op_sbc(read_mx(), m_a, D_FLAG)); inc_x(); break;
		case 0xf39: write_mx(op_sbc(read_mx(), m_b, D_FLAG)); inc_x(); break;
		case 0xf3a: write_mx(op_sbc(read_mx(), read_mx(), D_FLAG)); inc_x(); break;
		case 0xf3b: write_mx(op_sbc(read_mx(), read_my(), D_FLAG)); inc_x(); break;

		// SCPY MY,r: SBC MY,r, then increment Y (flags: C, Z)
		case 0xf3c: write_my(op_sbc(read_my(), m_a, D_FLAG)); inc_y(); break;
		case 0xf3d: write_my(op_sbc(read_my(), m_b, D_FLAG)); inc_y(); break;
		case 0xf3e: write_my(op_sbc(read_my(), read_mx(), D_FLAG)); inc_y(); break;
		case 0xf3f: write_my(op_sbc(read_my(), read_my(), D_FLAG)); inc_y(); break;

		// CP r,q: compare: SUB r,q, but discard result (flags: C, Z, no D flag)
		case 0xf00: op_sub(m_a, m_a); break;
		case 0xf01: op_sub(m_a, m_b); break;
		case 0xf02: op_sub(m_a, read_mx()); break;
		case 0xf03: op_sub(m_a, read_my()); break;
		case 0xf04: op_sub(m_b, m_a); break;
		case 0xf05: op_sub(m_b, m_b); break;
		case 0xf06: op_sub(m_b, read_mx()); break;
		case 0xf07: op_sub(m_b, read_my()); break;
		case 0xf08: op_sub(read_mx(), m_a); break;
		case 0xf09: op_sub(read_mx(), m_b); break;
		case 0xf0a: op_sub(read_mx(), read_mx()); break;
		case 0xf0b: op_sub(read_mx(), read_my()); break;
		case 0xf0c: op_sub(read_my(), m_a); break;
		case 0xf0d: op_sub(read_my(), m_b); break;
		case 0xf0e: op_sub(read_my(), read_mx()); break;
		case 0xf0f: op_sub(read_my(), read_my()); break;

		// AND r,q: logical AND register with register (flags: Z)
		case 0xac0: m_a = op_and(m_a, m_a); break;
		case 0xac1: m_a = op_and(m_a, m_b); break;
		case 0xac2: m_a = op_and(m_a, read_mx()); break;
		case 0xac3: m_a = op_and(m_a, read_my()); break;
		case 0xac4: m_b = op_and(m_b, m_a); break;
		case 0xac5: m_b = op_and(m_b, m_b); break;
		case 0xac6: m_b = op_and(m_b, read_mx()); break;
		case 0xac7: m_b = op_and(m_b, read_my()); break;
		case 0xac8: write_mx(op_and(read_mx(), m_a)); break;
		case 0xac9: write_mx(op_and(read_mx(), m_b)); break;
		case 0xaca: write_mx(op_and(read_mx(), read_mx())); break;
		case 0xacb: write_mx(op_and(read_mx(), read_my())); break;
		case 0xacc: write_my(op_and(read_my(), m_a)); break;
		case 0xacd: write_my(op_and(read_my(), m_b)); break;
		case 0xace: write_my(op_and(read_my(), read_mx())); break;
		case 0xacf: write_my(op_and(read_my(), read_my())); break;

		// FAN r,q: flag-check: AND r,q, but discard result (flags: Z)
		case 0xf10: op_and(m_a, m_a); break;
		case 0xf11: op_and(m_a, m_b); break;
		case 0xf12: op_and(m_a, read_mx()); break;
		case 0xf13: op_and(m_a, read_my()); break;
		case 0xf14: op_and(m_b, m_a); break;
		case 0xf15: op_and(m_b, m_b); break;
		case 0xf16: op_and(m_b, read_mx()); break;
		case 0xf17: op_and(m_b, read_my()); break;
		case 0xf18: op_and(read_mx(), m_a); break;
		case 0xf19: op_and(read_mx(), m_b); break;
		case 0xf1a: op_and(read_mx(), read_mx()); break;
		case 0xf1b: op_and(read_mx(), read_my()); break;
		case 0xf1c: op_and(read_my(), m_a); break;
		case 0xf1d: op_and(read_my(), m_b); break;
		case 0xf1e: op_and(read_my(), read_mx()); break;
		case 0xf1f: op_and(read_my(), read_my()); break;

		// OR r,q: logical OR register with register (flags: Z)
		case 0xad0: m_a = op_or(m_a, m_a); break;
		case 0xad1: m_a = op_or(m_a, m_b); break;
		case 0xad2: m_a = op_or(m_a, read_mx()); break;
		case 0xad3: m_a = op_or(m_a, read_my()); break;
		case 0xad4: m_b = op_or(m_b, m_a); break;
		case 0xad5: m_b = op_or(m_b, m_b); break;
		case 0xad6: m_b = op_or(m_b, read_mx()); break;
		case 0xad7: m_b = op_or(m_b, read_my()); break;
		case 0xad8: write_mx(op_or(read_mx(), m_a)); break;
		case 0xad9: write_mx(op_or(read_mx(), m_b)); break;
		case 0xada: write_mx(op_or(read_mx(), read_mx())); break;
		case 0xadb: write_mx(op_or(read_mx(), read_my())); break;
		case 0xadc: write_my(op_or(read_my(), m_a)); break;
		case 0xadd: write_my(op_or(read_my(), m_b)); break;
		case 0xade: write_my(op_or(read_my(), read_mx())); break;
		case 0xadf: write_my(op_or(read_my(), read_my())); break;

		// XOR r,q: exclusive-OR register with register (flags: Z)
		case 0xae0: m_a = op_xor(m_a, m_a); break;
		case 0xae1: m_a = op_xor(m_a, m_b); break;
		case 0xae2: m_a = op_xor(m_a, read_mx()); break;
		case 0xae3: m_a = op_xor(m_a, read_my()); break;
		case 0xae4: m_b = op_xor(m_b, m_a); break;
		case 0xae5: m_b = op_xor(m_b, m_b); break;
		case 0xae6: m_b = op_xor(m_b, read_mx()); break;
		case 0xae7: m_b = op_xor(m_b, read_my()); break;
		case 0xae8: write_mx(op_xor(read_mx(), m_a)); break;
		case 0xae9: write_mx(op_xor(read_mx(), m_b)); break;
		case 0xaea: write_mx(op_xor(read_mx(), read_mx())); break;
		case 0xaeb: write_mx(op_xor(read_mx(), read_my())); break;
		case 0xaec: write_my(op_xor(read_my(), m_a)); break;
		case 0xaed: write_my(op_xor(read_my(), m_b)); break;
		case 0xaee: write_my(op_xor(read_my(), read_mx())); break;
		case 0xaef: write_my(op_xor(read_my(), read_my())); break;

		// RLC r(,r): rotate register left through carry (flags: C, Z)
		case 0xaf0: m_a = op_rlc(m_a); break;
		case 0xaf5: m_b = op_rlc(m_b); break;
		case 0xafa: read_mx(); write_mx(op_rlc(read_mx())); break;
		case 0xaff: read_my(); write_my(op_rlc(read_my())); break;

		// RRC r: rotate register right through carry (flags: C, Z)
		case 0xe8c: m_a = op_rrc(m_a); break;
		case 0xe8d: m_b = op_rrc(m_b); break;
		case 0xe8e: write_mx(op_rrc(read_mx())); break;
		case 0xe8f: write_my(op_rrc(read_my())); break;

		// INC SP: increment stackpointer
		case 0xfdb:
			m_sp++;
			break;

		// DEC SP: decrement stackpointer
		case 0xfcb:
			m_sp--;
			break;

		// PUSH r/Xphl/Yphl/F: push register to stack
		case 0xfc0: push(m_a); break;
		case 0xfc1: push(m_b); break;
		case 0xfc2: push(read_mx()); break;
		case 0xfc3: push(read_my()); break;
		case 0xfc4: push(m_xp); break;
		case 0xfc5: push(m_xh); break;
		case 0xfc6: push(m_xl); break;
		case 0xfc7: push(m_yp); break;
		case 0xfc8: push(m_yh); break;
		case 0xfc9: push(m_yl); break;
		case 0xfca: push(m_f); break;

		// POP r/Xphl/Yphl/F: pop value from stack
		case 0xfd0: m_a = pop(); break;
		case 0xfd1: m_b = pop(); break;
		case 0xfd2: write_mx(pop()); break;
		case 0xfd3: write_my(pop()); break;
		case 0xfd4: m_xp = pop(); break;
		case 0xfd5: m_xh = pop(); break;
		case 0xfd6: m_xl = pop(); break;
		case 0xfd7: m_yp = pop(); break;
		case 0xfd8: m_yh = pop(); break;
		case 0xfd9: m_yl = pop(); break;
		case 0xfda: m_f = pop(); m_possible_irq = true; break;

		// RETS: return from subroutine, then skip next instruction
		case 0xfde:
			m_icount -= 7;
			pop_pc();
			m_pc = (m_pc & 0x1000) | ((m_pc + 1) & 0x0fff);
			break;

		// RET: return from subroutine
		case 0xfdf:
			m_icount -= 2;
			pop_pc();
			break;

		// JPBA: jump indirect using registers A and B
		case 0xfe8:
			m_pc = m_jpc | m_b << 4 | m_a;
			break;

		// HALT: halt (stop cpu core clock)
		case 0xff8:
			m_halt = true;
			break;

		// SLP: sleep (stop source oscillation)
		case 0xff9:
			m_sleep = true;
			break;

		// NOP5: no operation (5 clock cycles)
		case 0xffb:
			break;

		// NOP7: no operation (7 clock cycles)
		case 0xfff:
			m_icount -= 2;
			break;


		default:
			switch (m_op & 0xff0)
			{
		// LD r,i: load register with 4-bit immediate data
		case 0xe00: m_a = m_op & 0xf; break;
		case 0xe10: m_b = m_op & 0xf; break;
		case 0xe20: write_mx(m_op & 0xf); break;
		case 0xe30: write_my(m_op & 0xf); break;

		// LDPX MX,i: LD MX,i, then increment X
		case 0xe60:
			write_mx(m_op & 0xf); inc_x();
			break;

		// LDPY MY,i: LD MY,i, then increment Y
		case 0xe70:
			write_my(m_op & 0xf); inc_y();
			break;

		// LD A,Mn: load A with memory
		case 0xfa0:
			m_a = read_mn();
			break;

		// LD B,Mn: load B with memory
		case 0xfb0:
			m_b = read_mn();
			break;

		// LD Mn,A: load memory with A
		case 0xf80:
			write_mn(m_a);
			break;

		// LD Mn,B: load memory with B
		case 0xf90:
			write_mn(m_b);
			break;

		// INC Mn: increment memory (flags: C, Z)
		case 0xf60:
			write_mn(op_inc(read_mn()));
			break;

		// DEC Mn: decrement memory (flags: C, Z)
		case 0xf70:
			write_mn(op_dec(read_mn()));
			break;

		// ADD r,i: add 4-bit immediate data to register (flags: C, Z)
		case 0xc00: m_a = op_add(m_a, m_op & 0xf, D_FLAG); break;
		case 0xc10: m_b = op_add(m_b, m_op & 0xf, D_FLAG); break;
		case 0xc20: write_mx(op_add(read_mx(), m_op & 0xf, D_FLAG)); break;
		case 0xc30: write_my(op_add(read_my(), m_op & 0xf, D_FLAG)); break;

		// ADC r,i: add with carry 4-bit immediate data to register (flags: C, Z)
		case 0xc40: m_a = op_adc(m_a, m_op & 0xf, D_FLAG); break;
		case 0xc50: m_b = op_adc(m_b, m_op & 0xf, D_FLAG); break;
		case 0xc60: write_mx(op_adc(read_mx(), m_op & 0xf, D_FLAG)); break;
		case 0xc70: write_my(op_adc(read_my(), m_op & 0xf, D_FLAG)); break;

		// ADC Xhl/Yhl,i: add with carry 4-bit immediate data to X/Y (flags: C, Z, no D flag)
		case 0xa00: m_xh = op_adc(m_xh, m_op & 0xf); break;
		case 0xa10: m_xl = op_adc(m_xl, m_op & 0xf); break;
		case 0xa20: m_yh = op_adc(m_yh, m_op & 0xf); break;
		case 0xa30: m_yl = op_adc(m_yl, m_op & 0xf); break;

		// SBC r,i: subtract with carry 4-bit immediate data from register (flags: C, Z)
		case 0xd40: m_a = op_sbc(m_a, m_op & 0xf, D_FLAG); break;
		case 0xd50: m_b = op_sbc(m_b, m_op & 0xf, D_FLAG); break;
		case 0xd60: write_mx(op_sbc(read_mx(), m_op & 0xf, D_FLAG)); break;
		case 0xd70: write_my(op_sbc(read_my(), m_op & 0xf, D_FLAG)); break;

		// CP r,i: compare: SUB r,i, but discard result (flags: C, Z, no D flag)
		case 0xdc0: op_sub(m_a, m_op & 0xf); break;
		case 0xdd0: op_sub(m_b, m_op & 0xf); break;
		case 0xde0: op_sub(read_mx(), m_op & 0xf); break;
		case 0xdf0: op_sub(read_my(), m_op & 0xf); break;

		// CP XH,i: compare: SUB Xhl/Yhl,i, but discard result (flags: C, Z, no D flag)
		case 0xa40: op_sub(m_xh, m_op & 0xf); break;
		case 0xa50: op_sub(m_xl, m_op & 0xf); break;
		case 0xa60: op_sub(m_yh, m_op & 0xf); break;
		case 0xa70: op_sub(m_yl, m_op & 0xf); break;

		// AND r,i: logical AND register with 4-bit immediate data (flags: Z)
		case 0xc80: m_a = op_and(m_a, m_op & 0xf); break;
		case 0xc90: m_b = op_and(m_b, m_op & 0xf); break;
		case 0xca0: write_mx(op_and(read_mx(), m_op & 0xf)); break;
		case 0xcb0: write_my(op_and(read_my(), m_op & 0xf)); break;

		// FAN r,i: flag-check: AND r,i, but discard result (flags: Z)
		case 0xd80: op_and(m_a, m_op & 0xf); break;
		case 0xd90: op_and(m_b, m_op & 0xf); break;
		case 0xda0: op_and(read_mx(), m_op & 0xf); break;
		case 0xdb0: op_and(read_my(), m_op & 0xf); break;

		// OR r,i: logical OR register with 4-bit immediate data (flags: Z)
		case 0xcc0: m_a = op_or(m_a, m_op & 0xf); break;
		case 0xcd0: m_b = op_or(m_b, m_op & 0xf); break;
		case 0xce0: write_mx(op_or(read_mx(), m_op & 0xf)); break;
		case 0xcf0: write_my(op_or(read_my(), m_op & 0xf)); break;

		// XOR r,i: exclusive-OR register with 4-bit immediate data (flags: Z)
		case 0xd00: m_a = op_xor(m_a, m_op & 0xf); break;
		case 0xd10: m_b = op_xor(m_b, m_op & 0xf); break;
		case 0xd20: write_mx(op_xor(read_mx(), m_op & 0xf)); break;
		case 0xd30: write_my(op_xor(read_my(), m_op & 0xf)); break;

		// SET F,i: set flag(s), this includes opcodes SCF, SZF, SDF, EI
		case 0xf40:
			m_icount -= 2;
			m_f |= (m_op & 0xf);
			m_possible_irq = true;
			break;

		// RST F,i: reset flag(s), this includes opcodes RCF, RZF, RDF, DI
		case 0xf50:
			m_icount -= 2;
			m_f &= (m_op & 0xf);
			break;

		// PSET p: page set, used to set page/bank before a jump instruction
		case 0xe40: case 0xe50:
			m_npc = m_op << 8 & 0x1f00;
			break;


		// illegal opcode
		default:
			/* illegal opcode: ignored */
			break;

			} // 0xff0
			break;

			} // 0xfff
			break;

	} // 0xf00 (big switch)
}

/************************************************************************
 Construction / reset
************************************************************************/

E0C6S46::E0C6S46(int variant) : m_variant(variant)
{
    m_romWords = variant ? 0x2000 : 0x1800;
    m_ramSize = variant ? 0x300 : 0x280;
    m_vramSize = variant ? 0x66 : 0x50;
    reset();
}

void E0C6S46::reset()
{
    // power-on: everything cleared (MAME device_start zerofill), then device_reset
    memset(m_ram, 0, sizeof(m_ram));
    memset(m_vram, 0, sizeof(m_vram));
    m_op = m_prev_op = 0; m_irq_vector = 0; m_possible_irq = false; m_halt = m_sleep = false;
    m_pc = m_prev_pc = m_npc = m_jpc = 0;
    m_a = m_b = m_xp = m_xh = m_xl = m_yp = m_yh = m_yl = m_sp = m_f = 0;

    memset(m_port_r, 0, sizeof(m_port_r)); m_r_dir = 0;
    memset(m_port_p, 0, sizeof(m_port_p)); m_p_dir = 0; m_p_pullup = 0;
    memset(m_port_k, 0xf, sizeof(m_port_k)); m_dfk0 = 0xf;
    memset(m_irqflag, 0, sizeof(m_irqflag)); memset(m_irqmask, 0, sizeof(m_irqmask));
    m_osc = m_svd = m_lcd_control = m_lcd_contrast = 0;
    m_256_src_pulse = 0; m_watchdog_count = 0; m_clktimer_count = 0;
    m_stopwatch_on = 0; m_swl_cur_pulse = 0; m_swl_slice = 0; m_swl_count = 0; m_swh_count = 0;
    m_prgtimer_select = 0; m_prgtimer_on = 0; m_prgtimer_src_pulse = 0; m_prgtimer_cur_pulse = 0;
    m_prgtimer_count = 0; m_prgtimer_reload = 0;
    m_bz_43_on = 0; m_bz_freq = 0; m_bz_envelope = 0; m_bz_envelope_count = 0; m_bz_duty_ratio = 0;
    m_bz_1shot_on = 0; m_bz_1shot_running = false; m_bz_1shot_count = 0; m_bz_pulse = 0; m_r43 = 0;

    m_now = 0;
    m_t256 = 64 * UNITS_PER_TICK;
    m_tprg = NEVER;
    m_tbz = NEVER;
    device_reset();
}

void E0C6S46::cpu_reset()
{
    m_halt = m_sleep = false;
    m_op = 0xfff; // nop
    m_pc = 0x100;
    m_f &= 3;
}

// MAME e0c6s46_device::device_reset (also used by the watchdog): RAM is kept
void E0C6S46::device_reset()
{
    cpu_reset();
    memset(m_irqflag, 0, sizeof(m_irqflag));
    memset(m_irqmask, 0, sizeof(m_irqmask));
    io_w(0x41, 0xf);
    io_w(0x54, 0xf);
    io_w(0x70, 0x0);
    io_w(0x71, 0x8);
    io_w(0x73, m_svd & 3);
    io_w(0x74, 0x0);
    io_w(0x75, 0x4);
    io_w(0x76, 0x3);
    io_w(0x77, 0x2);
    io_w(0x78, 0x2);
    io_w(0x79, 0x0);
    io_w(0x7a, 0x0);
    io_w(0x7b, 0x0);
    io_w(0x7d, 0x0);
    io_w(0x7e, 0x0);
    for (int i = 0; i < 5; i++) write_r(i, m_port_r[i]);
}

/************************************************************************
 Data memory
************************************************************************/

uint8_t E0C6S46::data_r(uint16_t a)
{
    a &= 0xfff;
    if (a < m_ramSize) return m_ram[a];
    if (a >= 0xe00 && a < 0xe00 + m_vramSize) return m_vram[0][a - 0xe00];
    if (a >= 0xe80 && a < 0xe80 + m_vramSize) return m_vram[1][a - 0xe80];
    if (a >= 0xf00 && a < 0xf80) return io_r(a & 0x7f);
    return 0;
}

void E0C6S46::data_w(uint16_t a, uint8_t d)
{
    a &= 0xfff;
    d &= 0xf;
    if (a < m_ramSize) { m_ram[a] = d; return; }
    if (a >= 0xe00 && a < 0xe00 + m_vramSize) { m_vram[0][a - 0xe00] = d; return; }
    if (a >= 0xe80 && a < 0xe80 + m_vramSize) { m_vram[1][a - 0xe80] = d; return; }
    if (a >= 0xf00 && a < 0xf80) io_w(a & 0x7f, d);
}

/************************************************************************
 Interrupts / input
************************************************************************/

void E0C6S46::do_interrupt()
{
    // interrupt handling takes 13 cycles, plus 1 extra if cpu was halted
    m_icount -= 13;
    if (m_halt) m_icount--;
    m_halt = m_sleep = false;
    push_pc();
    m_f &= ~I_FLAG;
    m_pc = (m_pc & 0x1000) | 0x100 | m_irq_vector;
}

bool E0C6S46::check_interrupt()
{
    static const int priorder[6] = { IRQREG_CLKTIMER, IRQREG_STOPWATCH, IRQREG_INPUT0, IRQREG_INPUT1, IRQREG_SERIAL, IRQREG_PRGTIMER };
    for (int pri = 5; pri >= 0; pri--) {
        m_irq_vector = 2 * pri + 2;
        int reg = priorder[pri];
        switch (reg) {
        case IRQREG_INPUT0: case IRQREG_INPUT1:
            if (m_irqflag[reg]) return true;
            break;
        default:
            if (m_irqflag[reg] & m_irqmask[reg]) return true;
            break;
        }
    }
    return false;
}

void E0C6S46::setInputLine(int line, int state)
{
    if (line < 0 || line > 7) return;
    state = state ? 1 : 0;
    uint8_t port = line >> 2 & 1;
    uint8_t bit = 1 << (line & 3);
    uint8_t prev = m_port_k[port];
    m_port_k[port] = (m_port_k[port] & ~bit) | (state ? bit : 0);

    uint8_t dfk = (port == 0) ? m_dfk0 : 0xf;
    uint8_t edge = ~(prev ^ dfk) & (m_port_k[port] ^ dfk) & bit;
    if (m_irqmask[IRQREG_INPUT0 + port] & edge) {
        m_irqflag[IRQREG_INPUT0 + port] |= 1;
        m_possible_irq = true;
    }
    if ((prev & ~m_port_k[port] & bit) && m_prgtimer_on && (m_prgtimer_select & 7) < 2)
        clock_prgtimer();
}

/************************************************************************
 Ports
************************************************************************/

void E0C6S46::write_r(uint8_t port, uint8_t data)
{
    data &= 0xf;
    m_port_r[port] = data;
    if (port == 4) {
        // R43: buzzer on: direct output or 1-shot output
        if ((data & 8) != m_bz_43_on) {
            m_bz_43_on = data & 8;
            reset_bz_envelope();
            reset_buzzer();
        }
        write_r4_out();
    }
}

void E0C6S46::write_r4_out()
{
    // R43: BZ(buzzer) - the speaker follows bit 3
    int level = m_bz_pulse ? 1 : 0;
    if (level != m_r43) {
        m_r43 = level;
        if (onBuzzer) onBuzzer(buzzerCtx, m_now, level);
    }
}

/************************************************************************
 Timers
************************************************************************/

void E0C6S46::core_256_cb()
{
    m_256_src_pulse ^= 1;
    m_t256 = m_now + 64 * UNITS_PER_TICK;

    m_swl_cur_pulse = m_256_src_pulse | (m_stopwatch_on ^ 1);
    if (m_swl_cur_pulse == 0) clock_stopwatch();
    if (m_bz_1shot_on != 0 && m_256_src_pulse == 1) clock_bz_1shot();
    if (m_bz_envelope & 1 && m_256_src_pulse == 1) clock_bz_envelope();
    if (m_256_src_pulse == 0) clock_clktimer();
}

void E0C6S46::clock_watchdog()
{
    // initial reset after 3 to 4 seconds
    if (++m_watchdog_count == 4) {
        m_watchdog_count = 0;
        device_reset();
    }
}

void E0C6S46::clock_clktimer()
{
    m_clktimer_count++;
    uint8_t flag = 0;
    if ((m_clktimer_count & 0x07) == 0) flag |= 1;
    if ((m_clktimer_count & 0x1f) == 0) flag |= 2;
    if ((m_clktimer_count & 0x7f) == 0) flag |= 4;
    if (m_clktimer_count == 0) flag |= 8;
    m_irqflag[IRQREG_CLKTIMER] |= flag;
    if (m_irqflag[IRQREG_CLKTIMER] & m_irqmask[IRQREG_CLKTIMER]) m_possible_irq = true;
    if (m_clktimer_count == 0) clock_watchdog();
}

void E0C6S46::clock_stopwatch()
{
    m_swl_slice++;
    int swl_next = 3 - (m_swl_count & 1);
    if (m_swl_count == 1 && !(m_swh_count >> 1 & 1)) swl_next = 3;
    if (m_swl_slice == swl_next) {
        m_swl_slice = 0;
        m_swl_count = (m_swl_count + 1) % 10;
        if (m_swl_count == 0) {
            m_irqflag[IRQREG_STOPWATCH] |= 1;
            m_swh_count = (m_swh_count + 1) % 10;
            if (m_swh_count == 0) m_irqflag[IRQREG_STOPWATCH] |= 2;
            if (m_irqflag[IRQREG_STOPWATCH] & m_irqmask[IRQREG_STOPWATCH]) m_possible_irq = true;
        }
    }
}

void E0C6S46::clock_prgtimer()
{
    if (--m_prgtimer_count == 0) {
        m_irqflag[IRQREG_PRGTIMER] |= 1;
        if (m_irqflag[IRQREG_PRGTIMER] & m_irqmask[IRQREG_PRGTIMER]) m_possible_irq = true;
        m_prgtimer_count = m_prgtimer_reload;
    }
}

bool E0C6S46::prgtimer_reset_prescaler()
{
    uint8_t sel = m_prgtimer_select & 7;
    if (sel >= 2) m_tprg = m_now + (uint64_t)(2 << (sel ^ 7)) * UNITS_PER_TICK;
    return sel >= 2;
}

void E0C6S46::prgtimer_cb()
{
    m_tprg = NEVER;
    if (!prgtimer_reset_prescaler()) return;
    m_prgtimer_src_pulse ^= 1;
    m_prgtimer_cur_pulse = m_prgtimer_src_pulse | (m_prgtimer_on ^ 1);
    if (m_prgtimer_cur_pulse == 0) clock_prgtimer();
}

void E0C6S46::schedule_buzzer()
{
    if (m_bz_43_on && !m_bz_1shot_running) return;
    int mul = (m_bz_freq & 4) ? 1 : 2;
    int high = (m_bz_freq & 2) ? 12 : 8;
    int low = 16 + (m_bz_freq << 2 & 0xc);
    if (m_bz_envelope & 1) high -= m_bz_duty_ratio;
    low -= high;
    // ticks of (mul * osc1): one tick = 2/mul scheduler units
    uint64_t ticks = m_bz_pulse ? high : low;
    m_tbz = m_now + ticks * 2 / mul;
}

void E0C6S46::buzzer_cb()
{
    m_tbz = NEVER;
    if (m_bz_43_on && !m_bz_1shot_running) m_bz_pulse = 0;
    else m_bz_pulse ^= 1;
    write_r4_out();
    schedule_buzzer();
}

void E0C6S46::reset_buzzer()
{
    if (m_tbz == NEVER) schedule_buzzer();
}

void E0C6S46::clock_bz_1shot()
{
    m_bz_1shot_running = true;
    if (m_bz_1shot_count == 0) {
        m_bz_1shot_count = (m_bz_freq & 8) ? 16 : 8;
        reset_buzzer();
    } else if (--m_bz_1shot_count == 0) {
        m_bz_1shot_on = 0;
        m_bz_1shot_running = false;
    }
}

void E0C6S46::clock_bz_envelope()
{
    if (--m_bz_envelope_count == 0) {
        m_bz_envelope_count = (m_bz_envelope & 2) ? 32 : 16;
        if (m_bz_duty_ratio < 7) m_bz_duty_ratio++;
    }
}

void E0C6S46::reset_bz_envelope()
{
    m_bz_envelope_count = (m_bz_envelope & 2) ? 32 : 16;
    m_bz_duty_ratio = 0;
}

/************************************************************************
 Internal I/O
************************************************************************/

uint8_t E0C6S46::io_r(uint8_t offset)
{
    switch (offset) {
    case 0x00: case 0x01: case 0x02: case 0x03: case 0x04: case 0x05: {
        uint8_t flag = m_irqflag[offset];   // acked when read
        m_irqflag[offset] = 0;
        return flag;
    }
    case 0x10: case 0x11: case 0x12: case 0x13: case 0x14: case 0x15: return m_irqmask[offset & 7];
    case 0x40: case 0x42: return m_port_k[offset >> 1 & 1];
    case 0x41: return m_dfk0;
    case 0x50: case 0x51: case 0x52: case 0x53: case 0x54: return m_port_r[offset & 7];
    case 0x7b: return m_r_dir;
    case 0x60: case 0x61: case 0x62: case 0x63: return BIT(m_p_dir, offset & 3) ? m_port_p[offset & 3] : 0;
    case 0x7d: return m_p_dir;
    case 0x7e: return m_p_pullup;
    case 0x20: case 0x21: return m_clktimer_count >> (4 * (offset & 1)) & 0xf;
    case 0x22: return m_swl_count;
    case 0x23: return m_swh_count;
    case 0x77: return m_stopwatch_on;
    case 0x24: case 0x25: return m_prgtimer_count >> (4 * (offset & 1)) & 0xf;
    case 0x26: case 0x27: return m_prgtimer_reload >> (4 * (offset & 1)) & 0xf;
    case 0x78: return m_prgtimer_on;
    case 0x79: return m_prgtimer_select;
    case 0x74: return m_bz_freq;
    case 0x75: return m_bz_1shot_on | m_bz_envelope;
    case 0x70: return m_osc;
    case 0x71: return m_lcd_control;
    case 0x72: return m_lcd_contrast;
    case 0x73: return m_svd | ((m_svd & 4 && m_svd != 7) ? 0 : 8);
    }
    return 0;
}

void E0C6S46::io_w(uint8_t offset, uint8_t data)
{
    switch (offset) {
    case 0x10: case 0x11: case 0x12: case 0x13: case 0x14: case 0x15: {
        static const uint8_t maskmask[6] = { 0xf, 3, 1, 1, 0xf, 0xf };
        uint8_t prev = m_irqmask[offset & 7];
        m_irqmask[offset & 7] = data & maskmask[offset & 7];
        uint8_t rise = ~prev & m_irqmask[offset & 7];
        if (rise) {
            m_possible_irq = true;
            if (offset >= 0x14) {
                uint8_t port = offset & 1;
                uint8_t dfk = (port == 0) ? m_dfk0 : 0xf;
                if (rise & (m_port_k[port] ^ dfk)) m_irqflag[offset & 7] |= 1;
            }
        }
        break;
    }
    case 0x41: m_dfk0 = data; break;
    case 0x50: case 0x51: case 0x52: case 0x53: case 0x54: write_r(offset & 7, data); break;
    case 0x7b:
        if (data != m_r_dir) { m_r_dir = data; for (int i = 0; i < 5; i++) write_r(i, m_port_r[i]); }
        break;
    case 0x60: case 0x61: case 0x62: case 0x63: m_port_p[offset & 3] = data & 0xf; break;
    case 0x7d: m_p_dir = data; break;
    case 0x7e: m_p_pullup = data; break;
    case 0x70: m_osc = data; break;      // OSC3 not used by these toys
    case 0x71: m_lcd_control = data; break;
    case 0x72: m_lcd_contrast = data; break;
    case 0x73: m_svd = data & 7; break;
    case 0x76:
        if (data & 1) m_watchdog_count = 0;
        if (data & 2) m_clktimer_count = 0;
        break;
    case 0x77:
        m_stopwatch_on = data & 1;
        if (data & 2) { m_swh_count = 0; m_swl_count = 0; m_swl_slice = 0; }
        if (m_stopwatch_on && m_swl_cur_pulse && !m_256_src_pulse) { m_swl_cur_pulse = 0; clock_stopwatch(); }
        break;
    case 0x26: m_prgtimer_reload = (m_prgtimer_reload & 0xf0) | data; break;
    case 0x27: m_prgtimer_reload = (m_prgtimer_reload & 0x0f) | data << 4; break;
    case 0x78:
        m_prgtimer_on = data & 1;
        if (data & 2) m_prgtimer_count = m_prgtimer_reload;
        if (m_prgtimer_on && (m_prgtimer_select & 7) >= 2 && m_prgtimer_cur_pulse && !m_prgtimer_src_pulse) {
            m_prgtimer_cur_pulse = 0;
            clock_prgtimer();
        }
        break;
    case 0x79:
        if ((data & 7) != (m_prgtimer_select & 7)) {
            m_prgtimer_src_pulse = 0;
            m_prgtimer_cur_pulse = m_prgtimer_on ^ 1;
            m_prgtimer_select = data;
            prgtimer_reset_prescaler();
        }
        m_prgtimer_select = data;
        break;
    case 0x74: m_bz_freq = data; break;
    case 0x75:
        if ((~m_bz_envelope & data & 1) || (data & 4)) reset_bz_envelope();
        m_bz_envelope = data & 3;
        m_bz_1shot_on |= data & 8;
        break;
    default: break;
    }
}

/************************************************************************
 Scheduler
************************************************************************/

uint64_t E0C6S46::nextEvent() const
{
    return std::min(m_t256, std::min(m_tprg, m_tbz));
}

void E0C6S46::runEvents(uint64_t upTo)
{
    for (;;) {
        uint64_t t = nextEvent();
        if (t > upTo) break;
        uint64_t save = m_now;
        m_now = t;
        if (t == m_t256) core_256_cb();
        else if (t == m_tprg) prgtimer_cb();
        else buzzer_cb();
        m_now = std::max(save, t);
    }
}

void E0C6S46::run(uint64_t units)
{
    const uint64_t target = m_now + units;
    while (m_now < target) {
        // interrupts (not right after EI/PSET, as MAME)
        if (m_possible_irq && (m_op & 0xfe0) != 0xe40 && (m_op & 0xff8) != 0xf48) {
            m_possible_irq = false;
            if ((m_f & I_FLAG) && check_interrupt()) {
                m_icount = 0;
                do_interrupt();
                m_now += (uint64_t)(-m_icount) * UNITS_PER_TICK;
                runEvents(m_now);
                continue;
            }
        }

        if (m_halt || m_sleep) {
            // nothing runs until the next timer event: jump straight to it
            uint64_t t = std::min(nextEvent(), target);
            if (t > m_now) m_now = t;
            runEvents(m_now);
            if (m_now >= target) break;
            continue;
        }

        m_prev_op = m_op;
        m_prev_pc = m_pc;
        m_jpc = ((m_prev_op & 0xfe0) == 0xe40) ? m_npc : (m_prev_pc & 0x1f00);
        m_op = (m_pc < m_rom.size() ? m_rom[m_pc] : 0) & 0xfff;
        m_pc = (m_pc & 0x1000) | ((m_pc + 1) & 0x0fff);
        m_icount = -5;
        if (!(m_variant == 0 && m_op == 0xff9))   // E0C6S46 has no SLP
            execute_one();
        m_now += (uint64_t)(-m_icount) * UNITS_PER_TICK;
        if (m_now >= m_t256 || m_now >= m_tprg || m_now >= m_tbz)
            runEvents(m_now);
    }
}

/************************************************************************
 LCD
************************************************************************/

int E0C6S46::lcdPixel(int com, int seg) const
{
    int bank = com >> 3;
    if (m_lcd_control & 8 || (bank == 1 && m_lcd_control & 2)) return 0;
    if (m_lcd_control & 4) return 1;
    int offset = seg * 2 + ((com & 7) >= 4 ? 1 : 0);
    if (offset >= m_vramSize) return 0;
    return m_vram[bank][offset] >> (com & 3) & 1;
}

/************************************************************************
 Save state
************************************************************************/

#define E0C_FIELDS(X) X(m_ram) X(m_vram) X(m_op) X(m_prev_op) X(m_irq_vector) X(m_possible_irq) X(m_halt) X(m_sleep) \
    X(m_pc) X(m_prev_pc) X(m_npc) X(m_jpc) X(m_a) X(m_b) X(m_xp) X(m_xh) X(m_xl) X(m_yp) X(m_yh) X(m_yl) X(m_sp) X(m_f) \
    X(m_irqflag) X(m_irqmask) X(m_osc) X(m_svd) X(m_lcd_control) X(m_lcd_contrast) X(m_port_r) X(m_r_dir) X(m_port_p) \
    X(m_p_dir) X(m_p_pullup) X(m_port_k) X(m_dfk0) X(m_256_src_pulse) X(m_watchdog_count) X(m_clktimer_count) \
    X(m_stopwatch_on) X(m_swl_cur_pulse) X(m_swl_slice) X(m_swl_count) X(m_swh_count) X(m_prgtimer_select) X(m_prgtimer_on) \
    X(m_prgtimer_src_pulse) X(m_prgtimer_cur_pulse) X(m_prgtimer_count) X(m_prgtimer_reload) X(m_bz_43_on) X(m_bz_freq) \
    X(m_bz_envelope) X(m_bz_envelope_count) X(m_bz_duty_ratio) X(m_bz_1shot_on) X(m_bz_1shot_running) X(m_bz_1shot_count) \
    X(m_bz_pulse) X(m_r43) X(m_now) X(m_t256) X(m_tprg) X(m_tbz)

void E0C6S46::save(std::vector<uint8_t>& out) const
{
#define X(f) { const uint8_t* p_ = (const uint8_t*)&f; out.insert(out.end(), p_, p_ + sizeof(f)); }
    E0C_FIELDS(X)
#undef X
}

bool E0C6S46::load(const uint8_t*& p, const uint8_t* end)
{
#define X(f) { if (p + sizeof(f) > end) return false; memcpy(&f, p, sizeof(f)); p += sizeof(f); }
    E0C_FIELDS(X)
#undef X
    return true;
}
