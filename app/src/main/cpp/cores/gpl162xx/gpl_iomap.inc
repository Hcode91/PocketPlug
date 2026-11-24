// generated from MAME generalplus_gpl162xx_soc.cpp / _b_soc.cpp address maps (BSD-3-Clause)
u16 Gpl162xxCore::io_read_16218b(u32 a)
{
	if (a == 0x7af0) return byteswap_r();
	if (a == 0x7943) return spi_rxstatus_r();
	if ((a >= 0x7e00 && a <= 0x7fff)) return m_audio.audio_phase_r(a - 0x7e00);
	if ((a >= 0x7c00 && a <= 0x7dff)) return m_audio.audio_r(a - 0x7c00);
	if ((a >= 0x7b80 && a <= 0x7bbf)) return m_audio.control_r(a - 0x7b80);
	if (a == 0x7ae2) return efuse2_r();
	if (a == 0x7abf) return system_dma_status_r();
	if (a == 0x7abe) return system_dma_memtype_r();
	if ((a >= 0x7a98 && a <= 0x7a9f)) return system_dma_params_channel3_r(a - 0x7a98);
	if ((a >= 0x7a90 && a <= 0x7a97)) return system_dma_params_channel2_r(a - 0x7a90);
	if ((a >= 0x7a88 && a <= 0x7a8f)) return system_dma_params_channel1_r(a - 0x7a88);
	if ((a >= 0x7a80 && a <= 0x7a87)) return system_dma_params_channel0_r(a - 0x7a80);
	if (a == 0x7a54) return usb_7a54_r();
	if (a == 0x7a46) return usb_7a46_r();
	if (a == 0x7a3a) return usb_7a3a_r();
	if (a == 0x7a39) return usb_7a39_r();
	if (a == 0x7a37) return usb_7a37_r();
	if (a == 0x7a35) return usb_7a35_r();
	if (a == 0x7962) return madc_data_r();
	if (a == 0x7961) return madc_ctrl_r();
	if (a == 0x7945) return spi_7945_misc_control_reg_r();
	if (a == 0x7944) return spi_7944_rxdata_r();
	if (a == 0x7936) return rtc_int_ctrl_r();
	if (a == 0x7935) return rtc_int_status_r();
	if (a == 0x7934) return rtc_ctrl_r();
	if (a == 0x7904) return uart_status_r();
	if (a == 0x78fb) return dac_pga_r();
	if (a == 0x78f0) return cha_ctrl_r();
	if (a == 0x78ec) return timer_upcount_r<5>();
	if (a == 0x78ea) return timer_preload_r<5>();
	if (a == 0x78e8) return timer_ctrl_r<5>();
	if (a == 0x78e4) return timer_upcount_r<4>();
	if (a == 0x78e2) return timer_preload_r<4>();
	if (a == 0x78e0) return timer_ctrl_r<4>();
	if (a == 0x78dc) return timer_upcount_r<3>();
	if (a == 0x78da) return timer_preload_r<3>();
	if (a == 0x78d8) return timer_ctrl_r<3>();
	if (a == 0x78d4) return timer_upcount_r<2>();
	if (a == 0x78d3) return timer_cc_reg_r<2>();
	if (a == 0x78d2) return timer_preload_r<2>();
	if (a == 0x78d1) return timer_ccp_ctrl_r<2>();
	if (a == 0x78d0) return timer_ctrl_r<2>();
	if (a == 0x78cc) return timer_upcount_r<1>();
	if (a == 0x78cb) return timer_cc_reg_r<1>();
	if (a == 0x78ca) return timer_preload_r<1>();
	if (a == 0x78c9) return timer_ccp_ctrl_r<1>();
	if (a == 0x78c8) return timer_ctrl_r<1>();
	if (a == 0x78c4) return timer_upcount_r<0>();
	if (a == 0x78c3) return timer_cc_reg_r<0>();
	if (a == 0x78c2) return timer_preload_r<0>();
	if (a == 0x78c1) return timer_ccp_ctrl_r<0>();
	if (a == 0x78c0) return timer_ctrl_r<0>();
	if (a == 0x78b2) return timebasec_ctrl_r();
	if (a == 0x78b1) return timebaseb_ctrl_r();
	if (a == 0x78b0) return timebasea_ctrl_r();
	if (a == 0x78a3) return int_status3_r();
	if (a == 0x78a1) return int_status2_r();
	if (a == 0x78a0) return int_status1_r();
	if (a == 0x7883) return ioe_attrib_r();
	if (a == 0x7882) return ioe_dir_r();
	if (a == 0x7881) return ioe_buffer_r();
	if (a == 0x787c) return iod_drv_r();
	if (a == 0x787b) return iod_attib_r();
	if (a == 0x787a) return iod_dir_r();
	if (a == 0x7879) return iod_buffer_r();
	if (a == 0x7878) return iod_data_r();
	if (a == 0x7873) return ioc_attrib_r();
	if (a == 0x7872) return ioc_dir_r();
	if (a == 0x7871) return ioc_buffer_r();
	if (a == 0x7870) return ioc_data_r();
	if (a == 0x786b) return iob_attrib_r();
	if (a == 0x786a) return iob_dir_r();
	if (a == 0x7869) return iob_buffer_r();
	if (a == 0x7868) return iob_data_r();
	if (a == 0x7863) return ioa_attrib_r();
	if (a == 0x7862) return ioa_dir_r();
	if (a == 0x7861) return ioa_buffer_r();
	if (a == 0x7860) return ioa_data_r();
	if (a == 0x782d) return raw_war_r();
	if (a == 0x7819) return cache_ctrl_r();
	if (a == 0x7818) return pllclkwait_r();
	if (a == 0x7810) return membankswitch_7810_r();
	if (a == 0x780f) return power_state_r();
	if (a == 0x7804) return clk_ctrl0_r();
	if (a == 0x7803) return sys_ctrl_r();
	if ((a >= 0x7400 && a <= 0x77ff)) return m_video.spriteram_r(a - 0x7400);
	if ((a >= 0x7300 && a <= 0x73ff)) return m_video.palette_r(a - 0x7300);
	if ((a >= 0x7200 && a <= 0x72ff)) return m_video.m_rowzoom[a - 0x7200];
	if ((a >= 0x7100 && a <= 0x71ff)) return m_video.m_rowscroll[a - 0x7100];
	if (a == 0x70e0) return m_video.video_70e0_prng_r();
	if (a == 0x7083) return m_video.tv_sharpness_r();
	if (a == 0x707f) return m_video.ppu_enable_r();
	if (a == 0x707c) return m_video.video_707c_r();
	if (a == 0x7072) return m_video.video_dma_size_busy_r();
	if (a == 0x7063) return m_video.video_7063_videoirq_source_r();
	if (a == 0x7062) return m_video.videoirq_source_enable_r();
	if (a == 0x7051) return m_video.video_7051_r();
	if (a == 0x7042) return m_video.sprite_7042_extra_r();
	if (a == 0x703c) return m_video.video_703c_tvcontrol1_r();
	if (a == 0x703a) return m_video.video_703a_palettebank_r();
	if (a == 0x7038) return m_video.video_curline_r();
	if (a == 0x7030) return m_video.video_7030_brightness_r();
	if (a == 0x702f) return m_video.tmap3_tilebase_msb_r();
	if (a == 0x702e) return m_video.tmap2_tilebase_msb_r();
	if (a == 0x702d) return m_video.sprite_702d_gfxbase_msb_r();
	if (a == 0x702c) return m_video.tmap1_tilebase_msb_r();
	if (a == 0x702b) return m_video.tmap0_tilebase_msb_r();
	if (a == 0x7024) return m_video.tmap3_tilebase_lsb_r();
	if (a == 0x7023) return m_video.tmap2_tilebase_lsb_r();
	if (a == 0x7022) return m_video.sprite_7022_gfxbase_lsb_r();
	if (a == 0x7021) return m_video.tmap1_tilebase_lsb_r();
	if (a == 0x7020) return m_video.tmap0_tilebase_lsb_r();
	if ((a >= 0x7016 && a <= 0x701b)) return m_video.tmap1_regs_r(a - 0x7016);
	if ((a >= 0x7010 && a <= 0x7015)) return m_video.tmap0_regs_r(a - 0x7010);
	if ((a >= 0x7008 && a <= 0x700f)) return m_video.tmap3_regs_r(a - 0x7008);
	if ((a >= 0x7000 && a <= 0x7007)) return m_video.tmap2_regs_r(a - 0x7000);
	if ((a >= 0x7000 && a <= 0x7fff)) return unk_r(a - 0x7000);
	return 0;
}

