// handlers ported from MAME generalplus_gpl951xx_soc.cpp (BSD-3-Clause, David Haywood)
void Gpl951Core::recieve_spi_fifo_data(u8 data)
{
	if (m_bytes_in_spifc_rx_fifo < (16 * 2))
	{
		m_spifc_rx_fifo[m_bytes_in_spifc_rx_fifo] = data;
		m_bytes_in_spifc_rx_fifo++;
	}
}

u8 Gpl951Core::get_byte_from_rx_fifo()
{
	u8 ret = m_spifc_rx_fifo[0];
	if (m_bytes_in_spifc_rx_fifo > 0)
	{

		for (int i = 1; i < (16 * 2); i++)
		{
			m_spifc_rx_fifo[i - 1] = m_spifc_rx_fifo[i];
		}
		m_spifc_rx_fifo[(16 * 2) - 1] = 0;
		m_bytes_in_spifc_rx_fifo--;
	}
	return ret;
}

// this provides hardware accelerated SPI support, handling much of the underlying SPI
// access, allowing 'easier' use of the device, as well as allowing it to run in XIP
// mode (eXecute In Place) so the CPU can see it as a flat space

// P_SPIFC_Ctrl1

// 15  tx_done
// 14  rx_empty
// 13
// 12
//
// 11
// 10
// 9   ig_clk
// 8   manual
//
// 7   cmio[1]
// 6   cmio[0]
// 5   amio[1]
// 4   amio[0]
//
// 3   mio[1]
// 2   mio[0]
// 1
// 0   back2idle

u16 Gpl951Core::spifc_ctrl_r()
{
	
	u16 ret = m_spifc_ctrl;

	ret |= 0x8000; // tx_done
	if (m_bytes_in_spifc_rx_fifo == 0) ret |= 0x4000; // rx_empty
	ret |= 0x0001; // back2idle

	return ret;
}

void Gpl951Core::spifc_ctrl_w(u16 data)
{
	
	m_spifc_ctrl = data;
}

// P_SPIFC_CMD
//
// 15
// 14
// 13  one_cmd
// 12
//
// 11
// 10
//  9  cmd_only
//  8  wo_cmd
//
//  7  wpif_cmd[7]
//  6  wpif_cmd[6]
//  5  wpif_cmd[5]
//  4  wpif_cmd[4]
//
//  3  wpif_cmd[3]
//  2  wpif_cmd[2]
//  1  wpif_cmd[1]
//  0  wpif_cmd[0]

u16 Gpl951Core::spifc_cmd_r()
{
	
	return m_spifc_cmd;
}

void Gpl951Core::spifc_cmd_w(u16 data)
{
	

	m_spifc_cmd = data;

	spi_reset();
	spi_out_cmd(data & 0x00ff);

	if (!(m_spifc_para & 0x1000))
	{
		spi_out((m_spifc_addr >> 16) & 0xff);
		spi_out((m_spifc_addr >> 8) & 0xff);
		spi_out((m_spifc_addr >> 0) & 0xff);

	}

	// how does byte count work? the FIFO is apparently in words
	for (int i = 0; i < m_spifc_rx_bc; i++)
	{
		// clock it this many times to push data into the fifo?
		spi_out(0x00);
	}

}

// P_SPIFC_PARA
//
// 15
// 14  wo_enha
// 13  to_addr
// 12  wo_addr
//
// 11  dummy_ck_cnt[3]
// 10  dummy_ck_cnt[2]
// 9   dummy_ck_cnt[1]
// 8   dummy_ck_cnt[0]
//
// 7   enhan_by[7]
// 6   enhan_by[6]
// 5   enhan_by[5]
// 4   enhan_by[4]
//
// 3   enhan_by[4]
// 2   enhan_by[3]
// 1   enhan_by[2]
// 0   enhan_by[1]


u16 Gpl951Core::spifc_para_r()
{
	
	return m_spifc_para;
}

void Gpl951Core::spifc_para_w(u16 data)
{
	
	m_spifc_para = data;
}

// P_SPIFC_ADDRL
// 15-0 low 16 bits of SPIF Address

u16 Gpl951Core::spifc_addrl_r()
{
	
	return m_spifc_addr & 0x0000ffff;
}

