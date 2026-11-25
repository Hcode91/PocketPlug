// PocketPlug - GeneralPlus GPL162xx B-series (u'nSP 2.0) TV games / handhelds
// SPDX-License-Identifier: BSD-3-Clause
//
// Port of MAME's GPL162xx support: generalplus_gpl162xx_soc.cpp, generalplus_gpl162xx_b_soc.cpp,
// generalplus_gpl162xx_soc_video.cpp, gpl_renderer.cpp, generalplus_gpl_dma.cpp,
// generalplus_gpl_timebase.cpp (David Haywood, Ryan Holtz) and the drivers in
// generalplus_gpl16250*.cpp (David Haywood), all BSD-3-Clause. The u'nSP CPU (GPL-2.0+) and the
// SPG2xx sound device are shared with the spg2xx core; scheduling and timers use spg_lite.h.
//
// Machines: beijuehh (GPL16218B, 220-in-1 handheld, ROM in 4 MB banks), beambox (GPL16258VB
// with a 64 MB NAND; the internal boot ROM is simulated as MAME does), bkrankp (GPL16250 booting
// from SPI flash, generalplus_gpl16250_spi.cpp: the first copy is simulated as MAME does).

#pragma once
#include "../../core_api.h"
#include "../spg2xx/spg2xx.h"
#include "gpl_video.h"
#include <string>
#include <vector>
#include <deque>

using gpl::address_space;

class GplAudio : public spg2xx_audio_device
{
public:
    // 7b80-7b9f: the SPG2xx sound control registers for channels 0-15 (channel enable, main
    // volume, FIQ, beat, envelope clocks, ...); 7ba0-7bbf: the same for channels 16-31, which
    // the 16-channel sound device doesn't have (MAME leaves both unconnected)
    uint16_t control_r(offs_t offset) { return (offset & 0x20) ? m_control[1][offset & 0x1f] : audio_ctrl_r(offset & 0x1f); }
    void control_w(offs_t offset, uint16_t data) { if (offset & 0x20) m_control[1][offset & 0x1f] = data; else audio_ctrl_w(offset & 0x1f, data); }
    uint16_t m_control[2][0x20] = {};
};

class Gpl162xxCore : public Core, public unsp_device::Bus, public SpgAudioHost
{
public:
    enum Kind { BEIJUEHH, BEAMBOX, SPISPI };
    explicit Gpl162xxCore(Kind k);
    ~Gpl162xxCore() override;
    bool load(const std::vector<RomFile>& roms, std::string& error) override;
    bool wantsRomPaths() const override { return true; }
    void reset() override;
    void runFrame() override;
    const uint32_t* frameBuffer() const override { return m_out.data(); }
    int frameWidth() const override { return m_outW; }
    int frameHeight() const override { return m_outH; }
    double refreshRate() const override { return 60.0; }
    int sampleRate() const override { return 44100; }
    size_t readAudio(int16_t* out, size_t maxSamples) override;
    void setInputs(uint32_t pressed) override {
        if (m_kind != SPISPI) { m_inputs = pressed; return; }
        // HandheldView bits -> PA10-15 / PB10-11 (raw bits 0-7): Start = row jump (PA10),
        // Down / Up = next / previous letter (PA12 / PA13), Right / Left = input mode (PA14 / PA15),
        // A = OK (PB10), B = quit (PB11)
        uint32_t r = 0;
        if (pressed & 0x40) r |= 0x01;
        if (pressed & 0x02) r |= 0x04;
        if (pressed & 0x01) r |= 0x08;
        if (pressed & 0x08) r |= 0x10;
        if (pressed & 0x04) r |= 0x20;
        if (pressed & 0x10) r |= 0x40;
        if (pressed & 0x20) r |= 0x80;
        m_inputs = r;
    }
    bool saveState(std::vector<uint8_t>&) override { return false; }
    bool loadState(const std::vector<uint8_t>&) override { return false; }

