// license:BSD-3-Clause
// copyright-holders:Karl Stenerud
/* ======================================================================== */
/* =============================== COPYRIGHT ============================== */
/* ======================================================================== */
/*

G65C816 CPU Emulator V1.00

Copyright Karl Stenerud

*/
/* ======================================================================== */
/* ================================= NOTES ================================ */
/* ======================================================================== */
/*

Changes:
    1.02 (2019-02-02):
        R. Belmont
        - Corrected WDM to take 2 bytes and added callback

    1.01 (2010-04-04):
        Angelo Salese
        - Added boundary checks for MVP and MVN in M mode.

    1.00 (2008-11-27):
        R. Belmont
        - Reworked for modern MAME

    0.94 (2007-06-14):
            Zsolt Vasvari
            - Removed unnecessary checks from MVP and MVN

    0.93 (2003-07-05):
            Angelo Salese
            - Fixed the BCD conversion when using the Decimal Flag in ADC and SBC.
            - Removed the two conversion tables for ADC and SBC as they aren't
              needed anymore.

    0.92 (2000-05-28):
            Lee Hammerton <lee-hammerton@hammerhead.ltd.uk>
            - Fixed debugger bug that caused D to be misrepresented.
            - Fixed MVN and MVP (they were reversed)

    0.91 (2000-05-22):
            Lee Hammerton <lee-hammerton@hammerhead.ltd.uk>
            - Fixed reset vector fetch to be little endian
            - Fixed disassembler call bug
            - Fixed C flag in SBC (should be inverted before operation)
            - Fixed JSR to stack PC-1 and RTS to pull PC and add 1

            Karl Stenerud <karl@higashiyama-unet.ocn.ne.jp>
            - Added correct timing for absolute indexed operations
            - SBC: fixed corruption of interim values

    0.90 (2000-05-17):
            Karl Stenerud <karl@higashiyama-unet.ocn.ne.jp>
            - first public release


Note on timings:
    - For instructions that write to memory (ASL, ASR, LSL, ROL, ROR, DEC,
      INC, STA, STZ), the absolute indexed addressing mode takes 1 extra
      cycle to complete.
    - The spec says fc (JMP axi) is 6 cyles, but elsewhere says 8 cycles
      (which is what it should be)


TODO general:
    - WAI will not stop if RDY is held high.

    - RDY internally held low when WAI executed and returned to hi when RES,
      ABORT, NMI, or IRQ asserted.

    - ABORT will terminate WAI instruction but wil not restart the processor

    - If interrupt occurs after ABORT of WAI, processor returns to WAI
      instruction.

    - Add one cycle when indexing across page boundary and E=1 except for STA
      and STZ instructions.

    - Add 1 cycle if branch is taken. In Emulation (E= 1 ) mode only --add 1
      cycle if the branch is taken and crosses a page boundary.

    - Add 1 cycle in Emulation mode (E=1) for (dir),y; abs,x; and abs,y
      addressing modes.

    - Rename g65* to w65*? (including filenames)

    - Any difference between W65C8* and G65SC8*?

*/
/* ======================================================================== */
/* ================================= DATA ================================= */
/* ======================================================================== */

#include "g65816.h"

#include "g65816cm.h"


enum
{
	CPU_TYPE_W65C816 = 0,
	CPU_TYPE_W65C802 = 1,
	CPU_TYPE_5A22 = 2
};

void g65816_device::g65816i_set_execution_mode(unsigned mode)
{
	assert(mode < 5);
	switch (mode)
	{
		case 0: FTABLE_OPCODES = g65816i_opcodes_M0X0; break;
		case 1: FTABLE_OPCODES = g65816i_opcodes_M0X1; break;
		case 2: FTABLE_OPCODES = g65816i_opcodes_M1X0; break;
		case 3: FTABLE_OPCODES = g65816i_opcodes_M1X1; break;
		case 4: FTABLE_OPCODES = g65816i_opcodes_E; break;
	}
	FTABLE_GET_REG = s_g65816_get_reg[mode];
	FTABLE_SET_REG = s_g65816_set_reg[mode];
	FTABLE_SET_LINE = s_g65816_set_line[mode];
	FTABLE_EXECUTE = s_g65816_execute[mode];
}