void Gpl951Core::spifc_addrl_w(u16 data)
{
	
	m_spifc_addr = (m_spifc_addr & 0xffff0000) | data;
}

// P_SPIFC_ADDRH
// 15-0  high 16 bits of SPIF Address

u16 Gpl951Core::spifc_addrh_r()
{
	
	return (m_spifc_addr & 0xffff0000) >> 16;
}

void Gpl951Core::spifc_addrh_w(u16 data)
{
	
	m_spifc_addr = (m_spifc_addr & 0x0000ffff) | (data << 16);
}

u16 Gpl951Core::spifc_txdat_r()
{
	
	return 0xffff;
}

void Gpl951Core::spifc_txdat_w(u16 data)
{
	
	spi_out(data & 0xff);
}

// P_SPIFC_RX_Data
//
// 15-0  2 bytes of RX_Data
//
// write the register once before reading the register once

u16 Gpl951Core::spifc_rxdat_r()
{
	

	/*
	for bfpacman

	after auto command bite spifc_cmd_w is set to 9f (ident)

	at 672 R1 will be xx00 (results from device)
	R2 will be xxxx (results from device)

	continuing to 232 it restores these values and it checks if R1 is 0000 (it shouldn't be)

	at 237 it gets a value of C814 from 0d69 (RAM) and puts it in R1
	at 239 is compares R1 with R2
	*/

	return m_spifc_rx_read_latch;
}


void Gpl951Core::spifc_rxdat_w(u16 data)
{
	// write here to latch a word from the fifo into the read register
	u16 word = get_byte_from_rx_fifo();
	word = (get_byte_from_rx_fifo() << 8) | word;

	m_spifc_rx_read_latch = word;
}


// P_SPIFC_TX_BC - SPIFC TX byte count register
//
// 15-9 unused
// 8-0  9-bit byte count (TX_BC)

u16 Gpl951Core::spifc_tx_bc_r()
{
	
	return m_spifc_tx_bc;
}

void Gpl951Core::spifc_tx_bc_w(u16 data)
{
	
	m_spifc_tx_bc = data;
}

// P_SPIFC_RX_BC - SPIFC RX byte count register
//
// 15-9 unused
// 8-0  9-bit byte count (RX_BC)

u16 Gpl951Core::spifc_rx_bc_r()
{
	
	return m_spifc_rx_bc;
}

void Gpl951Core::spifc_rx_bc_w(u16 data)
{
	
	m_spifc_rx_bc = data;
}

u16 Gpl951Core::spifc_timing_r()
{
	
	return m_spifc_timing;
}

void Gpl951Core::spifc_timing_w(u16 data)
{
	
	m_spifc_timing = data;
}

// P_SPIFC_Ctrl2
//
// 15-8  Ear[7:0] - Extend address range from 24 bits to 32-bits
// 7-1
// 0     en - SPF flash controller enable

u16 Gpl951Core::spifc_ctrl2_r()
{
	
	return m_spifc_ctrl2;
}

void Gpl951Core::spifc_ctrl2_w(u16 data)
{
	
	m_spifc_ctrl2 = data;
}

u16 Gpl951Core::spi_improve_r()
{
	
	return 0x0000;
}

void Gpl951Core::spi_improve_w(u16 data)
{
	
}

// Other bits

void Gpl951Core::pm_ctrl_w(u16 data)
{
	
}

u16 Gpl951Core::pllsel_r()
{
	
	return m_pllchange;
}

void Gpl951Core::pllsel_w(u16 data)
{
	// very similar to pllchange on GPL162xx, but with extra SPI bits
	
	m_pllchange = data;
}

u16 Gpl951Core::byte_swap_r()
{
	return m_byteswap;
}

void Gpl951Core::byte_swap_w(u16 data)
{
	// words read from ROM are written here during the checksum routine in RAM, and must
	// be shifted for the checksum to pass.
	m_byteswap = ((data & 0xff00) >> 8) | ((data & 0x00ff) << 8);
}





// Timers





// Other timers are more generic
// although timers e and f have different behavior depending on SoC

template<int Timer>
u16 Gpl951Core::timer_ctrl_r()
{
	
	return m_timer_ctrl[Timer];
}

