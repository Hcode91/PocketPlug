// PocketPlug - SunPlus SPG2xx devices
// SPDX-License-Identifier: BSD-3-Clause
//
// Generated from MAME src/devices/machine/spg2xx_io.cpp, spg2xx_sysdma.cpp,
// spg2xx_video.cpp, spg_renderer.cpp (Ryan Holtz, David Haywood),
// spg2xx_audio.cpp (Ryan Holtz, Jonathan Gevaryahu) and sound/imaadpcm.cpp
// (Andrew Gardner, Aaron Giles), all BSD-3-Clause, with logging removed and
// MAME's framework calls mapped onto spg_lite.h. Logic is unchanged.
// Hand-written replacements for framework-bound functions are in spg2xx.cpp.

#include "spg2xx.h"
#include <cmath>
#include <cstdio>
#include <iterator>

bool ima_adpcm_state::s_tables_computed = false;
const int8_t ima_adpcm_state::s_index_shift[8] = { -1, -1, -1, -1, 2, 4, 6, 8 };
int ima_adpcm_state::s_diff_lookup[89*16];

//-------------------------------------------------
//  reset - reset the ADPCM state
//-------------------------------------------------

void ima_adpcm_state::reset()
{
	// reset the signal/step
	m_signal = m_loop_signal = 0;
	m_step = m_loop_step = 0;
	m_saved = false;
}


//-------------------------------------------------
//  clock - decode single nibble and update
//  ADPCM output
//-------------------------------------------------

int16_t ima_adpcm_state::clock(uint8_t nibble)
{
	// update the signal
	m_signal += s_diff_lookup[m_step * 16 + (nibble & 15)];

	// clamp to the maximumf
	if (m_signal > 32767)
		m_signal = 32767;
	else if (m_signal < -32768)
		m_signal = -32768;

	// adjust the step size and clamp
	m_step += s_index_shift[nibble & 7];
	if (m_step > 88)
		m_step = 88;
	else if (m_step < 0)
		m_step = 0;

	// return the signal
	return m_signal;
}


//-------------------------------------------------
//  save - save current ADPCM state to buffer
//-------------------------------------------------

void ima_adpcm_state::save()
{
	if (!m_saved)
	{
		m_loop_signal = m_signal;
		m_loop_step = m_step;
		m_saved = true;
	}
}


//-------------------------------------------------
//  restore - restore previous ADPCM state
//  from buffer
//-------------------------------------------------

void ima_adpcm_state::restore()
{
	m_signal = m_loop_signal;
	m_step = m_loop_step;
}


//-------------------------------------------------
//  compute_tables - precompute tables for faster
//  sound generation
//-------------------------------------------------

void ima_adpcm_state::compute_tables()
{
	// skip if we already did it
	if (s_tables_computed)
		return;
	s_tables_computed = true;

	// nibble to bit map
	static const int8_t nbl2bit[16][4] =
	{
		{ 1, 0, 0, 0}, { 1, 0, 0, 1}, { 1, 0, 1, 0}, { 1, 0, 1, 1},
		{ 1, 1, 0, 0}, { 1, 1, 0, 1}, { 1, 1, 1, 0}, { 1, 1, 1, 1},
		{-1, 0, 0, 0}, {-1, 0, 0, 1}, {-1, 0, 1, 0}, {-1, 0, 1, 1},
		{-1, 1, 0, 0}, {-1, 1, 0, 1}, {-1, 1, 1, 0}, {-1, 1, 1, 1}
	};

	// loop over all possible steps
	for (int step = -8; step <= 80; step++)
	{
		// compute the step value
		int stepval = std::min(floor(16.0 * pow(11.0 / 10.0, (double)step)), 32767.);

		// manual correction of some early values
		if (step == -5 || step == -4)
			stepval++;

		// loop over all nibbles and compute the difference
		for (int nib = 0; nib < 16; nib++)
		{
			s_diff_lookup[(step + 8)*16 + nib] = nbl2bit[nib][0] *
					(stepval * nbl2bit[nib][1] +
					stepval/2 * nbl2bit[nib][2] +
					stepval/4 * nbl2bit[nib][3] +
					stepval/8);
		}
	}
}


void spg_renderer_device::device_reset()
{
	m_video_regs_1c = 0x0000;
	m_video_regs_1d = 0x0000;
	m_video_regs_1e = 0x0000;

	m_video_regs_2a = 0x0000;

	m_video_regs_30 = 0x0000;
	m_video_regs_3c = 0x0020;

	m_video_regs_42 = 0x0001;

	for (int i = 0; i < 480; i++)
	{
		m_ycmp_table[i] = 0xffffffff;
	}

	m_brightness_or_saturation_dirty = true;
}

template<bool Blend, bool FlipX>
void spg_renderer_device::draw_tilestrip(uint32_t screenwidth, uint32_t drawwidthmask, uint32_t tile_h, uint32_t tile_w, uint32_t tilegfxdata_addr, uint32_t tile, uint32_t tile_scanline, int drawx, bool flip_y, uint32_t palette_offset, const uint32_t nc_bpp, const uint32_t bits_per_row, const uint32_t words_per_tile, uint16_t *paletteram, uint8_t blendlevel)
{
	const uint32_t yflipmask = flip_y ? tile_h - 1 : 0;
	uint32_t m = tilegfxdata_addr + words_per_tile * tile + bits_per_row * (tile_scanline ^ yflipmask);
	uint32_t bits = 0;
	uint32_t nbits = 0;

	for (int32_t x = FlipX ? (tile_w - 1) : 0; FlipX ? x >= 0 : x < tile_w; FlipX ? x-- : x++)
	{
		int realdrawpos = (drawx + x) & drawwidthmask;

		bits <<= nc_bpp;

		if (nbits < nc_bpp)
		{
			uint16_t b = read_word(m++ & 0x3fffff);
			b = (b << 8) | (b >> 8);
			bits |= b << (nc_bpp - nbits);
			nbits += 16;
		}
		nbits -= nc_bpp;

		uint32_t pal = palette_offset + (bits >> 16);
		bits &= 0xffff;


		if (realdrawpos >= 0 && realdrawpos < screenwidth)
		{
			uint16_t rgb = paletteram[pal];

			if (!(rgb & 0x8000))
			{
				if (Blend && !(m_linebuf[realdrawpos] & 0x8000))
				{

					m_linebuf[realdrawpos] = (mix_channel((uint8_t)(m_linebuf[realdrawpos] >> 10) & 0x1f,  (rgb >> 10) & 0x1f, blendlevel) << 10) |
											 (mix_channel((uint8_t)(m_linebuf[realdrawpos] >> 5)  & 0x1f,  (rgb >> 5)  & 0x1f, blendlevel) << 5) |
											 (mix_channel((uint8_t)(m_linebuf[realdrawpos] >> 0)  & 0x1f,  (rgb >> 0)  & 0x1f, blendlevel) << 0);
				}
				else
				{
					m_linebuf[realdrawpos]  = rgb;
				}
			}
		}
	}
}

void spg_renderer_device::draw_linemap(uint32_t scanline, int priority, uint32_t tilegfxdata_addr, uint16_t *scrollregs, uint16_t *tilemapregs, uint16_t *paletteram)
{
	if ((scanline < 0) || (scanline >= 240))
		return;

	uint32_t tilemap = tilemapregs[2];
	uint32_t palette_map = tilemapregs[3];

	//if (scanline == 128)
	//  /* log */

	//uint32_t xscroll = scrollregs[0];
	uint32_t yscroll = scrollregs[1];

	int realline = (scanline + yscroll) & 0xff;


	uint32_t tile = read_word(tilemap + realline);
	uint16_t palette = 0;

	//if (!tile)
	//  continue;

	palette = read_word(palette_map + realline / 2);
	if (scanline & 1)
		palette >>= 8;
	else
		palette &= 0x00ff;

	//const int linewidth = 320 / 2;
	int sourcebase = tile | (palette << 16);

	uint32_t ctrl = tilemapregs[1];

	if (ctrl & 0x80) // HiColor mode (rad_digi)
	{
		for (int i = 0; i < 320; i++)
		{
			const uint16_t data = read_word(sourcebase + i);

			if (!(data & 0x8000))
			{
				m_linebuf[i] = data & 0x7fff;
			}
		}
	}
	else
	{
		const uint32_t attr = tilemapregs[0];
		const uint8_t bpp = attr & 0x0003;
		const uint32_t nc_bpp = ((bpp)+1) << 1;
		uint32_t palette_offset = (attr & 0x0f00) >> 4;
		palette_offset >>= nc_bpp;
		palette_offset <<= nc_bpp;

		uint32_t bits = 0;
		uint32_t nbits = 0;

		for (int i = 0; i < 320; i++)
		{
			bits <<= nc_bpp;
			if (nbits < nc_bpp)
			{
				uint16_t b = read_word(sourcebase++ & 0x3fffff);
				b = (b << 8) | (b >> 8);
				bits |= b << (nc_bpp - nbits);
				nbits += 16;
			}
			nbits -= nc_bpp;

			uint32_t pal = palette_offset + (bits >> 16);
			bits &= 0xffff;

			uint16_t rgb = paletteram[pal];

			if (!(rgb & 0x8000))
			{
				m_linebuf[i] = rgb;
			}
		}
	}
}

void spg_renderer_device::update_vcmp_table()
{
	int currentline = 0;

	int step = m_video_regs_1e & 0xff;
	if (step & 0x80)
		step = step - 0x100;

	int current_inc_value = (m_video_regs_1c<<4);

	int counter = 0;

	for (int i = 0; i < 480; i++)
	{
		if (i < m_video_regs_1d)
		{
			m_ycmp_table[i] = 0xffffffff;
		}
		else
		{
			if ((currentline >= 0) && (currentline < 256))
			{
				m_ycmp_table[i] = currentline;
			}

			counter += current_inc_value;

			while (counter >= (0x20<<4))
			{
				currentline++;
				current_inc_value += step;

				counter -= (0x20<<4);
			}
		}
	}
}

void spg_renderer_device::draw_tilestrip(uint32_t screenwidth, uint32_t drawwidthmask, bool blend, bool flip_x, uint32_t tile_h, uint32_t tile_w, uint32_t tilegfxdata_addr, uint32_t tile, uint32_t tile_scanline, int drawx, bool flip_y, uint32_t palette_offset, const uint32_t nc_bpp, const uint32_t bits_per_row, const uint32_t words_per_tile, uint16_t *paletteram, uint8_t blendlevel)
{
	if (blend)
	{
		if (flip_x)
		{
			draw_tilestrip<true, true>(screenwidth, drawwidthmask, tile_h, tile_w, tilegfxdata_addr, tile, tile_scanline, drawx, flip_y, palette_offset, nc_bpp, bits_per_row, words_per_tile, paletteram, blendlevel);
		}
		else
		{
			draw_tilestrip<true, false>(screenwidth, drawwidthmask, tile_h, tile_w, tilegfxdata_addr, tile, tile_scanline, drawx, flip_y, palette_offset, nc_bpp, bits_per_row, words_per_tile, paletteram, blendlevel);
		}
	}
	else
	{
		if (flip_x)
		{
			draw_tilestrip<false, true>(screenwidth, drawwidthmask, tile_h, tile_w, tilegfxdata_addr, tile, tile_scanline, drawx, flip_y, palette_offset, nc_bpp, bits_per_row, words_per_tile, paletteram, blendlevel);
		}
		else
		{
			draw_tilestrip<false, false>(screenwidth, drawwidthmask, tile_h, tile_w, tilegfxdata_addr, tile, tile_scanline, drawx, flip_y, palette_offset, nc_bpp, bits_per_row, words_per_tile, paletteram, blendlevel);
		}
	}
}

void spg_renderer_device::draw_page(uint32_t scanline, int priority, uint16_t tilegfxdata_addr, uint16_t *scrollregs, uint16_t *tilemapregs, uint16_t *paletteram, uint16_t *scrollram, uint32_t which)
{
	const uint32_t attr = tilemapregs[0];
	const uint32_t ctrl = tilemapregs[1];

	if (!(ctrl & 0x0008))
	{
		return;
	}

	if (((attr & 0x3000) >> 12) != priority)
	{
		return;
	}

	// graphic data segments/bases
	uint32_t tilegfxdata_addr_full = tilegfxdata_addr * 0x40;

	if (ctrl & 0x0001) // Bitmap / Linemap mode! (basically screen width tile mode)
	{
		draw_linemap(scanline, priority, tilegfxdata_addr_full, scrollregs, tilemapregs, paletteram);
		return;
	}

	uint32_t logical_scanline = scanline;

	if (ctrl & 0x0040) // 'vertical compression feature' (later models only?)
	{
		// used by senspeed
		//if (m_video_regs_1e != 0x0000)
		//  /* log */

		logical_scanline = m_ycmp_table[scanline];
		if (logical_scanline == 0xffffffff)
			return;
	}

	const uint32_t total_width = 512;
	uint32_t y_mask = 0x100;
	const uint32_t screenwidth = 320;

	const uint32_t drawwidthmask = total_width - 1;
	y_mask--; // turn into actual mask

	const uint32_t xscroll = scrollregs[0];
	const uint32_t yscroll = scrollregs[1];
	const uint32_t tilemap_rambase = tilemapregs[2];
	const uint32_t exattributemap_rambase = tilemapregs[3];
	const int tile_width = (attr & 0x0030) >> 4;
	const uint32_t tile_h = 8 << ((attr & 0x00c0) >> 6);
	const uint32_t tile_w = 8 << (tile_width);
	const uint32_t tile_count_x = total_width / tile_w; // tilemaps are 512 or 1024 wide depending on screen mode?
	const uint32_t bitmap_y = (logical_scanline + yscroll) & y_mask; // tilemaps are 256 or 512 high depending on screen mode?
	const uint32_t y0 = bitmap_y / tile_h;
	const uint32_t tile_scanline = bitmap_y % tile_h;
	const uint8_t bpp = attr & 0x0003;
	const uint32_t nc_bpp = ((bpp)+1) << 1;
	const uint32_t bits_per_row = nc_bpp * tile_w / 16;
	const bool row_scroll = (ctrl & 0x0010);

	// Max blend level (3) should result in 100% opacity, per docs
	// Min blend level (0) should result in 25% opacity, per docs
	static const uint8_t s_blend_levels[4] = { 0x08, 0x10, 0x18, 0x20 };
	const uint8_t blendlevel = s_blend_levels[m_video_regs_2a & 3];

	const uint32_t words_per_tile = bits_per_row * tile_h;

	int realxscroll = xscroll;

	// Tennis in My Wireless Sports confirms the need to add the scroll value here rather than rowscroll being screen-aligned
	// this is different to GPL?
	if (row_scroll)
		realxscroll += (int16_t)scrollram[(logical_scanline + yscroll) & 0xff];

	const int upperscrollbits = (realxscroll >> (tile_width + 3));
	const int endpos = (screenwidth + tile_w) / tile_w;

	for (uint32_t x0 = 0; x0 < endpos; x0++)
	{
		bool blend;
		bool flip_x;
		bool flip_y;
		uint32_t tile;
		uint32_t palette_offset;

		// get tile info
		const int realx0 = (x0 + upperscrollbits) & (tile_count_x - 1);
		uint32_t tile_address = realx0 + (tile_count_x * y0);

		tile = (ctrl & 0x0004) ? read_word(tilemap_rambase) : read_word(tilemap_rambase + tile_address);

		if (!tile)
			continue;

		uint32_t tileattr = attr;
		uint32_t tilectrl = ctrl;

		if ((tilectrl & 2) == 0)
		{
			// -(1) bld(1) flip(2) pal(4)
			uint16_t exattribute = (tilectrl & 0x0004) ? read_word(exattributemap_rambase) : read_word(exattributemap_rambase + tile_address / 2);
			if (realx0 & 1)
				exattribute >>= 8;
			else
				exattribute &= 0x00ff;

			tileattr &= ~0x000c;
			tileattr |= (exattribute >> 2) & 0x000c;    // flip

			tileattr &= ~0x0f00;
			tileattr |= (exattribute << 8) & 0x0f00;    // palette

			tilectrl &= ~0x0100;
			tilectrl |= (exattribute << 2) & 0x0100;    // blend
		}

		blend = (tilectrl & 0x0100) ? true : false;
		flip_x = (tileattr & 0x0004) ? true : false;
		flip_y = (tileattr & 0x0008) ? true : false;

		palette_offset = (tileattr & 0x0f00) >> 4;
		// got tile info

		palette_offset >>= nc_bpp;
		palette_offset <<= nc_bpp;

		const int drawx = (x0 * tile_w) - (realxscroll & (tile_w - 1));
		draw_tilestrip(screenwidth, drawwidthmask, blend, flip_x, tile_h, tile_w, tilegfxdata_addr_full, tile, tile_scanline, drawx, flip_y, palette_offset, nc_bpp, bits_per_row, words_per_tile, paletteram, blendlevel);
	}
}

