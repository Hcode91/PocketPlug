// PocketPlug - Signetics 2650 CPU
// SPDX-License-Identifier: BSD-3-Clause
//
// Port of MAME's s2650 core (copyright Juergen Buchmueller, BSD-3-Clause).
// Instruction semantics and cycle counts are unchanged; the device glue is
// replaced with plain callbacks. The VC4000 family doesn't use the 2650's
// I/O or data ports, so those read 0 and writes are ignored.

#include "s2650.h"
#include <cstring>

// sign-extend a 7-bit relative displacement (bit 7 is the indirect flag)
#define SEXT7(v) (((v) & 0x40) ? (int)((v) | ~0x7f) : (int)((v) & 0x7f))

static const uint8_t ccc[0x100] =
{
	0x00,0x40,0x40,0x40,0x40,0x40,0x40,0x40,
	0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,
	0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,
	0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,
	0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,
	0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,
	0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,
	0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,
	0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,
	0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,
	0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,
	0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,
	0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,
	0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,
	0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,
	0x40,0x40,0x40,0x40,0x40,0x40,0x40,0x40,
	0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,
	0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,
	0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,
	0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,
	0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,
	0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,
	0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,
	0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,
	0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,
	0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,
	0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,
	0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,
	0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,
	0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,
	0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80,
	0x80,0x80,0x80,0x80,0x80,0x80,0x80,0x80
};

#define PMSK    0x1fff          // mask page offset
#define PLEN    0x2000          // page length
#define PAGE    0x6000          // mask page
#define AMSK    0x7fff          // mask address range
#define C       0x01            // carry flag
#define COM     0x02            // compare: 0 binary, 1 2s complement
#define OVF     0x04            // 2s complement overflow
#define WC      0x08            // with carry: use carry in arithmetic / rotate ops
#define RS      0x10            // register select 0: R0/R1/R2/R3 1: R0/R4/R5/R6
#define IDC     0x20            // inter digit carry: bit-3-to-bit-4 carry
#define CC      0xc0            // condition code
#define SP      0x07            // stack pointer: indexing 8 15bit words
#define PSU34   0x18            // unused bits
#define II      0x20            // interrupt inhibit 0: allow, 1: inhibit
#define FO      0x40            // flag output
#define SI      0x80            // sense input
#define R0      m_reg[0]
#define R1      m_reg[1]
#define R2      m_reg[2]
#define R3      m_reg[3]
#define SET_CC(result)                                          \
	m_psl = (m_psl & ~CC) | ccc[result & 0xff]