template<int Timer>
void Gpl951Core::timer_ctrl_w(u16 data)
{
	char const *const SRCB_NAME[8] =
	{
		"2048Hz",
		"1024Hz",
		"256Hz",
		"TimeBaseB",
		"TimeBaseA",
		"(logic low)",
		"(logic high)",
		"EXT1 with pre-scaler"
	};

	char const *const SRCA_NAME[16] =
	{
		"SYSCLK/2",
		"SYSCLK/256",
		"32768Hz",
		"8192Hz",
		"4096Hz",
		"(logic high)",
		"Timer Overflow",
		"EXT0 with pre-scaler",
		"(logic low)",
		"9: reserved",
		"a: reserved",
		"b: reserved",
		"c: reserved",
		"d: reserved",
		"e: reserved",
		"f: reserved"
	};

	u8 tmxif_clear = (data & 0x8000) >> 15;
	u8 tmxie = (data & 0x4000) >> 14;
	u8 tmxen = (data & 0x2000) >> 13;
	u8 ext0sel = (data & 0x0c00) >> 10;
	u8 ext1sel = (data & 0x0300) >> 8;
	u8 srcbsel = (data & 0x0070) >> 4;
	u8 srcasel = (data & 0x000f) >> 0;

	

	if (data & 0x8000)
	{
		m_timer_ctrl[Timer] &= 0x7fff;
	}

	if ((data & 0x2000) != (m_timer_ctrl[Timer] & 0x2000))
	{
		if (data & 0x2000)
		{
			// currently hardcoded, need to figure out how to use the preload and timer source registers properly!
			if (Timer == 6)
				m_timer[Timer]->adjust(attotime::zero(), 0, attotime::from_hz(8000));
			else if (Timer == 3)
				m_timer[Timer]->adjust(attotime::zero(), 0, attotime::from_hz(20000));
			else
				m_timer[Timer]->adjust(attotime::zero(), 0, attotime::from_hz(1000));
		}
		else
		{
			m_timer[Timer]->adjust(attotime::never());
		}

	}

	m_timer_ctrl[Timer] = (m_timer_ctrl[Timer] & 0x8000) | (data & 0x7fff);
	update_interrupts(1);
}

template<int Timer>
u16 Gpl951Core::timer_preload_r()
{
	
	return m_timer_preload[Timer];
}

template<int Timer>
void Gpl951Core::timer_preload_w(u16 data)
{
	
	m_timer_preload[Timer] = data;
}



// P_Timerx_CCPB_Ctrl
//
// 15  CCPBxEN[1]  - 00 = CCPB Mode Disabled, 01 = Capture Enabled, 10 = Comparison Enabled, 11 = PWM/BAM Enabled
// 14  CCPBxEN[0]
// 13
// 12
//
// 11
// 10
//  9  CAPxSEL[1]  - 00 = every falling, 01 = every rising, 10/11 = reserved
//  8  CAPxSEL[0]
//
//  7
//  6
//  5  CMPxSEL[1] - 00 = high pulse on CCPB, 01 = low pulse on CCPB, 10 = unaffected on CCPB, 11 = reserved
//  4  CMPxSEL[0]
//
//  3
//  2
//  1  PWMxSEL[1] - 00 = PWM mode/NRO output, 01 = PWM mode/NRZ output, 10 = BAM mode/NRO output, 11 = BAM mode/NRZ output
//  0  PWMxSEL[0]

template<int Timer>
void Gpl951Core::timer_ccpb_ctrl_w(u16 data)
{
	
}

template<int Timer>
u16 Gpl951Core::timer_upcount_r()
{
	
	return rand();
}



u16 Gpl951Core::i2c_ctrl_r()
{
	
	return rand();
}

u16 Gpl951Core::i2c_status_r()
{
	
	return rand();
}

// TFT

// 15
// 14
// 13
// 12
//
// 11
// 10
//  9
//  8
//
//  7  MEM_STATE[3] or HSTS[1] - horizontal status register
//  6  MEM_STATE[2] or HSTS[0]
//  5  MEM_STATE[1] or VSTS[1] - vertical status register
//  4  MEM_STATE[0] or VSTS[0]
//
//  3
//  2
//  1  Frame Interrupt Occured
//  0