void spg_renderer_device::draw_sprite(uint32_t scanline, int priority, uint32_t spritegfxdata_addr, uint32_t base_addr, uint16_t *paletteram, uint16_t *spriteram)
{
	uint32_t tilegfxdata_addr = spritegfxdata_addr;
	uint32_t tile = spriteram[base_addr + 0];
	int16_t x = spriteram[base_addr + 1];
	int16_t y = spriteram[base_addr + 2];
	uint16_t attr = spriteram[base_addr + 3];

	if (!tile)
	{
		return;
	}

	if (((attr & 0x3000) >> 12) != priority)
	{
		return;
	}

	const uint32_t screenwidth = 320;
	const uint32_t screenheight  = 256;
	const uint32_t xmask = 0x1ff;
	const uint32_t ymask = 0x1ff;

	const uint32_t tile_h = 8 << ((attr & 0x00c0) >> 6);
	const uint32_t tile_w = 8 << ((attr & 0x0030) >> 4);

	// TODO: not all SPG2xx models support this, check which ones do
	if (!(m_video_regs_42 & 0x0002))
	{
		x = ((screenwidth/2) + x) - tile_w / 2;
		y = ((screenheight/2) - y) - (tile_h / 2);
	}

	x &= xmask;
	y &= ymask;

	int firstline = y;
	int lastline = y + (tile_h - 1);
	lastline &= ymask;

	const bool blend = (attr & 0x4000) ? true : false;
	bool flip_x = (attr & 0x0004) ? true : false;
	bool flip_y = (attr & 0x0008) ? true : false;
	const uint8_t bpp = attr & 0x0003;
	const uint32_t nc_bpp = ((bpp)+1) << 1;
	const uint32_t bits_per_row = nc_bpp * tile_w / 16;

	// Max blend level (3) should result in 100% opacity on the sprite, per docs
	// Min blend level (0) should result in 25% opacity on the sprite, per docs
	static const uint8_t s_blend_levels[4] = { 0x08, 0x10, 0x18, 0x20 };
	uint8_t blendlevel = s_blend_levels[m_video_regs_2a & 3];

	uint32_t words_per_tile = bits_per_row * tile_h;

	uint32_t palette_offset = (attr & 0x0f00) >> 4;

	// the Circuit Racing game in PDC100 needs this or some graphics have bad colours at the edges when turning as it leaves stray lower bits set
	palette_offset >>= nc_bpp;
	palette_offset <<= nc_bpp;

	if (firstline < lastline)
	{
		int scanx = scanline - firstline;

		if ((scanx >= 0) && (scanline <= lastline))
		{
			draw_tilestrip(screenwidth, xmask, blend, flip_x, tile_h, tile_w, tilegfxdata_addr, tile, scanx, x, flip_y, palette_offset, nc_bpp, bits_per_row, words_per_tile, paletteram, blendlevel);
		}
	}
	else
	{
		// clipped from top
		int tempfirstline = firstline - (screenheight<<1);
		int templastline = lastline;
		int scanx = scanline - tempfirstline;

		if ((scanx >= 0) && (scanline <= templastline))
		{
			draw_tilestrip(screenwidth, xmask, blend, flip_x, tile_h, tile_w, tilegfxdata_addr, tile, scanx, x, flip_y, palette_offset, nc_bpp, bits_per_row, words_per_tile, paletteram, blendlevel);
		}
		// clipped against the bottom
		tempfirstline = firstline;
		templastline = lastline + (screenheight<<1);
		scanx = scanline - tempfirstline;

		if ((scanx >= 0) && (scanline <= templastline))
		{
			draw_tilestrip(screenwidth, xmask, blend, flip_x, tile_h, tile_w, tilegfxdata_addr, tile, scanx, x, flip_y, palette_offset, nc_bpp, bits_per_row, words_per_tile, paletteram, blendlevel);
		}
	}
}

void spg_renderer_device::draw_sprites(uint32_t scanline, int priority, uint32_t spritegfxdata_addr, uint16_t *paletteram, uint16_t *spriteram, int sprlimit)
{
	if (!(m_video_regs_42 & 0x0001))
		return;

	for (uint32_t n = 0; n < sprlimit; n++)
	{
		draw_sprite(scanline, priority, spritegfxdata_addr, 4 * n, paletteram, spriteram);
	}
}

void spg_renderer_device::update_palette_lookup()
{
	if (!m_brightness_or_saturation_dirty)
		return;

	static const float s_u8_to_float = 1.0f / 255.0f;
	static const float s_gray_r = 0.299f;
	static const float s_gray_g = 0.587f;
	static const float s_gray_b = 0.114f;
	const float sat_adjust = (0xff - (m_video_regs_3c & 0x00ff)) / (float)(0xff - 0x20);

	const uint16_t fade_offset = m_video_regs_30;

	for (uint16_t i = 0; i < 0x8000; i++)
	{
		uint32_t src = m_rgb555_to_rgb888[i];

		if ((m_video_regs_3c & 0x00ff) != 0x0020) // apply saturation
		{
			const uint32_t src_rgb = src;
			const float src_r = (uint8_t)(src_rgb >> 16) * s_u8_to_float;
			const float src_g = (uint8_t)(src_rgb >> 8) * s_u8_to_float;
			const float src_b = (uint8_t)(src_rgb >> 0) * s_u8_to_float;
			const float luma = src_r * s_gray_r + src_g * s_gray_g + src_b * s_gray_b;
			const float adjusted_r = luma + (src_r - luma) * sat_adjust;
			const float adjusted_g = luma + (src_g - luma) * sat_adjust;
			const float adjusted_b = luma + (src_b - luma) * sat_adjust;
			const int integer_r = (int)floor(adjusted_r * 255.0f);
			const int integer_g = (int)floor(adjusted_g * 255.0f);
			const int integer_b = (int)floor(adjusted_b * 255.0f);
			src = (integer_r > 255 ? 0xff0000 : (integer_r < 0 ? 0 : ((uint8_t)integer_r << 16))) |
				(integer_g > 255 ? 0x00ff00 : (integer_g < 0 ? 0 : ((uint8_t)integer_g << 8))) |
				(integer_b > 255 ? 0x0000ff : (integer_b < 0 ? 0 : (uint8_t)integer_b));
		}

		if (fade_offset != 0) // apply fade
		{
			const uint32_t src_rgb = src;
			const uint8_t src_r = (src_rgb >> 16) & 0xff;
			const uint8_t src_g = (src_rgb >> 8) & 0xff;
			const uint8_t src_b = (src_rgb >> 0) & 0xff;
			const uint8_t r = src_r - fade_offset;
			const uint8_t g = src_g - fade_offset;
			const uint8_t b = src_b - fade_offset;
			src = (r > src_r ? 0 : (r << 16)) |
				(g > src_g ? 0 : (g << 8)) |
				(b > src_b ? 0 : (b << 0));
		}

		m_rgb555_to_rgb888_current[i] = src;
	}

	m_brightness_or_saturation_dirty = false;
}

#define VIDEO_IRQ_ENABLE    m_video_regs[0x62]
#define VIDEO_IRQ_STATUS    m_video_regs[0x63]
void spg2xx_video_device::device_reset()
{
	memset(m_video_regs, 0, 0x100 * sizeof(uint16_t));

	m_video_regs[0x36] = 0xffff;
	m_video_regs[0x37] = 0xffff;
	//m_video_regs[0x3c] = 0x0020;
	//m_video_regs[0x42] = 0x0001;
}

void spg2xx_video_device::do_sprite_dma(uint32_t len)
{
	

	uint32_t src = m_video_regs[0x70] & 0x3fff;
	uint32_t dst = m_video_regs[0x71];

	for (uint32_t j = 0; j < len; j++)
	{
		int dest = dst + j;
		// jak_dpma does a full length transfer offset from the start, which causes corruption
		// on the options screen if we wrap, assume DMA just writes to nowhere if it goes out of bounds
		if (dest < 0x400)
			m_spriteram[dest] = m_soc->read_word(src + j);
	}

	m_video_regs[0x72] = 0;
	if (VIDEO_IRQ_ENABLE & 4)
	{
		const uint16_t old = VIDEO_IRQ_STATUS;
		VIDEO_IRQ_STATUS |= 4;
		const uint16_t changed = old ^ (VIDEO_IRQ_ENABLE & VIDEO_IRQ_STATUS);
		if (changed)
			check_video_irq();
	}
}

uint16_t spg2xx_video_device::video_r(offs_t offset)
{
	switch (offset)
	{
	case 0x10: // Page 1 X scroll
		/* log */
		return m_video_regs[offset];

	case 0x11: // Page 1 Y scroll
		/* log */
		return m_video_regs[offset];

	case 0x1c: // vertical compression, amount, 0x20 = no scale? (not on spg288?)
		/* log */
		return m_renderer->get_video_reg_1c();

	case 0x1d: // (not on spg288?)
		/* log */
		return m_renderer->get_video_reg_1d();

	case 0x1e: // (not on spg288?)
		/* log */
		return m_renderer->get_video_reg_1e();

	case 0x22: // Sprite Segment Address
		/* log */
		return m_video_regs[offset];

	case 0x2a: // Blend Level Control
		/* log */
		return m_renderer->get_video_reg_2a();

	case 0x30: // Fade Effect Control
		/* log */
		return m_renderer->get_video_reg_30();
		break;

	case 0x38: // Current Line
		/* log */
		return m_screen->vpos();

	case 0x3c: // TV Control 1
		/* log */
		return m_renderer->get_video_reg_3c();

	case 0x3e: // Light Pen Y Position
		/* log */
		return 0;

	case 0x3f: // Light Pen X Position
		/* log */
		return 0;

	case 0x42: // Sprite Control
		/* log */
		return m_renderer->get_video_reg_42();

	case 0x62: // Video IRQ Enable
		/* log */
		return VIDEO_IRQ_ENABLE;

	case 0x63: // Video IRQ Status
		/* log */
		return VIDEO_IRQ_STATUS;

	default:
		/* log */
		break;
	}
	return m_video_regs[offset];
}

void spg2xx_video_device::video_w(offs_t offset, uint16_t data)
{
	switch (offset)
	{
	case 0x10: // Page 1 X scroll
		/* log */
		m_video_regs[offset] = data & 0x01ff;
		break;

	case 0x11: // Page 1 Y scroll
		/* log */
		m_video_regs[offset] = data & 0x00ff;
		break;

	case 0x12: // Page 1 Attributes
	{
		uint16_t attr1 = data;
		/* log */
		m_video_regs[offset] = data;
		break;
	}

	case 0x13: // Page 1 Control
	{
		uint16_t ctrl1 = data;
		/* log */
		m_video_regs[offset] = data;
		break;
	}

	case 0x14: // Page 1 Tile Address
		/* log */
		m_video_regs[offset] = data;
		break;

	case 0x15: // Page 1 Attribute Address
		/* log */
		m_video_regs[offset] = data;
		break;

	case 0x16: // Page 2 X scroll
		/* log */
		m_video_regs[offset] = data & 0x01ff;
		break;

	case 0x17: // Page 2 Y scroll
		/* log */
		m_video_regs[offset] = data & 0x00ff;
		break;

	case 0x18: // Page 2 Attributes
	{
		uint16_t attr2 = data;
		/* log */
		m_video_regs[offset] = data;
		break;
	}

	case 0x19: // Page 2 Control
	{
		uint16_t ctrl2 = data;
		/* log */
		m_video_regs[offset] = data;
		break;
	}

	case 0x1a: // Page 2 Tile Address
		/* log */
		m_video_regs[offset] = data;
		break;

	case 0x1b: // Page 2 Attribute Address
		/* log */
		m_video_regs[offset] = data;
		break;

	case 0x1c: // vertical compression, amount, 0x20 = no scale? (not on spg288?)
		/* log */
		m_video_regs[offset] = data;
		m_renderer->set_video_reg_1c(data);
		break;

	case 0x1d: // (not on spg288?)
		/* log */
		m_video_regs[offset] = data;
		m_renderer->set_video_reg_1d(data);
		break;

	case 0x1e: // (not on spg288?)
		/* log */
		m_video_regs[offset] = data;
		m_renderer->set_video_reg_1e(data);
		break;

	case 0x20: // Page 1 Segment Address
		/* log */
		m_video_regs[offset] = data;
		break;

	case 0x21: // Page 2 Segment Address
		/* log */
		m_video_regs[offset] = data;
		break;

	case 0x22: // Sprite Segment Address
		/* log */
		m_video_regs[offset] = data;
		break;

	case 0x2a: // Blend Level Control
		/* log */
		m_video_regs[offset] = data & 0x0003;
		m_renderer->set_video_reg_2a(data);
		break;

	case 0x30: // Fade Effect Control
		/* log */
		m_video_regs[offset] = data & 0x00ff;
		m_renderer->set_video_reg_30(data);
		break;

	case 0x36: // IRQ pos V
	case 0x37: // IRQ pos H
		m_video_regs[offset] = data & 0x01ff;
		/* log */
		// Some smartvad games, stvscri, and some smartcyc games set the scanline IRQ to 240 and need it to trigger to progress.
		// should that be treated as valid, or is it intentionally disabling it for some other reason?
		// documentation suggests that 0-239 is the valid range, but could be incorrect
		if (m_video_regs[0x37] < 160 && m_video_regs[0x36] <= 240)
			m_screenpos_timer->adjust(m_screen->time_until_pos(m_video_regs[0x36], m_video_regs[0x37] << 1));
		else
			m_screenpos_timer->adjust(attotime::never());
		break;

	case 0x39: // Latch 1st Line Pen Pulse
		/* log */
		m_video_regs[offset] = data & 0x0001;
		break;

	case 0x3c: // TV Control 1
		/* log */
		m_video_regs[offset] = data;
		m_renderer->set_video_reg_3c(data);
		break;

	case 0x3d: // TV Control 2
	{
		static const char* const s_lpf_mode[4] = { "LPF1", "LPF2", "All", "Edge" };
		/* log */
		m_video_regs[offset] = data & 0x000f;
		break;
	}

	case 0x3e: // Light Pen Y Position
		/* log */
		break;

	case 0x3f: // Light Pen X Position
		/* log */
		break;

	case 0x42: // Sprite Control
		/* log */
		m_video_regs[offset] = data;
		m_renderer->set_video_reg_42(data);
		break;

	case 0x62: // Video IRQ Enable
	{
		/* log */
		const uint16_t old = VIDEO_IRQ_ENABLE & VIDEO_IRQ_STATUS;
		VIDEO_IRQ_ENABLE = data & 0x0007;
		const uint16_t changed = old ^ (VIDEO_IRQ_ENABLE & VIDEO_IRQ_STATUS);
		if (changed)
			check_video_irq();
		break;
	}

	case 0x63: // Video IRQ Acknowledge
	{
		/* log */
		const uint16_t old = VIDEO_IRQ_ENABLE & VIDEO_IRQ_STATUS;
		VIDEO_IRQ_STATUS &= ~data;
		const uint16_t changed = old ^ (VIDEO_IRQ_ENABLE & VIDEO_IRQ_STATUS);
		if (changed)
			check_video_irq();
		break;
	}

	case 0x70: // Sprite DMA Source
		/* log */
		m_video_regs[offset] = data & 0x3fff;
		break;

	case 0x71: // Sprite DMA Dest
		/* log */
		m_video_regs[offset] = data & 0x03ff;
		break;

	case 0x72: // Sprite DMA Length
	{
		/* log */
		uint16_t length = data & 0x3ff;
		do_sprite_dma(length ? length : 0x400);
		break;
	}

	default:
		/* log */
		m_video_regs[offset] = data;
		break;
	}
}