const g65816_device::get_reg_func g65816_device::s_g65816_get_reg[5] =
{
	&g65816_device::g65816i_get_reg_M0X0,
	&g65816_device::g65816i_get_reg_M0X1,
	&g65816_device::g65816i_get_reg_M1X0,
	&g65816_device::g65816i_get_reg_M1X1,
	&g65816_device::g65816i_get_reg_E
};

const g65816_device::set_reg_func g65816_device::s_g65816_set_reg[5] =
{
	&g65816_device::g65816i_set_reg_M0X0,
	&g65816_device::g65816i_set_reg_M0X1,
	&g65816_device::g65816i_set_reg_M1X0,
	&g65816_device::g65816i_set_reg_M1X1,
	&g65816_device::g65816i_set_reg_E
};

const g65816_device::set_line_func g65816_device::s_g65816_set_line[5] =
{
	&g65816_device::g65816i_set_line_M0X0,
	&g65816_device::g65816i_set_line_M0X1,
	&g65816_device::g65816i_set_line_M1X0,
	&g65816_device::g65816i_set_line_M1X1,
	&g65816_device::g65816i_set_line_E
};

const g65816_device::execute_func g65816_device::s_g65816_execute[5] =
{
	&g65816_device::g65816i_execute_M0X0,
	&g65816_device::g65816i_execute_M0X1,
	&g65816_device::g65816i_execute_M1X0,
	&g65816_device::g65816i_execute_M1X1,
	&g65816_device::g65816i_execute_E
};


/* ======================================================================== */
/* ================================= MEMORY =============================== */
/* ======================================================================== */

#define ADDRESS_65816(A) ((A)&0x00ffffff)

unsigned g65816_device::g65816i_read_8_normal(unsigned address)
{
	address = ADDRESS_65816(address);
	CLOCKS -= (bus_5A22_cycle_burst(address));
	return g65816_read_8(address);
}

unsigned g65816_device::g65816i_read_8_immediate(unsigned address)
{
	address = ADDRESS_65816(address);
	CLOCKS -= (bus_5A22_cycle_burst(address));
	return g65816_read_8_immediate(address);
}

unsigned g65816_device::g65816i_read_8_opcode(unsigned address)
{
	address = ADDRESS_65816(address);
	CLOCKS -= (bus_5A22_cycle_burst(address));
	return g65816_read_8_opcode(address);
}

unsigned g65816_device::g65816i_read_8_direct(unsigned address)
{
	if (FLAG_E && !MAKE_UINT_8(REGISTER_D))
	{
		/* force address into zero page */
		address = REGISTER_D | MAKE_UINT_8(address);
		CLOCKS -= (bus_5A22_cycle_burst(address));
	}
	else
	{
		address = ADDRESS_65816(address);
		CLOCKS -= (bus_5A22_cycle_burst(address));
	}
	return g65816_read_8(address);
}

unsigned g65816_device::g65816i_read_8_vector(unsigned address)
{
	CLOCKS -= (bus_5A22_cycle_burst(address));
	return m_bus->vector(address & 0x001f);
}

void g65816_device::g65816i_write_8_normal(unsigned address, unsigned value)
{
	address = ADDRESS_65816(address);
	CLOCKS -= (bus_5A22_cycle_burst(address));
	g65816_write_8(address, MAKE_UINT_8(value));
}

void g65816_device::g65816i_write_8_direct(unsigned address, unsigned value)
{
	if (FLAG_E && !MAKE_UINT_8(REGISTER_D))
	{
		/* force address into zero page */
		address = REGISTER_D | MAKE_UINT_8(address);
		CLOCKS -= (bus_5A22_cycle_burst(address));
	}
	else
	{
		address = ADDRESS_65816(address);
		CLOCKS -= (bus_5A22_cycle_burst(address));
	}
	g65816_write_8(address, MAKE_UINT_8(value));
}

unsigned g65816_device::g65816i_read_16_normal(unsigned address)
{
	return   g65816i_read_8_normal(address) |
			(g65816i_read_8_normal(address + 1) << 8);
}

unsigned g65816_device::g65816i_read_16_immediate(unsigned address)
{
	return   g65816i_read_8_immediate(address) |
			(g65816i_read_8_immediate(address + 1) << 8);
}

unsigned g65816_device::g65816i_read_16_direct(unsigned address)
{
	return   g65816i_read_8_direct(address) |
			(g65816i_read_8_direct(address + 1) << 8);
}