    // u'nSP bus (the CPU's program space)
    uint16_t read_word(uint32_t address) override;
    void write_word(uint32_t address, uint16_t data) override;
    // sound host
    uint16_t audio_read_word(uint32_t a) override { return read_space(a); }
    // MAME leaves the sound interrupts unconnected; the beat interrupt drives the games' music
    // players, so it goes to IRQ4 (acknowledged by irq_vector_cb) and channel IRQs to FIQ, as on the SPG2xx
    void audio_irq(int s) override { m_cpu.set_state_unsynced(UNSP_IRQ4_LINE, s); }
    void audio_ch_irq(int s) override { m_cpu.set_state_unsynced(UNSP_FIQ_LINE, s); }
    SpgMachine& audio_machine() override { return m_machine; }

    // ---- the SoC (generalplus_gpl162xx_base_device) ----
    struct MainSpace : address_space {
        Gpl162xxCore* c;
        uint16_t read_word(uint32_t a) override { return c->read_word(a); }
        void write_word(uint32_t a, uint16_t d) override { c->write_word(a, d); }
    } m_mainspace;
    struct CsSpace : address_space {
        Gpl162xxCore* c;
        uint16_t read_word(uint32_t a) override { return c->cs_read(a); }
        void write_word(uint32_t a, uint16_t d) override { c->cs_write(a, d); }
    } m_csspace;
    address_space* m_cs_space = &m_csspace;

    SpgMachine m_machine;
    SpgScreen m_screen;
    unsp_device m_cpu;
    gpl::gcm394_base_video_device m_video;
    GplAudio m_audio;
    uint16_t m_mainram[0x7000];
    uint32_t m_ramTop = 0x3000;

    u16 io_read_16218b(u32 a);
    void io_write_16218b(u32 a, u16 data);
    u16 io_read_16258vb(u32 a);
    void io_write_16258vb(u32 a, u16 data);

    // board hooks
    u16 porta_in(); u16 portb_in(); u16 portc_in(); u16 portd_in();
    void porta_out(u16 data); void portb_out(u16 data); void portc_out(u16 data); void portd_out(u16 data);
    void cs_callback(u16 cs0, u16 cs1, u16 cs2, u16 cs3, u16 cs4);
    u16 cs_read(u32 a);
    void cs_write(u32 a, u16 d);
    u16 cs0_r(offs_t offset);
    void cs0_w(offs_t offset, u16 data);
    u16 cs1_r(offs_t offset);
    void cs1_w(offs_t offset, u16 data);
    u32 m_csStart[5] = {}, m_csEnd[5] = {};
    void dma_complete_cb(int) {}
    u16 nand_data_in();
    void nand_data_out(u8 d);
    void nand_command_out(u8 d);
    void nand_address_out(u8 d);

