// handlers ported from MAME generalplus_gpl162xx_soc.cpp (BSD-3-Clause, David Haywood)






u16 Gpl162xxCore::usb_7a35_r()
{
	
	return rand();
}

u16 Gpl162xxCore::usb_7a37_r()
{
	
	return rand();
}

u16 Gpl162xxCore::usb_7a39_r()
{
	
	return rand();
}


u16 Gpl162xxCore::usb_7a3a_r()
{
	
	return rand();
}

u16 Gpl162xxCore::usb_7a46_r()
{
	
	return rand();
}

u16 Gpl162xxCore::usb_7a54_r()
{
	
	return rand();
}

// **************************************** 78xx region with some handling *************************************************

u16 Gpl162xxCore::power_state_r()
{
	
	return 0x0002;
}

u16 Gpl162xxCore::dac_pga_r()
{
	
	m_dac_pga ^= 0x0100; // status flag for something?
	return m_dac_pga;
}

// sets bit 0x0002 then expects it to have cleared
u16 Gpl162xxCore::cache_ctrl_r() {  return m_cache_ctrl & ~ 0x0002; }
void Gpl162xxCore::cache_ctrl_w(u16 data) {  m_cache_ctrl = data; }

// ****************************************  78xx region stubs *************************************************

u16 Gpl162xxCore::raw_war_r() {  return m_782d; }
void Gpl162xxCore::raw_war_w(u16 data) {  m_782d = data; }

u16 Gpl162xxCore::sys_ctrl_r() {  return m_sys_ctrl; }
void Gpl162xxCore::sys_ctrl_w(u16 data) {  m_sys_ctrl = data; }

void Gpl162xxCore::clock_ctrl_w(u16 data)
{
	
	m_clock_ctrl = data;
}

u16 Gpl162xxCore::clk_ctrl0_r()
{
	
	return 0x0000;
}

void Gpl162xxCore::clk_ctrl0_w(u16 data)
{
	
}

void Gpl162xxCore::watchdog_ctrl_w(u16 data)
{
	
}

void Gpl162xxCore::waitmode_enter_780c_w(u16 data)
{
	// 
	// must be followed by 6 nops to ensure wait mode is entered
}

// this gets stored / modified / restored before certain memory accesses (
// used extensively during SDRAM checks in jak_gtg and jak_car2

u16 Gpl162xxCore::membankswitch_7810_r()
{
//  
	return m_membankswitch_7810;
}

void Gpl162xxCore::membankswitch_7810_w(u16 data)
{
//  if (m_membankswitch_7810 != data)
//  

//  if (m_membankswitch_7810 != data)
//      

	m_membankswitch_7810 = data;
}


void Gpl162xxCore::unkarea_7816_w(u16 data)
{
	
	m_7816 = data;
}

void Gpl162xxCore::pllchange_w(u16 data)
{
	
	m_pllchange = data;
}

u16 Gpl162xxCore::pllclkwait_r()
{
	
	return 0x0000;
}

void Gpl162xxCore::pllclkwait_w(u16 data)
{
	
}

void Gpl162xxCore::chipselect_csx_memory_device_control_w(offs_t offset, u16 data)
{
	
	m_782x[offset] = data;


	static char const *const md[] =
	{
		"(00) ROM / SRAM",
		"(01) ROM / SRAM",
		"(10) NOR FLASH",
		"(11) NAND FLASH",
	};

	u8 cs_wait =  data & 0x000f;
	u8 cs_warat = (data & 0x0030)>>4;
	u8 cs_md    = (data & 0x00c0)>>6;
	int cs_size  = (data & 0xff00)>>8;

	

	cs_callback(m_782x[0], m_782x[1], m_782x[2], m_782x[3], m_782x[4]);

}



void Gpl162xxCore::mcs0_page_w(u16 data) {  m_7835 = data; }

// IO here?

// Port A

u16 Gpl162xxCore::ioa_data_r()
{
	
	return porta_in();
}

void Gpl162xxCore::ioa_data_w(u16 data)
{
	
	porta_out(data);
}

u16 Gpl162xxCore::ioa_buffer_r()
{
	
	return 0x0000;// 0xffff;
}