void spg2xx_video_device::vblank(int state)
{
	if (!state)
	{
		VIDEO_IRQ_STATUS &= ~1;
		/* log */
		check_video_irq();
		return;
	}

	if (VIDEO_IRQ_ENABLE & 1)
	{
		VIDEO_IRQ_STATUS |= 1;
		/* log */
		check_video_irq();
	}
}

void spg2xx_video_device::check_video_irq()
{
	/* log */
	m_soc->videoirq_w((VIDEO_IRQ_STATUS & VIDEO_IRQ_ENABLE) ? ASSERT_LINE : CLEAR_LINE);
}

void spg2xx_video_device::screenpos_hit(int param)
{
	if (VIDEO_IRQ_ENABLE & 2)
	{
		VIDEO_IRQ_STATUS |= 2;
		check_video_irq();
	}
	m_soc->update_partial(m_screen->vpos());

	// fire again, jak_dbz pinball needs this
	m_screenpos_timer->adjust(m_screen->time_until_pos(m_video_regs[0x36], m_video_regs[0x37] << 1));
}

uint16_t spg2xx_sysdma_device::dma_r(offs_t offset)
{
	offset &= 0x3;

	uint16_t val = m_dma_regs[offset];
	switch (offset)
	{

	case 0x000: // DMA Source (L)
		/* log */
		break;

	case 0x001: // DMA Source (H)
		/* log */
		break;

	case 0x002: // DMA Length
		/* log */
		val = 0;
		break;

	case 0x003: // DMA Destination
		/* log */
		break;
	}

	return val;
}

void spg2xx_sysdma_device::dma_w(offs_t offset, uint16_t data)
{
	offset &= 0x3;

	switch (offset)
	{
	case 0x000: // DMA Source (lo)
		/* log */
		m_dma_regs[offset] = data;
		break;

	case 0x001: // DMA Source (hi)
		/* log */
		m_dma_regs[offset] = data;
		break;

	case 0x002: // DMA Length
		/* log */
		if (!(data & 0xc000))  // jak_dora writes 0xffff here which ends up trashing registers etc. why? such writes can't be valid
			do_cpu_dma(data);
		break;

	case 0x003: // DMA Destination
		/* log */
		m_dma_regs[offset] = data;
		break;
	}
}

void spg2xx_sysdma_device::do_cpu_dma(uint32_t len)
{
	

	uint32_t src = ((m_dma_regs[0x001] & 0x3f) << 16) | m_dma_regs[0x000];
	uint32_t dst = m_dma_regs[0x003] & 0x3fff;

	for (uint32_t j = 0; j < len; j++)
	{
		m_soc->write_word((dst + j) & 0x3fff, m_soc->read_word(src + j));
	}

	src += len;
	m_dma_regs[0x000] = (uint16_t)src;
	m_dma_regs[0x001] = (src >> 16) & 0x3f;
	m_dma_regs[0x002] = 0;
	m_dma_regs[0x003] = (dst + len) & 0x3fff;
}

#define IO_IRQ_ENABLE       m_io_regs[REG_INT_CTRL]
#define IO_IRQ_STATUS       m_io_regs[REG_INT_CLEAR]
void spg2xx_io_device::device_reset()
{
	std::fill_n(&m_io_regs[0], 0x100, 0);

	m_timer_a_preload = 0;
	m_timer_b_preload = 0;
	m_timer_b_divisor = 0;
	m_timer_b_tick_rate = 0;

	m_io_regs[REG_EXT_MEMORY_CTRL] = 0x0028;
	m_io_regs[REG_PRNG1] = 0x1418;
	m_io_regs[REG_PRNG2] = 0x1658;

	std::fill(std::begin(m_uart_rx_fifo), std::end(m_uart_rx_fifo), 0);
	m_uart_rx_fifo_start = 0;
	m_uart_rx_fifo_end = 0;
	m_uart_rx_fifo_count = 0;
	m_uart_rx_available = false;
	m_uart_tx_irq = false;
	m_uart_rx_irq = false;

	std::fill(std::begin(m_spi_tx_fifo), std::end(m_spi_tx_fifo), 0);
	m_spi_tx_fifo_start = 0;
	m_spi_tx_fifo_end = 0;
	m_spi_tx_fifo_count = 0;
	m_spi_tx_buf = 0x00;
	m_spi_tx_bit = 8;

	std::fill(std::begin(m_spi_rx_fifo), std::end(m_spi_rx_fifo), 0);
	m_spi_rx_fifo_start = 0;
	m_spi_rx_fifo_end = 0;
	m_spi_rx_fifo_count = 0;
	m_spi_rx_buf = 0x00;
	m_spi_rx_bit = 7;

	m_spi_rate = 0;

	std::fill_n(&m_extint[0], 2, false);

	m_4khz_timer->adjust(attotime::from_hz(4096), 0, attotime::from_hz(4096));

	m_rng_timer->adjust(attotime::from_hz(1234), 0, attotime::from_hz(1234)); // timer value is arbitrary, maybe should match system clock, but that would result in heavy switching

	m_tmb1->adjust(attotime::never());
	m_tmb2->adjust(attotime::never());
	m_uart_tx_timer->adjust(attotime::never());
	m_uart_rx_timer->adjust(attotime::never());
	m_timer_src_ab->adjust(attotime::never());
	m_timer_src_c->adjust(attotime::never());
	m_watchdog_timer->adjust(attotime::never());
	m_spi_tx_timer->adjust(attotime::never());

	for (int i = 0; i < 4; i++)
	{
		m_adc_timer[i]->adjust(attotime::never());
	}

	m_2khz_divider = 0;
	m_1khz_divider = 0;
	m_4hz_divider = 0;

	m_sio_bits_remaining = 0;
	m_sio_writing = false;
}

void spg2xx_io_device::set_spi_irq(bool set)
{
	const uint16_t old = IO_IRQ_STATUS;
	if (set)
	{
		/* log */
		IO_IRQ_STATUS |= 0x4000;
	}
	else
	{
		/* log */
		IO_IRQ_STATUS &= ~0x4000;
	}

	const uint16_t changed = (old & IO_IRQ_ENABLE) ^ (IO_IRQ_STATUS & IO_IRQ_ENABLE);
	if (changed)
		check_data_irq();
}

void spg2xx_io_device::update_spi_irqs()
{
	bool ovf_set = BIT(m_io_regs[REG_SPI_RXSTATUS], 8);
	bool rxi_set = BIT(m_io_regs[REG_SPI_RXSTATUS], 15) && BIT(m_io_regs[REG_SPI_RXSTATUS], 14);
	bool txi_set = BIT(m_io_regs[REG_SPI_TXSTATUS], 15) && BIT(m_io_regs[REG_SPI_TXSTATUS], 14);
	set_spi_irq(ovf_set || rxi_set || txi_set);
}

void spg2xx_io_device::spi_tx_tick(int param)
{
	if (!BIT(m_io_regs[REG_SPI_CTRL], 15) || m_spi_tx_fifo_count == 0)
	{
		/* log */
		return;
	}

	if (m_spi_tx_bit == 8)
	{
		m_spi_tx_bit--;
		m_spi_tx_buf = m_spi_tx_fifo[m_spi_tx_fifo_end];
		m_spi_tx_fifo_end = (m_spi_tx_fifo_end + 1) & 0x0f;
		/* log */
	}

	m_spi_tx(BIT(m_spi_tx_buf, m_spi_tx_bit));

	if (m_spi_tx_bit == 0)
	{
		m_spi_tx_bit = 8;
		m_spi_tx_fifo_count--;
		/* log */
		if (m_spi_tx_fifo_count == 0)
		{
			/* log */
			m_spi_tx_timer->adjust(attotime::never());
		}

		m_io_regs[REG_SPI_TXSTATUS] &= ~0x000f;
		m_io_regs[REG_SPI_TXSTATUS] |= m_spi_tx_fifo_count;

		if ((m_io_regs[REG_SPI_TXSTATUS] & 0x000f) < ((m_io_regs[REG_SPI_TXSTATUS] >> 4) & 0x000f))
		{
			/* log */
			m_io_regs[REG_SPI_TXSTATUS] |= 0x8000;
			update_spi_irqs();
		}
	}
	else
	{
		m_spi_tx_bit--;
	}
}

void spg2xx_io_device::rng_clock_tick(int param)
{
	clock_rng(0);
	clock_rng(1);
}

uint16_t spg2xx_io_device::clock_rng(int which)
{
	const uint16_t value = m_io_regs[REG_PRNG1 + which];
	m_io_regs[REG_PRNG1 + which] = ((value << 1) | (BIT(value, 14) ^ BIT(value, 13))) & 0x7fff;
	return value;
}

uint16_t spg2xx_io_device::io_r(offs_t offset)
{
	static const char *const gpioregs[] = { "GPIO Data Port", "GPIO Buffer Port", "GPIO Direction Port", "GPIO Attribute Port", "GPIO IRQ/Latch Port" };
	static const char gpioports[] = { 'A', 'B', 'C' };

	uint16_t val = m_io_regs[offset];

	switch (offset)
	{
	case REG_IOA_DATA: case REG_IOB_DATA: case REG_IOC_DATA:
		if (!machine().side_effects_disabled())
		{
			do_gpio(offset, false);
			/* log */
		}
		val = m_io_regs[offset];
		break;

	case REG_IOA_BUFFER: case REG_IOA_DIR: case REG_IOA_ATTRIB: case REG_IOA_MASK:
	case REG_IOB_BUFFER: case REG_IOB_DIR: case REG_IOB_ATTRIB: case REG_IOB_MASK:
	case REG_IOC_BUFFER: case REG_IOC_DIR: case REG_IOC_ATTRIB: case REG_IOC_MASK:
		/* log */
		break;

	case REG_TIMEBASE_SETUP:
		/* log */
		break;

	case REG_TIMERA_DATA:
		/* log */
		break;

	case REG_VERT_LINE:
		val = m_screen->vpos();
		/* log */
		break;

	case REG_SYSTEM_CTRL:
		/* log */
		break;

	case REG_INT_CTRL:
		/* log */
		break;

	case REG_INT_CLEAR:
		/* log */
		break;

	case REG_EXT_MEMORY_CTRL:
		/* log */
		break;

	case REG_ADC_CTRL:
		/* log */
		break;

	case REG_ADC_PAD:
		/* log */
		break;

	case REG_ADC_DATA:
		/* log */
		break;

	case REG_WAKEUP_SOURCE:
		/* log */
		break;

	case REG_NTSC_PAL:
		val = m_pal_read_cb();
		/* log */
		break;

	case REG_PRNG1:
		if (!machine().side_effects_disabled())
			return clock_rng(0);
		return m_io_regs[REG_PRNG1];

	case REG_PRNG2:
		if (!machine().side_effects_disabled())
			return clock_rng(1);
		return m_io_regs[REG_PRNG2];

	case REG_FIQ_SEL:
		/* log */
		break;

	case REG_DATA_SEGMENT:
		val = m_cpu->get_ds();
		/* log */
		break;

	default:
		/* log */
		break;
	}

	return val;
}