    // generalplus_gpl162xx_base_device
    u16 cs_space_r(offs_t offset);
    void cs_space_w(offs_t offset, u16 data);
    u16 cs_bank_space_r(offs_t offset);
    void cs_bank_space_w(offs_t offset, u16 data);
    u16 internalrom_lower32_r(offs_t offset);
    u16 cs_space_boot_mirror_r(offs_t offset);
    void timer_cb(int param);
    void scheduler_cb(int param);
    u16 unk_r(offs_t offset);
    void unk_w(offs_t offset, u16 data);
    u16 power_state_r();
    u16 dac_pga_r();
    u16 sys_ctrl_r();
    void sys_ctrl_w(u16 data);
    void clock_ctrl_w(u16 data);
    u16 clk_ctrl0_r();
    void clk_ctrl0_w(u16 data);
    void waitmode_enter_780c_w(u16 data);
    u16 membankswitch_7810_r();
    void membankswitch_7810_w(u16 data);
    void unkarea_7816_w(u16 data);
    void pllchange_w(u16 data);
    u16 pllclkwait_r();
    void pllclkwait_w(u16 data);
    void watchdog_ctrl_w(u16 data);
    u16 cache_ctrl_r();
    void cache_ctrl_w(u16 data);
    void chipselect_csx_memory_device_control_w(offs_t offset, u16 data);
    void mcs0_page_w(u16 data);
    u16 raw_war_r();
    void raw_war_w(u16 data);
    u16 ioa_data_r(); void ioa_data_w(u16 data); u16 ioa_buffer_r(); void ioa_buffer_w(u16 data);
    u16 ioa_dir_r(); void ioa_dir_w(u16 data); u16 ioa_attrib_r(); void ioa_attrib_w(u16 data);
    u16 iob_data_r(); void iob_data_w(u16 data); u16 iob_buffer_r(); void iob_buffer_w(u16 data);
    u16 iob_dir_r(); void iob_dir_w(u16 data); u16 iob_attrib_r(); void iob_attrib_w(u16 data);
    u16 ioc_data_r(); void ioc_data_w(u16 data); u16 ioc_buffer_r(); void ioc_buffer_w(u16 data);
    u16 ioc_dir_r(); void ioc_dir_w(u16 data); u16 ioc_attrib_r(); void ioc_attrib_w(u16 data);
    u16 iod_data_r(); void iod_data_w(u16 data); u16 iod_buffer_r(); void iod_buffer_w(u16 data);
    u16 iod_dir_r(); void iod_dir_w(u16 data); u16 iod_attib_r(); void iod_attib_w(u16 data);
    u16 iod_drv_r(); void iod_drv_w(u16 data); u16 iod_mux_r(); void iod_mux_w(u16 data);
    u16 ioe_buffer_r(); void ioe_buffer_w(u16 data); u16 ioe_dir_r(); void ioe_dir_w(u16 data);
    u16 ioe_attrib_r(); void ioe_attrib_w(u16 data);
    void int_status1_w(u16 data); void int_status2_w(u16 data); void int_status3_w(u16 data);
    u16 int_status1_r(); u16 int_status2_r(); u16 int_status3_r();
    void int_priority_1_w(u16 data); void int_priority_2_w(u16 data); void int_priority_3_w(u16 data);
    void mint_ctrl_w(u16 data);
    void update_interrupts(int state);
    template<int Timer> u16 timer_ctrl_r();
    template<int Timer> void timer_ctrl_w(u16 data);
    template<int Timer> u16 timer_preload_r();
    template<int Timer> void timer_preload_w(u16 data);
    template<int Timer> u16 timer_ccp_ctrl_r();
    template<int Timer> void timer_ccp_ctrl_w(u16 data);
    template<int Timer> u16 timer_cc_reg_r();
    template<int Timer> void timer_cc_reg_w(u16 data);
    template<int Timer> u16 timer_upcount_r();
    u16 cha_ctrl_r(); void cha_ctrl_w(u16 data);
    u16 uart_status_r();
    u16 rtc_ctrl_r(); void rtc_ctrl_w(u16 data);
    u16 rtc_int_status_r(); void rtc_int_status_w(u16 data);
    u16 rtc_int_ctrl_r(); void rtc_int_ctrl_w(u16 data);
    u16 spi_7944_rxdata_r(); u16 spi_7945_misc_control_reg_r(); void spi_7942_txdata_w(u16 data);
    void adc_setup_w(u16 data); u16 madc_ctrl_r(); void madc_ctrl_w(u16 data); u16 madc_data_r();
    void videoirq_w(int state);
    void audioirq_w(int state);
    u16 usb_7a35_r(); u16 usb_7a37_r(); u16 usb_7a39_r(); u16 usb_7a3a_r(); u16 usb_7a46_r(); u16 usb_7a54_r();
    u16 read_space(offs_t offset);
    void write_space(offs_t offset, u16 data);
    void dma_complete(int state);
    int irq_vector_cb(int irqline);
    u16 nand_7850_status_r(); u16 nand_data_r(); void nand_data_w(u16 data); void nand_dma_ctrl_w(u16 data);
    void nand_7850_w(u16 data); void nand_command_w(u16 data); void nand_addr_low_w(u16 data); void nand_addr_high_w(u16 data);
    u16 nand_ecc_err1_lb_r(); void nand_bch_ctrl_w(u16 data); void nand_ecc_ctrl_w(u16 data);
    void nand_ecc_lpr_ckl_lb_w(u16 data); void nand_ecc_lpr_ckh_lb_w(u16 data); void nand_ecc_cpckr_lb_w(u16 data);
    u16 nand_ecc_err0_lb_r(); u16 spi_rxstatus_r(); u16 efuse2_r();
    void default_cs_callback(u16, u16, u16, u16, u16) {}