unsigned g65816_device::g65816i_read_16_direct_x(unsigned address)
{
	if (FLAG_E && MAKE_UINT_8(REGISTER_D))
	{
		// The (direct,X) addressing mode has a bug in which the high byte is
		// wrapped within the page if E = 1 and D&0xFF != 0.
		uint8_t lo = g65816i_read_8_direct(address);
		uint8_t hi = g65816i_read_8_direct((address & 0xffff00) |
				MAKE_UINT_8(address + 1));
		return lo | (hi<<8);
	}
	else
	{
		return g65816i_read_16_direct(address);
	}
}

unsigned g65816_device::g65816i_read_16_vector(unsigned address)
{
	return   g65816i_read_8_vector(address) |
			(g65816i_read_8_vector(address + 1) << 8);
}

void g65816_device::g65816i_write_16_normal(unsigned address, unsigned value)
{
	g65816i_write_8_normal(address, value & 0xff);
	g65816i_write_8_normal(address + 1, value >> 8);
}

void g65816_device::g65816i_write_16_direct(unsigned address, unsigned value)
{
	g65816i_write_8_direct(address, value & 0xff);
	g65816i_write_8_direct(address + 1, value >> 8);
}

unsigned g65816_device::g65816i_read_24_normal(unsigned address)
{
	return   g65816i_read_8_normal(address)       |
			(g65816i_read_8_normal(address + 1) << 8) |
			(g65816i_read_8_normal(address + 2) << 16);
}

unsigned g65816_device::g65816i_read_24_immediate(unsigned address)
{
	return   g65816i_read_8_immediate(address)       |
			(g65816i_read_8_immediate(address + 1) << 8) |
			(g65816i_read_8_immediate(address + 2) << 16);
}


/* ======================================================================== */
/* ================================= STACK ================================ */
/* ======================================================================== */

void g65816_device::g65816i_push_8(unsigned value)
{
	g65816i_write_8_normal(REGISTER_S, value);
	if (FLAG_E)
	{
		REGISTER_S = MAKE_UINT_8(REGISTER_S - 1) | 0x100;
	}
	else
	{
		REGISTER_S = MAKE_UINT_16(REGISTER_S - 1);
	}
}

unsigned g65816_device::g65816i_pull_8()
{
	if (FLAG_E)
	{
		REGISTER_S = MAKE_UINT_8(REGISTER_S + 1) | 0x100;
	}
	else
	{
		REGISTER_S = MAKE_UINT_16(REGISTER_S + 1);
	}
	return g65816i_read_8_normal(REGISTER_S);
}

void g65816_device::g65816i_push_16(unsigned value)
{
	g65816i_push_8(value >> 8);
	g65816i_push_8(value & 0xff);
}

unsigned g65816_device::g65816i_pull_16()
{
	unsigned res = g65816i_pull_8();
	return res | (g65816i_pull_8() << 8);
}

void g65816_device::g65816i_push_24(unsigned value)
{
	g65816i_push_8(value >> 16);
	g65816i_push_8((value >> 8) & 0xff);
	g65816i_push_8(value & 0xff);
}

unsigned g65816_device::g65816i_pull_24()
{
	unsigned res = g65816i_pull_8();
	res |= g65816i_pull_8() << 8;
	return ((res + 1) & 0xffff) | (g65816i_pull_8() << 16);
}

void g65816_device::g65816i_push_8_native(unsigned value)
{
	g65816i_write_8_normal(REGISTER_S, value);
	REGISTER_S = MAKE_UINT_16(REGISTER_S - 1);
}

unsigned g65816_device::g65816i_pull_8_native()
{
	REGISTER_S = MAKE_UINT_16(REGISTER_S + 1);
	return g65816i_read_8_normal(REGISTER_S);
}

void g65816_device::g65816i_push_16_native(unsigned value)
{
	g65816i_push_8_native(value >> 8);
	g65816i_push_8_native(value & 0xff);
}

unsigned g65816_device::g65816i_pull_16_native()
{
	unsigned res = g65816i_pull_8_native();
	return res | (g65816i_pull_8_native() << 8);
}

void g65816_device::g65816i_push_24_native(unsigned value)
{
	g65816i_push_8_native(value >> 16);
	g65816i_push_8_native((value >> 8) & 0xff);
	g65816i_push_8_native(value & 0xff);
}