uint16_t spg2xx_io_device::io_extended_r(offs_t offset)
{
	// this set of registers might only be on the 24x not the 11x

	offset += 0x30;

	uint16_t val = m_io_regs[offset];

	switch (offset)
	{
	case REG_UART_CTRL:
		/* log */
		break;

	case REG_UART_STATUS:
		/* log */
		break;

	case REG_UART_RXBUF:
		if (!machine().side_effects_disabled())
		{
			if (m_uart_rx_available)
			{
				m_io_regs[REG_UART_STATUS] &= ~0x0081;
				/* log */
				if (m_uart_rx_fifo_count)
				{
					/* log */
					m_io_regs[REG_UART_RXBUF] = m_uart_rx_fifo[m_uart_rx_fifo_start];
					val = m_io_regs[REG_UART_RXBUF];
					m_uart_rx_fifo_start = (m_uart_rx_fifo_start + 1) % std::size(m_uart_rx_fifo);
					m_uart_rx_fifo_count--;

					if (m_uart_rx_fifo_count == 0)
					{
						m_uart_rx_available = false;
					}
					else
					{
						/* log */
						if (m_uart_rx_timer->remaining() == attotime::never())
							m_uart_rx_timer->adjust(attotime::from_ticks(BIT(m_io_regs[REG_UART_CTRL], 5) ? 11 : 10, m_uart_baud_rate));
					}
				}
				else
				{
					m_uart_rx_available = false;
				}
			}
			else
			{
				m_io_regs[REG_UART_RXFIFO] |= 0x2000;
			}
			/* log */
		}
		break;

	case REG_UART_RXFIFO:
		val &= ~0x0070;
		val |= (m_uart_rx_available ? 7 : 0) << 4;
		/* log */
		break;

	case REG_SPI_CTRL:
		/* log */
		break;

	case REG_SPI_TXSTATUS:
		/* log */
		break;

	case REG_SPI_TXDATA:
		/* log */
		break;

	case REG_SPI_RXSTATUS:
		/* log */
		break;

	case REG_SPI_RXDATA:
		/* log */
		if (m_spi_rx_fifo_count > 0 && !machine().side_effects_disabled())
		{
			m_spi_rx_fifo_count--;
			if (m_spi_rx_fifo_count > 0)
			{
				m_spi_rx_fifo_end = (m_spi_rx_fifo_end + 1) & 0x0f;
				m_io_regs[REG_SPI_RXDATA] = m_spi_rx_fifo[m_spi_rx_fifo_end];
			}

			m_io_regs[REG_SPI_RXSTATUS] &= ~(0x0200); // Clear RXFULL
			m_io_regs[REG_SPI_MISC] &= ~(0x0008); // Clear RFF
			if (m_spi_rx_fifo_count == 0)
			{
				m_io_regs[REG_SPI_MISC] &= ~(0x0004); // Clear RNE
			}
		}
		break;

	case REG_SPI_MISC:
		/* log */
		break;

	case REG_SIO_SETUP:
		/* log */
		break;

	case REG_SIO_STATUS:
		/* log */
		break;

	case REG_SIO_DATA:
		/* log */
		if ((m_io_regs[REG_SIO_STATUS] & 0x8000) && !m_sio_writing && !machine().side_effects_disabled())
		{
			m_sio_bits_remaining--;
			if (m_sio_bits_remaining == 0)
			{
				m_io_regs[REG_SIO_STATUS] &= ~0x8000;
			}
		}
		break;

	case REG_I2C_CMD:
		/* log */
		break;

	case REG_I2C_STATUS:
		/* log */
		break;

	case REG_I2C_DATA_IN: // I2C Data In
		/* log */
		break;

	default:
		/* log */
		break;
	}

	return val;
}

void spg2xx_io_device::update_timer_b_rate()
{
	switch (m_io_regs[REG_TIMERB_CTRL] & 7)
	{
		case 0:
		case 1:
		case 5:
		case 6:
		case 7:
			m_timer_src_c->adjust(attotime::never());
			break;
		case 2:
			m_timer_src_c->adjust(attotime::from_hz(32768), 0, attotime::from_hz(32768));
			break;
		case 3:
			m_timer_src_c->adjust(attotime::from_hz(8192), 0, attotime::from_hz(8192));
			break;
		case 4:
			m_timer_src_c->adjust(attotime::from_hz(4096), 0, attotime::from_hz(4096));
			break;
	}
}

void spg2xx_io_device::timer_ab_tick(int param)
{
	if (m_timer_b_tick_rate == 0)
		return;

	m_timer_b_divisor++;
	if (m_timer_b_divisor >= m_timer_b_tick_rate)
	{
		m_timer_b_divisor = 0;
		increment_timer_a();
	}
}

void spg2xx_io_device::increment_timer_a()
{
	m_io_regs[REG_TIMERA_DATA]++;
	if (m_io_regs[REG_TIMERA_DATA] == 0)
	{
		m_io_regs[REG_TIMERA_DATA] = m_timer_a_preload;
		const uint16_t old = IO_IRQ_STATUS;
		IO_IRQ_STATUS |= 0x0800;
		const uint16_t changed = (old & IO_IRQ_ENABLE) ^ (IO_IRQ_STATUS & IO_IRQ_ENABLE);
		if (changed)
			check_timers_irq();
	}
}

void spg2xx_io_device::timer_c_tick(int param)
{
	m_io_regs[REG_TIMERB_DATA]++;
	if (m_io_regs[REG_TIMERB_DATA] == 0)
	{
		m_io_regs[REG_TIMERB_DATA] = m_timer_b_preload;
		const uint16_t old = IO_IRQ_STATUS;
		IO_IRQ_STATUS |= 0x0400;
		const uint16_t changed = (old & IO_IRQ_ENABLE) ^ (IO_IRQ_STATUS & IO_IRQ_ENABLE);
		if (changed)
			check_timers_irq();
	}
}

void spg2xx_io_device::io_w(offs_t offset, uint16_t data)
{
	static const char *const gpioregs[] = { "GPIO Data Port", "GPIO Buffer Port", "GPIO Direction Port", "GPIO Attribute Port", "GPIO IRQ/Latch Port" };
	static const char gpioports[3] = { 'A', 'B', 'C' };

	switch (offset)
	{
	case REG_IO_MODE:
	{
		/* log */
		const uint16_t old = m_io_regs[REG_IO_MODE];
		m_io_regs[REG_IO_MODE] = data;
		const uint16_t changed = old ^ data;
		if (BIT(changed, 0))
			update_porta_special_modes();
		if (BIT(changed, 1))
			update_portb_special_modes();
		break;
	}

	case REG_IOA_DATA: case REG_IOB_DATA: case REG_IOC_DATA:
		offset++;
		[[fallthrough]]; // we redirect data register writes to the buffer register.

	case REG_IOA_BUFFER: case REG_IOA_ATTRIB:
	case REG_IOB_BUFFER: case REG_IOB_ATTRIB:
	case REG_IOC_BUFFER: case REG_IOC_DIR: case REG_IOC_ATTRIB: case REG_IOC_MASK:
		/* log */
		m_io_regs[offset] = data;
		do_gpio(offset, true);
		break;

	case REG_IOA_DIR:
		/* log */
		m_io_regs[REG_IOA_DIR] = data;
		update_porta_special_modes();
		do_gpio(offset, true);
		break;

	case REG_IOB_DIR:
		/* log */
		m_io_regs[REG_IOB_DIR] = data;
		update_portb_special_modes();
		do_gpio(offset, true);
		break;

	case REG_IOA_MASK:
		/* log */
		m_io_regs[REG_IOA_MASK] = data;
		update_porta_special_modes();
		break;

	case REG_IOB_MASK:
		/* log */
		m_io_regs[REG_IOB_MASK] = data;
		update_portb_special_modes();
		break;

	case REG_TIMEBASE_SETUP:
	{
		static const char* const s_tmb1_sel[2][4] =
		{
			{ "8Hz", "16Hz", "32Hz", "64Hz" },
			{ "12kHz", "24kHz", "40kHz", "40kHz" }
		};
		static const char* const s_tmb2_sel[2][4] =
		{
			{ "128Hz", "256Hz", "512Hz", "1024Hz" },
			{ "105kHz", "210kHz", "420kHz", "840kHz" }
		};
		static const uint32_t s_tmb1_freq[2][4] =
		{
			{ 8, 16, 32, 64 },
			{ 12000, 24000, 40000, 40000 }
		};
		static const uint32_t s_tmb2_freq[2][4] =
		{
			{ 128, 256, 512, 1024 },
			{ 105000, 210000, 420000, 840000 }
		};
		/* log */
		m_io_regs[REG_TIMEBASE_SETUP] = data;
		const uint8_t hifreq = BIT(data, 4);
		const uint32_t tmb1freq = s_tmb1_freq[hifreq][data & 3];
		m_tmb1->adjust(attotime::from_hz(tmb1freq), 0, attotime::from_hz(tmb1freq));
		const uint32_t tmb2freq = s_tmb2_freq[hifreq][(data >> 2) & 3];
		m_tmb2->adjust(attotime::from_hz(tmb2freq), 0, attotime::from_hz(tmb2freq));
		break;
	}

	case REG_TIMEBASE_CLEAR:
		/* log */
		m_2khz_divider = 0;
		m_1khz_divider = 0;
		m_4hz_divider = 0;
		break;

	case REG_TIMERA_DATA:
		/* log */
		m_io_regs[REG_TIMERA_DATA] = data;
		m_timer_a_preload = data;
		break;

	case REG_TIMERA_CTRL:
	{
		static const char* const s_source_a[8] = { "0", "0", "32768Hz", "8192Hz", "4096Hz", "1", "0", "ExtClk1" };
		static const char* const s_source_b[8] = { "2048Hz", "1024Hz", "256Hz", "TMB1", "4Hz", "2Hz", "1", "ExtClk2" };
		/* log */
		m_io_regs[REG_TIMERA_CTRL] = data;
		int timer_a_rate = 0;
		switch (data & 7)
		{
			case 0:
			case 1:
			case 5:
			case 6:
			case 7:
				m_timer_src_ab->adjust(attotime::never());
				break;
			case 2:
				m_timer_src_ab->adjust(attotime::from_hz(32768), 0, attotime::from_hz(32768));
				timer_a_rate = 32768;
				break;
			case 3:
				m_timer_src_ab->adjust(attotime::from_hz(8192), 0, attotime::from_hz(8192));
				timer_a_rate = 8192;
				break;
			case 4:
				m_timer_src_ab->adjust(attotime::from_hz(4096), 0, attotime::from_hz(4096));
				timer_a_rate = 4096;
				break;
		}
		switch ((data >> 3) & 7)
		{
			case 0:
				m_timer_b_tick_rate = timer_a_rate / 2048;
				break;
			case 1:
				m_timer_b_tick_rate = timer_a_rate / 1024;
				break;
			case 2:
				m_timer_b_tick_rate = timer_a_rate / 256;
				break;
			case 3:
				m_timer_b_tick_rate = 0;
				break;
			case 4:
				m_timer_b_tick_rate = timer_a_rate / 4;
				break;
			case 5:
				m_timer_b_tick_rate = timer_a_rate / 2;
				break;
			case 6:
				m_timer_b_tick_rate = 1;
				break;
			case 7:
				m_timer_b_tick_rate = 0;
				break;
		}
		break;
	}

	case REG_TIMERA_IRQCLR:
	{
		/* log */
		const uint16_t old = IO_IRQ_STATUS;
		IO_IRQ_STATUS &= ~0x0800;
		const uint16_t changed = (old & IO_IRQ_ENABLE) ^ (IO_IRQ_STATUS & IO_IRQ_ENABLE);
		if (changed)
			check_timers_irq();
		break;
	}

	case REG_TIMERB_DATA:
		/* log */
		m_io_regs[REG_TIMERB_DATA] = data;
		m_timer_b_preload = data;
		break;

	case REG_TIMERB_CTRL:
	{
		static const char* const s_source_c[8] = { "0", "0", "32768Hz", "8192Hz", "4096Hz", "1", "0", "ExtClk1" };
		/* log */
		m_io_regs[REG_TIMERB_CTRL] = data;
		if (m_io_regs[REG_TIMERB_ON] == 1)
		{
			update_timer_b_rate();
		}
		break;
	}

	case REG_TIMERB_ON:
	{
		/* log */
		m_io_regs[REG_TIMERB_ON] = data & 1;
		if (data & 1)
		{
			update_timer_b_rate();
		}
		else
		{
			m_timer_src_c->adjust(attotime::never());
		}
		break;
	}

	case REG_TIMERB_IRQCLR:
	{
		/* log */
		const uint16_t old = IO_IRQ_STATUS;
		IO_IRQ_STATUS &= ~0x0400;
		const uint16_t changed = (old & IO_IRQ_ENABLE) ^ (IO_IRQ_STATUS & IO_IRQ_ENABLE);
		if (changed)
			check_timers_irq();
		break;
	}

	case REG_SYSTEM_CTRL:
	{
		static const char* const s_sysclk[4] = { "13.5MHz", "27MHz", "27MHz NoICE", "54MHz" };
		static const char* const s_lvd_voltage[4] = { "2.7V", "2.9V", "3.1V", "3.3V" };
		static const char* const s_weak_strong[2] = { "Weak", "Strong" };
		/* log */
		/* log */
		const uint16_t old = m_io_regs[REG_SYSTEM_CTRL];
		m_io_regs[REG_SYSTEM_CTRL] = data;
		if (BIT(old, 15) != BIT(data, 15))
		{
			if (BIT(data, 15))
				m_watchdog_timer->adjust(attotime::from_msec(750));
			else
				m_watchdog_timer->adjust(attotime::never());
		}
		break;
	}

	case REG_INT_CTRL:
	{
		/* log */
		const uint16_t old = IO_IRQ_ENABLE;
		m_io_regs[REG_INT_CTRL] = data;
		const uint16_t changed = (IO_IRQ_STATUS & old) ^ (IO_IRQ_STATUS & IO_IRQ_ENABLE);
		if (changed)
			check_all_irqs(changed);
		break;
	}

	case REG_INT_CLEAR:
	{
		/* log */
		const uint16_t old = IO_IRQ_STATUS;
		IO_IRQ_STATUS &= ~data;
		const uint16_t changed = (old & IO_IRQ_ENABLE) ^ (IO_IRQ_STATUS & IO_IRQ_ENABLE);
		if (m_uart_rx_irq || m_uart_tx_irq)
		{
			/* log */
			IO_IRQ_STATUS |= 0x0100;
		}
		if (changed)
			check_all_irqs(changed);
		break;
	}

	case REG_EXT_MEMORY_CTRL:
	{
		static const char* const s_bus_arb[8] =
		{
			"Forbidden", "Forbidden", "Forbidden", "Forbidden", "Forbidden", "1:SPU/2:PPU/3:CPU", "Forbidden", "1:PPU/2:SPU/3:CPU"
		};
		static const char* const s_addr_decode[4] =
		{
			"ROMCSB: 4000-3fffff, CSB1: ---,           CSB2: ---,           CSB3: ---",
			"ROMCSB: 4000-1fffff, CSB1: 200000-3fffff, CSB2: ---,           CSB3: ---",
			"ROMCSB: 4000-0fffff, CSB1: 100000-1fffff, CSB2: 200000-2fffff, CSB3: 300000-3fffff",
			"ROMCSB: 4000-0fffff, CSB1: 100000-1fffff, CSB2: 200000-2fffff, CSB3: 300000-3fffff"
		};
		static const char* const s_ram_decode[16] =
		{
			"None", "None", "None", "None", "None", "None", "None", "None",
			"4KW,   3ff000-3fffff\n",
			"8KW,   3fe000-3fffff\n",
			"16KW,  3fc000-3fffff\n",
			"32KW,  3f8000-3fffff\n",
			"64KW,  3f0000-3fffff\n",
			"128KW, 3e0000-3fffff\n",
			"256KW, 3c0000-3fffff\n",
			"512KW, 380000-3fffff\n"
		};
		/* log */
		/* log */
		/* log */
		/* log */
		m_chip_sel((data >> 6) & 3);
		m_io_regs[REG_EXT_MEMORY_CTRL] = data;
		break;
	}

	case REG_WATCHDOG_CLEAR:
		/* log */
		if (data == 0x55aa && BIT(m_io_regs[REG_SYSTEM_CTRL], 15))
		{
			m_watchdog_timer->adjust(attotime::from_msec(750));
		}
		break;

	case REG_ADC_CTRL:
	{
		/* log */

		const uint16_t old_ctrl = m_io_regs[REG_ADC_CTRL];
		m_io_regs[REG_ADC_CTRL] = data & ~0x2000;

		if (BIT(old_ctrl, 13) && BIT(data, 13))
		{
			m_io_regs[REG_ADC_CTRL] &= ~0x2000;
			IO_IRQ_STATUS &= ~0x2000;
			check_data_irq();
		}

		if (BIT(m_io_regs[REG_ADC_CTRL], 0))
		{
			// Assume ready unless stated otherwise (i.e., conversion request is issued).
			m_io_regs[REG_ADC_CTRL] |= 0x2000;

			const uint16_t adc_channel = (m_io_regs[REG_ADC_CTRL] >> 4) & 3;
			if (!BIT(old_ctrl, 12) && BIT(m_io_regs[REG_ADC_CTRL], 12))
			{
				m_io_regs[REG_ADC_CTRL] &= ~0x3000;
				const uint32_t adc_clocks = 16 << ((m_io_regs[REG_ADC_CTRL] >> 2) & 3);
				m_adc_timer[adc_channel]->adjust(attotime::from_ticks(adc_clocks, 27000000));
				m_io_regs[REG_ADC_DATA] &= ~0x8000;
			}

			// Req_Auto_8K
			if (BIT(data, 10))
			{
				m_io_regs[REG_ADC_DATA] &= ~0x8000;
				m_adc_timer[adc_channel]->adjust(attotime::from_hz(8000), 0, attotime::from_hz(8000));
			}
		}
		else
		{
			for (int i = 0; i < 4; i++)
			{
				m_adc_timer[i]->adjust(attotime::never());
			}
		}
		break;
	}

	case REG_ADC_PAD:
		/* log */
		m_io_regs[REG_ADC_PAD] = data;
		for (int i = 0; i < 4; i++)
		{
			if (!BIT(data, i))
			{
				m_adc_timer[i]->adjust(attotime::never());
			}
		}
		break;

	case REG_SLEEP_MODE:
		/* log */
		m_io_regs[REG_SLEEP_MODE] = data;
		break;

	case 0x29: // Wakeup Source
	{
		m_io_regs[REG_WAKEUP_SOURCE] = data;
		static const char* const s_sources[8] =
		{
			"TMB1", "TMB2", "2Hz", "4Hz", "1024Hz", "2048Hz", "4096Hz", "Key"
		};

		/* log */
		bool comma = false;
		char buf[1024];
		int char_idx = 0;
		for (int i = 7; i >= 0; i--)
		{
			if (BIT(data, i))
			{
				char_idx += sprintf(&buf[char_idx], "%s%s", comma ? ", " : "", s_sources[i]);
				comma = true;
			}
		}
		buf[char_idx] = 0;
		/* log */
		break;
	}

	case REG_PRNG1:
		/* log */
		m_io_regs[REG_PRNG1] = data & 0x7fff;
		break;

	case REG_PRNG2:
		/* log */
		m_io_regs[REG_PRNG2] = data & 0x7fff;
		break;

	case REG_FIQ_SEL:
	{
		static const char* const s_fiq_select[8] =
		{
			"PPU", "SPU Channel", "Timer A", "Timer B", "UART/SPI", "External", "Reserved", "None"
		};
		/* log */
		m_io_regs[REG_FIQ_SEL] = data;
		m_fiq_vector_w(data & 7);
		break;
	}

	case REG_DATA_SEGMENT:
		m_cpu->set_ds(data & 0x3f);
		/* log */
		break;

	default:
		/* log */
		m_io_regs[offset] = data;
		break;
	}
}

