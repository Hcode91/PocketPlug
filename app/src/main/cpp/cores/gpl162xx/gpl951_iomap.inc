// generated from MAME generalplus_gpl951xx_soc.cpp (BSD-3-Clause)
u16 Gpl951Core::io_read(u32 a)
{
	if ((a >= 0x7e00 && a <= 0x7fff)) return m_audio.audio_phase_r(a - 0x7e00);
	if ((a >= 0x7c00 && a <= 0x7dff)) return m_audio.audio_r(a - 0x7c00);
	if ((a >= 0x7b80 && a <= 0x7bbf)) return m_audio.control_r(a - 0x7b80);
	if (a == 0x7b4b) return spifc_ctrl2_r();
	if (a == 0x7b49) return spifc_timing_r();
	if (a == 0x7b48) return spifc_rx_bc_r();
	if (a == 0x7b47) return spifc_tx_bc_r();
	if (a == 0x7b46) return spifc_rxdat_r();
	if (a == 0x7b45) return spifc_txdat_r();
	if (a == 0x7b44) return spifc_addrh_r();
	if (a == 0x7b43) return spifc_addrl_r();
	if (a == 0x7b42) return spifc_para_r();
	if (a == 0x7b41) return spifc_cmd_r();
	if (a == 0x7b40) return spifc_ctrl_r();
	if (a == 0x7af0) return byte_swap_r();
	if (a == 0x7abf) return system_dma_status_r();
	if (a == 0x7abe) return system_dma_memtype_r();
	if ((a >= 0x7a88 && a <= 0x7a8f)) return system_dma_params_channel1_r(a - 0x7a88);
	if ((a >= 0x7a80 && a <= 0x7a87)) return system_dma_params_channel0_r(a - 0x7a80);
	if (a == 0x7a2b) return timer_upcount_r<5>();
	if (a == 0x7a2a) return timer_upcount_r<4>();
	if (a == 0x7a25) return timer_preload_r<5>();
	if (a == 0x7a24) return timer_preload_r<4>();
	if (a == 0x7a21) return timer_ctrl_r<5>();
	if (a == 0x7a20) return timer_ctrl_r<4>();
	if (a == 0x7a1c) return timer_upcount_r<3>();
	if (a == 0x7a1a) return timer_preload_r<3>();
	if (a == 0x7a18) return timer_ctrl_r<3>();
	if (a == 0x7a14) return timer_upcount_r<2>();
	if (a == 0x7a12) return timer_preload_r<2>();
	if (a == 0x7a10) return timer_ctrl_r<2>();
	if (a == 0x7a0c) return timer_upcount_r<1>();
	if (a == 0x7a0a) return timer_preload_r<1>();
	if (a == 0x7a08) return timer_ctrl_r<1>();
	if (a == 0x7a04) return timer_upcount_r<0>();
	if (a == 0x7a02) return timer_preload_r<0>();
	if (a == 0x7a00) return timer_ctrl_r<0>();
	if (a == 0x79f5) return m_rtc.rtc_readdata_r();
	if (a == 0x79f4) return m_rtc.rtc_ready_r();
	if (a == 0x79a2) return madc_data_r();
	if (a == 0x79a1) return madc_ctrl_r();
	if (a == 0x78fa) return m_chx.chb_fifo_r();
	if (a == 0x78f9) return m_chx.chb_data_r();
	if (a == 0x78f8) return m_chx.chb_ctrl_r();
	if (a == 0x78f2) return m_chx.cha_fifo_r();
	if (a == 0x78f1) return m_chx.cha_data_r();
	if (a == 0x78f0) return m_chx.cha_ctrl_r();
	if (a == 0x78ec) return timer_upcount_r<7>();
	if (a == 0x78ea) return timer_preload_r<7>();
	if (a == 0x78e8) return timer_ctrl_r<7>();
	if (a == 0x78e4) return timer_upcount_r<6>();
	if (a == 0x78e2) return timer_preload_r<6>();
	if (a == 0x78e0) return timer_ctrl_r<6>();
	if (a == 0x78c1) return i2c_status_r();
	if (a == 0x78c0) return i2c_ctrl_r();
	if (a == 0x78b2) return timebasec_ctrl_r();
	if (a == 0x78b1) return timebaseb_ctrl_r();
	if (a == 0x78b0) return timebasea_ctrl_r();
	if (a == 0x78a5) return int_priority_3_r();
	if (a == 0x78a4) return int_priority_2_r();
	if (a == 0x78a3) return int_priority_1_r();
	if (a == 0x78a2) return int_status3_r();
	if (a == 0x78a1) return int_status2_r();
	if (a == 0x78a0) return int_status1_r();
	if (a == 0x788f) return io_keyen_r<5>();
	if (a == 0x788e) return io_latch_r<5>();
	if (a == 0x788d) return io_mux_r<5>();
	if (a == 0x788c) return io_drv_r<5>();
	if (a == 0x788b) return io_attrib_r<5>();
	if (a == 0x788a) return io_dir_r<5>();
	if (a == 0x7889) return io_buffer_r<5>();
	if (a == 0x7888) return io_data_r<5>();
	if (a == 0x7887) return io_keyen_r<4>();
	if (a == 0x7886) return io_latch_r<4>();
	if (a == 0x7885) return io_mux_r<4>();
	if (a == 0x7884) return io_drv_r<4>();
	if (a == 0x7883) return io_attrib_r<4>();
	if (a == 0x7882) return io_dir_r<4>();
	if (a == 0x7881) return io_buffer_r<4>();
	if (a == 0x7880) return io_data_r<4>();
	if (a == 0x787f) return io_keyen_r<3>();
	if (a == 0x787e) return io_latch_r<3>();
	if (a == 0x787d) return io_mux_r<3>();
	if (a == 0x787c) return io_drv_r<3>();
	if (a == 0x787b) return io_attrib_r<3>();
	if (a == 0x787a) return io_dir_r<3>();
	if (a == 0x7879) return io_buffer_r<3>();
	if (a == 0x7878) return io_data_r<3>();
	if (a == 0x7877) return io_keyen_r<2>();
	if (a == 0x7876) return io_latch_r<2>();
	if (a == 0x7875) return io_mux_r<2>();
	if (a == 0x7874) return io_drv_r<2>();
	if (a == 0x7873) return io_attrib_r<2>();
	if (a == 0x7872) return io_dir_r<2>();
	if (a == 0x7871) return io_buffer_r<2>();
	if (a == 0x7870) return io_data_r<2>();
	if (a == 0x786f) return io_keyen_r<1>();
	if (a == 0x786e) return io_latch_r<1>();
	if (a == 0x786d) return io_mux_r<1>();
	if (a == 0x786c) return io_drv_r<1>();
	if (a == 0x786b) return io_attrib_r<1>();
	if (a == 0x786a) return io_dir_r<1>();
	if (a == 0x7869) return io_buffer_r<1>();
	if (a == 0x7868) return io_data_r<1>();
	if (a == 0x7867) return io_keyen_r<0>();
	if (a == 0x7866) return io_latch_r<0>();
	if (a == 0x7865) return io_mux_r<0>();
	if (a == 0x7864) return io_drv_r<0>();
	if (a == 0x7863) return io_attrib_r<0>();
	if (a == 0x7862) return io_dir_r<0>();
	if (a == 0x7861) return io_buffer_r<0>();
	if (a == 0x7860) return io_data_r<0>();
	if (a == 0x7840) return spi_improve_r();
	if (a == 0x7819) return cache_ctrl_r();
	if (a == 0x7818) return pllclkwait_r();
	if (a == 0x7817) return pllsel_r();
	if (a == 0x7810) return spi_bank_r();
	if (a == 0x780f) return power_state_r();
	if (a == 0x7804) return clk_ctrl0_r();
	if (a == 0x7803) return sys_ctrl_r();
	if ((a >= 0x7400 && a <= 0x77ff)) return m_video.spriteram_r(a - 0x7400);
	if ((a >= 0x7300 && a <= 0x73ff)) return m_video.palette_r(a - 0x7300);
	if ((a >= 0x7200 && a <= 0x72ff)) return m_video.m_rowzoom[a - 0x7200];
	if ((a >= 0x7100 && a <= 0x71ff)) return m_video.m_rowscroll[a - 0x7100];
	if (a == 0x70e0) return m_video.video_70e0_prng_r();
	if (a == 0x707f) return m_video.ppu_enable_r();
	if (a == 0x7072) return m_video.video_dma_size_busy_r();
	if (a == 0x7063) return m_video.video_7063_videoirq_source_r();
	if (a == 0x7062) return m_video.videoirq_source_enable_r();
	if (a == 0x705a) return tft_status_r();
	if (a == 0x7059) return tft_rgb_ctrl_r();
	if (a == 0x7042) return m_video.sprite_7042_extra_r();
	if (a == 0x703a) return m_video.video_703a_palettebank_r();
	if (a == 0x7030) return m_video.video_7030_brightness_r();
	if (a == 0x702e) return m_video.tmap2_tilebase_msb_r();
	if (a == 0x702d) return m_video.sprite_702d_gfxbase_msb_r();
	if (a == 0x702c) return m_video.tmap1_tilebase_msb_r();
	if (a == 0x702b) return m_video.tmap0_tilebase_msb_r();
	if (a == 0x7023) return m_video.tmap2_tilebase_lsb_r();
	if (a == 0x7022) return m_video.sprite_7022_gfxbase_lsb_r();
	if (a == 0x7021) return m_video.tmap1_tilebase_lsb_r();
	if (a == 0x7020) return m_video.tmap0_tilebase_lsb_r();
	if ((a >= 0x7016 && a <= 0x701b)) return m_video.tmap1_regs_r(a - 0x7016);
	if ((a >= 0x7010 && a <= 0x7015)) return m_video.tmap0_regs_r(a - 0x7010);
	if ((a >= 0x7000 && a <= 0x7007)) return m_video.tmap2_regs_r(a - 0x7000);
	return 0;
}