u16 Gpl951Core::tft_status_r()
{
	u16 ret = 0x0000;
	
	return ret;
}

// 15  VSU
// 14  INLA
// 13  BEN
// 12  HCMP
//
// 11  DINV
// 10  CINV
//  9  HINV
//  8  VINV
//
//  7  MODE[3]
//  6  MODE[2]
//  5  MODE[1]
//  4  MODE[0]
//
//  3  CLK_SEL[2]
//  2  CLK_SEL[1]
//  1  CLK_SEL[0]
//  0  TFTEN

void Gpl951Core::tft_ctrl_w(u16 data)
{
	u8 vsu   = (data & 0x8000) >> 15;
	u8 inla  = (data & 0x4000) >> 14;
	u8 ben   = (data & 0x2000) >> 13;
	u8 hcmp  = (data & 0x1000) >> 12;
	u8 dinv  = (data & 0x0800) >> 11;
	u8 cinv  = (data & 0x0400) >> 10;
	u8 hinv  = (data & 0x0200) >> 9;
	u8 vinv  = (data & 0x0100) >> 8;
	u8 mode  = (data & 0x00f0) >> 4;
	u8 clk_s = (data & 0x000e) >> 1;
	u8 tften = (data & 0x0001) >> 0;

	static char const *const modenames[16] =
	{
		"0: UPS051 mode",
		"1: UPS052 mode",
		"2: CCIR656 mode",
		"3: PARALLEL mode",
		"4: TOCN mode",
		"5: reserved",
		"6: reserved",
		"7: reserved",
		"8: I80 CMD write",
		"9: I80 CMD read",
		"a: I80 DATA write",
		"b: I80 DATA read",
		"c: I80 Continuous mode",
		"d: I80 Single mode",
		"e: reserved",
		"f: reserved"
	};

	

	if (tften)
	{
		if (mode == 0x8)
			i80_cmd_out(m_memmode_wcmd);
		else if (mode == 0x0a)
			i80_data_out(m_memmode_wcmd);
	}
}

void Gpl951Core::tft_memmode_wcmd_w(u16 data)
{
	
	m_memmode_wcmd = data;
}


u16 Gpl951Core::tft_rgb_ctrl_r()
{
	
	return m_tft_rgb_ctrl;
}

void Gpl951Core::tft_rgb_ctrl_w(u16 data)
{
	
	m_tft_rgb_ctrl = data;
}

void Gpl951Core::tft_v_width_w(u16 data)
{
	
}

void Gpl951Core::tft_vsync_setup_w(u16 data)
{
	
}

void Gpl951Core::tft_v_start_w(u16 data)
{
	
}

void Gpl951Core::tft_v_end_w(u16 data)
{
	
}

void Gpl951Core::tft_h_width_w(u16 data)
{
	
}

void Gpl951Core::tft_hsync_setup_w(u16 data)
{
	
}

void Gpl951Core::tft_h_start_w(u16 data)
{
	
}

void Gpl951Core::tft_h_end_w(u16 data)
{
	
}

void Gpl951Core::tft_v_show_start_w(u16 data)
{
	
}

void Gpl951Core::tft_v_show_end_w(u16 data)
{
	
}

void Gpl951Core::tft_h_show_start_w(u16 data)
{
	
}

void Gpl951Core::tft_h_show_end_w(u16 data)
{
	
}

void Gpl951Core::free_height_w(u16 data)
{
	
}

void Gpl951Core::free_width_w(u16 data)
{
	
}

//

u16 Gpl951Core::spi_bank_r()
{
	
	return m_spi_bank;
}

void Gpl951Core::spi_bank_w(u16 data)
{
	
	m_spi_bank = data;
}

template<int Timer>
void Gpl951Core::timer_cb(int param)
{
	m_timer_ctrl[Timer] |= 0x8000;
	update_interrupts(1);

	if (Timer == 6)
		m_chx.process_cha_fifo(); // is cha hardwired to timerg overflow?
	else if (Timer == 7)
		m_chx.process_chb_fifo(); // is cha hardwired to timerh overflow?
}