void Gpl162xxCore::ioa_buffer_w(u16 data)
{
	
}

u16 Gpl162xxCore::ioa_dir_r()
{
	
	return m_7862_porta_direction;
}

void Gpl162xxCore::ioa_dir_w(u16 data)
{
	
	m_7862_porta_direction = data;
}

u16 Gpl162xxCore::ioa_attrib_r()
{
	
	return m_7863_porta_attribute;
}

void Gpl162xxCore::ioa_attrib_w(u16 data)
{
	
	m_7863_porta_attribute = data;
}

// Port B

u16 Gpl162xxCore::iob_buffer_r()
{
	
	return rand();
}

void Gpl162xxCore::iob_buffer_w(u16 data)
{
	
	portb_out(data); // buffer writes must update output state too, beijuehh requires it for banking
}

u16 Gpl162xxCore::iob_data_r()
{
	
	return portb_in();
}

void Gpl162xxCore::iob_data_w(u16 data)
{
	
	portb_out(data);
}

u16 Gpl162xxCore::iob_dir_r()
{
	
	return m_786a_portb_direction;
}

void Gpl162xxCore::iob_dir_w(u16 data)
{
	
	m_786a_portb_direction = data;
}

u16 Gpl162xxCore::iob_attrib_r()
{
	
	return m_786b_portb_attribute;
}

void Gpl162xxCore::iob_attrib_w(u16 data)
{
	
	m_786b_portb_attribute = data;
}

// Port C

u16 Gpl162xxCore::ioc_data_r()
{
	
	return portc_in();
}

void Gpl162xxCore::ioc_data_w(u16 data)
{
	
	m_ioc_data = data;
	portc_out(data);
}

u16 Gpl162xxCore::ioc_buffer_r()
{
	
	return 0xffff;// m_7871;
}

void Gpl162xxCore::ioc_buffer_w(u16 data)
{
	
}

u16 Gpl162xxCore::ioc_dir_r()
{
	
	return m_7872_portc_direction;
}

void Gpl162xxCore::ioc_dir_w(u16 data)
{
	
	m_7872_portc_direction = data;
}

u16 Gpl162xxCore::ioc_attrib_r()
{
	
	return m_7873_portc_attribute;
}

void Gpl162xxCore::ioc_attrib_w(u16 data)
{
	
	m_7873_portc_attribute = data;
}

// Port D

u16 Gpl162xxCore::iod_data_r()
{
	
	return portd_in();
}

void Gpl162xxCore::iod_data_w(u16 data)
{
	
	//m_7878 = data;
	portd_out(data);
}

u16 Gpl162xxCore::iod_buffer_r()
{
	
	return 0xffff;
}

void Gpl162xxCore::iod_buffer_w(u16 data)
{
	
	portd_out(data); // buffer writes must update output state too, beijuehh requires it for banking
}

u16 Gpl162xxCore::iod_dir_r()
{
	
	return m_787a_portd_direction;
}

void Gpl162xxCore::iod_dir_w(u16 data)
{
	
	m_787a_portd_direction = data;
}

u16 Gpl162xxCore::iod_attib_r()
{
	
	return m_787b_portd_attribute;
}

void Gpl162xxCore::iod_attib_w(u16 data)
{
	
	m_787b_portd_attribute = data;
}

u16 Gpl162xxCore::iod_drv_r()
{
	
	return 0xffff;
}

void Gpl162xxCore::iod_drv_w(u16 data)
{
	
}

u16 Gpl162xxCore::iod_mux_r()
{
	
	return 0xffff;
}

void Gpl162xxCore::iod_mux_w(u16 data)
{
	
}

u16 Gpl162xxCore::ioe_buffer_r()
{
	
	return 0xffff;
}

void Gpl162xxCore::ioe_buffer_w(u16 data)
{
	
	//m_porte_out(data);
}

u16 Gpl162xxCore::ioe_dir_r()
{
	
	return m_ioe_dir;
}

void Gpl162xxCore::ioe_dir_w(u16 data)
{
	
	m_ioe_dir = data;
}

u16 Gpl162xxCore::ioe_attrib_r()
{
	
	return m_ioe_attrib;
}

void Gpl162xxCore::ioe_attrib_w(u16 data)
{
	
	m_ioe_attrib = data;
}