    u32 m_csbase = 0x20000;
    u16 m_sys_ctrl = 0, m_clock_ctrl = 0, m_membankswitch_7810 = 0, m_7816 = 0, m_pllchange = 0, m_cache_ctrl = 0;
    u16 m_782x[5] = {}, m_782d = 0, m_7835 = 0;
    u16 m_7862_porta_direction = 0, m_7863_porta_attribute = 0, m_786a_portb_direction = 0, m_786b_portb_attribute = 0;
    u16 m_7872_portc_direction = 0, m_7873_portc_attribute = 0, m_787a_portd_direction = 0, m_787b_portd_attribute = 0;
    u16 m_ioc_data = 0, m_ioe_dir = 0, m_ioe_attrib = 0;
    u16 m_int_status1 = 0, m_int_priority_1 = 0, m_int_priority_2 = 0, m_int_priority_3 = 0, m_misc_int_ctrl = 0;
    u16 m_cha_ctrl = 0, m_dac_pga = 0, m_rtc_ctrl = 0, m_rtc_int_status = 0, m_rtc_int_ctrl = 0, m_adc_setup = 0, m_madc_ctrl = 0;
    u16 m_timer_ctrl[6] = {}, m_timer_preload[6] = {}, m_timer_ccp_ctrl[3] = {}, m_timer_cc_reg[3] = {};
    u16 m_nand_addr_low = 0, m_nand_addr_high = 0, m_nand_dma_ctrl = 0, m_nand_ctrl = 0;
    u16 m_nand_ecc_cpckr_lb = 0, m_nand_ecc_lpr_ckh_lb = 0, m_nand_ecc_lpr_ckl_lb = 0, m_nand_bch_ctrl = 0, m_nand_ecc_ctrl = 0;
    emu_timer* m_scheduler = nullptr;
    bool m_disable_timebase_interrupts = false;
    int m_boot_mode = 1;

    // generalplus_gpl162xx_b_base
    u16 m_esp_ctrl = 0, m_byteswap = 0;
    u16 esp_ctrl_r() { return m_esp_ctrl; }
    void esp_ctrl_w(u16 d) { m_esp_ctrl = d; }
    u16 byteswap_r() { return ((m_byteswap & 0xff00) >> 8) | ((m_byteswap & 0x00ff) << 8); }
    void byteswap_w(u16 d) { m_byteswap = d; }

    // gpl_timebase_device
    u16 m_timebase_ctrl[3] = {}, m_timebase_reset = 0;
    emu_timer* m_timebasetimer[3] = {};
    bool timebase_irq_flag(int t) const { return (m_timebase_ctrl[t] & 0x8000) && (m_timebase_ctrl[t] & 0x4000); }
    attotime get_timer_period(int timer, int ctrlval);
    void timebase_cb(int t);
    void timebase_ctrl_w(int t, u16 data);
    u16 timebasea_ctrl_r() { return m_timebase_ctrl[0]; }
    u16 timebaseb_ctrl_r() { return m_timebase_ctrl[1]; }
    u16 timebasec_ctrl_r() { return m_timebase_ctrl[2]; }
    void timebasea_ctrl_w(u16 d) { timebase_ctrl_w(0, d); }
    void timebaseb_ctrl_w(u16 d) { timebase_ctrl_w(1, d); }
    void timebasec_ctrl_w(u16 d) { timebase_ctrl_w(2, d); }
    void timebase_reset_w(u16 d) { m_timebase_reset = d; }