void Gpl162xxCore::io_write_16218b(u32 a, u16 data)
{
	if (a == 0x7af0) { byteswap_w(data); return; }
	if ((a >= 0x7e00 && a <= 0x7fff)) { m_audio.audio_phase_w(a - 0x7e00, data); return; }
	if ((a >= 0x7c00 && a <= 0x7dff)) { m_audio.audio_w(a - 0x7c00, data); return; }
	if ((a >= 0x7b80 && a <= 0x7bbf)) { m_audio.control_w(a - 0x7b80, data); return; }
	if (a == 0x7abf) { system_dma_status_w(data); return; }
	if (a == 0x7abe) { system_dma_memtype_w(data); return; }
	if ((a >= 0x7a98 && a <= 0x7a9f)) { system_dma_params_channel3_w(a - 0x7a98, data); return; }
	if ((a >= 0x7a90 && a <= 0x7a97)) { system_dma_params_channel2_w(a - 0x7a90, data); return; }
	if ((a >= 0x7a88 && a <= 0x7a8f)) { system_dma_params_channel1_w(a - 0x7a88, data); return; }
	if ((a >= 0x7a80 && a <= 0x7a87)) { system_dma_params_channel0_w(a - 0x7a80, data); return; }
	if (a == 0x7961) { madc_ctrl_w(data); return; }
	if (a == 0x7960) { adc_setup_w(data); return; }
	if (a == 0x7942) { spi_7942_txdata_w(data); return; }
	if (a == 0x7936) { rtc_int_ctrl_w(data); return; }
	if (a == 0x7935) { rtc_int_status_w(data); return; }
	if (a == 0x7934) { rtc_ctrl_w(data); return; }
	if (a == 0x78f0) { cha_ctrl_w(data); return; }
	if (a == 0x78ea) { timer_preload_w<5>(data); return; }
	if (a == 0x78e8) { timer_ctrl_w<5>(data); return; }
	if (a == 0x78e2) { timer_preload_w<4>(data); return; }
	if (a == 0x78e0) { timer_ctrl_w<4>(data); return; }
	if (a == 0x78da) { timer_preload_w<3>(data); return; }
	if (a == 0x78d8) { timer_ctrl_w<3>(data); return; }
	if (a == 0x78d3) { timer_cc_reg_w<2>(data); return; }
	if (a == 0x78d2) { timer_preload_w<2>(data); return; }
	if (a == 0x78d1) { timer_ccp_ctrl_w<2>(data); return; }
	if (a == 0x78d0) { timer_ctrl_w<2>(data); return; }
	if (a == 0x78cb) { timer_cc_reg_w<1>(data); return; }
	if (a == 0x78ca) { timer_preload_w<1>(data); return; }
	if (a == 0x78c9) { timer_ccp_ctrl_w<1>(data); return; }
	if (a == 0x78c8) { timer_ctrl_w<1>(data); return; }
	if (a == 0x78c3) { timer_cc_reg_w<0>(data); return; }
	if (a == 0x78c2) { timer_preload_w<0>(data); return; }
	if (a == 0x78c1) { timer_ccp_ctrl_w<0>(data); return; }
	if (a == 0x78c0) { timer_ctrl_w<0>(data); return; }
	if (a == 0x78b8) { timebase_reset_w(data); return; }
	if (a == 0x78b2) { timebasec_ctrl_w(data); return; }
	if (a == 0x78b1) { timebaseb_ctrl_w(data); return; }
	if (a == 0x78b0) { timebasea_ctrl_w(data); return; }
	if (a == 0x78a8) { mint_ctrl_w(data); return; }
	if (a == 0x78a6) { int_priority_3_w(data); return; }
	if (a == 0x78a5) { int_priority_2_w(data); return; }
	if (a == 0x78a4) { int_priority_1_w(data); return; }
	if (a == 0x78a3) { int_status3_w(data); return; }
	if (a == 0x78a1) { int_status2_w(data); return; }
	if (a == 0x78a0) { int_status1_w(data); return; }
	if (a == 0x7883) { ioe_attrib_w(data); return; }
	if (a == 0x7882) { ioe_dir_w(data); return; }
	if (a == 0x7881) { ioe_buffer_w(data); return; }
	if (a == 0x787c) { iod_drv_w(data); return; }
	if (a == 0x787b) { iod_attib_w(data); return; }
	if (a == 0x787a) { iod_dir_w(data); return; }
	if (a == 0x7879) { iod_buffer_w(data); return; }
	if (a == 0x7878) { iod_data_w(data); return; }
	if (a == 0x7873) { ioc_attrib_w(data); return; }
	if (a == 0x7872) { ioc_dir_w(data); return; }
	if (a == 0x7871) { ioc_buffer_w(data); return; }
	if (a == 0x7870) { ioc_data_w(data); return; }
	if (a == 0x786b) { iob_attrib_w(data); return; }
	if (a == 0x786a) { iob_dir_w(data); return; }
	if (a == 0x7869) { iob_buffer_w(data); return; }
	if (a == 0x7868) { iob_data_w(data); return; }
	if (a == 0x7863) { ioa_attrib_w(data); return; }
	if (a == 0x7862) { ioa_dir_w(data); return; }
	if (a == 0x7861) { ioa_buffer_w(data); return; }
	if (a == 0x7860) { ioa_data_w(data); return; }
	if (a == 0x7835) { mcs0_page_w(data); return; }
	if (a == 0x782d) { raw_war_w(data); return; }
	if ((a >= 0x7820 && a <= 0x7824)) { chipselect_csx_memory_device_control_w(a - 0x7820, data); return; }
	if (a == 0x7819) { cache_ctrl_w(data); return; }
	if (a == 0x7818) { pllclkwait_w(data); return; }
	if (a == 0x7817) { pllchange_w(data); return; }
	if (a == 0x7816) { unkarea_7816_w(data); return; }
	if (a == 0x7810) { membankswitch_7810_w(data); return; }
	if (a == 0x780c) { waitmode_enter_780c_w(data); return; }
	if (a == 0x780b) return;
	if (a == 0x780a) { watchdog_ctrl_w(data); return; }
	if (a == 0x7807) { clock_ctrl_w(data); return; }
	if (a == 0x7804) { clk_ctrl0_w(data); return; }
	if (a == 0x7803) { sys_ctrl_w(data); return; }
	if ((a >= 0x7400 && a <= 0x77ff)) { m_video.spriteram_w(a - 0x7400, data); return; }
	if ((a >= 0x7300 && a <= 0x73ff)) { m_video.palette_w(a - 0x7300, data); return; }
	if ((a >= 0x7200 && a <= 0x72ff)) { m_video.m_rowzoom[a - 0x7200] = data; return; }
	if ((a >= 0x7100 && a <= 0x71ff)) { m_video.m_rowscroll[a - 0x7100] = data; return; }
	if (a == 0x7088) { m_video.tv_videodac_w(data); return; }
	if (a == 0x7087) { m_video.tv_h_position_w(data); return; }
	if (a == 0x7086) { m_video.tv_v_position_w(data); return; }
	if (a == 0x7085) { m_video.tv_y_delay_w(data); return; }
	if (a == 0x7084) { m_video.tv_y_gain_w(data); return; }
	if (a == 0x7083) { m_video.tv_sharpness_w(data); return; }
	if (a == 0x7082) { m_video.tv_brightness_w(data); return; }
	if (a == 0x7081) { m_video.tv_hue_w(data); return; }
	if (a == 0x7080) { m_video.tv_saturation_w(data); return; }
	if (a == 0x707f) { m_video.ppu_enable_w(data); return; }
	if (a == 0x707e) { m_video.ppu_ram_bank_w(data); return; }
	if (a == 0x7072) { m_video.video_dma_size_trigger_w(data); return; }
	if (a == 0x7071) { m_video.video_dma_dest_w(data); return; }
	if (a == 0x7070) { m_video.video_dma_source_w(data); return; }
	if (a == 0x7063) { m_video.video_7063_videoirq_source_ack_w(data); return; }
	if (a == 0x7062) { m_video.videoirq_source_enable_w(data); return; }
	if (a == 0x7042) { m_video.sprite_7042_extra_w(data); return; }
	if (a == 0x703c) { m_video.video_703c_tvcontrol1_w(data); return; }
	if (a == 0x703a) { m_video.video_703a_palettebank_w(data); return; }
	if (a == 0x7030) { m_video.video_7030_brightness_w(data); return; }
	if (a == 0x7037) { m_video.split_irq_ypos_w(data); return; }
	if (a == 0x7036) { m_video.split_irq_xpos_w(data); return; }
	if (a == 0x702f) { m_video.tmap3_tilebase_msb_w(data); return; }
	if (a == 0x702e) { m_video.tmap2_tilebase_msb_w(data); return; }
	if (a == 0x702d) { m_video.sprite_702d_gfxbase_msb_w(data); return; }
	if (a == 0x702c) { m_video.tmap1_tilebase_msb_w(data); return; }
	if (a == 0x702b) { m_video.tmap0_tilebase_msb_w(data); return; }
	if (a == 0x702a) { m_video.blending_w(data); return; }
	if (a == 0x7024) { m_video.tmap3_tilebase_lsb_w(data); return; }
	if (a == 0x7023) { m_video.tmap2_tilebase_lsb_w(data); return; }
	if (a == 0x7022) { m_video.sprite_7022_gfxbase_lsb_w(data); return; }
	if (a == 0x7021) { m_video.tmap1_tilebase_lsb_w(data); return; }
	if (a == 0x7020) { m_video.tmap0_tilebase_lsb_w(data); return; }
	if (a == 0x701e) { m_video.vcomp_step_w(data); return; }
	if (a == 0x701d) { m_video.vcomp_offset_w(data); return; }
	if (a == 0x701c) { m_video.vcomp_value_w(data); return; }
	if ((a >= 0x7016 && a <= 0x701b)) { m_video.tmap1_regs_w(a - 0x7016, data); return; }
	if ((a >= 0x7010 && a <= 0x7015)) { m_video.tmap0_regs_w(a - 0x7010, data); return; }
	if ((a >= 0x7008 && a <= 0x700f)) { m_video.tmap3_regs_w(a - 0x7008, data); return; }
	if ((a >= 0x7000 && a <= 0x7007)) { m_video.tmap2_regs_w(a - 0x7000, data); return; }
	if ((a >= 0x7000 && a <= 0x7fff)) { unk_w(a - 0x7000, data); return; }
}

