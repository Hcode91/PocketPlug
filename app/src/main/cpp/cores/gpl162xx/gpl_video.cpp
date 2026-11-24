// PocketPlug port of MAME generalplus_gpl162xx_soc_video.cpp (license:BSD-3-Clause, Ryan Holtz, David Haywood)
/*****************************************************************************

    SunPlus GPL162xx-series SoC peripheral emulation (Video)

    This is very similar to SPG2xx but with additional features, layers and modes

**********************************************************************/


#include "gpl_video.h"
namespace gpl {













void gcm394_base_video_device::device_start(SpgMachine& m)
{

	// debug helper only

	m_screenpos_timer = m.timer_alloc([](void* c, int p) { ((gcm394_base_video_device*)c)->screen_pos_reached(p); }, this);
	m_renderer->device_start();
	m_screenpos_timer->adjust(attotime::never());

	save_item(NAME(m_page0_addr_lsb));
	save_item(NAME(m_page0_addr_msb));
	save_item(NAME(m_page1_addr_lsb));
	save_item(NAME(m_page1_addr_msb));
	save_item(NAME(m_707e_spritebank));
	save_item(NAME(m_videodma_size));
	save_item(NAME(m_videodma_dest));
	save_item(NAME(m_videodma_source));
	save_item(NAME(m_tmap0_regs));
	save_item(NAME(m_tmap1_regs));
	save_item(NAME(m_tmap2_regs));
	save_item(NAME(m_tmap3_regs));
	save_item(NAME(m_tmap0_scroll));
	save_item(NAME(m_tmap1_scroll));
	save_item(NAME(m_tmap2_scroll));
	save_item(NAME(m_tmap3_scroll));
	save_item(NAME(m_707f));
	save_item(NAME(m_703a_palettebank));
	save_item(NAME(m_video_irq_enable));
	save_item(NAME(m_video_irq_status));
	save_item(NAME(m_702a));
	save_item(NAME(m_7030_brightness));
	save_item(NAME(m_xirqpos));
	save_item(NAME(m_yirqpos));
	save_item(NAME(m_703c_tvcontrol1));
	save_item(NAME(m_7042_sprite));
	save_item(NAME(m_7080));
	save_item(NAME(m_7081));
	save_item(NAME(m_7082));
	save_item(NAME(m_7083));
	save_item(NAME(m_7084));
	save_item(NAME(m_7085));
	save_item(NAME(m_7086));
	save_item(NAME(m_7087));
	save_item(NAME(m_7088));
	save_item(NAME(m_sprite_7022_gfxbase_lsb));
	save_item(NAME(m_sprite_702d_gfxbase_msb));
	save_item(NAME(m_page2_addr_lsb));
	save_item(NAME(m_page2_addr_msb));
	save_item(NAME(m_page3_addr_lsb));
	save_item(NAME(m_page3_addr_msb));
	save_item(NAME(m_spriteram));
	save_item(NAME(m_paletteram));
	save_item(NAME(m_maxgfxelement));
}

void gcm394_base_video_device::device_reset()
{
	for (int i = 0; i < 4; i++)
	{
		m_tmap0_regs[i] = 0x0000;
		m_tmap1_regs[i] = 0x0000;
		m_tmap2_regs[i] = 0x0000;
		m_tmap3_regs[i] = 0x0000;
		m_tmap2_scroll[i] = 0x0000;
		m_tmap3_scroll[i] = 0x0000;
	}

	for (int i = 0; i < 2; i++)
	{
		m_tmap0_scroll[i] = 0x0000;
		m_tmap1_scroll[i] = 0x0000;
	}

	for (int i = 0; i < 0x400*2; i++)
	{
		m_spriteram[i] = 0x0000;
	}

	for (int i=0;i<0x100 * 0x10;i++)
		m_paletteram[i] = rand() & 0x7fff;

	m_707f = 0x0001;
	m_703a_palettebank = 0x0000;
	m_video_irq_enable = 0x0000;
	m_video_irq_status = 0x0000;

	m_702a = 0x0000;
	m_7030_brightness = 0x0000;
	m_xirqpos = 0x0000;
	m_yirqpos = 0x0000;
	m_703c_tvcontrol1 = 0x0000;

	m_7042_sprite = 0x0000;

	m_7080 = 0x0000;
	m_7081 = 0x0000;
	m_7082 = 0x0000;
	m_7083 = 0x0000;
	m_7084 = 0x0000;
	m_7085 = 0x0000;
	m_7086 = 0x0000;
	m_7087 = 0x0000;
	m_7088 = 0x0000;

	m_707e_spritebank = 0x0000;
	m_videodma_size = 0x0000;
	m_videodma_dest = 0x0000;
	m_videodma_source = 0x0000;

	m_sprite_7022_gfxbase_lsb = 0;
	m_sprite_702d_gfxbase_msb = 0;

	m_page0_addr_lsb = 0;
	m_page0_addr_msb = 0;
	m_page1_addr_lsb = 0;
	m_page1_addr_msb = 0;
	m_page2_addr_lsb = 0;
	m_page2_addr_msb = 0;
	m_page3_addr_lsb = 0;
	m_page3_addr_msb = 0;

	// start in 320x240 mode
	if (!m_disallow_resolution_control)
		m_set_visible(320, 240);
}

u32 gcm394_base_video_device::screen_update(bitmap_rgb32 &bitmap, const rectangle &cliprect)
{
	// For jak_car2 and jak_gtg the palette entry for 'magenta' in the test mode is intentionally set to a transparent black pen
	// (it is stored in the palette table in ROM that way, and copied directly) so the only way for the magenta entries on the screen
	// to be correctly displayed is if there is a magenta BG pen to fall through to (or for another palette write to change the palette
	// that is copied, but this does not appear to be the case).  How the bg pen is set is unknown, it is not a regular palette entry.
	// The 'bitmap test mode' in jak_car2 requires this to be black instead.

	// jak_s500 briely sets pen 0 of the layer to magenta, but then ends up erasing it
	bool readfromcsspace = true;

	if (!m_cs_space)
		readfromcsspace = false;

	if (0)
	{
		u16 attr0 = m_tmap0_regs[0];
		u16 attr1 = m_tmap1_regs[0];
		u16 attr2 = m_tmap2_regs[0];
		u16 attr3 = m_tmap3_regs[0];
		u16 ctrl0 = m_tmap0_regs[1];
		u16 ctrl1 = m_tmap1_regs[1];
		u16 ctrl2 = m_tmap2_regs[1];
		u16 ctrl3 = m_tmap3_regs[1];

		
	}

	//const u16 bgcol = 0x7c1f; // magenta
//  const u16 bgcol = 0x0000; // black
	bool highres = false;
	if ((!m_disallow_resolution_control) && m_has_vga_modes)
	{
		if (m_707f & 0x0010)
		{
			highres = true;
			m_set_visible(640, 480);
		}
		else
		{
			highres = false;
			m_set_visible(320, 240);
		}
	}

	address_space &mem = *m_cpuspace;

	u32 sprites_addr;

	if (m_707f & 0x0040) // FREE == 1
	{
		sprites_addr = ((m_sprite_702d_gfxbase_msb & 0x07ff) << 16) | m_sprite_7022_gfxbase_lsb;
	}
	else
	{
		sprites_addr = m_sprite_7022_gfxbase_lsb * 0x40;
	}

	for (u32 scanline = (u32)cliprect.min_y; scanline <= (u32)cliprect.max_y; scanline++)
	{
		m_renderer->new_line(cliprect);

		for (int i = 0; i < 4; i++)
		{
			m_renderer->draw_page(readfromcsspace, m_703a_palettebank, cliprect, scanline, i, m_page0_addr_msb, m_page0_addr_lsb, m_tmap0_scroll, m_tmap0_regs, mem, m_paletteram, m_rowscroll, 0);
			m_renderer->draw_page(readfromcsspace, m_703a_palettebank, cliprect, scanline, i, m_page1_addr_msb, m_page1_addr_lsb, m_tmap1_scroll, m_tmap1_regs, mem, m_paletteram, m_rowscroll, 1);
			m_renderer->draw_page(readfromcsspace, m_703a_palettebank, cliprect, scanline, i, m_page2_addr_msb, m_page2_addr_lsb, m_tmap2_scroll, m_tmap2_regs, mem, m_paletteram, m_rowscroll, 2);
			m_renderer->draw_page(readfromcsspace, m_703a_palettebank, cliprect, scanline, i, m_page3_addr_msb, m_page3_addr_lsb, m_tmap3_scroll, m_tmap3_regs, mem, m_paletteram, m_rowscroll, 3);

			m_renderer->draw_sprites(readfromcsspace, m_use_legacy_mode ? 2 : 1, m_703a_palettebank, highres, cliprect, scanline, i, sprites_addr, mem, m_paletteram, m_spriteram);
		}

		m_renderer->apply_saturation_and_fade(bitmap, cliprect, scanline);
	}

	return 0;
}


void gcm394_base_video_device::write_tmap_scroll(int tmap, u16 *regs, int offset, u16 data)
{
	switch (offset)
	{
	case 0x0: // Page X scroll
		
		regs[offset] = data;
		break;

	case 0x1: // Page Y scroll
		
		regs[offset] = data;
		break;
	}
}

void gcm394_base_video_device::write_tmap_regs(int tmap, u16 *regs, int offset, u16 data)
{
	switch (offset)
	{
	case 0x0: // Page Attributes
		
		regs[offset] = data;
		break;

	case 0x1: // Page Control
		
		regs[offset] = data;
		break;

	case 0x2: // Page Tile Address
		
		regs[offset] = data;
		break;

	case 0x3: // Page Attribute Address
		
		regs[offset] = data;
		break;
	}
}


// offsets 0,1,4,5,6,7 used in main IRQ code
// offsets 2,3 only cleared on startup

// Based on code analysis this seems to be the same as the regular tilemap regs, except for the addition of regs 2,3 which shift the remaining ones along.
// As the hardware appears to support ROZ these are probably 2 extra tile layers, with the 2 additional words being the ROZ parameters?


void gcm394_base_video_device::write_tmap_extrascroll(int tmap, u16 *regs, int offset, u16 data)
{
	switch (offset)
	{
	case 0x0: // Page X scroll
	case 0x1: // Page Y scroll
		write_tmap_scroll(tmap, regs, offset, data);
		break;

	case 0x2: //
		
		regs[offset] = data;
		break;

	case 0x3:
		
		regs[offset] = data;
		break;

	}
}


// **************************************** TILEMAP 0 *************************************************

u16 gcm394_base_video_device::tmap0_regs_r(offs_t offset)
{
	if (offset < 2)
	{
		return m_tmap0_scroll[offset];
	}
	else
	{
		return m_tmap0_regs[offset-2];
	}
}

void gcm394_base_video_device::tmap0_regs_w(offs_t offset, u16 data)
{
	
	if (offset < 2)
	{
		write_tmap_scroll(0, m_tmap0_scroll, offset, data);
	}
	else
	{
		write_tmap_regs(0, m_tmap0_regs, offset-2, data);
	}
}

u16 gcm394_base_video_device::tmap0_tilebase_lsb_r()
{
	return m_page0_addr_lsb;
}

void gcm394_base_video_device::tmap0_tilebase_lsb_w(u16 data)
{
	
	m_page0_addr_lsb = data;
	
}

u16 gcm394_base_video_device::tmap0_tilebase_msb_r()
{
	return m_page0_addr_msb;
}

void gcm394_base_video_device::tmap0_tilebase_msb_w(u16 data)
{
	
	m_page0_addr_msb = data;
	
}

// **************************************** TILEMAP 1 *************************************************


u16 gcm394_base_video_device::tmap1_regs_r(offs_t offset)
{
	if (offset < 2)
	{
		return m_tmap1_scroll[offset];
	}
	else
	{
		return m_tmap1_regs[offset-2];
	}
}

void gcm394_base_video_device::tmap1_regs_w(offs_t offset, u16 data)
{
	
	if (offset < 2)
	{
		write_tmap_scroll(1, m_tmap1_scroll, offset, data);
	}
	else
	{
		write_tmap_regs(1, m_tmap1_regs, offset-2, data);
	}
}

u16 gcm394_base_video_device::tmap1_tilebase_lsb_r()
{
	return m_page1_addr_lsb;
}

void gcm394_base_video_device::tmap1_tilebase_lsb_w(u16 data)
{
	
	m_page1_addr_lsb = data;
	
}

u16 gcm394_base_video_device::tmap1_tilebase_msb_r()
{
	return m_page1_addr_msb;
}

void gcm394_base_video_device::tmap1_tilebase_msb_w(u16 data)
{
	
	m_page1_addr_msb = data;
	
}

// **************************************** unknown video device 1 (another tilemap? roz? line? zooming sprite layer?) *************************************************

u16 gcm394_base_video_device::tmap2_regs_r(offs_t offset)
{
	if (offset < 4)
	{
		return m_tmap2_scroll[offset];
	}
	else
	{
		return m_tmap2_regs[offset-4];
	}
}

void gcm394_base_video_device::tmap2_regs_w(offs_t offset, u16 data)
{
	
	if (offset < 4)
	{
		write_tmap_extrascroll(2, m_tmap2_scroll, offset, data);
	}
	else
	{
		write_tmap_regs(2, m_tmap2_regs, offset-4, data);
	}
}

u16 gcm394_base_video_device::tmap2_tilebase_lsb_r()
{
	return m_page2_addr_lsb;
}


void gcm394_base_video_device::tmap2_tilebase_lsb_w(u16 data)
{
	
	m_page2_addr_lsb = data;
	
}

u16 gcm394_base_video_device::tmap2_tilebase_msb_r()
{
	return m_page2_addr_msb;
}

void gcm394_base_video_device::tmap2_tilebase_msb_w(u16 data)
{
	
	m_page2_addr_msb = data;
	
}

// **************************************** unknown video device 2 (another tilemap? roz? lines? zooming sprite layer?) *************************************************

u16 gcm394_base_video_device::tmap3_regs_r(offs_t offset)
{
	if (offset < 4)
	{
		return m_tmap3_scroll[offset];
	}
	else
	{
		return m_tmap3_regs[offset-4];
	}
}

void gcm394_base_video_device::tmap3_regs_w(offs_t offset, u16 data)
{
	
	if (offset < 4)
	{
		write_tmap_extrascroll(3, m_tmap3_scroll, offset, data);
	}
	else
	{
		write_tmap_regs(3, m_tmap3_regs, offset-4, data);
	}
}

u16 gcm394_base_video_device::tmap3_tilebase_lsb_r()
{
	return m_page3_addr_lsb;
}


void gcm394_base_video_device::tmap3_tilebase_lsb_w(u16 data)
{
	
	m_page3_addr_lsb = data;
	
}

u16 gcm394_base_video_device::tmap3_tilebase_msb_r()
{
	return m_page3_addr_msb;
}


void gcm394_base_video_device::tmap3_tilebase_msb_w(u16 data)
{
	
	m_page3_addr_msb = data;
	
}

// **************************************** sprite control registers *************************************************

// set to 001264c0 in wrlshunt, which point at the menu selectors (game names, arrows etc.)

u16 gcm394_base_video_device::sprite_7022_gfxbase_lsb_r()
{
	return m_sprite_7022_gfxbase_lsb;
}

void gcm394_base_video_device::sprite_7022_gfxbase_lsb_w(u16 data)
{
	
	m_sprite_7022_gfxbase_lsb = data;
	
}

u16 gcm394_base_video_device::sprite_702d_gfxbase_msb_r()
{
	return m_sprite_702d_gfxbase_msb;
}

void gcm394_base_video_device::sprite_702d_gfxbase_msb_w(u16 data)
{
	
	m_sprite_702d_gfxbase_msb = data;
	
}

u16 gcm394_base_video_device::sprite_7042_extra_r()
{
	u16 retdata = m_7042_sprite;
	
	return retdata;
}

void gcm394_base_video_device::sprite_7042_extra_w(u16 data)
{
	
	m_7042_sprite = data;
	m_renderer->set_video_reg_42(data);

	// format is
	// NNNN NNNN ZRMD rBCE
	//
	// N = number of sprites to draw
	// Z = global sprite zoom enable
	// R = global sprite rotate enable
	// M = global sprite mosaic enable
	// D = direct addressing enable
	// r = sprite round robin mode
	// B = blend mode (0 = use 702a for blending, 1 = use individual sprite blending)
	// C = co-ordinate mode
	// E = sprite enable

	//
}


// **************************************** video DMA device *************************************************

void gcm394_base_video_device::video_dma_source_w(u16 data)
{
	
	m_videodma_source = data;
}

void gcm394_base_video_device::video_dma_dest_w(u16 data)
{
	
	m_videodma_dest = data;
}

u16 gcm394_base_video_device::video_dma_size_busy_r()
{
	
	return 0x0000;
}

void gcm394_base_video_device::video_dma_size_trigger_w(u16 data)
{
	address_space &space = *m_cpuspace;
	
	m_videodma_size = data;

	

	// sprite DMA can only write to spriteram (just like on spg2xx)
	int dest = m_videodma_dest & 0x03ff;
	int size = m_videodma_size & 0x03ff;

	// on spg2xx size 0 means full transfer, with a < condition in the loop rather than <=
	// jak_spmm seems to indicate that it differs here

	for (int i = 0; i <= size; i++)
	{
		u16 dat = space.read_word(m_videodma_source + i);

		if ((dest + i) < 0x400)
			space.write_word(0x7400 + dest + i, dat);
	}

	m_videodma_size = 0x0000;

	if (m_video_irq_enable & 4)
	{
		const u16 old = m_video_irq_status;
		m_video_irq_status |= 4;
		const u16 changed = old ^ (m_video_irq_enable & m_video_irq_status);
		if (changed)
			check_video_irq();
	}
}

// P_PPU_RAM_Bank
//
// only lowest bit is used

void gcm394_base_video_device::ppu_ram_bank_w(u16 data)
{
	
	m_707e_spritebank = data;
}

// P_FB_PPU_GO
//
// 15  FBI_UPD  (Read Only, indicates FBI_ADDR is updated to current pointer)
// 14  FBO_F    (Read Only, used in interlace mode)
// 13
// 12
//
// 11
// 10
//  9
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
//  0  PPU_GO  (Render to framebuffer in frame base mode)

u16 gcm394_base_video_device::video_707c_r()
{
	
	return 0x8000;
}

// Format for GPL1625x / GPAC800
//
// S000 MMDF EfIV BDCP
//
// S# = SAVE_ROM (0 = normal mode, 1 = ROM saving mode, uses even line data for odd lines)
// MM = FB_MONO (0 = RGB, 1 = Mono, 2 = 2bpp, 3 = 4bpp)
// D* = SPR25D (0 = disable virtual 3D sprites, 1 = enable)
// F = FB_FORMAT (0 = RGB565, 1 = RGBG)
// E = FB_EN (0 = Half-line base mode, 1 = Frame base mode)
// f = FREE (0 = 22 bit addressing, 1 = 27 bit addressing)
// I* = VGA_NOINTL (0 = VGA interlace mode 640x480, 1 = VGA-non interlace mode, 640x240)
// V* = VGA_EN  (0 = QVGA / 320x240, 1 = VGA / 640x480)
// B = TX_BOTUP (0 = top to bottom layers, 1 = bottom to top layers)
// D = TX_DIRECT (0 = relative addressing, 1 = direct addressing)
// C = TCHAR (0 = disable transparent tile, 1 = enable transparent tile - only works in relative address mode)
// P = PPU_EN
//
// # not on GPL16250 (GPAC500 only?)
// * not on GPL1622x / GPL1623x / GPAC500
//

u16 gcm394_base_video_device::ppu_enable_r()
{
	u16 retdata = m_renderer->get_video_reg_7f();
	
	return retdata;
}
void gcm394_base_video_device::ppu_enable_w(u16 data)
{
	u8 save_rom = (data & 0x8000) >> 15;
	u8 unused = (data & 0x7000) >> 12;
	u8 fb_mono = (data & 0x0c00) >> 10;
	u8 spr25d = (data & 0x0200) >> 9;
	u8 fb_format = (data & 0x0100) >> 8;
	u8 fb_en = (data & 0x0080) >> 7;
	u8 free_adr = (data & 0x0040) >> 6;
	u8 vga_nointl = (data & 0x0020) >> 5;
	u8 vga_en = (data & 0x0010) >> 4;
	u8 tx_botup = (data & 0x0008) >> 3;
	u8 tx_direct = (data & 0x0004) >> 2;
	u8 tchar = (data & 0x0002) >> 1;
	u8 ppu_en = (data & 0x0001) >> 0;

	

	m_707f = data;
	m_renderer->set_video_reg_7f(data);
	//
}

// P_Palette_Control
//
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
//  7
//  6
//  5
//  4
//
//  3  PAL_RAM_SEL[1] - ram banking for palette reads/writes
//  2  PAL_RAM_SEL[0]
//  1  PAL_TYPE[1] - enables 25-bit palette mode (otherwise 16-bit mode)
//  0  PAL_TYPE[0] - selects which palettes to use

u16 gcm394_base_video_device::video_703a_palettebank_r()
{
	
	return m_703a_palettebank;
}

void gcm394_base_video_device::video_703a_palettebank_w(u16 data)
{
	
	m_703a_palettebank = data;
}

// P_PPU_IRQ_EN - IRQ enable/disable registers
//
// 15
// 14
// 13
// 12
//
// 11  IRQ_EN11 R/W  TV/TFT Frame End IRQ
// 10  IRQ_EN10 R/W  Frame buffer FIFO under-run IRQ
//  9  IRQ_EN9  R/W  Motion detection FIFO under-run IRQ
//  8  IRQ_EN8  R/W  Sensor position hit IRQ
//
//  7  IRQ_EN7  R/w  Motion detection frame end IRQ
//  6  IRQ_EN6  R/W  Sensor frame end IRQ
//  5  IRQ_EN5  R/W  Sprite engine under-run IRQ
//  4  IRQ_EN4  R/W  Text engine under-run RIQ
//
//  3  IRQ_EN3  R/W  Palette write error IRQ
//  2  IRQ_EN2  R/W  Sprite DMA transfer complete IRQ
//  1  IRQ_EN1  R/W  Video position IRQ
//  0  IRQ_EN0  R/W  Vertical blaking IRQ

u16 gcm394_base_video_device::videoirq_source_enable_r()
{
	
	return m_video_irq_enable;
}

void gcm394_base_video_device::videoirq_source_enable_w(u16 data)
{
	
	const u16 old = m_video_irq_enable & m_video_irq_status;
	m_video_irq_enable = data & 0x0007;
	const u16 changed = old ^ (m_video_irq_enable & m_video_irq_status);
	if (changed)
		check_video_irq();
}

// P_PPU_IRQ_STS
//
// 15
// 14
// 13
// 12
//
// 11  IRQ_STS11   TV/TFT Frame End IRQ
// 10  IRQ_STS10   Frame buffer FIFO under-run IRQ
//  9  IRQ_STS9    Motion detection FIFO under-run IRQ
//  8  IRQ_STS8    Sensor position hit IRQ
//
//  7  IRQ_STS7    Motion detection frame end IRQ
//  6  IRQ_STS6    Sensor frame end IRQ
//  5  IRQ_STS5    Sprite engine under-run IRQ
//  4  IRQ_STS4    Text engine under-run IRQ
//
//  3  IRQ_STS3    Palette write error IRQ
//  2  IRQ_STS2    Sprite DMA transfer complete IRQ
//  1  IRQ_STS1    Video position IRQ
//  0  IRQ_STS0    Vertical blaking IRQ

u16 gcm394_base_video_device::video_7063_videoirq_source_r()
{
	
	return m_video_irq_status;
}

void gcm394_base_video_device::video_7063_videoirq_source_ack_w(u16 data)
{
	
	const u16 old = m_video_irq_enable & m_video_irq_status;
	m_video_irq_status &= ~data;
	const u16 changed = old ^ (m_video_irq_enable & m_video_irq_status);
	if (changed)
		check_video_irq();
}

// P_Blending
//
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
//  7
//  6
//  5
//  4
//
//  3
//  2
//  1  BLDLVL[1]
//  0  BLDLVL[0]

void gcm394_base_video_device::blending_w(u16 data)
{
	
	m_702a = data;
	m_renderer->set_video_reg_2a(data);
}

u16 gcm394_base_video_device::video_curline_r()
{
	
	return m_screen->vpos();
}

// read in IRQ
u16 gcm394_base_video_device::video_7030_brightness_r()
{
	/* wrlshunt ends up doing an explicit jump to 0000 shortly after boot if you just return the value written here, however I think that is correct code flow and something else is wrong
	   as this simply looks like some kind of brightness register - there is code to decrease it from 0xff to 0x00 by 0x5 increments (waiting for it to hit 0x05) and code to do the reverse
	   either way it really looks like the data written should be read back.
	*/
	u16 retdat = m_7030_brightness;
	
	return retdat;
}

void gcm394_base_video_device::video_7030_brightness_w(u16 data)
{
	
	m_7030_brightness = data;
	m_renderer->set_video_reg_30(data);
}

void gcm394_base_video_device::update_raster_split_position()
{
	// this might need updating to handle higher res modes
	
	if (m_xirqpos < 300 && m_yirqpos < 256)
	{
		// where does -19 come from? needed for raster on paccon xevious to fire at correct line for bg scrolling to be seamless
		m_screenpos_timer->adjust(m_screen->time_until_pos(m_yirqpos-19, m_xirqpos));
		//printf("setting irq timer for y:%d x:%d", m_yirqpos, m_xirqpos);
	}
	else
		m_screenpos_timer->adjust(attotime::never());
}

void gcm394_base_video_device::split_irq_ypos_w(u16 data)
{
	

	m_yirqpos = data & 0x1ff;
	update_raster_split_position();
}

void gcm394_base_video_device::split_irq_xpos_w(u16 data)
{
	

	m_xirqpos = data & 0x1ff;
	update_raster_split_position();
}

// P_TV_Control
//
// 15  TV_TEST
// 14
// 13
// 12  HD_SEL  (0 = CSYNC, 1 = HD)
//
// 11  VDSEL
// 10  EVEN   (0 = odd field, 1 = even field)
//  9  SYNC
//  8  VINFMT[1]  - video input data format, 0 = RGB888, others = reserved
//
//  7  VINFMT[0]
//  6  RESOLUTION[1]  - 0 = 320x240, 1 = 640x240, 2 = reserved, 3 = internal colour bar
//  5  RESOLUTION[0]
//  4  NONINTL        - 0 = interlaced, 1 = non-interlaced
//
//  3  TVSTD[2]  - TV Standard Select
//  2  TVSTD[1]
//  1  TVSTD[0]
//  0  TVEN      - TV Module Enabled

u16 gcm394_base_video_device::video_703c_tvcontrol1_r()
{
	
	return m_703c_tvcontrol1;
}

void gcm394_base_video_device::video_703c_tvcontrol1_w(u16 data)
{
	
	m_703c_tvcontrol1 = data;
	m_renderer->set_video_reg_3c(data);
}

// uncertain, apparently P_STN_COM_Clip or P_TFT_V_Width

u16 gcm394_base_video_device::video_7051_r()
{
	/* related to what ends up crashing wrlshunt? */
	u16 retdat = 0x03ff;
	
	return retdat;
}

u16 gcm394_base_video_device::video_70e0_prng_r()
{
	// several games in the beijuehh use this to generate random pieces / enemies, bit 0x8000 can't be set or it will generate a negative index into tables
	u16 retdat = rand() & 0x7fff;
	
	return retdat;
}


// this block get set once, in a single function, could be important
void gcm394_base_video_device::tv_saturation_w(u16 data) {  m_7080 = data; }
void gcm394_base_video_device::tv_hue_w(u16 data) {  m_7081 = data; }
void gcm394_base_video_device::tv_brightness_w(u16 data) {  m_7082 = data; }
void gcm394_base_video_device::tv_sharpness_w(u16 data) {  m_7083 = data; }
void gcm394_base_video_device::tv_y_gain_w(u16 data) {  m_7084 = data; }
void gcm394_base_video_device::tv_y_delay_w(u16 data) {  m_7085 = data; }
void gcm394_base_video_device::tv_v_position_w(u16 data) {  m_7086 = data; }
void gcm394_base_video_device::tv_h_position_w(u16 data) {  m_7087 = data; }
void gcm394_base_video_device::tv_videodac_w(u16 data) {  m_7088 = data; }

u16 gcm394_base_video_device::tv_sharpness_r() {  return m_7083; }

void gcm394_base_video_device::spriteram_w(offs_t offset, u16 data)
{
	// transfers an additional word for each sprite with this bit set (smartfp) or an entire extra bank (wrlshunt)
	// wrlshunt instead seems to base if it writes the extra data based on 707f so maybe this is more complex than banking

	// however for 707e only 0/1 is written, and it also gets written before system DMA, so despite being in the video DMA
	// region seems to operate separate from that.

	if (m_707e_spritebank == 0x0000)
	{
		m_spriteram[offset] = data;
	}
	else if (m_707e_spritebank == 0x0001)
	{
		m_spriteram[offset + 0x400] = data;
	}
	else
	{
		
	}
}

u16 gcm394_base_video_device::spriteram_r(offs_t offset)
{
	if (m_707e_spritebank == 0x0000)
	{
		return m_spriteram[offset];
	}
	else if (m_707e_spritebank == 0x0001)
	{
		return m_spriteram[offset + 0x400];
	}
	else
	{
		
		return 0x0000;
	}
}

void gcm394_base_video_device::palette_w(offs_t offset, u16 data)
{
	

	if (m_703a_palettebank & 0xfff0)
	{
		
	}


	offset |= (m_703a_palettebank & 0x000c) << 6;
	m_paletteram[offset] = data;

	// for debug

}

u16 gcm394_base_video_device::palette_r(offs_t offset)
{
	if (m_703a_palettebank & 0xfff0)
	{
		
	}

	offset |= (m_703a_palettebank & 0x000c) << 6;
	return m_paletteram[offset];
}

void gcm394_base_video_device::vcomp_value_w(u16 data)
{
	
	m_renderer->set_video_reg_1c(data);
}

void gcm394_base_video_device::vcomp_offset_w(u16 data)
{
	
	m_renderer->set_video_reg_1d(data);
}

void gcm394_base_video_device::vcomp_step_w(u16 data)
{
	
	m_renderer->set_video_reg_1e(data);
}


void gcm394_base_video_device::check_video_irq()
{
	
	m_video_irq_cb((m_video_irq_status & m_video_irq_enable) ? ASSERT_LINE : CLEAR_LINE);
}

void gcm394_base_video_device::vblank(int state)
{
	if (!state)
	{
		m_video_irq_status &= ~1;
		
		check_video_irq();
		return;
	}

	if (m_video_irq_enable & 1)
	{
		// jak_prft expects 0x800 to be set in the status register or most of the main vblank code is skipped, why?
		// 0x800 is TV/TFT Frame End IRQ

		m_video_irq_status |= (0x001 | 0x800);
		
		check_video_irq();
	}
}

void gcm394_base_video_device::screen_pos_reached(int param)
{
	if (m_video_irq_enable & 2)
	{
		m_video_irq_status |= 2;
		check_video_irq();
	}

	//printf("firing irq timer\n");

	m_update_partial(m_screen->vpos());

	// fire again, jak_dbz pinball needs this
	m_screenpos_timer->adjust(m_screen->time_until_pos(m_yirqpos-19, m_xirqpos));
}







}  // namespace gpl
