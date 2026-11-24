/* PocketPlug - B-em glue (see bem_glue.c)
 * SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#define BEM_W 704
#define BEM_H 552
int bem_init(const char* datadir, int model);
void bem_run_frame(void);
const uint32_t* bem_frame(void);
int bem_frame_ready(void);
int bem_read_audio(int16_t* out, int max);
int bem_load_disc(int drive, const char* path);
void bem_eject_disc(int drive);
void bem_load_tape(const char* path);
void bem_key(int bbckey, int down);
void bem_break(void);
int bem_disc_led(void);
int bem_make_font(const uint8_t* saa, size_t n, const char* fn);
#ifdef __cplusplus
}
#endif

