/* PocketPlug - Acorn Archimedes (Arculator) glue
   SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* from the glue into the core */
void archi_core_video(const uint32_t *pixels, int stride, int w, int h, int dblscan);
void archi_core_sound(const int16_t *stereo, int frames);
void archi_core_fatal(const char *msg);

/* from the core into the glue */
void archi_glue_key(int scancode, int down);
void archi_glue_mouse(int dx, int dy, int buttons);
void archi_glue_reload_config(void);

/* Arculator */
int arc_init(void);
void arc_reset(void);
void arc_run(void);
void arc_close(void);
extern char exname[512];
extern char machine_config_file[256];
extern char machine_config_name[256];

#ifdef __cplusplus
}
#endif