void Gpl951Core::adc_timer_cb(int param)
{
	m_madc_ctrl |= 0x8000;
	m_madc_ctrl |= 0x0080; // apparently always set after the first ADC operation, no way to clear?
}


// P_MADC_Ctrl
//
// 15  ADCRIF/C  - ADC Ready Interrupt Flag/Clear
// 14  ADCRIEN   - ADC Interrupt Enable
// 13
// 12
//
// 11
// 10
//  9
//  8
//
//  7  CNVRDY    - Conversion Ready
//  6  STRCNV    - Manual Start Conversion
//  5  MIASE     - ADC Conversion Error Flag 1
//  4  ASIME     - ADC Conversion Error Flag 2
//
//  3  CHSEL     - ADC Channel Selection (manual mode)
//  2
//  1
//  0

u16 Gpl951Core::madc_ctrl_r()
{
	
	return m_madc_ctrl;
}

void Gpl951Core::madc_ctrl_w(u16 data)
{
	char const *const CHANNEL_NAME[8] =
	{
		"LINEIN 0",
		"LINEIN 1",
		"LINEIN 2",
		"LINEIN 3",
		"LINEIN 4",
		"LINEIN 5",
		"1.2V bandgap",
		"LDOV50"
	};

	

	if (data & 0x8000)
	{
		// clear IRQ flag
		data = data &~ 0x8000;
	}

	if (data & 0x0010)
	{
		// clear Error Flag 2
		data = data &~ 0x0010;
	}

	if (data & 0x0020)
	{
		// clear Error Flag 1
		data = data &~ 0x0020;
	}

	if (data & 0x0040) // Manual start
	{
		data = data & ~0x0040;
		data = data & ~0x0080;

		u8 channel = data & 0x0007;

		

		if (channel < 6)
		{
			m_madc_data = adc_in(channel);
		}
		else
		{
			m_madc_data = 0x0000;
		}

		m_adc_timer->adjust(attotime(0, 10 * (ATTOSECONDS_PER_SECOND / 1000000)));
	}

	m_madc_ctrl = data;
}

u16 Gpl951Core::madc_data_r()
{
	
	return m_madc_data;
}


template<int Port>
u16 Gpl951Core::io_data_r()
{
	
	return port_in(Port);
}

template<int Port>
void Gpl951Core::io_data_w(u16 data)
{
	
	port_out(Port, data);
}

template<int Port>
u16 Gpl951Core::io_buffer_r()
{
	
	return port_in(Port);
}

template<int Port>
void Gpl951Core::io_buffer_w(u16 data)
{
	
	port_out(Port, data);
}

template<int Port>
u16 Gpl951Core::io_dir_r()
{
	
	return m_io_dir[Port];
}

template<int Port>
void Gpl951Core::io_dir_w(u16 data)
{
	
	m_io_dir[Port] = data;
}

template<int Port>
u16 Gpl951Core::io_attrib_r()
{
	
	return m_io_attrib[Port];
}

template<int Port>
void Gpl951Core::io_attrib_w(u16 data)
{
	
	m_io_attrib[Port] = data;
}

template<int Port>
u16 Gpl951Core::io_drv_r()
{
	
	return 0xffff;
}

template<int Port>
void Gpl951Core::io_drv_w(u16 data)
{
	
}

template<int Port>
u16 Gpl951Core::io_mux_r()
{
	
	return 0xffff;
}

template<int Port>
void Gpl951Core::io_mux_w(u16 data)
{
	
}

template<int Port>
u16 Gpl951Core::io_latch_r()
{
	
	return 0xffff;
}

template<int Port>
void Gpl951Core::io_latch_w(u16 data)
{
	
}

template<int Port>
u16 Gpl951Core::io_keyen_r()
{
	
	return 0xffff;
}

template<int Port>
void Gpl951Core::io_keyen_w(u16 data)
{
	
}

// Misc

u16 Gpl951Core::sys_ctrl_r()
{
	
	return m_sys_ctrl;
}

void Gpl951Core::sys_ctrl_w(u16 data)
{
	
	m_sys_ctrl = data;
}

void Gpl951Core::clock_ctrl_w(u16 data)
{
	
	m_clock_ctrl = data;
}

