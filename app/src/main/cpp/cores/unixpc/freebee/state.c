#define _STATE_C
#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include "wd279x.h"
#include "wd2010.h"
#include "keyboard.h"
#include "state.h"
#include "i8274.h"
#include "fbconfig.h"

static const uint8_t *rom14c_data, *rom15c_data;
static size_t rom14c_len, rom15c_len;

void state_set_roms(const uint8_t *r14c, size_t len14c, const uint8_t *r15c, size_t len15c)
{
	rom14c_data = r14c; rom14c_len = len14c;
	rom15c_data = r15c; rom15c_len = len15c;
}

int state_init(size_t base_ram_size, size_t exp_ram_size)
{
	// Free RAM if it's allocated
	if (state.base_ram != NULL)
		free(state.base_ram);
	if (state.exp_ram != NULL)
		free(state.exp_ram);

	// Initialise hardware registers
	state.romlmap = false;
	state.idmarw = state.dmaen = false;
	state.dma_count = state.dma_address = 0;
	state.pie = 0;
	state.ee = 0;
	state.leds = 0;
	state.genstat = 0;				// FIXME: check this
	state.bsr0 = state.bsr1 = 0;	// FIXME: check this
	// MCR reads back as zero after reset, so CLRSINT- starts asserted and
	// the 60Hz interrupt is held clear until the guest releases it.
	state.timer_int_latch = state.timer_clrsint = false;
	state.nmi_latch = false;
	state.dma_dev = DMA_DEV_UNDEF;
	state.mcr2mirror = 0;
	state.reverse_video = false;

	// Enable VIDPAL mod (allows user writing to VRAM), per config setting
	state.vidpal = fbc_get_bool("vidpal", "installed");

	// Allocate Base RAM, making sure the user has specified a valid RAM amount first
	// Basically: 512KiB minimum, 2MiB maximum, in increments of 512KiB.
	if ((base_ram_size < 512*1024) || (base_ram_size > 2048*1024) || ((base_ram_size % (512*1024)) != 0))
		return -1;
	state.base_ram = malloc(base_ram_size);
	if (state.base_ram == NULL)
		return -2;
	state.base_ram_size = base_ram_size;

	// Now allocate expansion RAM
	// The difference here is that we can have zero bytes of Expansion RAM; we're not limited to having a minimum of 512KiB.
	if ((exp_ram_size > 2048*1024) || ((exp_ram_size % (512*1024)) != 0))
		return -1;
	state.exp_ram = malloc(exp_ram_size);
	if (state.exp_ram == NULL)
		return -2;
	state.exp_ram_size = exp_ram_size;

	// Load ROMs (PocketPlug: the app hands them over in memory)
	if (rom14c_data == NULL || rom15c_data == NULL || rom14c_len != rom15c_len || rom14c_len * 2 > ROM_SIZE)
		return -3;
	size_t romlen = rom14c_len, romlen2 = rom15c_len;
	const uint8_t *romdat1 = rom15c_data, *romdat2 = rom14c_data;

	// convert the ROM data
	for (size_t i=0; i<(romlen + romlen2); i+=2) {
		state.rom[i+0] = romdat1[i/2];
		state.rom[i+1] = romdat2[i/2];
	}

	// If the PROMs are smaller than the space they decode into, the image
	// repeats. Per the TRM: "Boot PROMs can be 2x 2764 or 2x 27128. Total size
	// = 16KiB or 32KiB respectively. If 16K ROM installed, data is repeated to
	// make up 32K." A pair of 2764s is the common case -- the ROM set on
	// Bitsavers is 8KiB per device -- so without this the top half of the ROM
	// window reads back as zeroes instead of mirroring the bottom half.
	//
	// This is address decoding, not arithmetic: the PROMs are wired to only as
	// many address lines as they have, and the ones above that simply aren't
	// connected. So the image always repeats on a power-of-two boundary and the
	// address is masked, exactly as the read path masks with ROM_SIZE-1.
	size_t imglen = romlen + romlen2;
	if ((imglen & (imglen - 1)) != 0) {
		fprintf(stderr, "[state] Combined ROM size %zu is not a power of two.\n", imglen);
		return -3;
	}
	for (size_t i = imglen; i < ROM_SIZE; i++) {
		state.rom[i] = state.rom[i & (imglen - 1)];
	}


	// Initialise the disc controller
	wd2797_init(&state.fdc_ctx);
	// Initialise the keyboard controller
	keyboard_init(&state.kbd);
	// Initialise the serial controller
	i8274_init(&state.serial_ctx);

	return 0;
}

void state_done()
{
	if (state.base_ram != NULL) {
		free(state.base_ram);
		state.base_ram = NULL;
	}

	if (state.exp_ram != NULL) {
		free(state.exp_ram);
		state.exp_ram = NULL;
	}

	// Deinitialise the disc controller
	wd2797_done(&state.fdc_ctx);
	wd2010_done(&state.hdc_ctx);
	// Deinitialise the serial controller
	i8274_done(&state.serial_ctx);
}