unsigned g65816_device::g65816i_pull_24_native()
{
	unsigned res = g65816i_pull_8_native();
	res |= g65816i_pull_8_native() << 8;
	return ((res + 1) & 0xffff) | (g65816i_pull_8_native() << 16);
}

void g65816_device::g65816i_update_reg_s()
{
	if (FLAG_E)
	{
		REGISTER_S = MAKE_UINT_8(REGISTER_S) | 0x100;
	}
}

/* ======================================================================== */
/* ============================ PROGRAM COUNTER =========================== */
/* ======================================================================== */

void g65816_device::g65816i_jump_16(unsigned address)
{
	REGISTER_PC = MAKE_UINT_16(address);
	g65816i_jumping(REGISTER_PC);
}

void g65816_device::g65816i_jump_24(unsigned address)
{
	REGISTER_PB = address & 0xff0000;
	REGISTER_PC = MAKE_UINT_16(address);
	g65816i_jumping(REGISTER_PC);
}

void g65816_device::g65816i_branch_8(unsigned offset)
{
	if (FLAG_E)
	{
		unsigned old_pc = REGISTER_PC;
		REGISTER_PC = MAKE_UINT_16(REGISTER_PC + MAKE_INT_8(offset));
		if ((REGISTER_PC ^ old_pc) & 0xff00)
			CLK(1);
	}
	else
	{
		REGISTER_PC = MAKE_UINT_16(REGISTER_PC + MAKE_INT_8(offset));
	}
	g65816i_branching(REGISTER_PC);
}

void g65816_device::g65816i_branch_16(unsigned offset)
{
	REGISTER_PC = MAKE_UINT_16(REGISTER_PC + offset);
	g65816i_branching(REGISTER_PC);
}


/* ======================================================================== */
/* ============================ STATUS REGISTER =========================== */
/* ======================================================================== */

void g65816_device::g65816i_set_flag_mx(unsigned value)
{
	if (FLAG_M)
	{
		if (!(value & FLAGPOS_M))
		{
			REGISTER_A |= REGISTER_B;
			REGISTER_B = 0;
			FLAG_M = MFLAG_CLEAR;
		}
	}
	else
	{
		if (value & FLAGPOS_M)
		{
			REGISTER_B = REGISTER_A & 0xff00;
			REGISTER_A = MAKE_UINT_8(REGISTER_A);
			FLAG_M = MFLAG_SET;
		}
	}
	if (FLAG_X)
	{
		if (!(value & FLAGPOS_X))
		{
			FLAG_X = XFLAG_CLEAR;
		}
	}
	else
	{
		if (value & FLAGPOS_X)
		{
			REGISTER_X = MAKE_UINT_8(REGISTER_X);
			REGISTER_Y = MAKE_UINT_8(REGISTER_Y);
			FLAG_X = XFLAG_SET;
		}
	}
	g65816i_set_execution_mode((FLAG_M >> 4) | (FLAG_X >> 4));
}

void g65816_device::g65816i_set_flag_e(unsigned value)
{
	if (FLAG_E)
	{
		if (!value)
		{
			FLAG_E = EFLAG_CLEAR;
			g65816i_set_execution_mode(EXECUTION_MODE_M1X1);
		}
	}
	else
	{
		if(value)
		{
			if (!FLAG_M)
			{
				REGISTER_B = REGISTER_A & 0xff00;
				REGISTER_A &= 0x00ff;
				FLAG_M = MFLAG_SET;
			}
			if (!FLAG_X)
			{
				REGISTER_X = MAKE_UINT_8(REGISTER_X);
				REGISTER_Y = MAKE_UINT_8(REGISTER_Y);
				FLAG_X = XFLAG_SET;
			}
			REGISTER_S = MAKE_UINT_8(REGISTER_S) | 0x100;
			FLAG_E = EFLAG_SET;
			g65816i_set_execution_mode(EXECUTION_MODE_E);
		}
	}
}

void g65816_device::g65816i_set_flag_i(unsigned value)
{
	value &= FLAGPOS_I;
	if(!FLAG_I || value)
	{
		FLAG_I = value;
		return;
	}
	FLAG_I = value;
}