void spg2xx_io_device::io_extended_w(offs_t offset, uint16_t data)
{
	// this set of registers might only be on the 24x not the 11x

	offset += REG_UART_CTRL;

	switch (offset)
	{

	case REG_UART_CTRL: // UART Control
	{
		static const char* const s_9th_bit[4] = { "0", "1", "Odd", "Even" };
		/* log */
		const uint16_t changed = m_io_regs[REG_UART_CTRL] ^ data;
		m_io_regs[REG_UART_CTRL] = data;
		if (!BIT(data, 6))
		{
			m_uart_rx_available = false;
			m_io_regs[REG_UART_RXBUF] = 0;
		}
		if (BIT(changed, 7))
		{
			if (BIT(data, 7))
			{
				m_io_regs[REG_UART_STATUS] |= 0x0002;
			}
			else
			{
				m_io_regs[REG_UART_STATUS] &= ~0x0042;
				m_uart_tx_timer->adjust(attotime::never());
			}
		}
		break;
	}

	case REG_UART_STATUS:
		/* log */
		if (BIT(data, 0))
		{
			m_io_regs[REG_UART_STATUS] &= ~1;
			m_uart_rx_irq = false;
		}
		if (BIT(data, 1))
		{
			m_io_regs[REG_UART_STATUS] &= ~2;
			m_uart_tx_irq = false;
		}
		if (!m_uart_rx_irq && !m_uart_tx_irq)
		{
			const uint16_t old = IO_IRQ_STATUS;
			IO_IRQ_STATUS &= ~0x0100;
			const uint16_t changed = (old & IO_IRQ_ENABLE) ^ (IO_IRQ_STATUS & IO_IRQ_ENABLE);
			if (changed)
				check_data_irq();
		}
		break;

	case REG_UART_BAUD1: // (low byte)
	case REG_UART_BAUD2: // (high byte)
	{
		m_io_regs[offset] = data;
		const uint32_t divisor = 16 * (0x10000 - ((m_io_regs[REG_UART_BAUD2] << 8) | m_io_regs[REG_UART_BAUD1]));
		/* log */
		m_uart_baud_rate = 27000000 / divisor;
		break;
	}

	case REG_UART_TXBUF:
		/* log */
		m_io_regs[REG_UART_TXBUF] = data;
		if (BIT(m_io_regs[REG_UART_CTRL], 7))
		{
			/* log */
			m_uart_tx_timer->adjust(attotime::from_ticks(BIT(m_io_regs[REG_UART_CTRL], 5) ? 11 : 10, m_uart_baud_rate));
			m_io_regs[REG_UART_STATUS] &= ~0x0002;
			m_io_regs[REG_UART_STATUS] |= 0x0040;
		}
		break;

	case REG_UART_RXBUF:
		/* log */
		break;

	case REG_UART_RXFIFO:
		/* log */
		if (data & 0x8000)
		{
			m_uart_rx_available = false;
			m_io_regs[REG_UART_RXBUF] = 0;
		}
		m_io_regs[REG_UART_RXFIFO] &= ~data & 0x6000;
		m_io_regs[REG_UART_RXFIFO] &= ~0x0007;
		m_io_regs[REG_UART_RXFIFO] |= data & 0x0007;
		break;

	case REG_SPI_CTRL:
	{
		static const char* const s_spi_clock[8] = { "SYSCLK/2" , "SYSCLK/4", "SYSCLK/8", "SYSCLK/16", "SYSCLK/32", "SYSCLK/64", "SYSCLK/128", "Reserved" };
		/* log */
		m_io_regs[offset] = data;
		if ((data & 7) == 7 || !BIT(m_io_regs[offset], 15))
		{
			m_spi_rate = 0;
			m_spi_tx_timer->adjust(attotime::never());
		}
		else
		{
			m_spi_rate = 2 << (data & 7);
			attotime rate = attotime::from_ticks(m_spi_rate, clock());
			if (m_spi_tx_timer->remaining() != attotime::never())
				m_spi_tx_timer->adjust(rate);
		}
		break;
	}

	case REG_SPI_TXSTATUS:
		/* log */
		m_io_regs[offset] &= ~0x40f0;
		m_io_regs[offset] |= data & 0x40f0;
		if (BIT(data, 15))
		{
			m_io_regs[offset] &= ~0x8000;
			update_spi_irqs();
		}
		break;

	case REG_SPI_TXDATA:
		/* log */
		m_io_regs[offset] = data;
		if (BIT(m_io_regs[REG_SPI_CTRL], 15))
		{
			if (m_spi_tx_fifo_count < 16)
			{
				/* log */
				m_spi_tx_fifo[m_spi_tx_fifo_start] = (uint8_t)m_io_regs[REG_SPI_TXDATA];
				m_spi_tx_fifo_start = (m_spi_tx_fifo_start + 1) & 0x0f;
				m_spi_tx_fifo_count++;

				attotime rate = attotime::from_ticks(m_spi_rate, clock());
				m_spi_tx_timer->adjust(rate, 0, rate);
			}
			else
			{
				/* log */
			}
		}
		break;

	case REG_SPI_RXSTATUS:
		/* log */
		m_io_regs[offset] &= ~0x40f0;
		m_io_regs[offset] |= data & 0x40f0;
		m_io_regs[offset] &= ~0x0100; // Clear RXFOV
		if (BIT(data, 15))
		{
			m_io_regs[offset] &= ~0x8000;
			update_spi_irqs();
		}
		break;

	case REG_SPI_RXDATA:
		/* log */
		break;

	case REG_SPI_MISC:
	{
		/* log */
		m_io_regs[offset] &= ~0x0300;
		m_io_regs[offset] |= data & 0x0300;
		break;
	}

	case REG_SIO_SETUP:
	{
		static const char* const s_addr_mode[4] = { "16-bit", "None", "8-bit", "24-bit" };
		static const char* const s_baud_rate[4] = { "/16", "/4", "/8", "/32" };
		/* log */
		/* log */
		if (BIT(data, 10))
		{
			m_io_regs[REG_SIO_STATUS] |= 0x8000;
			m_sio_bits_remaining = BIT(data, 7) ? 16 : 8;
			m_sio_writing = BIT(data, 5);
		}
		else
		{
			m_io_regs[REG_SIO_STATUS] &= ~0x8000;
		}
		break;
	}

	case REG_SIO_ADDRL:
		/* log */
		break;

	case REG_SIO_ADDRH:
		/* log */
		break;

	case REG_SIO_DATA:
		/* log */
		if ((m_io_regs[REG_SIO_SETUP] & 0x8000) && m_sio_writing)
		{
			m_sio_bits_remaining--;
			if (m_sio_bits_remaining == 0)
			{
				m_io_regs[REG_SIO_STATUS] &= ~0x8000;
			}
		}
		break;

	case REG_SIO_AUTO_TX_NUM:
		/* log */
		break;

	case REG_I2C_CMD:
		/* log */
		m_io_regs[REG_I2C_CMD] = data;
		do_i2c();
		break;

	case REG_I2C_STATUS:
		/* log */
		m_io_regs[REG_I2C_STATUS] &= ~data;
		break;

	case REG_I2C_ACCESS:
		/* log */
		m_io_regs[REG_I2C_ACCESS] = data;
		break;

	case REG_I2C_ADDR:
		/* log */
		m_io_regs[REG_I2C_ADDR] = data;
		break;

	case REG_I2C_SUBADDR:
		/* log */
		m_io_regs[REG_I2C_SUBADDR] = data;
		break;

	case REG_I2C_DATA_OUT:
		/* log */
		m_io_regs[REG_I2C_DATA_OUT] = data;
		break;

	case REG_I2C_DATA_IN:
		/* log */
		m_io_regs[REG_I2C_DATA_IN] = data;
		break;

	case REG_I2C_MODE:
		/* log */
		m_io_regs[REG_I2C_MODE] = data;
		break;

	default:
		/* log */
		m_io_regs[offset] = data;
		break;
	}
}

template <int Which>
void spg2xx_io_device::tmb_timer_tick(int param)
{
	/* log */
	const uint16_t old = IO_IRQ_STATUS;
	IO_IRQ_STATUS |= (1 << Which);
	const uint16_t changed = (old & IO_IRQ_ENABLE) ^ (IO_IRQ_STATUS & IO_IRQ_ENABLE);
	if (changed)
		check_tmb_lofreq_key_irq();
}

void spg2xx_io_device::watchdog_tick(int param)
{
	m_soc->m_cpu.reset();
	
}

void spg2xx_io_device::system_timer_tick(int param)
{
	IO_IRQ_STATUS |= 0x0040;

	bool check_lofreq = false;

	m_2khz_divider++;
	if (m_2khz_divider == 2)
	{
		m_2khz_divider = 0;
		IO_IRQ_STATUS |= 0x0020;

		m_1khz_divider++;
		if (m_1khz_divider == 2)
		{
			m_1khz_divider = 0;
			IO_IRQ_STATUS |= 0x0010;

			m_4hz_divider++;
			if (m_4hz_divider == 256)
			{
				m_4hz_divider = 0;
				IO_IRQ_STATUS |= 0x0008;
				check_lofreq = BIT(IO_IRQ_ENABLE, 3);
			}
		}
	}

	check_hifreq_periodic_irq();
	if (check_lofreq)
		check_tmb_lofreq_key_irq();
}

void spg2xx_io_device::uart_transmit_tick(int param)
{
	/* log */
	m_uart_tx((uint8_t)m_io_regs[REG_UART_TXBUF]);
	m_io_regs[REG_UART_STATUS] |= 0x0002;
	m_io_regs[REG_UART_STATUS] &= ~0x0040;
	if (BIT(m_io_regs[REG_UART_CTRL], 1))
	{
		const uint16_t old = IO_IRQ_STATUS;
		IO_IRQ_STATUS |= 0x0100;
		m_uart_tx_irq = true;
		/* log */
		if (IO_IRQ_STATUS != old && BIT(IO_IRQ_ENABLE, 8))
		{
			/* log */
			check_data_irq();
		}
	}
}

void spg2xx_io_device::uart_receive_tick(int param)
{
	/* log */
	m_io_regs[REG_UART_STATUS] |= 0x81;
	m_uart_rx_available = true;
	if (BIT(m_io_regs[REG_UART_CTRL], 0))
	{
		/* log */
		m_uart_rx_irq = true;
		IO_IRQ_STATUS |= 0x0100;
		if (BIT(IO_IRQ_ENABLE, 8))
			check_data_irq();
	}
}

void spg2xx_io_device::check_extint_irq(int channel)
{
	/* log */
	const uint16_t mask = (channel == 0) ? 0x0200 : 0x1000;
	const uint16_t old_irq = IO_IRQ_STATUS;
	if (m_extint[channel])
		IO_IRQ_STATUS |= mask;
	else
		IO_IRQ_STATUS &= ~mask;

	if (old_irq != IO_IRQ_STATUS)
	{
		/* log */
		check_external_irq();
	}
}