void Gpl951Core::io_write(u32 a, u16 data)
{
	if ((a >= 0x7e00 && a <= 0x7fff)) { m_audio.audio_phase_w(a - 0x7e00, data); return; }
	if ((a >= 0x7c00 && a <= 0x7dff)) { m_audio.audio_w(a - 0x7c00, data); return; }
	if ((a >= 0x7b80 && a <= 0x7bbf)) { m_audio.control_w(a - 0x7b80, data); return; }
	if (a == 0x7b4b) { spifc_ctrl2_w(data); return; }
	if (a == 0x7b49) { spifc_timing_w(data); return; }
	if (a == 0x7b48) { spifc_rx_bc_w(data); return; }
	if (a == 0x7b47) { spifc_tx_bc_w(data); return; }
	if (a == 0x7b46) { spifc_rxdat_w(data); return; }
	if (a == 0x7b45) { spifc_txdat_w(data); return; }
	if (a == 0x7b44) { spifc_addrh_w(data); return; }
	if (a == 0x7b43) { spifc_addrl_w(data); return; }
	if (a == 0x7b42) { spifc_para_w(data); return; }
	if (a == 0x7b41) { spifc_cmd_w(data); return; }
	if (a == 0x7b40) { spifc_ctrl_w(data); return; }
	if (a == 0x7af0) { byte_swap_w(data); return; }
	if (a == 0x7abf) { system_dma_status_w(data); return; }
	if (a == 0x7abe) { system_dma_memtype_w(data); return; }
	if ((a >= 0x7a88 && a <= 0x7a8f)) { system_dma_params_channel1_w(a - 0x7a88, data); return; }
	if ((a >= 0x7a80 && a <= 0x7a87)) { system_dma_params_channel0_w(a - 0x7a80, data); return; }
	if (a == 0x7a25) { timer_preload_w<5>(data); return; }
	if (a == 0x7a24) { timer_preload_w<4>(data); return; }
	if (a == 0x7a23) { timer_ccpb_ctrl_w<5>(data); return; }
	if (a == 0x7a22) { timer_ccpb_ctrl_w<4>(data); return; }
	if (a == 0x7a21) { timer_ctrl_w<5>(data); return; }
	if (a == 0x7a20) { timer_ctrl_w<4>(data); return; }
	if (a == 0x7a1a) { timer_preload_w<3>(data); return; }
	if (a == 0x7a19) { timer_ccpb_ctrl_w<3>(data); return; }
	if (a == 0x7a18) { timer_ctrl_w<3>(data); return; }
	if (a == 0x7a12) { timer_preload_w<2>(data); return; }
	if (a == 0x7a11) { timer_ccpb_ctrl_w<2>(data); return; }
	if (a == 0x7a10) { timer_ctrl_w<2>(data); return; }
	if (a == 0x7a0a) { timer_preload_w<1>(data); return; }
	if (a == 0x7a09) { timer_ccpb_ctrl_w<1>(data); return; }
	if (a == 0x7a08) { timer_ctrl_w<1>(data); return; }
	if (a == 0x7a02) { timer_preload_w<0>(data); return; }
	if (a == 0x7a01) { timer_ccpb_ctrl_w<0>(data); return; }
	if (a == 0x7a00) { timer_ctrl_w<0>(data); return; }
	if (a == 0x79f3) { m_rtc.rtc_request_w(data); return; }
	if (a == 0x79f2) { m_rtc.rtc_writedata_w(data); return; }
	if (a == 0x79f1) { m_rtc.rtc_addr_w(data); return; }
	if (a == 0x79f0) { m_rtc.rtc_ctrl_w(data); return; }
	if (a == 0x79a1) { madc_ctrl_w(data); return; }
	if (a == 0x78fa) { m_chx.chb_fifo_w(data); return; }
	if (a == 0x78f9) { m_chx.chb_data_w(data); return; }
	if (a == 0x78f8) { m_chx.chb_ctrl_w(data); return; }
	if (a == 0x78f2) { m_chx.cha_fifo_w(data); return; }
	if (a == 0x78f1) { m_chx.cha_data_w(data); return; }
	if (a == 0x78f0) { m_chx.cha_ctrl_w(data); return; }
	if (a == 0x78ea) { timer_preload_w<7>(data); return; }
	if (a == 0x78e8) { timer_ctrl_w<7>(data); return; }
	if (a == 0x78e2) { timer_preload_w<6>(data); return; }
	if (a == 0x78e0) { timer_ctrl_w<6>(data); return; }
	if (a == 0x78b8) { timebase_reset_w(data); return; }
	if (a == 0x78b2) { timebasec_ctrl_w(data); return; }
	if (a == 0x78b1) { timebaseb_ctrl_w(data); return; }
	if (a == 0x78b0) { timebasea_ctrl_w(data); return; }
	if (a == 0x78a6) { mint_ctrl_w(data); return; }
	if (a == 0x78a5) { int_priority_3_w(data); return; }
	if (a == 0x78a4) { int_priority_2_w(data); return; }
	if (a == 0x78a3) { int_priority_1_w(data); return; }
	if (a == 0x78a2) { int_status3_w(data); return; }
	if (a == 0x78a1) { int_status2_w(data); return; }
	if (a == 0x78a0) { int_status1_w(data); return; }
	if (a == 0x788f) { io_keyen_w<5>(data); return; }
	if (a == 0x788e) { io_latch_w<5>(data); return; }
	if (a == 0x788d) { io_mux_w<5>(data); return; }
	if (a == 0x788c) { io_drv_w<5>(data); return; }
	if (a == 0x788b) { io_attrib_w<5>(data); return; }
	if (a == 0x788a) { io_dir_w<5>(data); return; }
	if (a == 0x7889) { io_buffer_w<5>(data); return; }
	if (a == 0x7888) { io_data_w<5>(data); return; }
	if (a == 0x7887) { io_keyen_w<4>(data); return; }
	if (a == 0x7886) { io_latch_w<4>(data); return; }
	if (a == 0x7885) { io_mux_w<4>(data); return; }
	if (a == 0x7884) { io_drv_w<4>(data); return; }
	if (a == 0x7883) { io_attrib_w<4>(data); return; }
	if (a == 0x7882) { io_dir_w<4>(data); return; }
	if (a == 0x7881) { io_buffer_w<4>(data); return; }
	if (a == 0x7880) { io_data_w<4>(data); return; }
	if (a == 0x787f) { io_keyen_w<3>(data); return; }
	if (a == 0x787e) { io_latch_w<3>(data); return; }
	if (a == 0x787d) { io_mux_w<3>(data); return; }
	if (a == 0x787c) { io_drv_w<3>(data); return; }
	if (a == 0x787b) { io_attrib_w<3>(data); return; }
	if (a == 0x787a) { io_dir_w<3>(data); return; }
	if (a == 0x7879) { io_buffer_w<3>(data); return; }
	if (a == 0x7878) { io_data_w<3>(data); return; }
	if (a == 0x7877) { io_keyen_w<2>(data); return; }
	if (a == 0x7876) { io_latch_w<2>(data); return; }
	if (a == 0x7875) { io_mux_w<2>(data); return; }
	if (a == 0x7874) { io_drv_w<2>(data); return; }
	if (a == 0x7873) { io_attrib_w<2>(data); return; }
	if (a == 0x7872) { io_dir_w<2>(data); return; }
	if (a == 0x7871) { io_buffer_w<2>(data); return; }
	if (a == 0x7870) { io_data_w<2>(data); return; }
	if (a == 0x786f) { io_keyen_w<1>(data); return; }
	if (a == 0x786e) { io_latch_w<1>(data); return; }
	if (a == 0x786d) { io_mux_w<1>(data); return; }
	if (a == 0x786c) { io_drv_w<1>(data); return; }
	if (a == 0x786b) { io_attrib_w<1>(data); return; }
	if (a == 0x786a) { io_dir_w<1>(data); return; }
	if (a == 0x7869) { io_buffer_w<1>(data); return; }
	if (a == 0x7868) { io_data_w<1>(data); return; }
	if (a == 0x7867) { io_keyen_w<0>(data); return; }
	if (a == 0x7866) { io_latch_w<0>(data); return; }
	if (a == 0x7865) { io_mux_w<0>(data); return; }
	if (a == 0x7864) { io_drv_w<0>(data); return; }
	if (a == 0x7863) { io_attrib_w<0>(data); return; }
	if (a == 0x7862) { io_dir_w<0>(data); return; }
	if (a == 0x7861) { io_buffer_w<0>(data); return; }
	if (a == 0x7860) { io_data_w<0>(data); return; }
	if (a == 0x7840) { spi_improve_w(data); return; }
	if (a == 0x7819) { cache_ctrl_w(data); return; }
	if (a == 0x7818) { pllclkwait_w(data); return; }
	if (a == 0x7817) { pllsel_w(data); return; }
	if (a == 0x7810) { spi_bank_w(data); return; }
	if (a == 0x780b) return;
	if (a == 0x780a) { watchdog_ctrl_w(data); return; }
	if (a == 0x780a) { pm_ctrl_w(data); return; }
	if (a == 0x7807) { clock_ctrl_w(data); return; }
	if (a == 0x7804) { clk_ctrl0_w(data); return; }
	if (a == 0x7803) { sys_ctrl_w(data); return; }
	if ((a >= 0x7400 && a <= 0x77ff)) { m_video.spriteram_w(a - 0x7400, data); return; }
	if ((a >= 0x7300 && a <= 0x73ff)) { m_video.palette_w(a - 0x7300, data); return; }
	if ((a >= 0x7200 && a <= 0x72ff)) { m_video.m_rowzoom[a - 0x7200] = data; return; }
	if ((a >= 0x7100 && a <= 0x71ff)) { m_video.m_rowscroll[a - 0x7100] = data; return; }
	if (a == 0x70dc) { free_width_w(data); return; }
	if (a == 0x70db) { free_height_w(data); return; }
	if (a == 0x707f) { m_video.ppu_enable_w(data); return; }
	if (a == 0x707e) { m_video.ppu_ram_bank_w(data); return; }
	if (a == 0x7072) { m_video.video_dma_size_trigger_w(data); return; }
	if (a == 0x7071) { m_video.video_dma_dest_w(data); return; }
	if (a == 0x7070) { m_video.video_dma_source_w(data); return; }
	if (a == 0x706f) { tft_h_show_end_w(data); return; }
	if (a == 0x706e) { tft_h_show_start_w(data); return; }
	if (a == 0x706d) { tft_v_show_end_w(data); return; }
	if (a == 0x706c) { tft_v_show_start_w(data); return; }
	if (a == 0x7063) { m_video.video_7063_videoirq_source_ack_w(data); return; }
	if (a == 0x7062) { m_video.videoirq_source_enable_w(data); return; }
	if (a == 0x705b) { tft_memmode_wcmd_w(data); return; }
	if (a == 0x7059) { tft_rgb_ctrl_w(data); return; }
	if (a == 0x7058) { tft_h_end_w(data); return; }
	if (a == 0x7057) { tft_h_start_w(data); return; }
	if (a == 0x7056) { tft_hsync_setup_w(data); return; }
	if (a == 0x7055) { tft_h_width_w(data); return; }
	if (a == 0x7054) { tft_v_end_w(data); return; }
	if (a == 0x7053) { tft_v_start_w(data); return; }
	if (a == 0x7052) { tft_vsync_setup_w(data); return; }
	if (a == 0x7051) { tft_v_width_w(data); return; }
	if (a == 0x7050) { tft_ctrl_w(data); return; }
	if (a == 0x7042) { m_video.sprite_7042_extra_w(data); return; }
	if (a == 0x703a) { m_video.video_703a_palettebank_w(data); return; }
	if (a == 0x7030) { m_video.video_7030_brightness_w(data); return; }
	if (a == 0x702e) { m_video.tmap2_tilebase_msb_w(data); return; }
	if (a == 0x702d) { m_video.sprite_702d_gfxbase_msb_w(data); return; }
	if (a == 0x702c) { m_video.tmap1_tilebase_msb_w(data); return; }
	if (a == 0x702b) { m_video.tmap0_tilebase_msb_w(data); return; }
	if (a == 0x702a) { m_video.blending_w(data); return; }
	if (a == 0x7023) { m_video.tmap2_tilebase_lsb_w(data); return; }
	if (a == 0x7022) { m_video.sprite_7022_gfxbase_lsb_w(data); return; }
	if (a == 0x7021) { m_video.tmap1_tilebase_lsb_w(data); return; }
	if (a == 0x7020) { m_video.tmap0_tilebase_lsb_w(data); return; }
	if (a == 0x701e) { m_video.vcomp_step_w(data); return; }
	if (a == 0x701d) { m_video.vcomp_offset_w(data); return; }
	if (a == 0x701c) { m_video.vcomp_value_w(data); return; }
	if ((a >= 0x7016 && a <= 0x701b)) { m_video.tmap1_regs_w(a - 0x7016, data); return; }
	if ((a >= 0x7010 && a <= 0x7015)) { m_video.tmap0_regs_w(a - 0x7010, data); return; }
	if ((a >= 0x7000 && a <= 0x7007)) { m_video.tmap2_regs_w(a - 0x7000, data); return; }
}