// P_INT_Status1  (different on GPL162xx vs GP95xx)
// 15  KEYIF
// 14  ADCRIF
// 13  TFTUFIF
// 12  TFTEIF
//
// 11  UTIRIF
// 10  SPIIF
//  9  FPIF
//  8
//
//  7  ASIF
//  6
//  5  AUDBIF
//  4  AUDAIF

//  3  USB
//  2  DMA
//  1  EXTBIF
//  0  EXTAIF

u16 Gpl162xxCore::int_status1_r()
{
	
	return 0x0000;// rand();
}

void Gpl162xxCore::int_status1_w(u16 data)
{
	
	//m_int_status1 = data;
}

// P_INT_Status2  (different on GPL162xx vs GP95xx)
// 15  TMDIF
// 14  TMCIF
// 13  TMBIF
// 12  TMAIF
//
// 11  KSIF
// 10  TMBCIF
//  9  TMBBIF
//  8  TMBAIF
//
//  7  SDC2
//  6  SDC1
//  5
//  4  NAND
//
//  3
//  2  SCHIF
//  1  ALMIF
//  0  HMSIF

u16 Gpl162xxCore::int_status2_r()
{
	
	u16 ret = 0;

	if (timebase_irq_flag(0))
		ret |= 0x0100;

	if (timebase_irq_flag(1))
		ret |= 0x0200;

	if (timebase_irq_flag(2))
		ret |= 0x0400;

	return ret;
}

void Gpl162xxCore::int_status2_w(u16 data)
{
	
	// none of this bits are listed as wrieable for GPL95xx or GPL162xx
}

// P_INT_Status3 (different on GPL162xx vs GP95xx)
// 15
// 14
// 13
// 12
//
// 11
// 10
//  9
//  8  UMAA
//
//  7
//  6
//  5
//  4
//
//  3
//  2  BEAT
//  1  ENV
//  0  CHANNEL

u16 Gpl162xxCore::int_status3_r()
{
	
	return 0x0000;
}

void Gpl162xxCore::int_status3_w(u16 data)
{
	
	// bit 2 (SPU Beat Interrupt) is listed as R/W for GPL95, but not GPL162? (verify)
}

void Gpl162xxCore::int_priority_1_w(u16 data)
{
	
	m_int_priority_1 = data;
}

void Gpl162xxCore::int_priority_2_w(u16 data)
{
	
	m_int_priority_2 = data;
}

void Gpl162xxCore::int_priority_3_w(u16 data)
{
	
	m_int_priority_3 = data;
}

void Gpl162xxCore::mint_ctrl_w(u16 data)
{
	
	m_misc_int_ctrl = data;
}

// FIQ
// many sources can be set to FIQ
//
// IRQ0
// Audio Channel A FIFO Empty
// Audio Channel B FIFO Empty
//
// IRQ1
// ?
//
// IRQ2
// External interrupt A
//
// IRQ3
// SPI interrupt
// DMA interrupt
// USB interrupt
//
// IRQ4
// Timer A/B/C/D interrupt
// SPU interrupt
//
// IRQ5
// Key Change interrupt
// PPU interrupt
// NAND Overflow/Underflow interrupt
//
// IRQ6
// Timebase C interrupt
// Scheduler interrupt (RTC)
// SDC2 Controller interrupt
//
// IRQ7
// Timebase A interrupt
// Timebase B interrupt
// Alarm interrupt
// Hour/Minute/Second/Half-Second interrupt (RTC)
// Unexpected memory access interrupt

void Gpl162xxCore::update_interrupts(int state)
{
	if ((timebase_irq_flag(0) && !m_disable_timebase_interrupts) || (timebase_irq_flag(1) && !m_disable_timebase_interrupts))
	{
		m_cpu.set_state_unsynced(UNSP_IRQ7_LINE, ASSERT_LINE);
	}
	else
	{
		m_cpu.set_state_unsynced(UNSP_IRQ7_LINE, CLEAR_LINE);
	}

	if ((timebase_irq_flag(2) && !m_disable_timebase_interrupts) || (m_rtc_int_status & 0x0100))
	{
		m_cpu.set_state_unsynced(UNSP_IRQ6_LINE, ASSERT_LINE);
	}
	else
	{
		m_cpu.set_state_unsynced(UNSP_IRQ6_LINE, CLEAR_LINE);
	}
}


