// PocketPlug - GeneralPlus GPL951xx (u'nSP 2.0, SPI-flash boot) mini arcades
// SPDX-License-Identifier: BSD-3-Clause
//
// Port of MAME's generalplus_gpl951xx_soc.cpp, generalplus_gpl_chx.cpp, generalplus_gpl951xx_rtc.cpp,
// generic_spi_flash.cpp and the generalplus_gpl951xx.cpp driver (David Haywood), BSD-3-Clause, on the
// GPL162xx video / renderer port and the shared u'nSP core and SPG2xx sound device.
// Machines: Basic Fun's Pac-Man, Ms. Pac-Man, Galaga, Dig Dug, Spy Hunter and Tetris mini arcades (25Q80 /
// 25Q16 SPI flash, encrypted as init_fif); Tetris drives its LCD controller by hand (unknown_bftetris_lcdc.cpp) with the chip's 8 KB internal boot ROM (gpl951.bin).
//
// Inputs (as the mini arcade cabinet skin): A 0x01, B 0x02 (Spy Hunter's and Tetris' second button; otherwise also A),
// Start 0x08, Up 0x10, Down 0x20, Left 0x40, Right 0x80.

#pragma once
#include "gpl162xx.h"

class Gpl951Core : public Core, public unsp_device::Bus, public SpgAudioHost
{
public:
    explicit Gpl951Core(const std::string& id = "bfmpac");
    bool load(const std::vector<RomFile>& roms, std::string& error) override;
    void reset() override;
    void runFrame() override;
    const uint32_t* frameBuffer() const override { return m_out.data(); }
    int frameWidth() const override { return 320; }
    int frameHeight() const override { return 240; }
    double refreshRate() const override { return 30.0; }
    int sampleRate() const override { return 44100; }
    size_t readAudio(int16_t* out, size_t maxSamples) override;
    void setInputs(uint32_t pressed) override { m_inputs = pressed; }
    bool saveState(std::vector<uint8_t>&) override { return false; }
    bool loadState(const std::vector<uint8_t>&) override { return false; }

    uint16_t read_word(uint32_t address) override;
    void write_word(uint32_t address, uint16_t data) override;
    uint16_t audio_read_word(uint32_t a) override { return read_word(a); }
    void audio_irq(int s) override { m_cpu.set_state_unsynced(UNSP_IRQ4_LINE, s); }
    void audio_ch_irq(int s) override { m_cpu.set_state_unsynced(UNSP_FIQ_LINE, s); }
    SpgMachine& audio_machine() override { return m_machine; }

    struct MainSpace : address_space {
        Gpl951Core* c;
        uint16_t read_word(uint32_t a) override { return c->read_word(a); }
        void write_word(uint32_t a, uint16_t d) override { c->write_word(a, d); }
    } m_mainspace;

    SpgMachine m_machine;
    SpgScreen m_screen;
    unsp_device m_cpu;
    gpl::gcm394_base_video_device m_video;
    GplAudio m_audio;
    uint16_t m_mainram[0x2800];
    uint16_t m_internal[0x1000];
    std::vector<uint8_t> m_spi;        // decrypted flash, bytes
    u8* m_spiregion = nullptr;
    u32 m_spisize = 0;

    u16 io_read(u32 a);
    void io_write(u32 a, u16 data);

