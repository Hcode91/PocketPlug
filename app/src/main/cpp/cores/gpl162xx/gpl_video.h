// PocketPlug port of MAME generalplus_gpl162xx_soc_video (license:BSD-3-Clause, Ryan Holtz, David Haywood)
/*****************************************************************************

    SunPlus GPL162xx-series SoC peripheral emulation (Video)

**********************************************************************/

#ifndef MAME_MACHINE_GENERALPLUS_GPL162XX_SOC_VIDEO_H
#define MAME_MACHINE_GENERALPLUS_GPL162XX_SOC_VIDEO_H

#pragma once

#include "gpl_renderer.h"

#include <functional>
namespace gpl {


class gcm394_base_video_device
{
public:

	void set_video_space(address_space* s) { m_cpuspace = s; m_renderer->set_video_space(s); }
	void set_cs_video_space(address_space* s, u32 csbase) { m_cs_space = s; m_csbase = csbase; m_renderer->set_cs_video_space(s, csbase); }

	u32 screen_update(bitmap_rgb32 &bitmap, const rectangle &cliprect);
	void vblank(int state);

	void write_tmap_scroll(int tmap, u16 *regs, int offset, u16 data);
	void write_tmap_extrascroll(int tmap, u16 *regs, int offset, u16 data);

	void write_tmap_regs(int tmap, u16 *regs, int offset, u16 data);

	void set_legacy_video_mode() { m_use_legacy_mode = true; }
	void set_disallow_resolution_control() { m_disallow_resolution_control = true; }

	void set_has_vga_modes() { m_has_vga_modes = true; } // GPL1624x and above add VGA modes
	void set_has_3d_sprite_modes() { m_has_3d_sprites = true; } // GPL1625x and above add VGA modes

	void set_has_gpl162xx_b_features() { m_has_gpl162xx_b_features = true; } // B models have different sprite/tilemap palette handling etc.
	void set_has_gpl162xx_b_extended_sprites() { m_has_gpl162xx_b_extended_sprites = true; } // higher numbered B models have extended sprites


	u16 tmap0_regs_r(offs_t offset);
	void tmap0_regs_w(offs_t offset, u16 data);
	u16 tmap0_tilebase_lsb_r();
	u16 tmap0_tilebase_msb_r();
	void tmap0_tilebase_lsb_w(u16 data);
	void tmap0_tilebase_msb_w(u16 data);

	u16 tmap1_regs_r(offs_t offset);
	void tmap1_regs_w(offs_t offset, u16 data);
	u16 tmap1_tilebase_lsb_r();
	u16 tmap1_tilebase_msb_r();
	void tmap1_tilebase_lsb_w(u16 data);
	void tmap1_tilebase_msb_w(u16 data);

	u16 tmap2_regs_r(offs_t offset);
	void tmap2_regs_w(offs_t offset, u16 data);
	u16 tmap2_tilebase_lsb_r();
	u16 tmap2_tilebase_msb_r();
	void tmap2_tilebase_lsb_w(u16 data);
	void tmap2_tilebase_msb_w(u16 data);

	u16 tmap3_regs_r(offs_t offset);
	void tmap3_regs_w(offs_t offset, u16 data);
	u16 tmap3_tilebase_lsb_r();
	u16 tmap3_tilebase_msb_r();
	void tmap3_tilebase_lsb_w(u16 data);
	void tmap3_tilebase_msb_w(u16 data);

	void vcomp_value_w(u16 data);
	void vcomp_offset_w(u16 data);
	void vcomp_step_w(u16 data);

	u16 sprite_7022_gfxbase_lsb_r();
	u16 sprite_702d_gfxbase_msb_r();

	void sprite_7022_gfxbase_lsb_w(u16 data);
	void sprite_702d_gfxbase_msb_w(u16 data);
	u16 sprite_7042_extra_r();
	void sprite_7042_extra_w(u16 data);

	void video_dma_source_w(u16 data);
	void video_dma_dest_w(u16 data);
	u16 video_dma_size_busy_r();
	void video_dma_size_trigger_w(u16 data);
	void ppu_ram_bank_w(u16 data);

	u16 video_703a_palettebank_r();
	void video_703a_palettebank_w(u16 data);

	void update_raster_split_position();
	void split_irq_xpos_w(u16 data);
	void split_irq_ypos_w(u16 data);

	u16 videoirq_source_enable_r();
	void videoirq_source_enable_w(u16 data);