// programmable timers

void Gpl162xxCore::timer_cb(int param)
{
}

// P_Timer*_Ctrl
//
// 15  TM*IF/C
// 14  TM*IE
// 13  TM*EN
// 12
//
// 11  EXTASEL[1]
// 10  EXTASEL[0]
//  9  EXTBSEL[1]
//  8  EXTBSEL[0]
//
//  7
//  6  SRCBSEL[2]
//  5  SRCBSEL[1]
//  4  SRCBSEL[0]
//
//  3  SRCASEL[3]
//  2  SRCASEL[2]
//  1  SRCASEL[1]
//  0  SRCASEL[0]

template<int Timer>
u16 Gpl162xxCore::timer_ctrl_r()
{
	

	// TODO: hack to maintain old behavior until timers are implemented
	if (Timer > 1)
		return rand();

	return m_timer_ctrl[Timer];
}

template<int Timer>
void Gpl162xxCore::timer_ctrl_w(u16 data)
{
	
	m_timer_ctrl[Timer] = data;
}

// P_Timer*_Preload
//
// all bits are the preload value

template<int Timer>
u16 Gpl162xxCore::timer_preload_r()
{
	
	return m_timer_preload[Timer];
}

template<int Timer>
void Gpl162xxCore::timer_preload_w(u16 data)
{
	
	m_timer_preload[Timer] = data;
}

// P_Timer*_Upcount
//
// all bits are the upcount value (read only)

template<int Timer>
u16 Gpl162xxCore::timer_upcount_r()
{
	
	return rand();
}


// P_Timer*_CCP_Ctrl - Timer CCP Control Register (Capture / Compare) (Timers A,B,C only)
//
// 15  CCP*EN[1]
// 14  CCP*EN[0]
// 13
// 12
//
// 11
// 10
//  9  CAP*SEL[1]
//  8  CAP*SEL[0]
//
//  7
//  6
//  5  CMP*SEL[1]
//  4  CMP*SEL[0]
//
//  3
//  2
//  1  PWM*SEL[1]
//  0  PWM*SEL[0]

template<int Timer>
u16 Gpl162xxCore::timer_ccp_ctrl_r()
{
	
	return m_timer_ccp_ctrl[Timer];
}

template<int Timer>
void Gpl162xxCore::timer_ccp_ctrl_w(u16 data)
{
	
	m_timer_ccp_ctrl[Timer] = data;
}


// P_Timer*_CCReg  (Timers A,B,C only)
//
// all bits are the comparison value

template<int Timer>
u16 Gpl162xxCore::timer_cc_reg_r()
{
	
	return m_timer_cc_reg[Timer];
}

template<int Timer>
void Gpl162xxCore::timer_cc_reg_w(u16 data)
{
	
	m_timer_cc_reg[Timer] = data;
}



// CHA (for sound output)

u16 Gpl162xxCore::cha_ctrl_r()
{
	
	return 0xffff;
}

void Gpl162xxCore::cha_ctrl_w(u16 data)
{
	
	m_cha_ctrl = data;
}


// UART

u16 Gpl162xxCore::uart_status_r()
{
	
	return rand(); // lazertag waits on a bit, status flag for something?
}

// RTC

void Gpl162xxCore::scheduler_cb(int param)
{
	if (m_rtc_int_ctrl & 0x0100)
	{
		
		m_rtc_int_status |= 0x0100;
		update_interrupts(1);
	}
}


// P_RTC_Ctrl
//
// 15  RTCEN  - RTC Module Enable
// 14
// 13
// 12

// 11
// 10  ALMEN  - Alarm Function Enable
//  9  HMSEN  - Hour/Minute/Second Update Enable
//  8  SCHEN  - Scheduler Function Enable

//  7
//  6
//  5
//  4

//  3
//  2  SCHSEL[2] - Secheduler Period
//  1  SCHSEL[1]
//  0  SCHSEL[0]
//
// Scheduler Periods are
// 0: 16hz
// 1: 32Hz
// 2: 64Hz
// 3: 128Hz
// 4: 256Hz
// 5: 512Hz
// 6: 1024Hz
// 7: 2048Hz