#define _REL_EA(page)                                           \
{                                                               \
	uint8_t hr = ARG();             \
	        \
	m_ea = page + ((m_iar + SEXT7(hr)) & PMSK);         \
	if (hr & 0x80)                                              \
	{                                                           \
		                                \
		int addr = m_ea;                                        \
		m_icount -= 6;                                          \
		                        \
		m_ea = RDMEM(addr) << 8;                                \
		if ((++addr & PMSK) == 0) addr -= PLEN;  \
		m_ea = (m_ea + RDMEM(addr)) & AMSK;                     \
	}                                                           \
}
#define _REL_ZERO(page)                                         \
{                                                               \
	uint8_t hr = ARG();             \
	                        \
	m_ea = (SEXT7(hr) & PMSK);                          \
	if (hr & 0x80)                                              \
	{                                                           \
		                                \
		int addr = m_ea;                                        \
		m_icount -= 6;                                          \
		                        \
		m_ea = RDMEM(addr) << 8;                                \
		if ((++addr & PMSK) == 0) addr -= PLEN;  \
		m_ea = (m_ea + RDMEM(addr)) & AMSK;                     \
	}                                                           \
}
#define _ABS_EA()                                               \
{                                                               \
	uint8_t hr, dr;                                             \
	hr = ARG();                     \
	dr = ARG();                    \
	        \
	m_ea = m_page + (((hr << 8) + dr) & PMSK);                  \
	                                 \
	if (hr & 0x80)                                              \
	{                                                           \
		int addr = m_ea;                                        \
		m_icount -= 6;                                          \
		                        \
		                        \
		m_ea = RDMEM(addr) << 8;                                \
		if ((++addr & PMSK) == 0) addr -= PLEN;  \
		m_ea = (m_ea + RDMEM(addr)) & AMSK;                     \
	}                                                           \
	                        \
	switch (hr & 0x60)                                          \
	{                                                           \
		case 0x00:                             \
			break;                                              \
		case 0x20:                  \
			m_reg[m_r] += 1;                                    \
			m_ea = (m_ea & PAGE) + ((m_ea + m_reg[m_r]) & PMSK);\
			m_r = 0;         \
			break;                                              \
		case 0x40:                  \
			m_reg[m_r] -= 1;                                    \
			m_ea = (m_ea & PAGE) + ((m_ea + m_reg[m_r]) & PMSK);\
			m_r = 0;         \
			break;                                              \
		case 0x60:                                 \
			m_ea = (m_ea & PAGE) + ((m_ea + m_reg[m_r]) & PMSK);\
			m_r = 0;         \
			break;                                              \
	}                                                           \
}
#define _BRA_EA()                                               \
{                                                               \
	uint8_t hr, dr;                                             \
	hr = ARG();                     \
	dr = ARG();                    \
	                    \
	m_ea = ((hr << 8) + dr) & AMSK;                             \
	                                 \
	if (hr & 0x80)                                              \
	{                                                           \
		int addr = m_ea;                                        \
		m_icount -= 6;                                          \
		                        \
		m_ea = RDMEM(addr) << 8;                                \
		if ((++addr & PMSK) == 0) addr -= PLEN;  \
		m_ea = (m_ea + RDMEM(addr)) & AMSK;                     \
	}                                                           \
}
#define SWAP_REGS                                               \
{                                                               \
	uint8_t tmp;                                                \
	tmp = m_reg[1];                                             \
	m_reg[1] = m_reg[4];                                        \
	m_reg[4] = tmp;                                             \
	tmp = m_reg[2];                                             \
	m_reg[2] = m_reg[5];                                        \
	m_reg[5] = tmp;                                             \
	tmp = m_reg[3];                                             \
	m_reg[3] = m_reg[6];                                        \
	m_reg[6] = tmp;                                             \
}
#define M_BRR(cond)                                             \
{                                                               \
	if (cond)                                                   \
	{                                                           \
		REL_EA(m_page);                                         \
		m_page = m_ea & PAGE;                                   \
		m_iar  = m_ea & PMSK;                                   \
	}                                                           \
	else                                                        \
	{                                                           \
		m_iar = (m_iar + 1) & PMSK;                             \
	}                                                           \
}
#define M_ZBRR()                                                \
{                                                               \
	REL_ZERO(0);                                                \
	m_page = m_ea & PAGE;                                       \
	m_iar  = m_ea & PMSK;                                       \
}
#define M_BRA(cond)                                             \
{                                                               \
	if (cond)                                                   \
	{                                                           \
		BRA_EA();                                               \
		m_page = m_ea & PAGE;                                   \
		m_iar  = m_ea & PMSK;                                   \
	}                                                           \
	else                                                        \
	{                                                           \
		m_iar = (m_iar + 2) & PMSK;                             \
	}                                                           \
}
#define M_BXA()                                                 \
{                                                               \
	BRA_EA();                                                   \
	m_ea   = (m_ea + m_reg[3]) & AMSK;                          \
	m_page = m_ea & PAGE;                                       \
	m_iar  = m_ea & PMSK;                                       \
}
#define M_BSR(cond)                                             \
{                                                               \
	if (cond)                                                   \
	{                                                           \
		REL_EA(m_page);                                         \
		set_sp(get_sp() + 1);                                   \
		m_ras[get_sp()] = m_page + m_iar;                       \
		m_page = m_ea & PAGE;                                   \
		m_iar  = m_ea & PMSK;                                   \
	}                                                           \
	else                                                        \
	{                                                           \
		m_iar = (m_iar + 1) & PMSK;                             \
	}                                                           \
}
#define M_ZBSR()                                                \
{                                                               \
	REL_ZERO(0);                                                \
	set_sp(get_sp() + 1);                                       \
	m_ras[get_sp()] = m_page + m_iar;                           \
	m_page = m_ea & PAGE;                                       \
	m_iar  = m_ea & PMSK;                                       \
}
#define M_BSA(cond)                                             \
{                                                               \
	if (cond)                                                   \
	{                                                           \
		BRA_EA();                                               \
		set_sp(get_sp() + 1);                                   \
		m_ras[get_sp()] = m_page + m_iar;                       \
		m_page = m_ea & PAGE;                                   \
		m_iar  = m_ea & PMSK;                                   \
	}                                                           \
	else                                                        \
	{                                                           \
		m_iar = (m_iar + 2) & PMSK;                             \
	}                                                           \
}
#define M_BSXA()                                                \
{                                                               \
	BRA_EA();                                                   \
	m_ea = (m_ea + m_reg[3]) & AMSK;                            \
	set_sp(get_sp() + 1);                                       \
	m_ras[get_sp()] = m_page + m_iar;                           \
	m_page = m_ea & PAGE;                                       \
	m_iar  = m_ea & PMSK;                                       \
}
#define M_RET(cond)                                             \
{                                                               \
	if (cond)                                                   \
	{                                                           \
		m_icount -= 6;                                          \
		m_ea = m_ras[get_sp()];                                 \
		set_sp(get_sp() - 1);                                   \
		m_page = m_ea & PAGE;                                   \
		m_iar  = m_ea & PMSK;                                   \
	}                                                           \
}
#define M_RETE(cond)                                            \
{                                                               \
	if (cond)                                                   \
	{                                                           \
		m_ea = m_ras[get_sp()];                                 \
		set_sp(get_sp() - 1);                                   \
		m_page = m_ea & PAGE;                                   \
		m_iar  = m_ea & PMSK;                                   \
		set_psu(m_psu & ~II);                                   \
		m_icount -= check_irq_line();                           \
	}                                                           \
}
#define M_LOD(dest,source)                                      \
{                                                               \
	dest = source;                                              \
	SET_CC(dest);                                               \
}
#define M_STR(address,source)                                   \
	WRMEM(address, source)