    // gpl_chx_device
    struct Chx {
        Gpl951Core* c = nullptr;
        u16 m_cha_ctrl = 0, m_chb_ctrl = 0, m_cha_fifo[16] = {}, m_chb_fifo[16] = {}, m_cha_fifo_reg = 0, m_chb_fifo_reg = 0;
        u8 m_cha_fifo_readpos = 0, m_cha_fifo_writepos = 0, m_cha_fifo_entries = 0;
        u8 m_chb_fifo_readpos = 0, m_chb_fifo_writepos = 0, m_chb_fifo_entries = 0;
        bool is_cha_fifo_empty_irq() { return m_cha_ctrl & 0x8000; }
        bool is_chb_fifo_empty_irq() { return m_chb_ctrl & 0x8000; }
        void reset() { *this = Chx{ c }; }
        u16 cha_ctrl_r() { return m_cha_ctrl; }
        void cha_ctrl_w(u16 data) { if (data & 0x8000) { data &= 0x7fff; c->update_interrupts(1); } m_cha_ctrl = data; }
        u16 cha_data_r() { return 0xffff; }
        void cha_data_w(u16 data) { if (m_cha_fifo_entries < 16) { m_cha_fifo[m_cha_fifo_writepos] = data; m_cha_fifo_writepos = (m_cha_fifo_writepos + 1) & 0xf; m_cha_fifo_entries++; } }
        u16 cha_fifo_r() { return (m_cha_fifo_reg & 0xfff0) | (m_cha_fifo_entries & 0x000f); }
        void cha_fifo_w(u16 data) { m_cha_fifo_reg = (m_cha_fifo_reg & 0xff0f) | (data & 0x00f0); if (data & 0x0100) { m_cha_fifo_readpos = m_cha_fifo_writepos = m_cha_fifo_entries = 0; } }
        u16 chb_ctrl_r() { return m_chb_ctrl; }
        void chb_ctrl_w(u16 data) { if (data & 0x8000) { data &= 0x7fff; c->update_interrupts(1); } m_chb_ctrl = data; }
        u16 chb_data_r() { return 0xffff; }
        void chb_data_w(u16 data) { if (m_chb_fifo_entries < 16) { m_chb_fifo[m_chb_fifo_writepos] = data; m_chb_fifo_writepos = (m_chb_fifo_writepos + 1) & 0xf; m_chb_fifo_entries++; } }
        u16 chb_fifo_r() { return (m_chb_fifo_reg & 0xfff0) | (m_chb_fifo_entries & 0x000f); }
        void chb_fifo_w(u16 data) { m_chb_fifo_reg = (m_chb_fifo_reg & 0xff0f) | (data & 0x00f0); if (data & 0x0100) { m_chb_fifo_readpos = m_chb_fifo_writepos = m_chb_fifo_entries = 0; } }
        void process_cha_fifo() {
            if (!(m_cha_ctrl & 0x2000)) return;
            if (m_cha_fifo_entries > 0) { u16 d = m_cha_fifo[m_cha_fifo_readpos]; m_cha_fifo_entries--; m_cha_fifo_readpos = (m_cha_fifo_readpos + 1) & 0xf; c->m_dacA.push_back(int16_t(d)); }
            if (m_cha_fifo_entries <= ((m_cha_fifo_reg & 0xf0) >> 4) && (m_cha_ctrl & 0x4000)) { m_cha_ctrl |= 0x8000; c->update_interrupts(1); }
        }
        void process_chb_fifo() {
            if (!(m_chb_ctrl & 0x2000)) return;
            if (m_chb_fifo_entries > 0) { u16 d = m_chb_fifo[m_chb_fifo_readpos]; m_chb_fifo_entries--; m_chb_fifo_readpos = (m_chb_fifo_readpos + 1) & 0xf; c->m_dacB.push_back(int16_t(d)); }
            if (m_chb_fifo_entries <= ((m_chb_fifo_reg & 0xf0) >> 4) && (m_chb_ctrl & 0x4000)) { m_chb_ctrl |= 0x8000; c->update_interrupts(1); }
        }
    } m_chx;
    int16_t m_dac[2] = {};
    std::vector<int16_t> m_dacA, m_dacB;   // channel A / B DAC output this frame (timer G / H rate)