void spg2xx_io_device::check_timers_irq()
{
	/* log */
	m_timer_irq_cb((IO_IRQ_ENABLE & IO_IRQ_STATUS & 0x0c00) ? ASSERT_LINE : CLEAR_LINE);
}

void spg2xx_io_device::check_data_irq()
{
	/* log */
	m_uart_adc_irq_cb((IO_IRQ_ENABLE & IO_IRQ_STATUS & 0x6100) ? ASSERT_LINE : CLEAR_LINE);
}

void spg2xx_io_device::check_external_irq()
{
	/* log */
	m_external_irq_cb((IO_IRQ_ENABLE & IO_IRQ_STATUS & 0x1200) ? ASSERT_LINE : CLEAR_LINE);
}

void spg2xx_io_device::check_hifreq_periodic_irq()
{
	/* log */
	m_ffreq_tmr1_irq_cb((IO_IRQ_ENABLE & IO_IRQ_STATUS & 0x0070) ? ASSERT_LINE : CLEAR_LINE);
}

void spg2xx_io_device::check_tmb_lofreq_key_irq()
{
	/* log */
	m_ffreq_tmr2_irq_cb((IO_IRQ_ENABLE & IO_IRQ_STATUS & 0x008b) ? ASSERT_LINE : CLEAR_LINE);
}

void spg2xx_io_device::check_all_irqs(const uint16_t changed)
{
	if (changed & 0x0c00) // Timer A, Timer B IRQ
		check_timers_irq();

	if (changed & 0x6100) // UART, SPI, SIO, I2C, ADC IRQ
		check_data_irq();

	if (changed & 0x1200) // External IRQ
		check_external_irq();

	if (changed & 0x0070) // 1024Hz, 2048Hz, 4096Hz IRQ
		check_hifreq_periodic_irq();

	if (changed & 0x008b) // TMB1, TMB2, 4Hz, key change IRQ
		check_tmb_lofreq_key_irq();
}

uint16_t spg2xx_io_device::do_special_gpio(uint32_t index, uint16_t mask)
{
	uint16_t data = 0;
	switch (index)
	{
		case 0: // Port A
			if (mask & 0xe000)
			{
				const uint8_t csel = m_cpu->get_csb() & 0x0e;
				data = (csel << 12) & mask;
			}
			break;
		case 1: // Port B
			// To do
			break;
		case 2: // Port C
			// To do
			break;
		default:
			// Can't happen
			break;
	}
	return data;
}

void spg2xx_io_device::do_gpio(uint32_t offset, bool write)
{
	uint32_t index = (offset - 1) / 5;
	uint16_t buffer = m_io_regs[5 * index + 2];
	uint16_t dir = m_io_regs[5 * index + 3];
	uint16_t attr = m_io_regs[5 * index + 4];
	uint16_t special = m_io_regs[5 * index + 5];

	uint16_t push = dir;
	uint16_t pull = ~dir;
	uint16_t what = (buffer & (push | pull));
	what ^= (dir & ~attr);
	what &= ~special;

	switch (index)
	{
		case 0:
			if (write)
				m_porta_out(0, what, push &~ special);
			what = (what & ~pull);
			if (!write)
				what |= m_porta_in(0, pull &~ special) & pull;
			break;
		case 1:
			if (write)
				m_portb_out(0, what, push &~ special);
			what = (what & ~pull);
			if (!write)
				what |= m_portb_in(0, pull &~ special) & pull;
			break;
		case 2:
			if (write)
				m_portc_out(0, what, push &~ special);
			what = (what & ~pull);
			if (!write)
				what |= m_portc_in(0, pull &~ special) & pull;
			break;
	}

	what |= do_special_gpio(index, special);
	m_io_regs[5 * index + 1] = what;
}

template <int Which>
void spg2xx_io_device::adc_convert_tick(int param)
{
	m_io_regs[REG_ADC_DATA] = (m_adc_in(Which) & 0x0fff) | 0x8000;
	m_io_regs[REG_ADC_CTRL] |= 0x2000;
	if (BIT(m_io_regs[REG_ADC_CTRL], 9))
	{
		IO_IRQ_STATUS |= 0x2000;
		if (BIT(m_io_regs[REG_ADC_CTRL], 9) && BIT(IO_IRQ_ENABLE, 13))
		{
			check_data_irq();
		}
	}
}

void spg2xx_io_device::do_i2c()
{
	const uint16_t addr = ((m_io_regs[REG_I2C_ADDR] & 0x06) << 7) | (uint8_t)m_io_regs[REG_I2C_SUBADDR];

	if (m_io_regs[REG_I2C_CMD] & 0x40) // Serial EEPROM read
		m_io_regs[REG_I2C_DATA_IN] = m_i2c_r(addr);
	else
		m_i2c_w(addr, m_io_regs[REG_I2C_DATA_OUT]);

	m_io_regs[REG_I2C_STATUS] |= 1;
}

#define SPG_DEBUG_AUDIO (0)
#define SPG_LOG_ADPCM36 (0)
const uint32_t spg2xx_audio_device::s_rampdown_frame_counts[8] =
{
	13*4, 13*16, 13*64, 13*256, 13*1024, 13*4096, 13*8192, 13*8192
};

const uint32_t spg2xx_audio_device::s_envclk_frame_counts[16] =
{
	4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 8192, 8192, 8192, 8192
};

void spg2xx_audio_device::device_reset()
{
	memset(m_audio_regs, 0, 0x200 * sizeof(uint16_t));
	memset(m_audio_phase_regs, 0, 0x200 * sizeof(uint16_t));
	memset(m_audio_ctrl_regs, 0, 0x400 * sizeof(uint16_t));

	memset(m_sample_shift, 0, 16);
	memset(m_sample_count, 0, sizeof(uint32_t) * 16);
	memset(m_channel_rate, 0, sizeof(double) * 16);
	memset(m_channel_rate_accum, 0, sizeof(double) * 16);
	memset(m_rampdown_frame, 0, sizeof(uint32_t) * 16);
	memset(m_envclk_frame, 4, sizeof(uint32_t) * 16);
	memset(m_envelope_addr, 0, sizeof(uint32_t) * 16);

	m_debug_samples = false;
	m_debug_rates = false;
	m_audio_curr_beat_base_count = 0;

	m_audio_ctrl_regs[AUDIO_CHANNEL_REPEAT] = 0x3f;
	m_audio_ctrl_regs[AUDIO_CHANNEL_ENV_MODE] = 0x3f;

	m_audio_beat->adjust(attotime::from_ticks(4, 281250), 0, attotime::from_ticks(4, 281250));

	for (int i = 0; i < 16; i++)
	{
		m_channel_irq[i]->adjust(attotime::never());
	}
}

void spg2xx_audio_device::irq_tick(int param)
{
	if (!BIT(m_audio_ctrl_regs[AUDIO_CHANNEL_FIQ_STATUS], param))
	{
		m_audio_ctrl_regs[AUDIO_CHANNEL_FIQ_STATUS] |= (1 << param);
		m_ch_irq_cb(1);
	}
}

void spg2xx_audio_device::check_irqs(const uint16_t changed)
{
	if (changed & (AUDIO_BIS_MASK | AUDIO_BIE_MASK)) // Beat IRQ
	{
		if ((m_audio_ctrl_regs[AUDIO_BEAT_COUNT] & (AUDIO_BIS_MASK | AUDIO_BIE_MASK)) == (AUDIO_BIS_MASK | AUDIO_BIE_MASK))
		{
			/* log */
			m_irq_cb(true);
		}
		else
		{
			/* log */
			m_irq_cb(false);
		}
	}
}

uint16_t spg2xx_audio_device::audio_ctrl_r(offs_t offset)
{
	uint16_t data = m_audio_ctrl_regs[offset];

	switch (offset)
	{
	case AUDIO_CHANNEL_ENABLE:
		/* log */
		break;

	case AUDIO_MAIN_VOLUME:
		/* log */
		break;

	case AUDIO_CHANNEL_FIQ_ENABLE:
		/* log */
		break;

	case AUDIO_CHANNEL_FIQ_STATUS:
		/* log */
		break;

	case AUDIO_BEAT_BASE_COUNT:
		/* log */
		break;

	case AUDIO_BEAT_COUNT:
		/* log */
		break;

	case AUDIO_ENVCLK0:
	case AUDIO_ENVCLK1:
		/* log */
		break;

	case AUDIO_ENVCLK0_HIGH:
	case AUDIO_ENVCLK1_HIGH:
		/* log */
		break;

	case AUDIO_ENV_RAMP_DOWN:
		/* log */
		break;

	case AUDIO_CHANNEL_STOP:
		/* log */
		break;

	case AUDIO_CHANNEL_ZERO_CROSS:
		/* log */
		break;

	case AUDIO_CONTROL:
		/* log */
		break;

	case AUDIO_COMPRESS_CTRL:
		/* log */
		break;

	case AUDIO_CHANNEL_STATUS:
		/* log */
		break;

	case AUDIO_WAVE_IN_L:
		/* log */
		break;

	case AUDIO_WAVE_IN_R:
		/* log */
		break;

	case AUDIO_WAVE_OUT_L:
		/* log */
		break;

	case AUDIO_WAVE_OUT_R:
		/* log */
		break;

	case AUDIO_CHANNEL_REPEAT:
		/* log */
		break;

	case AUDIO_CHANNEL_ENV_MODE:
		/* log */
		break;

	case AUDIO_CHANNEL_TONE_RELEASE:
		/* log */
		break;

	case AUDIO_CHANNEL_ENV_IRQ:
		/* log */
		break;

	case AUDIO_CHANNEL_PITCH_BEND:
		/* log */
		break;

	case AUDIO_SOFT_PHASE:
		/* log */
		break;

	case AUDIO_ATTACK_RELEASE:
		/* log */
		break;

	case AUDIO_EQ_CUTOFF10:
		/* log */
		break;

	case AUDIO_EQ_CUTOFF32:
		/* log */
		break;

	case AUDIO_EQ_GAIN10:
		/* log */
		break;

	case AUDIO_EQ_GAIN32:
		/* log */
		break;

	default:
		/* log */
		break;
	}

	return data;
}

uint16_t spg2xx_audio_device::audio_r(offs_t offset)
{
	const uint16_t channel = (offset & 0x01f0) >> 4;
	uint16_t data = m_audio_regs[offset];


	switch (offset & AUDIO_CHAN_OFFSET_MASK)
	{
	case AUDIO_WAVE_ADDR:
		/* log */
		break;

	case AUDIO_MODE:
		/* log */
		break;

	case AUDIO_LOOP_ADDR:
		/* log */
		break;

	case AUDIO_PAN_VOL:
		/* log */
		break;

	case AUDIO_ENVELOPE0:
		/* log */
		break;

	case AUDIO_ENVELOPE_DATA:
		/* log */
		break;

	case AUDIO_ENVELOPE1:
		/* log */
		break;

	case AUDIO_ENVELOPE_ADDR_HIGH:
		/* log */
		break;

	case AUDIO_ENVELOPE_ADDR:
		/* log */
		break;

	case AUDIO_WAVE_DATA_PREV:
		/* log */
		break;

	case AUDIO_ENVELOPE_LOOP_CTRL:
		/* log */
		break;

	case AUDIO_WAVE_DATA:
		/* log */
		break;

	case AUDIO_ADPCM_SEL:
		/* log */
		break;

	default:
		/* log */
		break;

	}

	return data;
}

uint16_t spg2xx_audio_device::audio_phase_r(offs_t offset)
{
	const uint16_t channel = (offset & 0x01f0) >> 4;
	uint16_t data = m_audio_phase_regs[offset];

	switch (offset & AUDIO_CHAN_OFFSET_MASK)
	{
	case AUDIO_PHASE_HIGH:
		/* log */
		break;

	case AUDIO_PHASE_ACCUM_HIGH:
		/* log */
		break;

	case AUDIO_TARGET_PHASE_HIGH:
		/* log */
		break;

	case AUDIO_RAMP_DOWN_CLOCK:
		/* log */
		break;

	case AUDIO_PHASE:
		/* log */
		break;

	case AUDIO_PHASE_ACCUM:
		/* log */
		break;

	case AUDIO_TARGET_PHASE:
		/* log */
		break;

	case AUDIO_PHASE_CTRL:
		/* log */
		break;

	default:
		/* log */
		break;
	}

	return data;
}