u16 Gpl162xxCore::rtc_ctrl_r()
{
	// does this return data written, or is it a status flag?
	
	return m_rtc_ctrl;
}

void Gpl162xxCore::rtc_ctrl_w(u16 data)
{
	u16 no_use = data & 0x78f8;
	u8 rtcen = (data & 0x8000) >> 15;
	u8 almen = (data & 0x0400) >> 10;
	u8 hmsen = (data & 0x0200) >> 9;
	u8 schen = (data & 0x0100) >> 8;
	u8 schsel = (data & 0x0007) >> 0;

	
	m_rtc_ctrl = data;

	if (rtcen)
	{
		if (schen)
		{
			//
			attotime period = attotime::from_hz(1 << (4 + schsel));
			m_scheduler->adjust(period, 0, period);
		}
		else
		{
			//
			m_scheduler->adjust(attotime::never());
		}
	}
	else
	{
		//
		m_scheduler->adjust(attotime::never());
	}

}

// P_RTC_INT_Status
//
// 15
// 14
// 13
// 12

// 11
// 10  ALMIEF/C  - Alarm Interrupt Flag/Clear
//  9
//  8  SCHIF/C - Scheduler Interrupt Flag/Clear

//  7
//  6
//  5
//  4

//  3  HRIF/C - Hour Interrupt Flag/Clear
//  2  MINIF/C - Minute Interrupt Flag/Clear
//  1  SECIF/C - Second Interrupt Flag/Clear
//  0  HSECIF/C - Half Second Interrupt Flag/Clear

// value of 7935 is read then written in irq6, nothing happens unless bit 0x0100 was set, which could be some kind of irq source being acked?
u16 Gpl162xxCore::rtc_int_status_r()
{
	
	return m_rtc_int_status;
}

void Gpl162xxCore::rtc_int_status_w(u16 data)
{
	
	m_rtc_int_status &= ~data;
	update_interrupts(1);
}

// P_RTC_INT_Ctrl
//
// 15
// 14
// 13
// 12

// 11
// 10  ALMIEN   - Alarm Interrupt Enable (IRQ7)
//  9
//  8  SCHIEN   - Scheduler Interrupt Enable (IRQ6)

//  7
//  6
//  5
//  4

//  3  HRIEN   - Hour Interrupt Enable
//  2  MINIEN  - Minute Interrupt Enable
//  1  SECIEN  - Second Interrupt Enable
//  0  HSECIEN - Half Second Interrupt Enable

u16 Gpl162xxCore::rtc_int_ctrl_r()
{
	
	return m_rtc_int_ctrl;
}

void Gpl162xxCore::rtc_int_ctrl_w(u16 data)
{
	u16 no_use = data & 0xfaf0;
	u8 almien =  (data & 0x0400) >> 10;
	u8 schien = (data & 0x0100) >> 8;
	u8 hrien = (data & 0x0008) >> 3;
	u8 minien = (data & 0x0004) >> 2;
	u8 secien = (data & 0x0002) >> 1;
	u8 hsecien = (data & 0x0001) >> 0;

	
	m_rtc_int_ctrl = data;
}

// **************************************** 794x SPI *************************************************

// these are related to the accelerometer values on jak_g500 (8-bit signed) and also the SPI reads for bkrankp
u16 Gpl162xxCore::spi_7944_rxdata_r()
{
	
	return rand();
}

u16 Gpl162xxCore::spi_7945_misc_control_reg_r()
{
	
	return rand();// &0x0007;
}

void Gpl162xxCore::spi_7942_txdata_w(u16 data)
{
	
}

// **************************************** 796x unknown *************************************************

void Gpl162xxCore::adc_setup_w(u16 data)
{
	
	m_adc_setup = data;
}

// 7961 and 7962 are used by dressmtv when detecting battery status, adc?
u16 Gpl162xxCore::madc_ctrl_r()
{
	
	return 0xffff; /* return m_madc_ctrl; */
}

void Gpl162xxCore::madc_ctrl_w(u16 data)
{
	
	m_madc_ctrl = data;
}