    // gpl951xx_rtc_device
    struct Rtc {
        u8 m_rtc_addr = 0; u16 m_read_dat = 0, m_write_dat = 0; u8 m_reg02 = 0, m_reg40 = 0, m_reg50 = 0;
        u64 m_loadcnt = 0, m_alarmdat = 0, m_timerval = 0; u8 m_ram[0x100] = {};
        u8 readbyte(u8 a) {
            if (a == 0) return 0; if (a == 1) return 1; if (a == 2) return m_reg02;
            if (a >= 0x30 && a <= 0x35) return u8(m_timerval >> ((a - 0x30) * 8));
            if (a == 0x40) return m_reg40; if (a == 0x50) return m_reg50;
            if (a >= 0x90) return m_ram[a];
            return 0;
        }
        void writebyte(u8 a, u8 d) {
            if (a == 2) m_reg02 = d;
            else if (a >= 0x10 && a <= 0x15) { int o = a - 0x10; m_loadcnt = (m_loadcnt & ~(u64(0xff) << (8 * o))) | (u64(d) << (8 * o)); if (o == 5) m_timerval = m_loadcnt; }
            else if (a >= 0x20 && a <= 0x25) { int o = a - 0x20; m_alarmdat = (m_alarmdat & ~(u64(0xff) << (8 * o))) | (u64(d) << (8 * o)); }
            else if (a == 0x40) m_reg40 = d; else if (a == 0x50) m_reg50 = d;
            else if (a >= 0x90) m_ram[a] = d;
        }
        u16 rtc_readdata_r() { return m_read_dat; }
        u16 rtc_ready_r() { return 1; }
        void rtc_ctrl_w(u16) {}
        void rtc_addr_w(u16 d) { m_rtc_addr = u8(d); }
        void rtc_writedata_w(u16 d) { m_write_dat = d; }
        void rtc_request_w(u16 d) { if (d & 1) writebyte(m_rtc_addr, u8(m_write_dat)); if (d & 2) m_read_dat = readbyte(m_rtc_addr); }
    } m_rtc;

    // generic_spi_flash_device (HLE)
    struct SpiFlash {
        const u8* ptr = nullptr; u32 len = 0;
        u8 id[3] = { 0xc8, 0x40, 0x14 };
        int state = 0, step = 0; u32 addr = 0; u8 latch = 0, status = 0;
        void reset() { addr = 0; state = 0; latch = 0; step = 0; }
        void write(u8 d);
        u8 read() const { return latch; }
    } m_flash;
    void spi_reset() { m_flash.reset(); }
    void spi_out(u8 d) { m_flash.write(d); recieve_spi_fifo_data(m_flash.read()); }
    void spi_out_cmd(u8 d) { m_flash.write(d); }
    // unknown_bftetris_lcdc_device (bftetris writes the panel itself)
    struct Lcdc {
        std::vector<u16> buf = std::vector<u16>(512 * 256, 0);
        u16 posx = 0, posy = 0, minx = 0, maxx = 0, miny = 0, maxy = 0;
        u8 cmd = 0, step = 0, on = 0, sleep = 1;
        void command(u16 d) {
            cmd = u8(d); step = 0;
            if (cmd == 0x10) sleep = 1; else if (cmd == 0x11) sleep = 0; else if (cmd == 0x29) on = 1;
        }
        void data(u16 d) {
            if (cmd == 0x2b) {
                if (step == 0) miny = u16(d << 8 | (miny & 0xff)); else if (step == 1) miny = u16((miny & 0xff00) | d);
                else if (step == 2) maxy = u16(d << 8 | (maxy & 0xff)); else if (step == 3) { maxy = u16((maxy & 0xff00) | d); posy = miny; }
            } else if (cmd == 0x2a) {
                if (step == 0) minx = u16(d << 8 | (minx & 0xff)); else if (step == 1) minx = u16((minx & 0xff00) | d);
                else if (step == 2) maxx = u16(d << 8 | (maxx & 0xff)); else if (step == 3) { maxx = u16((maxx & 0xff00) | d); posx = minx; }
            } else if (cmd == 0x2c) {
                buf[(posx + posy * 0x200) & 0x1ffff] = d;
                if (++posx > maxx) { posx = minx; if (++posy > maxy) posy = miny; }
            }
            step++;
        }
    } m_lcdc;
    bool m_useLcdc = false, m_twoButtons = false;
    void i80_cmd_out(u16 d) { m_lcdc.command(d); }
    void i80_data_out(u16 d) { m_lcdc.data(d); }
    u16 port_in(int port);
    void port_out(int, u16) {}
    u16 adc_in(int) { return 0; }