u16 Gpl162xxCore::io_read_16258vb(u32 a)
{
	if (a == 0x7af0) return byteswap_r();
	if (a == 0x70cd) return esp_ctrl_r();
	if (a == 0x7943) return spi_rxstatus_r();
	if (a == 0x785f) return nand_ecc_err1_lb_r();
	if (a == 0x785e) return nand_ecc_err0_lb_r();
	if (a == 0x7854) return nand_data_r();
	if (a == 0x7850) return nand_7850_status_r();
	if ((a >= 0x7e00 && a <= 0x7fff)) return m_audio.audio_phase_r(a - 0x7e00);
	if ((a >= 0x7c00 && a <= 0x7dff)) return m_audio.audio_r(a - 0x7c00);
	if ((a >= 0x7b80 && a <= 0x7bbf)) return m_audio.control_r(a - 0x7b80);
	if (a == 0x7ae2) return efuse2_r();
	if (a == 0x7abf) return system_dma_status_r();
	if (a == 0x7abe) return system_dma_memtype_r();
	if ((a >= 0x7a98 && a <= 0x7a9f)) return system_dma_params_channel3_r(a - 0x7a98);
	if ((a >= 0x7a90 && a <= 0x7a97)) return system_dma_params_channel2_r(a - 0x7a90);
	if ((a >= 0x7a88 && a <= 0x7a8f)) return system_dma_params_channel1_r(a - 0x7a88);
	if ((a >= 0x7a80 && a <= 0x7a87)) return system_dma_params_channel0_r(a - 0x7a80);
	if (a == 0x7a54) return usb_7a54_r();
	if (a == 0x7a46) return usb_7a46_r();
	if (a == 0x7a3a) return usb_7a3a_r();
	if (a == 0x7a39) return usb_7a39_r();
	if (a == 0x7a37) return usb_7a37_r();
	if (a == 0x7a35) return usb_7a35_r();
	if (a == 0x7962) return madc_data_r();
	if (a == 0x7961) return madc_ctrl_r();
	if (a == 0x7945) return spi_7945_misc_control_reg_r();
	if (a == 0x7944) return spi_7944_rxdata_r();
	if (a == 0x7936) return rtc_int_ctrl_r();
	if (a == 0x7935) return rtc_int_status_r();
	if (a == 0x7934) return rtc_ctrl_r();
	if (a == 0x7904) return uart_status_r();
	if (a == 0x78fb) return dac_pga_r();
	if (a == 0x78f0) return cha_ctrl_r();
	if (a == 0x78ec) return timer_upcount_r<5>();
	if (a == 0x78ea) return timer_preload_r<5>();
	if (a == 0x78e8) return timer_ctrl_r<5>();
	if (a == 0x78e4) return timer_upcount_r<4>();
	if (a == 0x78e2) return timer_preload_r<4>();
	if (a == 0x78e0) return timer_ctrl_r<4>();
	if (a == 0x78dc) return timer_upcount_r<3>();
	if (a == 0x78da) return timer_preload_r<3>();
	if (a == 0x78d8) return timer_ctrl_r<3>();
	if (a == 0x78d4) return timer_upcount_r<2>();
	if (a == 0x78d3) return timer_cc_reg_r<2>();
	if (a == 0x78d2) return timer_preload_r<2>();
	if (a == 0x78d1) return timer_ccp_ctrl_r<2>();
	if (a == 0x78d0) return timer_ctrl_r<2>();
	if (a == 0x78cc) return timer_upcount_r<1>();
	if (a == 0x78cb) return timer_cc_reg_r<1>();
	if (a == 0x78ca) return timer_preload_r<1>();
	if (a == 0x78c9) return timer_ccp_ctrl_r<1>();
	if (a == 0x78c8) return timer_ctrl_r<1>();
	if (a == 0x78c4) return timer_upcount_r<0>();
	if (a == 0x78c3) return timer_cc_reg_r<0>();
	if (a == 0x78c2) return timer_preload_r<0>();
	if (a == 0x78c1) return timer_ccp_ctrl_r<0>();
	if (a == 0x78c0) return timer_ctrl_r<0>();
	if (a == 0x78b2) return timebasec_ctrl_r();
	if (a == 0x78b1) return timebaseb_ctrl_r();
	if (a == 0x78b0) return timebasea_ctrl_r();
	if (a == 0x78a3) return int_status3_r();
	if (a == 0x78a1) return int_status2_r();
	if (a == 0x78a0) return int_status1_r();
	if (a == 0x7883) return ioe_attrib_r();
	if (a == 0x7882) return ioe_dir_r();
	if (a == 0x7881) return ioe_buffer_r();
	if (a == 0x787c) return iod_drv_r();
	if (a == 0x787b) return iod_attib_r();
	if (a == 0x787a) return iod_dir_r();
	if (a == 0x7879) return iod_buffer_r();
	if (a == 0x7878) return iod_data_r();
	if (a == 0x7873) return ioc_attrib_r();
	if (a == 0x7872) return ioc_dir_r();
	if (a == 0x7871) return ioc_buffer_r();
	if (a == 0x7870) return ioc_data_r();
	if (a == 0x786b) return iob_attrib_r();
	if (a == 0x786a) return iob_dir_r();
	if (a == 0x7869) return iob_buffer_r();
	if (a == 0x7868) return iob_data_r();
	if (a == 0x7863) return ioa_attrib_r();
	if (a == 0x7862) return ioa_dir_r();
	if (a == 0x7861) return ioa_buffer_r();
	if (a == 0x7860) return ioa_data_r();
	if (a == 0x782d) return raw_war_r();
	if (a == 0x7819) return cache_ctrl_r();
	if (a == 0x7818) return pllclkwait_r();
	if (a == 0x7810) return membankswitch_7810_r();
	if (a == 0x780f) return power_state_r();
	if (a == 0x7804) return clk_ctrl0_r();
	if (a == 0x7803) return sys_ctrl_r();
	if ((a >= 0x7400 && a <= 0x77ff)) return m_video.spriteram_r(a - 0x7400);
	if ((a >= 0x7300 && a <= 0x73ff)) return m_video.palette_r(a - 0x7300);
	if ((a >= 0x7200 && a <= 0x72ff)) return m_video.m_rowzoom[a - 0x7200];
	if ((a >= 0x7100 && a <= 0x71ff)) return m_video.m_rowscroll[a - 0x7100];
	if (a == 0x70e0) return m_video.video_70e0_prng_r();
	if (a == 0x7083) return m_video.tv_sharpness_r();
	if (a == 0x707f) return m_video.ppu_enable_r();
	if (a == 0x707c) return m_video.video_707c_r();
	if (a == 0x7072) return m_video.video_dma_size_busy_r();
	if (a == 0x7063) return m_video.video_7063_videoirq_source_r();
	if (a == 0x7062) return m_video.videoirq_source_enable_r();
	if (a == 0x7051) return m_video.video_7051_r();
	if (a == 0x7042) return m_video.sprite_7042_extra_r();
	if (a == 0x703c) return m_video.video_703c_tvcontrol1_r();
	if (a == 0x703a) return m_video.video_703a_palettebank_r();
	if (a == 0x7038) return m_video.video_curline_r();
	if (a == 0x7030) return m_video.video_7030_brightness_r();
	if (a == 0x702f) return m_video.tmap3_tilebase_msb_r();
	if (a == 0x702e) return m_video.tmap2_tilebase_msb_r();
	if (a == 0x702d) return m_video.sprite_702d_gfxbase_msb_r();
	if (a == 0x702c) return m_video.tmap1_tilebase_msb_r();
	if (a == 0x702b) return m_video.tmap0_tilebase_msb_r();
	if (a == 0x7024) return m_video.tmap3_tilebase_lsb_r();
	if (a == 0x7023) return m_video.tmap2_tilebase_lsb_r();
	if (a == 0x7022) return m_video.sprite_7022_gfxbase_lsb_r();
	if (a == 0x7021) return m_video.tmap1_tilebase_lsb_r();
	if (a == 0x7020) return m_video.tmap0_tilebase_lsb_r();
	if ((a >= 0x7016 && a <= 0x701b)) return m_video.tmap1_regs_r(a - 0x7016);
	if ((a >= 0x7010 && a <= 0x7015)) return m_video.tmap0_regs_r(a - 0x7010);
	if ((a >= 0x7008 && a <= 0x700f)) return m_video.tmap3_regs_r(a - 0x7008);
	if ((a >= 0x7000 && a <= 0x7007)) return m_video.tmap2_regs_r(a - 0x7000);
	if ((a >= 0x7000 && a <= 0x7fff)) return unk_r(a - 0x7000);
	return 0;
}