/* Get the Processor Status Register */
unsigned g65816_device::g65816i_get_reg_p()
{
	return  (FLAG_N & 0x80)     |
			((FLAG_V>>1)&0x40)  |
			FLAG_M              |
			FLAG_X              |
			FLAG_D              |
			FLAG_I              |
			((!FLAG_Z)<<1)      |
			((FLAG_C>>8)&1);
}

void g65816_device::g65816i_set_reg_p(unsigned value)
{
	if (FLAG_E)
	{
		FLAG_N = value;
		FLAG_V = value << 1;
		FLAG_D = value & FLAGPOS_D;
		FLAG_Z = !(value & FLAGPOS_Z);
		FLAG_C = value << 8;
		g65816i_set_flag_i(value);
	}
	else
	{
		FLAG_N = value;
		FLAG_V = value << 1;
		FLAG_D = value & FLAGPOS_D;
		FLAG_Z = !(value & FLAGPOS_Z);
		FLAG_C = value << 8;
		g65816i_set_flag_mx(value);
		g65816i_set_flag_i(value);
	}
}


/* ======================================================================== */
/* =============================== INTERRUPTS ============================= */
/* ======================================================================== */

void g65816_device::g65816i_interrupt_hardware(unsigned vector)
{
	if (FLAG_E)
	{
		CLK(7);
		g65816i_push_16(REGISTER_PC);
		g65816i_push_8(g65816i_get_reg_p() & ~FLAGPOS_B);
		FLAG_D = DFLAG_CLEAR;
		g65816i_set_flag_i(IFLAG_SET);
		REGISTER_PB = 0;
		g65816i_jump_16(g65816i_read_16_vector(vector));
	}
	else
	{
		CLK(8);
		g65816i_push_8(REGISTER_PB>>16);
		g65816i_push_16(REGISTER_PC);
		g65816i_push_8(g65816i_get_reg_p());
		FLAG_D = DFLAG_CLEAR;
		g65816i_set_flag_i(IFLAG_SET);
		REGISTER_PB = 0;
		g65816i_jump_16(g65816i_read_16_vector(vector));
	}
}

void g65816_device::g65816i_interrupt_software(unsigned vector)
{
	if (FLAG_E)
	{
		CLK(7);
		g65816i_push_16(REGISTER_PC);
		g65816i_push_8(g65816i_get_reg_p());
		FLAG_D = DFLAG_CLEAR;
		g65816i_set_flag_i(IFLAG_SET);
		REGISTER_PB = 0;
		g65816i_jump_16(g65816i_read_16_immediate(vector));
	}
	else
	{
		CLK(8);
		g65816i_push_8(REGISTER_PB >> 16);
		g65816i_push_16(REGISTER_PC);
		g65816i_push_8(g65816i_get_reg_p());
		FLAG_D = DFLAG_CLEAR;
		g65816i_set_flag_i(IFLAG_SET);
		REGISTER_PB = 0;
		g65816i_jump_16(g65816i_read_16_immediate(vector));
	}
}

void g65816_device::g65816i_interrupt_nmi()
{
	if (FLAG_E)
	{
		CLK(7);
		g65816i_push_16(REGISTER_PC);
		g65816i_push_8(g65816i_get_reg_p() & ~FLAGPOS_B);
		FLAG_D = DFLAG_CLEAR;
		REGISTER_PB = 0;
		g65816i_jump_16(g65816i_read_16_vector((FLAG_E) ? VECTOR_NMI_E : VECTOR_NMI_N));
	}
	else
	{
		CLK(8);
		g65816i_push_8(REGISTER_PB >> 16);
		g65816i_push_16(REGISTER_PC);
		g65816i_push_8(g65816i_get_reg_p());
		FLAG_D = DFLAG_CLEAR;
		REGISTER_PB = 0;
		g65816i_jump_16(g65816i_read_16_vector((FLAG_E) ? VECTOR_NMI_E : VECTOR_NMI_N));
	}
}


void g65816_device::g65816i_check_maskable_interrupt()
{
	if(!(CPU_STOPPED & STOP_LEVEL_STOP) && LINE_IRQ && !FLAG_I)
	{
		g65816i_interrupt_hardware((FLAG_E) ? VECTOR_IRQ_E : VECTOR_IRQ_N);
		CPU_STOPPED &= ~STOP_LEVEL_WAI;
	}
}