#define M_AND(dest,source)                                      \
{                                                               \
	dest &= source;                                             \
	SET_CC(dest);                                               \
}
#define M_IOR(dest,source)                                      \
{                                                               \
	dest |= source;                                             \
	SET_CC(dest);                                               \
}
#define M_EOR(dest,source)                                      \
{                                                               \
	dest ^= source;                                             \
	SET_CC(dest);                                               \
}
#define M_ADD(dest,_source)                                     \
{                                                               \
	uint8_t source = _source;                                   \
	uint8_t before = dest;                                      \
	                   \
	uint16_t res = dest + source + ((m_psl >> 3) & m_psl & C);  \
	m_psl &= ~(C | OVF | IDC);                                  \
	if (res & 0x100) m_psl |= C;                                \
	dest = res & 0xff;                                          \
	if ((dest ^ before ^ source) & 0x10) m_psl |= IDC;          \
	if ((before ^ dest) & (source ^ dest) & 0x80) m_psl |= OVF; \
	SET_CC(dest);                                               \
}
#define M_SUB(dest,_source)                                     \
{                                                               \
	uint8_t source = _source;                                   \
	uint8_t before = dest;                                      \
	             \
	uint16_t res = dest - source - ((m_psl >> 3) & (m_psl ^ C) & C); \
	m_psl &= ~(C | OVF | IDC);                                  \
	if ((res & 0x100) == 0) m_psl |= C;                         \
	dest = res & 0xff;                                          \
	if (~(dest ^ before ^ source) & 0x10) m_psl |= IDC;         \
	if ((before ^ source) & (before ^ dest) & 0x80) m_psl |= OVF; \
	SET_CC(dest);                                               \
}
#define M_COM(reg,val)                                          \
{                                                               \
	int16_t res;                                                \
	m_psl &= ~CC;                                               \
	if (m_psl & COM) res = (uint8_t)reg - (uint8_t)val;         \
	else res = (int8_t)reg - (int8_t)val;                       \
	if (res < 0) m_psl |= 0x80;                                 \
	else if (res > 0) m_psl |= 0x40;                            \
}
#define M_DAR(dest)                                             \
{                                                               \
	if ((m_psl & C) == 0) dest += 0xa0;                         \
	if ((m_psl & IDC) == 0) dest = (dest & 0xf0) | ((dest + 0x0a) & 0x0f); \
}
#define M_RRL(dest)                                             \
{                                                               \
	uint8_t before = dest;                                      \
	if (m_psl & WC)                                             \
	{                                                           \
		uint8_t c = m_psl & C;                                  \
		m_psl &= ~(C + IDC);                                    \
		dest = (before << 1) | c;                               \
		m_psl |= (before >> 7) + (dest & IDC);                  \
	}                                                           \
	else                                                        \
	{                                                           \
		dest = (before << 1) | (before >> 7);                   \
	}                                                           \
	SET_CC(dest);                                               \
	m_psl = (m_psl & ~OVF) | (((dest ^ before) >> 5) & OVF);    \
}
#define M_RRR(dest)                                             \
{                                                               \
	uint8_t before = dest;                                      \
	if (m_psl & WC)                                             \
	{                                                           \
		uint8_t c = m_psl & C;                                  \
		m_psl &= ~(C + IDC);                                    \
		dest = (before >> 1) | (c << 7);                        \
		m_psl |= (before & C) + (dest & IDC);                   \
	}                                                           \
	else                                                        \
	{                                                           \
		dest = (before >> 1) | (before << 7);                   \
	}                                                           \
	SET_CC(dest);                                               \
	m_psl = (m_psl & ~OVF) | (((dest ^ before) >> 5) & OVF);    \
}
#define M_SPSU()                                                \
{                                                               \
	R0 = get_psu() & ~PSU34;                                    \
	SET_CC(R0);                                                 \
}
#define M_SPSL()                                                \
{                                                               \
	R0 = m_psl;                                                 \
	SET_CC(R0);                                                 \
}
#define M_CPSU()                                                \
{                                                               \
	uint8_t cpsu = ARG() & ~SI;                                 \
	set_psu(m_psu & ~cpsu);                                     \
	m_icount -= check_irq_line();                               \
}
#define M_CPSL()                                                \
{                                                               \
	uint8_t cpsl = ARG();                                       \
	                       \
	if ((cpsl & RS) && (m_psl & RS))                            \
		SWAP_REGS;                                              \
	m_psl = m_psl & ~cpsl;                                      \
}
#define M_PPSU()                                                \
{                                                               \
	uint8_t ppsu = (ARG() & ~PSU34) & ~SI;                      \
	set_psu(m_psu | ppsu);                                      \
}
#define M_PPSL()                                                \
{                                                               \
	uint8_t ppsl = ARG();                                       \
	                         \
	if ((ppsl & RS) && !(m_psl & RS))                           \
		SWAP_REGS;                                              \
	m_psl = m_psl | ppsl;                                       \
}
#define M_TPSU()                                                \
{                                                               \
	uint8_t tpsu = ARG();                                       \
	uint8_t rpsu = get_psu();                                   \
	m_psl &= ~CC;                                               \
	if ((rpsu & tpsu) != tpsu)                                  \
		m_psl |= 0x80;                                          \
}
#define M_TPSL()                                                \
{                                                               \
	uint8_t tpsl = ARG();                                       \
	if ((m_psl & tpsl) != tpsl)                                 \
		m_psl = (m_psl & ~CC) | 0x80;                           \
	else                                                        \
		m_psl &= ~CC;                                           \
}
#define M_TMI(value)                                            \
{                                                               \
	uint8_t tmi = ARG();                                        \
	m_psl &= ~CC;                                               \
	if ((value & tmi) != tmi)                                   \
		m_psl |= 0x80;                                          \
}