    // generalplus_gpl951xx_device
    void recieve_spi_fifo_data(u8 data);
    u8 get_byte_from_rx_fifo();
    void update_interrupts(int state);
    void videoirq_w(int state) { m_cpu.set_state_unsynced(UNSP_IRQ5_LINE, state); }
    int irq_vector_cb(int irqline) { if (irqline == UNSP_IRQ4_LINE) m_cpu.set_state_unsynced(UNSP_IRQ4_LINE, CLEAR_LINE); return 0; }
    u16 spifc_ctrl_r(); void spifc_ctrl_w(u16 data); u16 spifc_cmd_r(); void spifc_cmd_w(u16 data);
    u16 spifc_para_r(); void spifc_para_w(u16 data); u16 spifc_addrl_r(); void spifc_addrl_w(u16 data);
    u16 spifc_addrh_r(); void spifc_addrh_w(u16 data); u16 spifc_txdat_r(); void spifc_txdat_w(u16 data);
    u16 spifc_rxdat_r(); void spifc_rxdat_w(u16 data); u16 spifc_tx_bc_r(); void spifc_tx_bc_w(u16 data);
    u16 spifc_rx_bc_r(); void spifc_rx_bc_w(u16 data); u16 spifc_timing_r(); void spifc_timing_w(u16 data);
    u16 spifc_ctrl2_r(); void spifc_ctrl2_w(u16 data); u16 spi_improve_r(); void spi_improve_w(u16 data);
    void pm_ctrl_w(u16 data); u16 pllsel_r(); void pllsel_w(u16 data); u16 byte_swap_r(); void byte_swap_w(u16 data);
    template<int Timer> u16 timer_ctrl_r();
    template<int Timer> void timer_ctrl_w(u16 data);
    template<int Timer> u16 timer_preload_r();
    template<int Timer> void timer_preload_w(u16 data);
    template<int Timer> void timer_ccpb_ctrl_w(u16 data);
    template<int Timer> u16 timer_upcount_r();
    template<int Timer> void timer_cb(int param);
    u16 i2c_ctrl_r(); u16 i2c_status_r(); u16 tft_status_r(); void tft_ctrl_w(u16 data); void tft_memmode_wcmd_w(u16 data);
    u16 tft_rgb_ctrl_r(); void tft_rgb_ctrl_w(u16 data); void tft_v_width_w(u16 data); void tft_vsync_setup_w(u16 data);
    void tft_v_start_w(u16 data); void tft_v_end_w(u16 data); void tft_h_width_w(u16 data); void tft_hsync_setup_w(u16 data);
    void tft_h_start_w(u16 data); void tft_h_end_w(u16 data); void tft_v_show_start_w(u16 data); void tft_v_show_end_w(u16 data);
    void tft_h_show_start_w(u16 data); void tft_h_show_end_w(u16 data); void free_height_w(u16 data); void free_width_w(u16 data);
    u16 spi_bank_r(); void spi_bank_w(u16 data);
    void adc_timer_cb(int param);
    u16 madc_ctrl_r(); void madc_ctrl_w(u16 data); u16 madc_data_r();
    template<int Port> u16 io_data_r(); template<int Port> void io_data_w(u16 data);
    template<int Port> u16 io_buffer_r(); template<int Port> void io_buffer_w(u16 data);
    template<int Port> u16 io_dir_r(); template<int Port> void io_dir_w(u16 data);
    template<int Port> u16 io_attrib_r(); template<int Port> void io_attrib_w(u16 data);
    template<int Port> u16 io_drv_r(); template<int Port> void io_drv_w(u16 data);
    template<int Port> u16 io_mux_r(); template<int Port> void io_mux_w(u16 data);
    template<int Port> u16 io_latch_r(); template<int Port> void io_latch_w(u16 data);
    template<int Port> u16 io_keyen_r(); template<int Port> void io_keyen_w(u16 data);
    u16 sys_ctrl_r(); void sys_ctrl_w(u16 data); void clock_ctrl_w(u16 data); u16 clk_ctrl0_r(); void clk_ctrl0_w(u16 data);
    void watchdog_ctrl_w(u16 data); u16 power_state_r(); u16 pllclkwait_r(); void pllclkwait_w(u16 data);
    u16 cache_ctrl_r(); void cache_ctrl_w(u16 data);
    u16 int_status1_r(); void int_status1_w(u16 data); u16 int_status2_r(); void int_status2_w(u16 data);
    u16 int_status3_r(); void int_status3_w(u16 data);
    u16 int_priority_1_r(); void int_priority_1_w(u16 data); u16 int_priority_2_r(); void int_priority_2_w(u16 data);
    u16 int_priority_3_r(); void int_priority_3_w(u16 data); void mint_ctrl_w(u16 data);
    u16 spi_direct_r(offs_t offset); u16 spi_direct_bank_r(offs_t offset);