u16 Gpl951Core::clk_ctrl0_r()
{
	
	return 0x0000;
}

void Gpl951Core::clk_ctrl0_w(u16 data)
{
	
}

void Gpl951Core::watchdog_ctrl_w(u16 data)
{
	
}

u16 Gpl951Core::power_state_r()
{
	
	return 0x0002;
}

u16 Gpl951Core::pllclkwait_r()
{
	
	return 0x0000;
}

void Gpl951Core::pllclkwait_w(u16 data)
{
	
}

// sets bit 0x0002 then expects it to have cleared
u16 Gpl951Core::cache_ctrl_r()
{
	
	return m_cache_ctrl & ~ 0x0002;
}

void Gpl951Core::cache_ctrl_w(u16 data)
{
	
	m_cache_ctrl = data;
}

// P_INT_Status1
//
// 15  KEYIF  - Key-change interrupt status (R/W)
// 14  ADCRIF - Line ADC Converstion Ready interrupt status
// 13  PwrONIF - Power on key change event status
// 12  OTIF    - Over temperature event status
//
// 11  UARTIF  - UART Interrupt status
// 10  SPI0    - Serial Peripheral Interface 0 interrupt status
//  9  FPIF    - FP Interrupt Status (LCD)
//  8
//
//  7  ASIF    - Line ADC Auto Sample Mode FIFO full interrupt status
//  6
//  5  AUDBIF  - Audio Channel B (CHB) FIFO Empty Interrupt status
//  4  AUDAIF  - Audio Channel A (CHA) FIFO Empty Interrupt status
//
//  3  USB     - USB interrupt status
//  2  DMA     - System DMA interrupt status
//  1  EXT1IF  - External interrupt 1 status (R/W)
//  0  EXT0IF  - External interrupt 0 status (R/W)

u16 Gpl951Core::int_status1_r()
{
	u16 ret = 0x0000;
	

	if (m_chx.is_cha_fifo_empty_irq())
		ret |= 0x0010;

	if (m_chx.is_chb_fifo_empty_irq())
		ret |= 0x0020;

	return ret;
}

void Gpl951Core::int_status1_w(u16 data)
{
	
	//m_int_status1 = data;
}

// P_INT_Status2
//
// 15
// 14
// 13
// 12
//
// 11  KSIF - Key-Scan Interrupt status
// 10  TMBCIF - TimebaseC interrupt status
//  9  TMBBIF - TimebaseB interrupt status
//  8  TMBAIF - TimebaseA interrupt status
//
//  7  CTSIF  - CTS Controller interrupt status
//  6
//  5  I2C - I2C Controller interrupt status
//  4  NAND - NAND Flash Controller interrupt status
//
//  3
//  2 SPI1    - Serial Peripheral Interface 1 interrupt status
//  1 RTC     - RTC Controller interrupt status
//  0 ADCFF   - MIC ADC Auto Sample Mode FIFO Full interrupt status

u16 Gpl951Core::int_status2_r()
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

void Gpl951Core::int_status2_w(u16 data)
{
	
	// none of this bits are listed as writeable for GPL95xx or GPL162xx
}

// P_INT_Status3
//
// 15  TMHIF - TimerH Interrupt status
// 14  TMGIF - TimerG Interrupt status
// 13  TMFIF - TimerF Interrupt status
// 12  TMEIF - TimerE Interrupt status
//
// 11  TMDIF - TimerD Interrupt status
// 10  TMCIF - TimerC Interrupt status
//  9  TMBIF - TimerB Interrupt status
//  8  TMAIF - TimerA Interrupt status
//
//  7  UNEXP - unexpected access Interrupt status
//  6
//  5
//  4
//
//  3
//  2 BEAT - SPU beat interrupt status (R/W)
//  1
//  0

u16 Gpl951Core::int_status3_r()
{
	u16 ret = 0;
	

	if (m_timer_ctrl[7] & 0x8000)
		ret |= 0x8000;

	if (m_timer_ctrl[6] & 0x8000)
		ret |= 0x4000;

	if (m_timer_ctrl[1] & 0x8000)
		ret |= 0x0200;

	if (m_timer_ctrl[0] & 0x8000)
		ret |= 0x0100;

	return ret;
}