#define REL_EA(page) _REL_EA(page)
#define REL_ZERO(page) _REL_ZERO(page)
#define ABS_EA() _ABS_EA()
#define BRA_EA() _BRA_EA()

inline uint8_t S2650::get_psu()
{
    if (m_sense) {
        if (m_sense(m_ctx)) m_psu |= SI;
        else m_psu &= ~SI;
    }
    return m_psu;
}

inline int S2650::check_irq_line()
{
    int cycles = 0;
    if (m_irq_state != 0)
    {
        if ((m_psu & II) == 0)
        {
            if (m_halt)
            {
                m_halt = 0;
                m_iar = (m_iar + 1) & PMSK;
            }
            int vector = m_intack;
            cycles += 9; // ZBSR
            m_ea = SEXT7(vector) & PMSK;
            if (vector & 0x80) // indirect bit set ?
            {
                int addr = m_ea;
                cycles += 6;
                m_ea = RDMEM(addr) << 8;
                if (!(++addr & PMSK)) addr -= PLEN;
                m_ea = (m_ea + RDMEM(addr)) & AMSK;
            }
            set_sp(get_sp() + 1);
            set_psu(m_psu | II);
            m_ras[get_sp()] = m_page + m_iar;
            m_page = m_ea & PAGE;
            m_iar  = m_ea & PMSK;
        }
    }
    return cycles;
}

inline uint8_t S2650::ROP()
{
    uint8_t result = RDMEM(m_page + m_iar);
    m_iar = (m_iar + 1) & PMSK;
    return result;
}

inline uint8_t S2650::ARG()
{
    uint8_t result = RDMEM(m_page + m_iar);
    m_iar = (m_iar + 1) & PMSK;
    return result;
}

void S2650::reset()
{
    m_ppc = 0;
    m_page = 0;
    m_iar = 0;
    m_ea = 0;
    m_r = 0;
    m_halt = 0;
    m_ir = 0;
    m_irq_state = 0;
    memset(m_reg, 0, sizeof(m_reg));
    memset(m_ras, 0, sizeof(m_ras));
    m_psl = COM | WC;
    m_psu = 0;
    m_icount = 0;
}

