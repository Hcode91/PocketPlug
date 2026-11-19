/* PocketPlug - Acorn A7000 / A7000+: between rpcemu_glue.c and AcornCore
 * SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void acorn_glue_set_datadir(const char *dir);
void acorn_glue_timers(void);
void acorn_glue_advance(uint64_t ns);
void acorn_glue_sync(void);
void acorn_glue_set_mhz(int mhz);
void acorn_glue_set_frame_end(uint64_t ns);
uint64_t rpcemu_nsec_timer_ticks(void);
extern int g_acorn_idle_break;
/* implemented by the core (acorn.cpp) */
void acorn_core_video(const uint32_t *buffer, int xsize, int ysize, int yl, int yh,
                      int double_size, int host_xsize, int host_ysize);
void acorn_core_sound(uint32_t samplerate, const int16_t *stereo, uint32_t frames);
void acorn_core_fatal(const char *msg);
#ifdef __cplusplus
}
#endif