void Gpl951Core::int_status3_w(u16 data)
{
	
	// bit 2 (SPU Beat Interrupt) is listed as R/W for GPL95, but not GPL162? (verify)
}

u16 Gpl951Core::int_priority_1_r()
{
	
	return m_int_priority_1;
}

void Gpl951Core::int_priority_1_w(u16 data)
{
	
	m_int_priority_1 = data;
}

u16 Gpl951Core::int_priority_2_r()
{
	
	return m_int_priority_2;
}

void Gpl951Core::int_priority_2_w(u16 data)
{
	
	m_int_priority_2 = data;
}

u16 Gpl951Core::int_priority_3_r()
{
	
	return m_int_priority_3;
}

void Gpl951Core::int_priority_3_w(u16 data)
{
	
	m_int_priority_3 = data;
}

void Gpl951Core::mint_ctrl_w(u16 data)
{
	
	m_misc_int_ctrl = data;
}


u16 Gpl951Core::spi_direct_r(offs_t offset)
{
	// The GPL951xx chips can see a bank of SPI memory as a flat space
	// as they will automatically generate SPI signals for random memory
	// access on demand
	//
	// Emulating it this way is impractical for emulation (DASM wouldn't
	// work) so we just present it as a flat space directly.
	if (!m_spiregion)
		return 0x0000;

	u16 ret = (m_spiregion[((offset * 2) + 0) & (m_spisize-1)]) | (m_spiregion[((offset * 2) + 1) & (m_spisize-1)] << 8);
	return ret;
}

u16 Gpl951Core::spi_direct_bank_r(offs_t offset)
{
	if (!m_spiregion)
		return 0x0000;

	offset += 0x1f7000;
	offset += (m_spi_bank & 0x3f) * 0x200000;

	u16 ret = (m_spiregion[((offset * 2) + 0) & (m_spisize-1)]) | (m_spiregion[((offset * 2) + 1) & (m_spisize-1)] << 8);
	return ret;

}


void Gpl951Core::update_interrupts(int state)
{
	if (timebase_irq_flag(0) || timebase_irq_flag(1))
	{
		m_cpu.set_state_unsynced(UNSP_IRQ7_LINE, ASSERT_LINE);
	}
	else
	{
		m_cpu.set_state_unsynced(UNSP_IRQ7_LINE, CLEAR_LINE);
	}

	if (timebase_irq_flag(2))
	{
		m_cpu.set_state_unsynced(UNSP_IRQ6_LINE, ASSERT_LINE);
	}
	else
	{
		m_cpu.set_state_unsynced(UNSP_IRQ6_LINE, CLEAR_LINE);
	}

	if (((m_timer_ctrl[7] & 0x8000) && (m_timer_ctrl[7] & 0x4000)) ||
		((m_timer_ctrl[6] & 0x8000) && (m_timer_ctrl[6] & 0x4000)) ||
		((m_timer_ctrl[5] & 0x8000) && (m_timer_ctrl[5] & 0x4000)) ||
		((m_timer_ctrl[4] & 0x8000) && (m_timer_ctrl[4] & 0x4000)) ||
		((m_timer_ctrl[3] & 0x8000) && (m_timer_ctrl[3] & 0x4000)) ||
		((m_timer_ctrl[2] & 0x8000) && (m_timer_ctrl[2] & 0x4000)) ||
		((m_timer_ctrl[1] & 0x8000) && (m_timer_ctrl[1] & 0x4000)) ||
		((m_timer_ctrl[0] & 0x8000) && (m_timer_ctrl[0] & 0x4000))
		)
	{
		m_cpu.set_state_unsynced(UNSP_IRQ4_LINE, ASSERT_LINE);
	}
	else
	{
		m_cpu.set_state_unsynced(UNSP_IRQ4_LINE, CLEAR_LINE);
	}

	if ((m_chx.is_cha_fifo_empty_irq()) || (m_chx.is_chb_fifo_empty_irq()))
	{
		m_cpu.set_state_unsynced(UNSP_IRQ1_LINE, ASSERT_LINE);
	}
	else
	{
		m_cpu.set_state_unsynced(UNSP_IRQ1_LINE, CLEAR_LINE);
	}

}