void spg2xx_audio_device::audio_ctrl_w(offs_t offset, uint16_t data)
{
	switch (offset)
	{
	case AUDIO_CHANNEL_ENABLE:
	{
		/* log */
		const uint16_t old = m_audio_ctrl_regs[offset];
		m_audio_ctrl_regs[offset] = data;
		const uint16_t changed = old ^ m_audio_ctrl_regs[offset];
		for (uint32_t channel_bit = 0; channel_bit < 16; channel_bit++)
		{
			const uint16_t mask = 1 << channel_bit;
			if (!(changed & mask))
				continue;

			if (data & mask)
			{
				/* log */
				if (m_audio_ctrl_regs[AUDIO_CHANNEL_STOP] & mask)
					continue;

				if (!(m_audio_ctrl_regs[AUDIO_CHANNEL_STATUS] & mask))
				{
					/* log */
					start_channel(channel_bit);
				}
			}
			else
			{
				/* log */
				if (m_audio_ctrl_regs[AUDIO_CHANNEL_STATUS] & mask)
				{
					/* log */
					stop_channel(channel_bit);
				}
			}
		}
		break;
	}

	case AUDIO_MAIN_VOLUME:
		/* log */
		m_audio_ctrl_regs[offset] = data & AUDIO_MAIN_VOLUME_MASK;
		break;

	case AUDIO_CHANNEL_FIQ_ENABLE:
		/* log */
		m_audio_ctrl_regs[offset] = data & AUDIO_CHANNEL_FIQ_ENABLE_MASK;
		break;

	case AUDIO_CHANNEL_FIQ_STATUS:
		/* log */
		//machine().debug_break();
		m_audio_ctrl_regs[offset] &= ~(data & AUDIO_CHANNEL_FIQ_STATUS_MASK);
		if (!m_audio_ctrl_regs[offset])
		{
			m_ch_irq_cb(0);
		}
		break;

	case AUDIO_BEAT_BASE_COUNT:
		/* log */
		m_audio_ctrl_regs[offset] = data & AUDIO_BEAT_BASE_COUNT_MASK;
		m_audio_curr_beat_base_count = m_audio_ctrl_regs[offset];
		break;

	case AUDIO_BEAT_COUNT:
	{
		/* log */
		const uint16_t old = m_audio_ctrl_regs[offset];
		m_audio_ctrl_regs[offset] &= ~(data & AUDIO_BIS_MASK);
		m_audio_ctrl_regs[offset] &= AUDIO_BIS_MASK;
		m_audio_ctrl_regs[offset] |= data & ~AUDIO_BIS_MASK;
		const uint16_t changed = old ^ m_audio_ctrl_regs[offset];
		if (changed & (AUDIO_BIS_MASK | AUDIO_BIE_MASK))
		{
			/* log */
			check_irqs(changed & (AUDIO_BIS_MASK | AUDIO_BIE_MASK));
		}
		break;
	}

	case AUDIO_ENVCLK0:
	case AUDIO_ENVCLK1:
	{
		/* log */
		const uint16_t old = m_audio_ctrl_regs[offset];
		m_audio_ctrl_regs[offset] = data;
		const uint16_t changed = old ^ m_audio_ctrl_regs[offset];

		if (!changed)
			break;

		const uint8_t channel_offset = offset == AUDIO_ENVCLK0 ? 0 : 8;
		for (uint8_t channel_bit = 0; channel_bit < 4; channel_bit++)
		{
			const uint8_t shift = channel_bit << 2;
			const uint16_t mask = 0x0f << shift;
			if (changed & mask)
			{
				m_envclk_frame[channel_bit + channel_offset] = get_envclk_frame_count(channel_bit + channel_offset);
			}
		}
		break;
	}

	case AUDIO_ENVCLK0_HIGH:
	case AUDIO_ENVCLK1_HIGH:
	{
		/* log */
		const uint16_t old = m_audio_ctrl_regs[offset];
		m_audio_ctrl_regs[offset] = data;
		const uint16_t changed = old ^ m_audio_ctrl_regs[offset];
		if (!changed)
			break;

		const uint8_t channel_offset = offset == AUDIO_ENVCLK0_HIGH ? 0 : 8;
		for (uint8_t channel_bit = 0; channel_bit < 4; channel_bit++)
		{
			const uint8_t shift = channel_bit << 2;
			const uint16_t mask = 0x0f << shift;
			if (changed & mask)
			{
				m_envclk_frame[channel_bit + channel_offset + 4] = get_envclk_frame_count(channel_bit + channel_offset);
			}
		}
		break;
	}

	case AUDIO_ENV_RAMP_DOWN:
	{
		/* log */
		const uint16_t old = m_audio_ctrl_regs[offset];
		m_audio_ctrl_regs[offset] = (data & AUDIO_ENV_RAMP_DOWN_MASK) & m_audio_ctrl_regs[AUDIO_CHANNEL_STATUS];
		const uint16_t changed = old ^ m_audio_ctrl_regs[offset];
		if (!changed)
			break;

		for (uint32_t channel_bit = 0; channel_bit < 16; channel_bit++)
		{
			const uint16_t mask = 1 << channel_bit;
			if ((changed & mask) && (data & mask))
			{
				m_rampdown_frame[channel_bit] = get_rampdown_frame_count(channel_bit);
				/* log */
			}
		}
		break;
	}

	case AUDIO_CHANNEL_STOP:
	{
		/* log */
		const uint16_t old = m_audio_ctrl_regs[offset];
		m_audio_ctrl_regs[offset] &= ~data;
		const uint16_t changed = old ^ m_audio_ctrl_regs[offset];
		for (uint32_t channel_bit = 0; channel_bit < 16; channel_bit++)
		{
			const uint16_t mask = 1 << channel_bit;
			if (!(changed & mask))
				continue;

			/* log */
			if (!(m_audio_ctrl_regs[AUDIO_CHANNEL_ENABLE] & mask))
				continue;

			if (!(m_audio_ctrl_regs[AUDIO_CHANNEL_STATUS] & mask))
			{
				/* log */
				start_channel(channel_bit);
			}
		}
		break;
	}

	case AUDIO_CHANNEL_ZERO_CROSS:
		/* log */
		m_audio_ctrl_regs[offset] = data & AUDIO_CHANNEL_ZERO_CROSS_MASK;
		break;

	case AUDIO_CONTROL:
		/* log */
		m_audio_ctrl_regs[offset] = data & AUDIO_CONTROL_MASK;
		break;

	case AUDIO_COMPRESS_CTRL:
		/* log */
		m_audio_ctrl_regs[offset] = data;
		break;

	case AUDIO_CHANNEL_STATUS:
		/* log */
		break;

	case AUDIO_WAVE_IN_L:
		/* log */
		stream_update();
		m_audio_ctrl_regs[offset] = data;
		break;

	case AUDIO_WAVE_IN_R:
		/* log */
		stream_update();
		m_audio_ctrl_regs[offset] = data;
		break;

	case AUDIO_WAVE_OUT_L:
		/* log */
		m_audio_ctrl_regs[offset] = data;
		break;

	case AUDIO_WAVE_OUT_R:
		/* log */
		m_audio_ctrl_regs[offset] = data;
		break;

	case AUDIO_CHANNEL_REPEAT:
		/* log */
		m_audio_ctrl_regs[offset] = data & AUDIO_CHANNEL_REPEAT_MASK;
		break;

	case AUDIO_CHANNEL_ENV_MODE:
		/* log */
		m_audio_ctrl_regs[offset] = data & AUDIO_CHANNEL_ENV_MODE_MASK;
		break;

	case AUDIO_CHANNEL_TONE_RELEASE:
		/* log */
		m_audio_ctrl_regs[offset] = data & AUDIO_CHANNEL_TONE_RELEASE_MASK;
		break;

	case AUDIO_CHANNEL_ENV_IRQ:
		/* log */
		m_audio_ctrl_regs[offset] &= ~data & AUDIO_CHANNEL_ENV_IRQ_MASK;
		break;

	case AUDIO_CHANNEL_PITCH_BEND:
		/* log */
		m_audio_ctrl_regs[offset] = data & AUDIO_CHANNEL_PITCH_BEND_MASK;
		break;

	case AUDIO_SOFT_PHASE:
		/* log */
		m_audio_ctrl_regs[offset] = data;
		break;

	case AUDIO_ATTACK_RELEASE:
		/* log */
		m_audio_ctrl_regs[offset] = data;
		break;

	case AUDIO_EQ_CUTOFF10:
		/* log */
		m_audio_ctrl_regs[offset] = data & AUDIO_EQ_CUTOFF10_MASK;
		break;

	case AUDIO_EQ_CUTOFF32:
		/* log */
		m_audio_ctrl_regs[offset] = data & AUDIO_EQ_CUTOFF32_MASK;
		break;

	case AUDIO_EQ_GAIN10:
		/* log */
		m_audio_ctrl_regs[offset] = data & AUDIO_EQ_GAIN10_MASK;
		break;

	case AUDIO_EQ_GAIN32:
		/* log */
		m_audio_ctrl_regs[offset] = data & AUDIO_EQ_GAIN32_MASK;
		break;

	default:
		m_audio_ctrl_regs[offset] = data;
		/* log */
		break;
	}
}

void spg2xx_audio_device::audio_phase_w(offs_t offset, uint16_t data)
{
	const uint16_t channel = (offset & 0x01f0) >> 4;

	switch (offset & AUDIO_CHAN_OFFSET_MASK)
	{
	case AUDIO_PHASE_HIGH:
		m_audio_phase_regs[offset] = data & AUDIO_PHASE_HIGH_MASK;
		m_channel_rate[channel] = ((double)get_phase(channel) * 140625.0 * 2.0) / (double)(1 << 19);
		m_channel_rate_accum[channel] = 0.0;
		/* log */
		break;

	case AUDIO_PHASE_ACCUM_HIGH:
		m_audio_phase_regs[offset] = data & AUDIO_PHASE_ACCUM_HIGH_MASK;
		/* log */
		break;

	case AUDIO_TARGET_PHASE_HIGH:
		m_audio_phase_regs[offset] = data & AUDIO_TARGET_PHASE_HIGH_MASK;
		/* log */
		break;

	case AUDIO_RAMP_DOWN_CLOCK:
		m_audio_phase_regs[offset] = data & AUDIO_RAMP_DOWN_CLOCK_MASK;
		/* log */
		break;

	case AUDIO_PHASE:
		m_audio_phase_regs[offset] = data;
		m_channel_rate[channel] = ((double)get_phase(channel) * 140625.0 * 2.0) / (double)(1 << 19);
		m_channel_rate_accum[channel] = 0.0;
		/* log */
		break;

	case AUDIO_PHASE_ACCUM:
		m_audio_phase_regs[offset] = data;
		/* log */
		break;

	case AUDIO_TARGET_PHASE:
		m_audio_phase_regs[offset] = data;
		/* log */
		break;

	case AUDIO_PHASE_CTRL:
		m_audio_phase_regs[offset] = data;
		/* log */
		break;

	default:
		m_audio_phase_regs[offset] = data;
		/* log */
		break;
	}
}

void spg2xx_audio_device::audio_w(offs_t offset, uint16_t data)
{
	const uint16_t channel = (offset & 0x01f0) >> 4;

	switch (offset & AUDIO_CHAN_OFFSET_MASK)
	{
	case AUDIO_WAVE_ADDR:
		m_audio_regs[offset] = data;
		/* log */
		break;

	case AUDIO_MODE:
		m_audio_regs[offset] = data;
		/* log */
		break;

	case AUDIO_LOOP_ADDR:
		m_audio_regs[offset] = data;
		/* log */
		break;

	case AUDIO_PAN_VOL:
		m_audio_regs[offset] = data & AUDIO_PAN_VOL_MASK;
		/* log */
		break;

	case AUDIO_ENVELOPE0:
		m_audio_regs[offset] = data;
		/* log */
		break;

	case AUDIO_ENVELOPE_DATA:
		m_audio_regs[offset] = data & AUDIO_ENVELOPE_DATA_MASK;
		/* log */
		break;

	case AUDIO_ENVELOPE1:
		m_audio_regs[offset] = data;
		/* log */
		break;

	case AUDIO_ENVELOPE_ADDR_HIGH:
		m_audio_regs[offset] = data;
		/* log */
		break;

	case AUDIO_ENVELOPE_ADDR:
		m_audio_regs[offset] = data;
		/* log */
		break;

	case AUDIO_WAVE_DATA_PREV:
		m_audio_regs[offset] = data;
		/* log */
		break;

	case AUDIO_ENVELOPE_LOOP_CTRL:
		m_audio_regs[offset] = data;
		/* log */
		break;

	case AUDIO_WAVE_DATA:
		m_audio_regs[offset] = data;
		/* log */
		break;

	case AUDIO_ADPCM_SEL:
		m_audio_regs[offset] = data & AUDIO_ADPCM_SEL_MASK;
		/* log */
		break;

	default:
		m_audio_regs[offset] = data;
		/* log */
		break;

	}
}

void spg2xx_audio_device::sound_stream_update(int16_t* left, int16_t* right, int samples)
{
	for (int i = 0; i < samples; i++)
	{
		int32_t left_total = 0;
		int32_t right_total = 0;

		for (uint32_t channel = 0; channel < 16; channel++)
		{
			if (!get_channel_status(channel))
			{
				continue;
			}

			if (SPG_DEBUG_AUDIO && m_debug_rates)
				printf("%f:%f ", m_channel_rate[channel], m_channel_rate_accum[channel]);
			bool playing = advance_channel(channel);
			if (playing)
			{
				int32_t sample = (int16_t)(m_audio_regs[(channel << 4) | AUDIO_WAVE_DATA] ^ 0x8000);
				if (!(m_audio_ctrl_regs[AUDIO_CONTROL] & AUDIO_CONTROL_NOINT_MASK))
				{
					int32_t prev_sample = (int16_t)(m_audio_regs[(channel << 4) | AUDIO_WAVE_DATA_PREV] ^ 0x8000);
					int16_t lerp_factor = (int16_t)((m_channel_rate_accum[channel] / 70312.5) * 256.0);
					prev_sample = (prev_sample * (0x100 - lerp_factor)) >> 8;
					sample = (sample * lerp_factor) >> 8;
					sample += prev_sample;
				}

				sample = (sample * (int32_t)get_edd(channel)) >> 7;

				int32_t vol = get_volume(channel);
				int32_t pan = get_pan(channel);

				int32_t pan_left, pan_right;
				if (pan < 0x40)
				{
					pan_left = 0x7f * vol;
					pan_right = pan * 2 * vol;
				}
				else
				{
					pan_left = (0x7f - pan) * 2 * vol;
					pan_right = 0x7f * vol;
				}

				left_total += ((int16_t)sample * (int16_t)pan_left) >> 14;
				right_total += ((int16_t)sample * (int16_t)pan_right) >> 14;

				const uint16_t mask = (1 << channel);
				if (m_audio_ctrl_regs[AUDIO_ENV_RAMP_DOWN] & mask)
				{
					if (m_rampdown_frame[channel] > 0)
					{
						m_rampdown_frame[channel]--;
					}

					if (m_rampdown_frame[channel] == 0)
					{
						/* log */
						audio_rampdown_tick(channel);
					}
				}
				else if (!(m_audio_ctrl_regs[AUDIO_CHANNEL_ENV_MODE] & mask))
				{
					if (m_envclk_frame[channel] > 0)
					{
						m_envclk_frame[channel]--;
					}

					if (m_envclk_frame[channel] == 0)
					{
						/* log */
						audio_envelope_tick(channel);
						m_envclk_frame[channel] = get_envclk_frame_count(channel);
					}
				}
			}
		}

		if (m_audio_ctrl_regs[AUDIO_WAVE_IN_L])
			left_total += (int32_t)(m_audio_ctrl_regs[AUDIO_WAVE_IN_L] - 0x8000);
		if (m_audio_ctrl_regs[AUDIO_WAVE_IN_R])
			right_total += (int32_t)(m_audio_ctrl_regs[AUDIO_WAVE_IN_R] - 0x8000);

		switch (get_vol_sel())
		{
			case 0: // 1/16
				left_total >>= 4;
				right_total >>= 4;
				break;
			case 1: // 1/4
			case 2: // 1
			case 3: // 2 // Both x1 and x2 clip like mad even with only 6 voices. Hack it for now.
				left_total >>= 2;
				right_total >>= 2;
				break;
		}

		int32_t left_final = (int16_t)((left_total * (int16_t)m_audio_ctrl_regs[AUDIO_MAIN_VOLUME]) >> 7);
		int32_t right_final = (int16_t)((right_total * (int16_t)m_audio_ctrl_regs[AUDIO_MAIN_VOLUME]) >> 7);

		left[i] = int16_t(left_final);
		right[i] = int16_t(right_final);
	}
}

void spg2xx_audio_device::start_channel(const uint32_t channel)
{
	if (BIT(m_audio_ctrl_regs[AUDIO_CHANNEL_FIQ_ENABLE], channel))
	{
		m_channel_irq[channel]->adjust(attotime::from_hz(m_channel_rate[channel]), channel, attotime::from_hz(m_channel_rate[channel]));
	}
	else
	{
		m_channel_irq[channel]->adjust(attotime::never());
	}

	m_audio_ctrl_regs[AUDIO_CHANNEL_STATUS] |= (1 << channel);
	m_envelope_addr[channel] = get_envelope_addr(channel);
	set_envelope_count(channel, get_envelope_load(channel));

	m_adpcm[channel].reset();
	m_sample_shift[channel] = 0;
	m_sample_count[channel] = 0;

	if (get_adpcm36_bit(channel))
	{
		memset(m_adpcm36_state + channel, 0, sizeof(adpcm36_state));
	}
}