void Gpl162xxCore::io_write_16258vb(u32 a, u16 data)
{
	if (a == 0x7af0) { byteswap_w(data); return; }
	if (a == 0x70cd) { esp_ctrl_w(data); return; }
	if (a == 0x785d) { nand_ecc_cpckr_lb_w(data); return; }
	if (a == 0x785c) { nand_ecc_lpr_ckh_lb_w(data); return; }
	if (a == 0x785b) { nand_ecc_lpr_ckl_lb_w(data); return; }
	if (a == 0x7857) { nand_ecc_ctrl_w(data); return; }
	if (a == 0x7856) { nand_bch_ctrl_w(data); return; }
	if (a == 0x7855) { nand_dma_ctrl_w(data); return; }
	if (a == 0x7854) { nand_data_w(data); return; }
	if (a == 0x7853) { nand_addr_high_w(data); return; }
	if (a == 0x7852) { nand_addr_low_w(data); return; }
	if (a == 0x7851) { nand_command_w(data); return; }
	if (a == 0x7850) { nand_7850_w(data); return; }
	if ((a >= 0x7e00 && a <= 0x7fff)) { m_audio.audio_phase_w(a - 0x7e00, data); return; }
	if ((a >= 0x7c00 && a <= 0x7dff)) { m_audio.audio_w(a - 0x7c00, data); return; }
	if ((a >= 0x7b80 && a <= 0x7bbf)) { m_audio.control_w(a - 0x7b80, data); return; }
	if (a == 0x7abf) { system_dma_status_w(data); return; }
	if (a == 0x7abe) { system_dma_memtype_w(data); return; }
	if ((a >= 0x7a98 && a <= 0x7a9f)) { system_dma_params_channel3_w(a - 0x7a98, data); return; }
	if ((a >= 0x7a90 && a <= 0x7a97)) { system_dma_params_channel2_w(a - 0x7a90, data); return; }
	if ((a >= 0x7a88 && a <= 0x7a8f)) { system_dma_params_channel1_w(a - 0x7a88, data); return; }
	if ((a >= 0x7a80 && a <= 0x7a87)) { system_dma_params_channel0_w(a - 0x7a80, data); return; }
	if (a == 0x7961) { madc_ctrl_w(data); return; }
	if (a == 0x7960) { adc_setup_w(data); return; }
	if (a == 0x7942) { spi_7942_txdata_w(data); return; }
	if (a == 0x7936) { rtc_int_ctrl_w(data); return; }
	if (a == 0x7935) { rtc_int_status_w(data); return; }
	if (a == 0x7934) { rtc_ctrl_w(data); return; }
	if (a == 0x78f0) { cha_ctrl_w(data); return; }
	if (a == 0x78ea) { timer_preload_w<5>(data); return; }
	if (a == 0x78e8) { timer_ctrl_w<5>(data); return; }
	if (a == 0x78e2) { timer_preload_w<4>(data); return; }
	if (a == 0x78e0) { timer_ctrl_w<4>(data); return; }
	if (a == 0x78da) { timer_preload_w<3>(data); return; }
	if (a == 0x78d8) { timer_ctrl_w<3>(data); return; }
	if (a == 0x78d3) { timer_cc_reg_w<2>(data); return; }
	if (a == 0x78d2) { timer_preload_w<2>(data); return; }
	if (a == 0x78d1) { timer_ccp_ctrl_w<2>(data); return; }
	if (a == 0x78d0) { timer_ctrl_w<2>(data); return; }
	if (a == 0x78cb) { timer_cc_reg_w<1>(data); return; }
	if (a == 0x78ca) { timer_preload_w<1>(data); return; }
	if (a == 0x78c9) { timer_ccp_ctrl_w<1>(data); return; }
	if (a == 0x78c8) { timer_ctrl_w<1>(data); return; }
	if (a == 0x78c3) { timer_cc_reg_w<0>(data); return; }
	if (a == 0x78c2) { timer_preload_w<0>(data); return; }
	if (a == 0x78c1) { timer_ccp_ctrl_w<0>(data); return; }
	if (a == 0x78c0) { timer_ctrl_w<0>(data); return; }
	if (a == 0x78b8) { timebase_reset_w(data); return; }
	if (a == 0x78b2) { timebasec_ctrl_w(data); return; }
	if (a == 0x78b1) { timebaseb_ctrl_w(data); return; }
	if (a == 0x78b0) { timebasea_ctrl_w(data); return; }
	if (a == 0x78a8) { mint_ctrl_w(data); return; }
	if (a == 0x78a6) { int_priority_3_w(data); return; }
	if (a == 0x78a5) { int_priority_2_w(data); return; }
	if (a == 0x78a4) { int_priority_1_w(data); return; }
	if (a == 0x78a3) { int_status3_w(data); return; }
	if (a == 0x78a1) { int_status2_w(data); return; }
	if (a == 0x78a0) { int_status1_w(data); return; }
	if (a == 0x7883) { ioe_attrib_w(data); return; }
	if (a == 0x7882) { ioe_dir_w(data); return; }
	if (a == 0x7881) { ioe_buffer_w(data); return; }
	if (a == 0x787c) { iod_drv_w(data); return; }
	if (a == 0x787b) { iod_attib_w(data); return; }
	if (a == 0x787a) { iod_dir_w(data); return; }
	if (a == 0x7879) { iod_buffer_w(data); return; }
	if (a == 0x7878) { iod_data_w(data); return; }
	if (a == 0x7873) { ioc_attrib_w(data); return; }
	if (a == 0x7872) { ioc_dir_w(data); return; }
	if (a == 0x7871) { ioc_buffer_w(data); return; }
	if (a == 0x7870) { ioc_data_w(data); return; }
	if (a == 0x786b) { iob_attrib_w(data); return; }
	if (a == 0x786a) { iob_dir_w(data); return; }
	if (a == 0x7869) { iob_buffer_w(data); return; }
	if (a == 0x7868) { iob_data_w(data); return; }
	if (a == 0x7863) { ioa_attrib_w(data); return; }
	if (a == 0x7862) { ioa_dir_w(data); return; }
	if (a == 0x7861) { ioa_buffer_w(data); return; }
	if (a == 0x7860) { ioa_data_w(data); return; }
	if (a == 0x7835) { mcs0_page_w(data); return; }
	if (a == 0x782d) { raw_war_w(data); return; }
	if ((a >= 0x7820 && a <= 0x7824)) { chipselect_csx_memory_device_control_w(a - 0x7820, data); return; }
	if (a == 0x7819) { cache_ctrl_w(data); return; }
	if (a == 0x7818) { pllclkwait_w(data); return; }
	if (a == 0x7817) { pllchange_w(data); return; }
	if (a == 0x7816) { unkarea_7816_w(data); return; }
	if (a == 0x7810) { membankswitch_7810_w(data); return; }
	if (a == 0x780c) { waitmode_enter_780c_w(data); return; }
	if (a == 0x780b) return;
	if (a == 0x780a) { watchdog_ctrl_w(data); return; }
	if (a == 0x7807) { clock_ctrl_w(data); return; }
	if (a == 0x7804) { clk_ctrl0_w(data); return; }
	if (a == 0x7803) { sys_ctrl_w(data); return; }
	if ((a >= 0x7400 && a <= 0x77ff)) { m_video.spriteram_w(a - 0x7400, data); return; }
	if ((a >= 0x7300 && a <= 0x73ff)) { m_video.palette_w(a - 0x7300, data); return; }
	if ((a >= 0x7200 && a <= 0x72ff)) { m_video.m_rowzoom[a - 0x7200] = data; return; }
	if ((a >= 0x7100 && a <= 0x71ff)) { m_video.m_rowscroll[a - 0x7100] = data; return; }
	if (a == 0x7088) { m_video.tv_videodac_w(data); return; }
	if (a == 0x7087) { m_video.tv_h_position_w(data); return; }
	if (a == 0x7086) { m_video.tv_v_position_w(data); return; }
	if (a == 0x7085) { m_video.tv_y_delay_w(data); return; }
	if (a == 0x7084) { m_video.tv_y_gain_w(data); return; }
	if (a == 0x7083) { m_video.tv_sharpness_w(data); return; }
	if (a == 0x7082) { m_video.tv_brightness_w(data); return; }
	if (a == 0x7081) { m_video.tv_hue_w(data); return; }
	if (a == 0x7080) { m_video.tv_saturation_w(data); return; }
	if (a == 0x707f) { m_video.ppu_enable_w(data); return; }
	if (a == 0x707e) { m_video.ppu_ram_bank_w(data); return; }
	if (a == 0x7072) { m_video.video_dma_size_trigger_w(data); return; }
	if (a == 0x7071) { m_video.video_dma_dest_w(data); return; }
	if (a == 0x7070) { m_video.video_dma_source_w(data); return; }
	if (a == 0x7063) { m_video.video_7063_videoirq_source_ack_w(data); return; }
	if (a == 0x7062) { m_video.videoirq_source_enable_w(data); return; }
	if (a == 0x7042) { m_video.sprite_7042_extra_w(data); return; }
	if (a == 0x703c) { m_video.video_703c_tvcontrol1_w(data); return; }
	if (a == 0x703a) { m_video.video_703a_palettebank_w(data); return; }
	if (a == 0x7030) { m_video.video_7030_brightness_w(data); return; }
	if (a == 0x7037) { m_video.split_irq_ypos_w(data); return; }
	if (a == 0x7036) { m_video.split_irq_xpos_w(data); return; }
	if (a == 0x702f) { m_video.tmap3_tilebase_msb_w(data); return; }
	if (a == 0x702e) { m_video.tmap2_tilebase_msb_w(data); return; }
	if (a == 0x702d) { m_video.sprite_702d_gfxbase_msb_w(data); return; }
	if (a == 0x702c) { m_video.tmap1_tilebase_msb_w(data); return; }
	if (a == 0x702b) { m_video.tmap0_tilebase_msb_w(data); return; }
	if (a == 0x702a) { m_video.blending_w(data); return; }
	if (a == 0x7024) { m_video.tmap3_tilebase_lsb_w(data); return; }
	if (a == 0x7023) { m_video.tmap2_tilebase_lsb_w(data); return; }
	if (a == 0x7022) { m_video.sprite_7022_gfxbase_lsb_w(data); return; }
	if (a == 0x7021) { m_video.tmap1_tilebase_lsb_w(data); return; }
	if (a == 0x7020) { m_video.tmap0_tilebase_lsb_w(data); return; }
	if (a == 0x701e) { m_video.vcomp_step_w(data); return; }
	if (a == 0x701d) { m_video.vcomp_offset_w(data); return; }
	if (a == 0x701c) { m_video.vcomp_value_w(data); return; }
	if ((a >= 0x7016 && a <= 0x701b)) { m_video.tmap1_regs_w(a - 0x7016, data); return; }
	if ((a >= 0x7010 && a <= 0x7015)) { m_video.tmap0_regs_w(a - 0x7010, data); return; }
	if ((a >= 0x7008 && a <= 0x700f)) { m_video.tmap3_regs_w(a - 0x7008, data); return; }
	if ((a >= 0x7000 && a <= 0x7007)) { m_video.tmap2_regs_w(a - 0x7000, data); return; }
	if ((a >= 0x7000 && a <= 0x7fff)) { unk_w(a - 0x7000, data); return; }
}