unsigned g65816_device::EA_IMM8()  {REGISTER_PC += 1; return REGISTER_PB | MAKE_UINT_16(REGISTER_PC-1);}
unsigned g65816_device::EA_IMM16() {REGISTER_PC += 2; return REGISTER_PB | MAKE_UINT_16(REGISTER_PC-2);}
unsigned g65816_device::EA_IMM24() {REGISTER_PC += 3; return REGISTER_PB | MAKE_UINT_16(REGISTER_PC-3);}
unsigned g65816_device::EA_D()     {if(MAKE_UINT_8(REGISTER_D)) CLK(1); return MAKE_UINT_16(REGISTER_D + g65816i_read_8_immediate(EA_IMM8()));}
unsigned g65816_device::EA_A()     {return REGISTER_DB | g65816i_read_16_immediate(EA_IMM16());}
unsigned g65816_device::EA_AL()    {return g65816i_read_24_immediate(EA_IMM24());}
unsigned g65816_device::EA_DX()    {return MAKE_UINT_16(REGISTER_D + g65816i_read_8_immediate(EA_IMM8()) + REGISTER_X);}
unsigned g65816_device::EA_DY()    {return MAKE_UINT_16(REGISTER_D + g65816i_read_8_immediate(EA_IMM8()) + REGISTER_Y);}
unsigned g65816_device::EA_AX()    {unsigned tmp = EA_A(); if((tmp^(tmp+REGISTER_X))&0xff00) CLK(1); return tmp + REGISTER_X;}
unsigned g65816_device::EA_ALX()   {return EA_AL() + REGISTER_X;}
unsigned g65816_device::EA_AY()    {unsigned tmp = EA_A(); if((tmp^(tmp+REGISTER_Y))&0xff00) CLK(1); return tmp + REGISTER_Y;}
unsigned g65816_device::EA_DI()    {return REGISTER_DB | g65816i_read_16_direct(EA_D());}
unsigned g65816_device::EA_DLI()   {return g65816i_read_24_normal(EA_D());}
unsigned g65816_device::EA_AI()    {return g65816i_read_16_normal(g65816i_read_16_immediate(EA_IMM16()));}
unsigned g65816_device::EA_ALI()   {return g65816i_read_24_normal(EA_A());}
unsigned g65816_device::EA_DXI()   {return REGISTER_DB | g65816i_read_16_direct_x(EA_DX());}
unsigned g65816_device::EA_DIY()   {unsigned tmp = REGISTER_DB | g65816i_read_16_direct(EA_D()); if((tmp^(tmp+REGISTER_Y))&0xff00) CLK(1); return tmp + REGISTER_Y;}
unsigned g65816_device::EA_DLIY()  {return g65816i_read_24_normal(EA_D()) + REGISTER_Y;}
unsigned g65816_device::EA_AXI()   {return g65816i_read_16_normal(MAKE_UINT_16(g65816i_read_16_immediate(EA_IMM16()) + REGISTER_X));}
unsigned g65816_device::EA_S()     {return MAKE_UINT_16(REGISTER_S + g65816i_read_8_immediate(EA_IMM8()));}
unsigned g65816_device::EA_SIY()   {return (g65816i_read_16_normal(REGISTER_S + g65816i_read_8_immediate(EA_IMM8())) | REGISTER_DB) + REGISTER_Y;}



/* ======================================================================== */
/* ================================= API ================================== */
/* ======================================================================== */


void g65816_device::device_reset()
{
	/* Start the CPU */
	CPU_STOPPED = 0;

	/* Put into emulation mode */
	REGISTER_D = 0;
	REGISTER_PB = 0;
	REGISTER_DB = 0;
	REGISTER_S = (REGISTER_S & 0xff) | 0x100;
	REGISTER_X &= 0xff;
	REGISTER_Y &= 0xff;
	if(!FLAG_M)
	{
		REGISTER_B = REGISTER_A & 0xff00;
		REGISTER_A &= 0xff;
	}
	FLAG_E = EFLAG_SET;
	FLAG_M = MFLAG_SET;
	FLAG_X = XFLAG_SET;

	/* Clear D and set I */
	FLAG_D = DFLAG_CLEAR;
	FLAG_I = IFLAG_SET;

	/* Clear all pending interrupts (should we really do this?) */
	LINE_IRQ = 0;
	LINE_NMI = 0;
	IRQ_DELAY = 0;

	/* Set the function tables to emulation mode */
	g65816i_set_execution_mode(EXECUTION_MODE_E);

	/* 6502 expects these, but its not in the 65816 spec */
	FLAG_Z = ZFLAG_CLEAR;
	REGISTER_S = 0x1ff;

	/* Fetch the reset vector */
	REGISTER_PC = m_bus->vector(VECTOR_RESET & 0x001f) | (m_bus->vector((VECTOR_RESET + 1) & 0x001f) << 8);
	g65816i_jumping(REGISTER_PB | REGISTER_PC);
}