    // gpl_dma_device
    u16 m_dma_params[8][4] = {};
    bool m_dma_latched[4] = {};
    u16 m_system_dma_memtype = 0, m_dma_status = 0;
    u16 read_dma_params(int channel, int offset) { return m_dma_params[offset][channel]; }
    void write_dma_params(int channel, int offset, u16 data);
    void trigger_systemm_dma(int channel);
    u16 system_dma_params_channel0_r(offs_t o) { return read_dma_params(0, o); }
    u16 system_dma_params_channel1_r(offs_t o) { return read_dma_params(1, o); }
    u16 system_dma_params_channel2_r(offs_t o) { return read_dma_params(2, o); }
    u16 system_dma_params_channel3_r(offs_t o) { return read_dma_params(3, o); }
    void system_dma_params_channel0_w(offs_t o, u16 d) { write_dma_params(0, o, d); }
    void system_dma_params_channel1_w(offs_t o, u16 d) { write_dma_params(1, o, d); }
    void system_dma_params_channel2_w(offs_t o, u16 d) { write_dma_params(2, o, d); }
    void system_dma_params_channel3_w(offs_t o, u16 d) { write_dma_params(3, o, d); }
    u16 system_dma_status_r() { return m_dma_status; }
    void system_dma_status_w(u16 d) { m_dma_status &= ~d; }
    u16 system_dma_memtype_r() { return m_system_dma_memtype; }
    void system_dma_memtype_w(u16 d) { m_system_dma_memtype = d; }

public:
    Kind m_kind;
    uint32_t m_cpuClock;
    // ROM / NAND image (mapped)
    uint16_t* m_rom = nullptr;
    uint64_t m_romWords = 0;
    void* m_map = nullptr;
    size_t m_mapLen = 0;
    std::vector<uint16_t> m_romCopy;
    // beijuehh banking
    u32 m_upperbase = 0;
    u8 m_bank = 0;
    u16 m_portb_data = 0, m_portd_data = 0;
    // beambox: SDRAM on cs0 / cs1, NAND, simulated boot ROM
    std::vector<uint16_t> m_sdram, m_sdram2;
    uint16_t m_internal[0x8000] = {};
    std::vector<uint8_t> m_stripped;
    int m_initialCopyWords = 0x2000;
    uint32_t m_vectorbase = 0x2fe0;
    void nandInit();
    // SPI-flash type (generalplus_gpl16250_spi.cpp): a serial flash on the SPI port, read with
    // 03 / 0b commands; MAME simulates the boot ROM's first copy (init_spi) and so do we
    struct SpiFlash { int state = 0; uint32_t addr = 0; std::deque<uint8_t> rx; } m_spi;
    void spiTx(uint8_t d);
    uint8_t spiRead();
    // the NAND chip (512 + 16 byte pages)
    struct Nand {
        int mode = 0;          // 0 idle, 1 read, 2 status, 3 id
        int addrCount = 0;
        uint32_t col = 0, row = 0;
        uint32_t pos = 0;
        uint8_t cmd = 0;
        int idPos = 0;
    } m_nand;

    // screen / timing
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
    int m_visW = 320, m_visH = 240;
    std::vector<uint32_t> m_bitmap, m_out;
    int m_outW = 320, m_outH = 240;
    uint32_t m_inputs = 0;
    std::vector<int16_t> m_samples;
    double m_resampleAcc = 0;
    int32_t m_resampleSum = 0, m_resampleN = 0;
    double m_dcPrevIn = 0, m_dcPrevOut = 0;
};