void spg2xx_audio_device::stop_channel(const uint32_t channel)
{
	m_audio_ctrl_regs[AUDIO_CHANNEL_STATUS] &= ~(1 << channel);
	m_audio_regs[(channel << 4) | AUDIO_MODE] &= ~AUDIO_ADPCM_MASK;
	m_audio_ctrl_regs[AUDIO_CHANNEL_TONE_RELEASE] &= ~(1 << channel);
	m_audio_ctrl_regs[AUDIO_ENV_RAMP_DOWN] &= ~(1 << channel);
	m_channel_irq[channel]->adjust(attotime::never());
#if SPG_LOG_ADPCM36
	if (get_adpcm36_bit(channel))
	{
		fclose(adpcm_file[channel]);
		adpcm_file[channel] = nullptr;
	}
#endif
}

bool spg2xx_audio_device::advance_channel(const uint32_t channel)
{
	m_channel_rate_accum[channel] += m_channel_rate[channel];
	uint32_t samples_to_advance = 0;
	while (m_channel_rate_accum[channel] >= 70312.5)
	{
		m_channel_rate_accum[channel] -= 70312.5;
		samples_to_advance++;
	}

	if (!samples_to_advance)
		return true;

	bool playing = true;

	for (uint32_t sample = 0; sample < samples_to_advance; sample++)
	{
		playing = fetch_sample(channel);
		if (!playing)
			break;

		if (get_adpcm_bit(channel) || get_adpcm36_bit(channel))
		{
			// ADPCM mode
			m_sample_shift[channel] += 4;
			if (m_sample_shift[channel] >= 16)
			{
				m_sample_shift[channel] = 0;
				inc_wave_addr(channel);
				if (get_adpcm36_bit(channel))
				{
					m_adpcm36_state[channel].m_remaining--;
				}
			}
		}
		else if (get_16bit_bit(channel))
		{
			// 16-bit mode
			inc_wave_addr(channel);
		}
		else
		{
			// 8-bit mode
			m_sample_shift[channel] += 8;
			if (m_sample_shift[channel] >= 16)
			{
				m_sample_shift[channel] = 0;
				inc_wave_addr(channel);
			}
		}
	}

	return playing;
}

uint16_t spg2xx_audio_device::read_space(offs_t offset)
{
	return m_soc->audio_read_word(offset);
}

uint16_t spg2xx_audio_device::decode_adpcm36_nybble(const uint32_t channel, const uint8_t data)
{
	/*static const int8_t s_filter_coef[16][2] =
	{
	    { 0, 0 },
	    { 60, 0 },
	    { 115,-52 },
	    { 98,-55 },
	    { 122,-60 },
	    { 122,-60 },
	    { 122,-60 },
	    { 122,-60 },
	    { 0, 0 },
	    { 60, 0 },
	    { 115,-52 },
	    { 98,-55 },
	    { 122,-60 },
	    { 122,-60 },
	    { 122,-60 },
	    { 122,-60 },
	};*/

	adpcm36_state &state = m_adpcm36_state[channel];
	int32_t shift = state.m_header & 0xf;
	int16_t filter = (state.m_header & 0x3f0) >> 4;
	int16_t f0 = filter | ((filter & 0x20) ? ~0x3f : 0); // sign extend
	int32_t f1 = 0;
	int16_t sdata = data << 12;
	sdata = (sdata >> shift) + (((state.m_prevsamp[0] * f0) + (state.m_prevsamp[1] * f1) + 32) >> 12);
	state.m_prevsamp[1] = state.m_prevsamp[0];
	state.m_prevsamp[0] = sdata;
	return (uint16_t)sdata ^ 0x8000;
}

bool spg2xx_audio_device::fetch_sample(const uint32_t channel)
{
	const uint32_t channel_mask = channel << 4;
	m_audio_regs[channel_mask | AUDIO_WAVE_DATA_PREV] = m_audio_regs[channel_mask | AUDIO_WAVE_DATA];

	const uint32_t wave_data_reg = channel_mask | AUDIO_WAVE_DATA;
	const uint16_t tone_mode = get_tone_mode(channel);

	if (get_adpcm36_bit(channel) && tone_mode != 0 && m_adpcm36_state[channel].m_remaining == 0)
	{
		m_adpcm36_state[channel].m_header = read_space(get_wave_addr(channel));
		m_adpcm36_state[channel].m_remaining = 8;
		inc_wave_addr(channel);
	}

	uint16_t raw_sample = tone_mode ? read_space(get_wave_addr(channel)) : m_audio_regs[wave_data_reg];

#if SPG_LOG_ADPCM36
	if (get_adpcm36_bit(channel))
	{
		static int adpcm_file_counts[16] = {};

		if (adpcm_file[channel] == nullptr)
		{
			char file_buf[256];
			snprintf(file_buf, 256, "adpcm36_chan%d_%d.bin", channel, adpcm_file_counts[channel]);
			adpcm_file[channel] = fopen(file_buf, "wb");
		}
		static int blah[16] = {};
		if ((blah[channel] & 3) == 0)
		{
			/* log */
			fwrite(&raw_sample, sizeof(uint16_t), 1, adpcm_file[channel]);
		}
		blah[channel]++;
		blah[channel] &= 3;
	}
#endif

	if (get_adpcm_bit(channel) || get_adpcm36_bit(channel))
	{
		// ADPCM mode
		if (tone_mode != 0 && raw_sample == 0xffff)
		{
			if (tone_mode == AUDIO_TONE_MODE_HW_ONESHOT)
			{
				/* log */
				m_sample_count[channel] = 0;
				m_audio_ctrl_regs[AUDIO_CHANNEL_STOP] |= (1 << channel);
				stop_channel(channel);
				return false;
			}
			else
			{
				/* log */
				m_sample_count[channel] = 0;
				loop_channel(channel);
				m_audio_regs[(channel << 4) | AUDIO_MODE] &= ~AUDIO_ADPCM_MASK;
			}
		}
		else
		{
			m_audio_regs[wave_data_reg] = raw_sample;
			m_audio_regs[wave_data_reg] >>= m_sample_shift[channel];
			const uint8_t adpcm_sample = (uint8_t)(m_audio_regs[wave_data_reg] & 0x000f);
			if (get_adpcm36_bit(channel))
				m_audio_regs[wave_data_reg] = decode_adpcm36_nybble(channel, adpcm_sample);
			else
				m_audio_regs[wave_data_reg] = (uint16_t)(m_adpcm[channel].clock(adpcm_sample)) ^ 0x8000;
		}
		m_sample_count[channel]++;
	}
	else if (get_16bit_bit(channel))
	{
		// 16-bit mode
		if (tone_mode != 0 && raw_sample == 0xffff)
		{
			if (tone_mode == AUDIO_TONE_MODE_HW_ONESHOT)
			{
				/* log */
				m_sample_count[channel] = 0;
				stop_channel(channel);
				return false;
			}
			else
			{
				/* log */
				m_sample_count[channel] = 0;
				loop_channel(channel);
			}
		}
		else
		{
			m_audio_regs[wave_data_reg] = raw_sample;
		}
		m_sample_count[channel]++;
	}
	else
	{
		// 8-bit mode
		/* log */
		if (tone_mode != 0)
		{
			if (m_sample_shift[channel])
				raw_sample &= 0xff00;
			else
				raw_sample <<= 8;
			raw_sample |= raw_sample >> 8;

			if (raw_sample == 0xffff)
			{
				if (tone_mode == AUDIO_TONE_MODE_HW_ONESHOT)
				{
					/* log */
					m_sample_count[channel] = 0;
					m_audio_ctrl_regs[AUDIO_CHANNEL_STOP] |= (1 << channel);
					stop_channel(channel);
					return false;
				}
				else
				{
					/* log */
					m_sample_count[channel] = 0;
					loop_channel(channel);
				}
			}
			else
			{
				m_audio_regs[wave_data_reg] = raw_sample;
			}
		}
		m_sample_count[channel]++;
	}

	return true;
}

void spg2xx_audio_device::loop_channel(const uint32_t channel)
{
	set_wave_addr(channel, get_loop_addr(channel));
	m_sample_shift[channel] = 0;
	/* log */
}

void spg2xx_audio_device::audio_beat_tick(int param)
{
	if (m_audio_curr_beat_base_count > 0)
	{
		m_audio_curr_beat_base_count--;
	}

	if (m_audio_curr_beat_base_count == 0)
	{
		/* log */
		m_audio_curr_beat_base_count = m_audio_ctrl_regs[AUDIO_BEAT_BASE_COUNT];

		uint16_t beat_count = m_audio_ctrl_regs[AUDIO_BEAT_COUNT] & AUDIO_BEAT_COUNT_MASK;

		if (beat_count > 0)
		{
			beat_count--;
			m_audio_ctrl_regs[AUDIO_BEAT_COUNT] = (m_audio_ctrl_regs[AUDIO_BEAT_COUNT] & ~AUDIO_BEAT_COUNT_MASK) | beat_count;
		}

		if (beat_count == 0)
		{
			if (m_audio_ctrl_regs[AUDIO_BEAT_COUNT] & AUDIO_BIE_MASK)
			{
				/* log */
				m_audio_ctrl_regs[AUDIO_BEAT_COUNT] |= AUDIO_BIS_MASK;
				check_irqs(AUDIO_BIS_MASK);
			}
			else
			{
				/* log */
			}
		}
	}
}

void spg2xx_audio_device::audio_rampdown_tick(const uint32_t channel)
{
	const uint8_t old_edd = get_edd(channel);
	uint8_t new_edd = old_edd - get_rampdown_offset(channel);
	if (new_edd > old_edd)
		new_edd = 0;

	if (new_edd)
	{
		/* log */
		const uint16_t channel_mask = channel << 4;
		m_audio_regs[channel_mask | AUDIO_ENVELOPE_DATA] &= ~AUDIO_EDD_MASK;
		m_audio_regs[channel_mask | AUDIO_ENVELOPE_DATA] |= new_edd & AUDIO_EDD_MASK;
		m_rampdown_frame[channel] = get_rampdown_frame_count(channel);
	}
	else
	{
		/* log */
		const uint16_t channel_mask = 1 << channel;
		m_audio_ctrl_regs[AUDIO_CHANNEL_STOP] |= channel_mask;
		stop_channel(channel);
	}
}

uint32_t spg2xx_audio_device::get_rampdown_frame_count(const uint32_t channel)
{
	return s_rampdown_frame_counts[get_rampdown_clock(channel)];
}

uint32_t spg2xx_audio_device::get_envclk_frame_count(const uint32_t channel)
{
	return s_envclk_frame_counts[get_envelope_clock(channel)];
}

uint32_t spg2xx_audio_device::get_envelope_clock(const offs_t channel) const
{
	if (channel < 4)
		return (m_audio_ctrl_regs[AUDIO_ENVCLK0] >> (channel << 2)) & 0x000f;
	else if (channel < 8)
		return (m_audio_ctrl_regs[AUDIO_ENVCLK0_HIGH] >> ((channel - 4) << 2)) & 0x000f;
	else if (channel < 12)
		return (m_audio_ctrl_regs[AUDIO_ENVCLK1] >> ((channel - 8) << 2)) & 0x000f;
	else
		return (m_audio_ctrl_regs[AUDIO_ENVCLK1_HIGH] >> ((channel - 12) << 2)) & 0x000f;
}

bool spg2xx_audio_device::audio_envelope_tick(const uint32_t channel)
{
	const uint16_t channel_mask = channel << 4;
	uint16_t new_count = get_envelope_count(channel);
	const uint16_t curr_edd = get_edd(channel);
	/* log */
	bool edd_changed = false;
	if (new_count > 0)
	{
		new_count--;
		set_envelope_count(channel, new_count);
	}

	if (new_count == 0)
	{
		const uint16_t target = get_envelope_target(channel);
		uint16_t new_edd = curr_edd;
		const uint16_t inc = get_envelope_inc(channel);

		if (new_edd != target)
		{
			if (get_envelope_sign_bit(channel))
			{
				new_edd -= inc;
				/* log */
				if (new_edd > curr_edd)
					new_edd = 0;
				else if (new_edd < target)
					new_edd = target;

				if (new_edd == 0)
				{
					/* log */
					m_audio_ctrl_regs[AUDIO_CHANNEL_STOP] |= (1 << channel);
					stop_channel(channel);
					return true;
				}
			}
			else
			{
				new_edd += inc;
				/* log */
				if (new_edd >= target)
					new_edd = target;
			}
		}

		if (new_edd == target)
		{
			/* log */
			new_edd = target;

			if (get_envelope_repeat_bit(channel))
			{
				const uint16_t repeat_count = get_envelope_repeat_count(channel) - 1;
				/* log */
				if (repeat_count == 0)
				{
					m_audio_regs[channel_mask | AUDIO_ENVELOPE0] = read_space(m_envelope_addr[channel]);
					m_audio_regs[channel_mask | AUDIO_ENVELOPE1] = read_space(m_envelope_addr[channel] + 1);
					m_audio_regs[channel_mask | AUDIO_ENVELOPE_LOOP_CTRL] = read_space(m_envelope_addr[channel] + 2);
					m_envelope_addr[channel] = get_envelope_addr(channel) + get_envelope_eaoffset(channel);
					/* log */
				}
				else
				{
					set_envelope_repeat_count(channel, repeat_count);
				}
			}
			else
			{
				/* log */
				m_audio_regs[channel_mask | AUDIO_ENVELOPE0] = read_space(m_envelope_addr[channel]);
				m_audio_regs[channel_mask | AUDIO_ENVELOPE1] = read_space(m_envelope_addr[channel] + 1);
				/* log */
				m_envelope_addr[channel] += 2;
			}
			new_count = get_envelope_load(channel);
			set_envelope_count(channel, new_count);
		}
		else
		{
			/* log */
			new_count = get_envelope_load(channel);
			set_envelope_count(channel, new_count);
		}
		/* log */

		set_edd(channel, new_edd);
		edd_changed = true;
		/* log */
	}
	/* log */
	return edd_changed;
}

// explicit instantiations used by spg2xx.cpp
template void spg2xx_io_device::tmb_timer_tick<0>(int);
template void spg2xx_io_device::tmb_timer_tick<1>(int);
template void spg2xx_io_device::adc_convert_tick<0>(int);
template void spg2xx_io_device::adc_convert_tick<1>(int);
template void spg2xx_io_device::adc_convert_tick<2>(int);
template void spg2xx_io_device::adc_convert_tick<3>(int);