void S2650::run(int cycles)
{
    m_icount += cycles;
    if (m_icount <= 0) return;
    m_icount -= check_irq_line();
    do
    {
        m_ppc = m_page + m_iar;
        m_ir = ROP();
        m_r = m_ir & 3;     // register / value
		switch (m_ir)
		{
			case 0x00:      // LODZ,0
			case 0x01:      // LODZ,1
			case 0x02:      // LODZ,2
			case 0x03:      // LODZ,3
				m_icount -= 6;
				M_LOD(R0, m_reg[m_r]);
				break;
			case 0x04:      // LODI,0 v
			case 0x05:      // LODI,1 v
			case 0x06:      // LODI,2 v
			case 0x07:      // LODI,3 v
				m_icount -= 6;
				M_LOD(m_reg[m_r], ARG());
				break;
			case 0x08:      // LODR,0 (*)a
			case 0x09:      // LODR,1 (*)a
			case 0x0a:      // LODR,2 (*)a
			case 0x0b:      // LODR,3 (*)a
				m_icount -= 9;
				REL_EA(m_page);
				M_LOD(m_reg[m_r], RDMEM(m_ea));
				break;
			case 0x0c:      // LODA,0 (*)a(,X)
			case 0x0d:      // LODA,1 (*)a(,X)
			case 0x0e:      // LODA,2 (*)a(,X)
			case 0x0f:      // LODA,3 (*)a(,X)
				m_icount -= 12;
				ABS_EA();
				M_LOD(m_reg[m_r], RDMEM(m_ea));
				break;
			case 0x12:      // SPSU
				m_icount -= 6;
				M_SPSU();
				break;
			case 0x13:      // SPSL
				m_icount -= 6;
				M_SPSL();
				break;
			case 0x14:      // RETC,0   (zero)
			case 0x15:      // RETC,1   (plus)
			case 0x16:      // RETC,2   (minus)
				m_icount -= 9;    // +2 cycles if condition is true
				M_RET((m_psl >> 6) == m_r);
				break;
			case 0x17:      // RETC,3   (always)
				m_icount -= 9;    // +2 cycles if condition is true
				M_RET(1);
				break;
			case 0x18:      // BCTR,0  (*)a
			case 0x19:      // BCTR,1  (*)a
			case 0x1a:      // BCTR,2  (*)a
				m_icount -= 9;
				M_BRR((m_psl >> 6) == m_r);
				break;
			case 0x1b:      // BCTR,3  (*)a
				m_icount -= 9;
				M_BRR(1);
				break;
			case 0x1c:      // BCTA,0  (*)a
			case 0x1d:      // BCTA,1  (*)a
			case 0x1e:      // BCTA,2  (*)a
				m_icount -= 9;
				M_BRA((m_psl >> 6) == m_r);
				break;
			case 0x1f:      // BCTA,3  (*)a
				m_icount -= 9;
				M_BRA(1);
				break;
			case 0x20:      // EORZ,0
			case 0x21:      // EORZ,1
			case 0x22:      // EORZ,2
			case 0x23:      // EORZ,3
				m_icount -= 6;
				M_EOR(R0, m_reg[m_r]);
				break;
			case 0x24:      // EORI,0 v
			case 0x25:      // EORI,1 v
			case 0x26:      // EORI,2 v
			case 0x27:      // EORI,3 v
				m_icount -= 6;
				M_EOR(m_reg[m_r], ARG());
				break;
			case 0x28:      // EORR,0 (*)a
			case 0x29:      // EORR,1 (*)a
			case 0x2a:      // EORR,2 (*)a
			case 0x2b:      // EORR,3 (*)a
				m_icount -= 9;
				REL_EA(m_page);
				M_EOR(m_reg[m_r], RDMEM(m_ea));
				break;
			case 0x2c:      // EORA,0 (*)a(,X)
			case 0x2d:      // EORA,1 (*)a(,X)
			case 0x2e:      // EORA,2 (*)a(,X)
			case 0x2f:      // EORA,3 (*)a(,X)
				m_icount -= 12;
				ABS_EA();
				M_EOR(m_reg[m_r], RDMEM(m_ea));
				break;
			case 0x30:      // REDC,0
			case 0x31:      // REDC,1
			case 0x32:      // REDC,2
			case 0x33:      // REDC,3
				m_icount -= 6;
				m_reg[m_r] = 0;
				SET_CC(m_reg[m_r]);
				break;
			case 0x34:      // RETE,0
			case 0x35:      // RETE,1
			case 0x36:      // RETE,2
				m_icount -= 9;
				M_RETE((m_psl >> 6) == m_r);
				break;
			case 0x37:      // RETE,3
				m_icount -= 9;
				M_RETE(1);
				break;
			case 0x38:      // BSTR,0 (*)a
			case 0x39:      // BSTR,1 (*)a
			case 0x3a:      // BSTR,2 (*)a
				m_icount -= 9;
				M_BSR((m_psl >> 6) == m_r);
				break;
			case 0x3b:      // BSTR,R3 (*)a
				m_icount -= 9;
				M_BSR(1);
				break;
			case 0x3c:      // BSTA,0 (*)a
			case 0x3d:      // BSTA,1 (*)a
			case 0x3e:      // BSTA,2 (*)a
				m_icount -= 9;
				M_BSA((m_psl >> 6) == m_r);
				break;
			case 0x3f:      // BSTA,3 (*)a
				m_icount -= 9;
				M_BSA(1);
				break;
			case 0x40:      // HALT
				m_icount -= 6;
				m_iar = (m_iar - 1) & PMSK;
				m_halt = 1;
				if (m_icount > 0)
					m_icount = 0;
				break;
			case 0x41:      // ANDZ,1
			case 0x42:      // ANDZ,2
			case 0x43:      // ANDZ,3
				m_icount -= 6;
				M_AND(R0, m_reg[m_r]);
				break;
			case 0x44:      // ANDI,0 v
			case 0x45:      // ANDI,1 v
			case 0x46:      // ANDI,2 v
			case 0x47:      // ANDI,3 v
				m_icount -= 6;
				M_AND(m_reg[m_r], ARG());
				break;
			case 0x48:      // ANDR,0 (*)a
			case 0x49:      // ANDR,1 (*)a
			case 0x4a:      // ANDR,2 (*)a
			case 0x4b:      // ANDR,3 (*)a
				m_icount -= 9;
				REL_EA(m_page);
				M_AND(m_reg[m_r], RDMEM(m_ea));
				break;
			case 0x4c:      // ANDA,0 (*)a(,X)
			case 0x4d:      // ANDA,1 (*)a(,X)
			case 0x4e:      // ANDA,2 (*)a(,X)
			case 0x4f:      // ANDA,3 (*)a(,X)
				m_icount -= 12;
				ABS_EA();
				M_AND(m_reg[m_r], RDMEM(m_ea));
				break;
			case 0x50:      // RRR,0
			case 0x51:      // RRR,1
			case 0x52:      // RRR,2
			case 0x53:      // RRR,3
				m_icount -= 6;
				M_RRR(m_reg[m_r]);
				break;
			case 0x54:      // REDE,0 v
			case 0x55:      // REDE,1 v
			case 0x56:      // REDE,2 v
			case 0x57:      // REDE,3 v
				m_icount -= 9;
				m_reg[m_r] = (ARG(), 0);
				SET_CC(m_reg[m_r]);
				break;
			case 0x58:      // BRNR,0 (*)a
			case 0x59:      // BRNR,1 (*)a
			case 0x5a:      // BRNR,2 (*)a
			case 0x5b:      // BRNR,3 (*)a
				m_icount -= 9;
				M_BRR(m_reg[m_r]);
				break;
			case 0x5c:      // BRNA,0 (*)a
			case 0x5d:      // BRNA,1 (*)a
			case 0x5e:      // BRNA,2 (*)a
			case 0x5f:      // BRNA,3 (*)a
				m_icount -= 9;
				M_BRA(m_reg[m_r]);
				break;
			case 0x60:      // IORZ,0
			case 0x61:      // IORZ,1
			case 0x62:      // IORZ,2
			case 0x63:      // IORZ,3
				m_icount -= 6;
				M_IOR(R0, m_reg[m_r]);
				break;
			case 0x64:      // IORI,0 v
			case 0x65:      // IORI,1 v
			case 0x66:      // IORI,2 v
			case 0x67:      // IORI,3 v
				m_icount -= 6;
				M_IOR(m_reg[m_r], ARG());
				break;
			case 0x68:      // IORR,0 (*)a
			case 0x69:      // IORR,1 (*)a
			case 0x6a:      // IORR,2 (*)a
			case 0x6b:      // IORR,3 (*)a
				m_icount -= 9;
				REL_EA(m_page);
				M_IOR(m_reg[m_r],RDMEM(m_ea));
				break;
			case 0x6c:      // IORA,0 (*)a(,X)
			case 0x6d:      // IORA,1 (*)a(,X)
			case 0x6e:      // IORA,2 (*)a(,X)
			case 0x6f:      // IORA,3 (*)a(,X)
				m_icount -= 12;
				ABS_EA();
				M_IOR(m_reg[m_r], RDMEM(m_ea));
				break;
			case 0x70:      // REDD,0
			case 0x71:      // REDD,1
			case 0x72:      // REDD,2
			case 0x73:      // REDD,3
				m_icount -= 6;
				m_reg[m_r] = 0;
				SET_CC(m_reg[m_r]);
				break;
			case 0x74:      // CPSU
				m_icount -= 9;
				M_CPSU();
				break;
			case 0x75:      // CPSL
				m_icount -= 9;
				M_CPSL();
				break;
			case 0x76:      // PPSU
				m_icount -= 9;
				M_PPSU();
				break;
			case 0x77:      // PPSL
				m_icount -= 9;
				M_PPSL();
				break;
			case 0x78:      // BSNR,0 (*)a
			case 0x79:      // BSNR,1 (*)a
			case 0x7a:      // BSNR,2 (*)a
			case 0x7b:      // BSNR,3 (*)a
				m_icount -= 9;
				M_BSR(m_reg[m_r]);
				break;
			case 0x7c:      // BSNA,0 (*)a
			case 0x7d:      // BSNA,1 (*)a
			case 0x7e:      // BSNA,2 (*)a
			case 0x7f:      // BSNA,3 (*)a
				m_icount -= 9;
				M_BSA(m_reg[m_r]);
				break;
			case 0x80:      // ADDZ,0
			case 0x81:      // ADDZ,1
			case 0x82:      // ADDZ,2
			case 0x83:      // ADDZ,3
				m_icount -= 6;
				M_ADD(R0,m_reg[m_r]);
				break;
			case 0x84:      // ADDI,0 v
			case 0x85:      // ADDI,1 v
			case 0x86:      // ADDI,2 v
			case 0x87:      // ADDI,3 v
				m_icount -= 6;
				M_ADD(m_reg[m_r], ARG());
				break;
			case 0x88:      // ADDR,0 (*)a
			case 0x89:      // ADDR,1 (*)a
			case 0x8a:      // ADDR,2 (*)a
			case 0x8b:      // ADDR,3 (*)a
				m_icount -= 9;
				REL_EA(m_page);
				M_ADD(m_reg[m_r], RDMEM(m_ea));
				break;
			case 0x8c:      // ADDA,0 (*)a(,X)
			case 0x8d:      // ADDA,1 (*)a(,X)
			case 0x8e:      // ADDA,2 (*)a(,X)
			case 0x8f:      // ADDA,3 (*)a(,X)
				m_icount -= 12;
				ABS_EA();
				M_ADD(m_reg[m_r], RDMEM(m_ea));
				break;
			case 0x92:      // LPSU
				m_icount -= 6;
				set_psu((R0 & ~PSU34 & ~SI) | (m_psu & SI));
				break;
			case 0x93:      // LPSL
				m_icount -= 6;
				if ((m_psl ^ R0) & RS)
					SWAP_REGS;
				m_psl = R0;
				break;
			case 0x94:      // DAR,0
			case 0x95:      // DAR,1
			case 0x96:      // DAR,2
			case 0x97:      // DAR,3
				m_icount -= 9;
				M_DAR(m_reg[m_r]);
				break;
			case 0x98:      // BCFR,0 (*)a
			case 0x99:      // BCFR,1 (*)a
			case 0x9a:      // BCFR,2 (*)a
				m_icount -= 9;
				M_BRR((m_psl >> 6) != m_r);
				break;
			case 0x9b:      // ZBRR    (*)a
				m_icount -= 9;
				M_ZBRR();
				break;
			case 0x9c:      // BCFA,0 (*)a
			case 0x9d:      // BCFA,1 (*)a
			case 0x9e:      // BCFA,2 (*)a
				m_icount -= 9;
				M_BRA((m_psl >> 6) != m_r);
				break;
			case 0x9f:      // BXA     (*)a
				m_icount -= 9;
				M_BXA();
				break;
			case 0xa0:      // SUBZ,0
			case 0xa1:      // SUBZ,1
			case 0xa2:      // SUBZ,2
			case 0xa3:      // SUBZ,3
				m_icount -= 6;
				M_SUB(R0, m_reg[m_r]);
				break;
			case 0xa4:      // SUBI,0 v
			case 0xa5:      // SUBI,1 v
			case 0xa6:      // SUBI,2 v
			case 0xa7:      // SUBI,3 v
				m_icount -= 6;
				M_SUB(m_reg[m_r], ARG());
				break;
			case 0xa8:      // SUBR,0 (*)a
			case 0xa9:      // SUBR,1 (*)a
			case 0xaa:      // SUBR,2 (*)a
			case 0xab:      // SUBR,3 (*)a
				m_icount -= 9;
				REL_EA(m_page);
				M_SUB(m_reg[m_r], RDMEM(m_ea));
				break;
			case 0xac:      // SUBA,0 (*)a(,X)
			case 0xad:      // SUBA,1 (*)a(,X)
			case 0xae:      // SUBA,2 (*)a(,X)
			case 0xaf:      // SUBA,3 (*)a(,X)
				m_icount -= 12;
				ABS_EA();
				M_SUB(m_reg[m_r], RDMEM(m_ea));
				break;
			case 0xb0:      // WRTC,0
			case 0xb1:      // WRTC,1
			case 0xb2:      // WRTC,2
			case 0xb3:      // WRTC,3
				m_icount -= 6;
				(void)0;
				break;
			case 0xb4:      // TPSU
				m_icount -= 9;
				M_TPSU();
				break;
			case 0xb5:      // TPSL
				m_icount -= 9;
				M_TPSL();
				break;
			case 0xb8:      // BSFR,0 (*)a
			case 0xb9:      // BSFR,1 (*)a
			case 0xba:      // BSFR,2 (*)a
				m_icount -= 9;
				M_BSR((m_psl >> 6) != m_r);
				break;
			case 0xbb:      // ZBSR    (*)a
				m_icount -= 9;
				M_ZBSR();
				break;
			case 0xbc:      // BSFA,0 (*)a
			case 0xbd:      // BSFA,1 (*)a
			case 0xbe:      // BSFA,2 (*)a
				m_icount -= 9;
				M_BSA((m_psl >> 6) != m_r);
				break;
			case 0xbf:      // BSXA    (*)a
				m_icount -= 9;
				M_BSXA();
				break;
			case 0xc0:      // NOP
				m_icount -= 6;
				break;
			case 0xc1:      // STRZ,1
			case 0xc2:      // STRZ,2
			case 0xc3:      // STRZ,3
				m_icount -= 6;
				M_LOD(m_reg[m_r], R0);
				break;
			case 0xc8:      // STRR,0 (*)a
			case 0xc9:      // STRR,1 (*)a
			case 0xca:      // STRR,2 (*)a
			case 0xcb:      // STRR,3 (*)a
				m_icount -= 9;
				REL_EA(m_page);
				M_STR(m_ea, m_reg[m_r]);
				break;
			case 0xcc:      // STRA,0 (*)a(,X)
			case 0xcd:      // STRA,1 (*)a(,X)
			case 0xce:      // STRA,2 (*)a(,X)
			case 0xcf:      // STRA,3 (*)a(,X)
				m_icount -= 12;
				ABS_EA();
				M_STR(m_ea, m_reg[m_r]);
				break;
			case 0xd0:      // RRL,0
			case 0xd1:      // RRL,1
			case 0xd2:      // RRL,2
			case 0xd3:      // RRL,3
				m_icount -= 6;
				M_RRL(m_reg[m_r]);
				break;
			case 0xd4:      // WRTE,0 v
			case 0xd5:      // WRTE,1 v
			case 0xd6:      // WRTE,2 v
			case 0xd7:      // WRTE,3 v
				m_icount -= 9;
				ARG();
				break;
			case 0xd8:      // BIRR,0 (*)a
			case 0xd9:      // BIRR,1 (*)a
			case 0xda:      // BIRR,2 (*)a
			case 0xdb:      // BIRR,3 (*)a
				m_icount -= 9;
				M_BRR(++m_reg[m_r]);
				break;
			case 0xdc:      // BIRA,0 (*)a
			case 0xdd:      // BIRA,1 (*)a
			case 0xde:      // BIRA,2 (*)a
			case 0xdf:      // BIRA,3 (*)a
				m_icount -= 9;
				M_BRA(++m_reg[m_r]);
				break;
			case 0xe0:      // COMZ,0
			case 0xe1:      // COMZ,1
			case 0xe2:      // COMZ,2
			case 0xe3:      // COMZ,3
				m_icount -= 6;
				M_COM(R0, m_reg[m_r]);
				break;
			case 0xe4:      // COMI,0 v
			case 0xe5:      // COMI,1 v
			case 0xe6:      // COMI,2 v
			case 0xe7:      // COMI,3 v
				m_icount -= 6;
				M_COM(m_reg[m_r], ARG());
				break;
			case 0xe8:      // COMR,0 (*)a
			case 0xe9:      // COMR,1 (*)a
			case 0xea:      // COMR,2 (*)a
			case 0xeb:      // COMR,3 (*)a
				m_icount -= 9;
				REL_EA(m_page);
				M_COM(m_reg[m_r], RDMEM(m_ea));
				break;
			case 0xec:      // COMA,0 (*)a(,X)
			case 0xed:      // COMA,1 (*)a(,X)
			case 0xee:      // COMA,2 (*)a(,X)
			case 0xef:      // COMA,3 (*)a(,X)
				m_icount -= 12;
				ABS_EA();
				M_COM(m_reg[m_r], RDMEM(m_ea));
				break;
			case 0xf0:      // WRTD,0
			case 0xf1:      // WRTD,1
			case 0xf2:      // WRTD,2
			case 0xf3:      // WRTD,3
				m_icount -= 6;
				(void)0;
				break;
			case 0xf4:      // TMI,0  v
			case 0xf5:      // TMI,1  v
			case 0xf6:      // TMI,2  v
			case 0xf7:      // TMI,3  v
				m_icount -= 9;
				M_TMI(m_reg[m_r]);
				break;
			case 0xf8:      // BDRR,0 (*)a
			case 0xf9:      // BDRR,1 (*)a
			case 0xfa:      // BDRR,2 (*)a
			case 0xfb:      // BDRR,3 (*)a
				m_icount -= 9;
				M_BRR(--m_reg[m_r]);
				break;
			case 0xfc:      // BDRA,0 (*)a
			case 0xfd:      // BDRA,1 (*)a
			case 0xfe:      // BDRA,2 (*)a
			case 0xff:      // BDRA,3 (*)a
				m_icount -= 9;
				M_BRA(--m_reg[m_r]);
				break;
			default:        // illegal
				m_icount -= 6;
				break;
		}
	} while (m_icount > 0);
}