/* Execute some instructions */
void g65816_device::execute_run()
{
	int clocks = m_ICount;
	m_ICount = clocks - (this->*FTABLE_EXECUTE)(m_ICount);
}


/* Get the current Program Counter */
unsigned g65816_device::g65816_get_pc()
{
	return REGISTER_PB | REGISTER_PC;
}

/* Set the Program Counter */
void g65816_device::g65816_set_pc(unsigned val)
{
	REGISTER_PC = MAKE_UINT_16(val);
	REGISTER_PB = (val >> 16) & 0xff;
	g65816_jumping(REGISTER_PB | REGISTER_PC);
}

/* Get the current Stack Pointer */
unsigned g65816_device::g65816_get_sp()
{
	return REGISTER_S;
}

/* Set the Stack Pointer */
void g65816_device::g65816_set_sp(unsigned val)
{
	REGISTER_S = FLAG_E ? MAKE_UINT_8(val) | 0x100 : MAKE_UINT_16(val);
}

/* Get a register */
unsigned g65816_device::g65816_get_reg(int regnum)
{
	/* Set the function tables to emulation mode if the FTABLE is nullptr */
	if (FTABLE_GET_REG == nullptr)
		g65816i_set_execution_mode(EXECUTION_MODE_E);

	return (this->*FTABLE_GET_REG)(regnum);
}

/* Set a register */
void g65816_device::g65816_set_reg(int regnum, unsigned value)
{
	(this->*FTABLE_SET_REG)(regnum, value);
}

/* Set an interrupt line */
void g65816_device::execute_set_input(int line, int state)
{
	(this->*FTABLE_SET_LINE)(line, state);
}




void g65816_device::g65816_restore_state()
{
	// restore proper function pointers
	g65816i_set_execution_mode(FLAG_E ? EXECUTION_MODE_E : ((FLAG_M >> 4) | (FLAG_X >> 4)));

	// make sure the memory system can keep up
	g65816i_jumping(REGISTER_PB | REGISTER_PC);
}

void g65816_device::device_start()
{
	m_a = 0;
	m_b = 0;
	m_x = 0;
	m_y = 0;
	m_s = 0;
	m_pc = 0;
	m_ppc = 0;
	m_pb = 0;
	m_db = 0;
	m_d = 0;
	m_flag_e = 0;
	m_flag_m = 0;
	m_flag_x = 0;
	m_flag_n = 0;
	m_flag_v = 0;
	m_flag_d = 0;
	m_flag_i = 0;
	m_flag_z = 0;
	m_flag_c = 0;
	m_line_irq = 0;
	m_line_nmi = 0;
	m_fastROM = 0;
	m_ir = 0;
	m_irq_delay = 0;
	m_stopped = 0;
	m_source = 0;
	m_destination = 0;
	m_wrmpya = 0;
	m_wrmpyb = 0;
	m_rdmpy = 0;
	m_wrdiv = 0;
	m_dvdd = 0;
	m_rddiv = 0;
	m_opcodes = nullptr;
	m_get_reg = nullptr;
	m_set_reg = nullptr;
	m_set_line = nullptr;
	m_execute = nullptr;
	m_debugger_temp = 0;

	m_divider = 1;
	m_cpu_type = CPU_TYPE_W65C816;
	m_ICount = 0;
}





/*
SNES specific, used to handle master cycles, based off byuu's BSNES code
*/

int g65816_device::bus_5A22_cycle_burst(unsigned addr)
{
	if(m_cpu_type != CPU_TYPE_5A22)
		return 0;

	if(addr & 0x408000) {
		if(addr & 0x800000)
			return (m_fastROM & 1) ? 0 : 2;

		return 2;
	}
	if((addr + 0x6000) & 0x4000) return 2;
	if((addr - 0x4000) & 0x7e00) return 0;

	return 6;
}