	u16 video_7063_videoirq_source_r();
	void video_7063_videoirq_source_ack_w(u16 data);

	void blending_w(u16 data);
	u16 video_7030_brightness_r();
	void video_7030_brightness_w(u16 data);
	u16 video_curline_r();

	u16 video_703c_tvcontrol1_r();
	void video_703c_tvcontrol1_w(u16 data);

	u16 video_707c_r();

	u16 ppu_enable_r();
	void ppu_enable_w(u16 data);

	void tv_saturation_w(u16 data);
	void tv_hue_w(u16 data);
	void tv_brightness_w(u16 data);
	void tv_sharpness_w(u16 data);
	void tv_y_gain_w(u16 data);
	void tv_y_delay_w(u16 data);
	void tv_v_position_w(u16 data);
	void tv_h_position_w(u16 data);
	void tv_videodac_w(u16 data);

	u16 tv_sharpness_r();

	u16 palette_r(offs_t offset);
	void palette_w(offs_t offset, u16 data);

	void spriteram_w(offs_t offset, u16 data);
	u16 spriteram_r(offs_t offset);

	u16 video_7051_r();
	u16 video_70e0_prng_r();

public:
	void device_start(SpgMachine& m);
	void device_reset();

	
	SpgScreen* m_screen = nullptr;
	std::function<void(int)> m_video_irq_cb;
	std::function<void(int,int)> m_set_visible;   // width, height
	std::function<void(int)> m_update_partial;

public:
	void screen_pos_reached(int param);

	void check_video_irq();

	void unk_vid_regs_w(int which, int offset, u16 data);


	u16 m_rowscroll[0x100];
	u16 m_rowzoom[0x100];

	gpl_renderer_device m_renderer_obj;
	gpl_renderer_device* m_renderer = &m_renderer_obj;

	address_space* m_cpuspace = nullptr;
	address_space* m_cs_space = nullptr;
	int m_csbase = 0;

	u16 m_page0_addr_lsb;
	u16 m_page0_addr_msb;

	u16 m_page1_addr_lsb;
	u16 m_page1_addr_msb;

	u16 m_707e_spritebank;
	u16 m_videodma_size;
	u16 m_videodma_dest;
	u16 m_videodma_source;

	u16 m_tmap0_regs[0x4];
	u16 m_tmap1_regs[0x4];

	u16 m_tmap2_regs[0x4];
	u16 m_tmap3_regs[0x4];


	u16 m_tmap0_scroll[0x2];
	u16 m_tmap1_scroll[0x2];

	u16 m_tmap2_scroll[0x4];
	u16 m_tmap3_scroll[0x4];


	u16 m_707f;
	u16 m_703a_palettebank;
	u16 m_video_irq_enable;
	u16 m_video_irq_status;

	u16 m_702a;
	u16 m_7030_brightness;
	u16 m_xirqpos;
	u16 m_yirqpos;
	u16 m_703c_tvcontrol1;

	u16 m_7042_sprite;

	u16 m_7080;
	u16 m_7081;
	u16 m_7082;
	u16 m_7083;
	u16 m_7084;
	u16 m_7085;
	u16 m_7086;
	u16 m_7087;
	u16 m_7088;

	u16 m_sprite_7022_gfxbase_lsb;
	u16 m_sprite_702d_gfxbase_msb;
	u16 m_page2_addr_lsb;
	u16 m_page2_addr_msb;
	u16 m_page3_addr_lsb;
	u16 m_page3_addr_msb;

	emu_timer *m_screenpos_timer;

	u16 m_spriteram[0x400*2];
	u16 m_paletteram[0x100*0x10];


	// TODO: this is a hack, emulate relevant registers
	bool m_use_legacy_mode = false; // could be related to the 'unused' bits in the palete bank select being set, but uncertain

	// used for LCD games with fixed (croppted) resolution output, these might be enabling something else we can check
	bool m_disallow_resolution_control = false;

	// TODO: these could be handled by sub-classing the device, but until this gets refactored at least set these
	//       to represent the differences
	bool m_has_vga_modes = false;  // 4x / 5x parts only (V in the part number)
	bool m_has_3d_sprites = false; // 5x parts only
	bool m_has_gpl162xx_b_features = false;
	bool m_has_gpl162xx_b_extended_sprites = false; // could probably just check if it's a B model and has VGA support
};

}  // namespace gpl

#endif // MAME_MACHINE_GENERALPLUS_GPL162XX_SOC_VIDEO_H