u16 Gpl162xxCore::madc_data_r()
{
	
	return 0xffff;
}

// **************************************** fallthrough logger etc. *************************************************

u16 Gpl162xxCore::unk_r(offs_t offset)
{
	switch (offset)
	{
	default:
		
		return 0x0000;
	}

	return 0x0000;
}

void Gpl162xxCore::unk_w(offs_t offset, u16 data)
{

	switch (offset)
	{
	default:
		
		break;
	}
}
u16 Gpl162xxCore::nand_data_r()
{
	// straight trampoline for now, but this is talking to the NAND controller, which in turn talks to the NAND
	return nand_data_in();
}

void Gpl162xxCore::nand_data_w(u16 data)
{
	nand_data_out(data & 0xff);
}

// 7998

void Gpl162xxCore::nand_command_w(u16 data)
{
	// documentation indicate this is a 16-bit port, but NAND commands are 8-bit?
	
	nand_command_out(data & 0xff);
}

void Gpl162xxCore::nand_addr_low_w(u16 data)
{
	
	m_nand_addr_low = data;
}

void Gpl162xxCore::nand_addr_high_w(u16 data)
{
	
	m_nand_addr_high = data;

	// documentation indicates that the NAND Interface won't write the address to the NAND, even if only 2 bytes are needed
	// unless this address is written, so presumably all 4 address bytes get sent after the write here

	// m_nand_dma_ctrl is 8600 when this is written, which would indicate the 4th byte isn't used
	// but it's needed? (eg. jak_gtg going to title screen) so maybe doesn't realte to these bytes

	// the timing of these writes on hardware is unclear
	nand_address_out(m_nand_addr_low & 0xff);
	nand_address_out(m_nand_addr_low >> 8);
	nand_address_out(m_nand_addr_high & 0xff);
	nand_address_out(m_nand_addr_high >> 8);
}

// 0x7855 - P_NF_INT_Ctrl ( DMA/INT Control Register )
//
// 15  REQF/C
// 14  DMAEN
// 13  INTEN
// 12
//
// 11  ADR4EN  - enables 4th byte of address
// 10  ADR3EN  - enables 3rd byte of address
//  9  ADR2EN  - enables 2nd byte of address
//  8
//
//  7
//  6
//  5
//  4
//
//  3
//  2
//  1
//  0

void Gpl162xxCore::nand_dma_ctrl_w(u16 data)
{
	
	m_nand_dma_ctrl = data;
}

// 0x7850 - P_NF_Ctrl ( NAND Flash Control Register )
//
// 15  NFBF - Read Back Busy Status (1 = Ready) (RO)
// 14
// 13  OLDTYPE - Old-Type NAND Flash Read Support
// 12
//
// 11
// 10
//  9
//  8
//
//  7
//  6  NFCTRL[6] - tREA[2]
//  5  NFCTRL[5] - tREA[1]
//  4  NFCTRL[4] - tREA[0]
//
//  3  NFCTRL[3] - tWH[1] / tREH[1]
//  2  NFCTRL[2] - tWH[0] / tREH[0]
//  1  NFCTRL[1] - tWP[1]
//  0  NFCTRL[0] - tWP[0]

u16 Gpl162xxCore::nand_7850_status_r()
{
	// TODO, talks to the NAND controller, which talks to the NAND
	// so the status byte here should reflect that
	// 0x8000 = ready
	return m_nand_ctrl | 0x8000;
}

void Gpl162xxCore::nand_7850_w(u16 data)
{
	
	m_nand_ctrl = data;
}

// 0x7856 - P_BCH_Control ( BCH Control Register )
//
// 15
// 14
// 13
// 12
//
// 11
// 10  PAGE_SEL [2] - Read parity page selection (R/W)
//  9  PAGE_SEL [1]
//  8  PAGE_SEL [0]
//
//  7
//  6
//  5  PARITY_CLR - Write pairy clear flag (WO)
//  4  BCH_WR - Page read/write control (1 = write, 0 = read) (R/W)
//
//  3  PAGE_SIZE[1] - NAND flash page size selection. (0 = 528 bytes, 1 = 2112 bytes, 2 = 4224 bytes, 3 = reserved)
//  2  PAGE_SIZE[0]
//  1  BCH_8 - BCH 4/8-bit control register (0 = 4 bits, 1 = 8-bits)
//  0  BCH_EN - BCH Controller enable register. (1 = enable)