    u16 m_byteswap = 0, m_timer_preload[8] = {}, m_timer_ctrl[8] = {};
    u16 m_spifc_ctrl = 0, m_spifc_ctrl2 = 0; u32 m_spifc_addr = 0; u16 m_spifc_cmd = 0, m_spifc_para = 0, m_spifc_rx_bc = 0, m_spifc_tx_bc = 0, m_spifc_timing = 0;
    u8 m_bytes_in_spifc_rx_fifo = 0, m_spifc_rx_fifo[32] = {}; u16 m_spifc_rx_read_latch = 0;
    u16 m_io_dir[6] = {}, m_io_attrib[6] = {};
    u16 m_sys_ctrl = 0, m_clock_ctrl = 0, m_cache_ctrl = 0, m_int_priority_1 = 0, m_int_priority_2 = 0, m_int_priority_3 = 0, m_misc_int_ctrl = 0;
    u16 m_pllchange = 0, m_spi_bank = 0, m_memmode_wcmd = 0, m_tft_rgb_ctrl = 0, m_madc_ctrl = 0, m_madc_data = 0;
    emu_timer* m_timer[8] = {};
    emu_timer* m_adc_timer = nullptr;

    // gpl_timebase_device / gpl_dma_device
    u16 m_timebase_ctrl[3] = {}, m_timebase_reset = 0;
    emu_timer* m_timebasetimer[3] = {};
    bool timebase_irq_flag(int t) const { return (m_timebase_ctrl[t] & 0x8000) && (m_timebase_ctrl[t] & 0x4000); }
    void timebase_ctrl_w(int t, u16 data);
    u16 timebasea_ctrl_r() { return m_timebase_ctrl[0]; }
    u16 timebaseb_ctrl_r() { return m_timebase_ctrl[1]; }
    u16 timebasec_ctrl_r() { return m_timebase_ctrl[2]; }
    void timebasea_ctrl_w(u16 d) { timebase_ctrl_w(0, d); }
    void timebaseb_ctrl_w(u16 d) { timebase_ctrl_w(1, d); }
    void timebasec_ctrl_w(u16 d) { timebase_ctrl_w(2, d); }
    void timebase_reset_w(u16 d) { m_timebase_reset = d; }
    u16 m_dma_params[8][4] = {};
    bool m_dma_latched[4] = {};
    u16 m_system_dma_memtype = 0, m_dma_status = 0;
    void write_dma_params(int channel, int offset, u16 data);
    void trigger_systemm_dma(int channel);
    u16 system_dma_params_channel0_r(offs_t o) { return m_dma_params[o][0]; }
    u16 system_dma_params_channel1_r(offs_t o) { return m_dma_params[o][1]; }
    void system_dma_params_channel0_w(offs_t o, u16 d) { write_dma_params(0, o, d); }
    void system_dma_params_channel1_w(offs_t o, u16 d) { write_dma_params(1, o, d); }
    u16 system_dma_status_r() { return m_dma_status; }
    void system_dma_status_w(u16 d) { m_dma_status &= ~d; }
    u16 system_dma_memtype_r() { return m_system_dma_memtype; }
    void system_dma_memtype_w(u16 d) { m_system_dma_memtype = d; }

private:
    void startDevices();
    void vblank_begin();
    void vblank_end();
    void reset_partial_updates();
    bool update_partial(int scanline);
    emu_timer* m_vblank_begin_timer = nullptr;
    emu_timer* m_vblank_end_timer = nullptr;
    emu_timer* m_scanline0_timer = nullptr;
    emu_timer* m_sound_update_timer = nullptr;
    bool m_frameDone = false;
    std::vector<uint32_t> m_bitmap, m_out;
    uint32_t m_inputs = 0;
    std::vector<int16_t> m_samples;
    double m_resampleAcc = 0;
    int32_t m_resampleSum = 0, m_resampleN = 0;
    double m_dcPrevIn = 0, m_dcPrevOut = 0;
};