void Gpl162xxCore::nand_bch_ctrl_w(u16 data)
{
	
	m_nand_bch_ctrl = data;
}

void Gpl162xxCore::nand_ecc_ctrl_w(u16 data)
{
	
	m_nand_ecc_ctrl = data;
}

void Gpl162xxCore::nand_ecc_lpr_ckl_lb_w(u16 data)
{
	
	m_nand_ecc_lpr_ckl_lb = data;
}

void Gpl162xxCore::nand_ecc_lpr_ckh_lb_w(u16 data)
{
	
	m_nand_ecc_lpr_ckh_lb = data;
}

void Gpl162xxCore::nand_ecc_cpckr_lb_w(u16 data)
{
	
	m_nand_ecc_cpckr_lb = data;
}

// [:maincpu] ':maincpu' (00146D)  jak_tsm
u16 Gpl162xxCore::nand_ecc_err0_lb_r()
{
	return 0x0000;
}

//[:maincpu] ':maincpu' (001490)  jak_tsm
u16 Gpl162xxCore::nand_ecc_err1_lb_r()
{
	return 0x0000;
}

u16 Gpl162xxCore::spi_rxstatus_r()
{
	return 0x0007;
}

u16 Gpl162xxCore::efuse2_r()
{
	
	return 0x0300;
}

// it is not clear if these are just different revisions of a standard internal boot ROM, assuming so for now
// they appear to be for the 'A' models at least, because there are hardcoded addresses which would be above
// the RAM area of the 'B' models
//
// not currently used by the emulation as we overwrite this with our own bootstrap logic

u16 Gpl162xxCore::cs_space_r(offs_t offset)
{
	return m_cs_space->read_word(offset);
}

void Gpl162xxCore::cs_space_w(offs_t offset, u16 data)
{
	m_cs_space->write_word(offset, data);
}
u16 Gpl162xxCore::cs_bank_space_r(offs_t offset)
{
	int bank = m_membankswitch_7810 & 0x3f;
	int realoffset = offset + (bank * 0x200000) - m_csbase;

	if (realoffset < 0)
	{
		
		return 0;
	}

	return m_cs_space->read_word(realoffset);
}

void Gpl162xxCore::cs_bank_space_w(offs_t offset, u16 data)
{
	int bank = m_membankswitch_7810 & 0x3f;
	int realoffset = offset + (bank * 0x200000) - m_csbase;

	if (realoffset < 0)
	{
		
		return;
	}

	m_cs_space->write_word(realoffset, data);
}



u16 Gpl162xxCore::internalrom_lower32_r(offs_t offset)
{
	if (m_boot_mode == 0)
	{
		return m_internal[offset & 0x7fff];
	}
	else
	{
		if (!m_cs_space)
			return 0x0000;

		u16 val = m_cs_space->read_word(offset+0x8000);
		return val;
	}
}

u16 Gpl162xxCore::cs_space_boot_mirror_r(offs_t offset)
{
	if (!m_cs_space)
		return 0x0000;

	u16 val = m_cs_space->read_word(offset+0x8000);
	return val;
}



int Gpl162xxCore::irq_vector_cb(int irqline)
{
	if (irqline == UNSP_IRQ4_LINE)
		m_cpu.set_state_unsynced(UNSP_IRQ4_LINE, CLEAR_LINE);
	return 0;
}

void Gpl162xxCore::audioirq_w(int state)
{
}

void Gpl162xxCore::videoirq_w(int state)
{
	m_cpu.set_state_unsynced(UNSP_IRQ5_LINE, state);
}

u16 Gpl162xxCore::read_space(offs_t offset)
{
	if (offset < m_csbase)
		return read_word(offset);
	return m_cs_space->read_word(offset - m_csbase);
}

void Gpl162xxCore::write_space(offs_t offset, u16 data)
{
	if (offset < m_csbase)
		write_word(offset, data);
	else
		m_cs_space->write_word(offset - m_csbase, data);
}

void Gpl162xxCore::dma_complete(int state)
{
	dma_complete_cb(state);
}
