/* PocketPlug - Acorn Archimedes (Arculator)
   SPDX-License-Identifier: GPL-2.0-or-later

   The host side Arculator expects from its SDL / wxWidgets front end:
   logging and errors, video presentation, sound buffers, keyboard and mouse
   state, and stubs for the configuration dialogs that aren't used here. */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arculator/arc.h"
#include "arculator/config.h"
#include "arculator/plat_input.h"
#include "arculator/plat_joystick.h"
#include "arculator/plat_video.h"
#include "arculator/podules.h"
#include "arculator/vidc.h"
#include "arc_glue.h"



int selected_video_renderer = 0;
joystick_t joystick_state[MAX_JOYSTICKS];
plat_joystick_t plat_joystick_state[MAX_PLAT_JOYSTICKS];
int joysticks_present = 0;
int key[512];

/* ---- logging and errors ---- */

void rpclog(const char *format, ...)
{
	static int on = -1;
	if (on < 0) on = getenv("ARCLOG") != NULL;
	if (!on) return;
	va_list ap;
	va_start(ap, format);
	vfprintf(stderr, format, ap);
	va_end(ap);
}

void fatal(const char *format, ...)
{
	char buf[512];
	va_list ap;
	va_start(ap, format);
	vsnprintf(buf, sizeof(buf), format, ap);
	va_end(ap);
	archi_core_fatal(buf);
}

void error(const char *format, ...)
{
	char buf[512];
	va_list ap;
	va_start(ap, format);
	vsnprintf(buf, sizeof(buf), format, ap);
	va_end(ap);
	archi_core_fatal(buf);       /* Arculator stops after error() too */
}

void arc_print_error(const char *format, ...)
{
	char buf[512];
	va_list ap;
	va_start(ap, format);
	vsnprintf(buf, sizeof(buf), format, ap);
	va_end(ap);
	fprintf(stderr, "Arculator: %s\n", buf);
}

/* ---- debugger console (not used) ---- */

void console_output(char *s) { (void)s; }
int console_input_get(char *s) { (void)s; return 0; }
void console_input_disable(void) {}
void console_input_enable(void) {}

/* ---- podule configuration dialogs (not used) ---- */

void *podule_config_get_current(void *window_p, int id) { (void)window_p; (void)id; return NULL; }
void podule_config_set_current(void *window_p, int id, void *val) { (void)window_p; (void)id; (void)val; }
int podule_config_file_selector(void *window_p, const char *title, const char *default_path, const char *default_fn,
                                const char *default_ext, const char *wildcard, char *dest, int dest_len, int flags)
{
	(void)window_p; (void)title; (void)default_path; (void)default_fn; (void)default_ext; (void)wildcard;
	(void)dest; (void)dest_len; (void)flags;
	return -1;
}
int podule_config_open(void *window_p, podule_config_t *config, const char *prefix)
{
	(void)window_p; (void)config; (void)prefix;
	return 0;
}

/* ---- video ---- */

#define TEX_W 1024
#define TEX_H 1024
static uint32_t texture[TEX_W * TEX_H];

int video_renderer_available(int id) { return id == 0; }
char *video_renderer_get_name(int id) { (void)id; return "auto"; }
int video_renderer_get_id(char *name) { (void)name; return 0; }
int video_renderer_init(void *main_window) { (void)main_window; return 0; }
int video_renderer_reinit(void *main_window) { (void)main_window; return 0; }
void video_renderer_close(void) {}
void updatewindowsize(int x, int y) { (void)x; (void)y; }

void video_renderer_update(BITMAP *src, int src_x, int src_y, int dest_x, int dest_y, int w, int h)
{
	if (src_x < 0) { w += src_x; dest_x -= src_x; src_x = 0; }
	if (src_y < 0) { h += src_y; dest_y -= src_y; src_y = 0; }
	if (dest_x < 0) { w += dest_x; src_x -= dest_x; dest_x = 0; }
	if (dest_y < 0) { h += dest_y; src_y -= dest_y; dest_y = 0; }
	if (dest_x + w > TEX_W) w = TEX_W - dest_x;
	if (dest_y + h > TEX_H) h = TEX_H - dest_y;
	if (src_x + w > src->w) w = src->w - src_x;
	if (src_y + h > src->h) h = src->h - src_y;
	if (w <= 0 || h <= 0)
		return;
	for (int y = 0; y < h; y++)
		memcpy(&texture[(dest_y + y) * TEX_W + dest_x], &((uint32_t *)src->dat)[(src_y + y) * src->w + src_x], w * 4);
}

void video_renderer_present(int src_x, int src_y, int src_w, int src_h, int dblscan)
{
	if (src_x < 0 || src_y < 0 || src_w <= 0 || src_h <= 0 || src_x + src_w > TEX_W || src_y + src_h > TEX_H)
		return;
	archi_core_video(&texture[src_y * TEX_W + src_x], TEX_W, src_w, src_h, dblscan);
}

/* ---- sound: 2400 stereo frames at 48 kHz per buffer ---- */

void sound_dev_init(void) {}
void sound_dev_close(void) {}

void sound_givebuffer(int16_t *buf)
{
	archi_core_sound(buf, 2400);
}

void sound_givebufferdd(int16_t *buf) { (void)buf; }

/* ---- keyboard and mouse ---- */

static int mouse_dx, mouse_dy, mouse_buttons;
static int pending_keys[512];

void input_init(void) {}
void input_close(void) {}
void mouse_capture_enable(void) {}
void mouse_capture_disable(void) {}
void mouse_poll_host(void) {}

void mouse_get_mickeys(int *x, int *y)
{
	*x = mouse_dx;
	*y = mouse_dy;
	mouse_dx = mouse_dy = 0;
}

int mouse_get_buttons(void) { return mouse_buttons; }

void keyboard_poll_host(void)
{
	memcpy(key, pending_keys, sizeof(key));
}

void archi_glue_key(int scancode, int down)
{
	if (scancode > 0 && scancode < 512)
		pending_keys[scancode] = down;
}

void archi_glue_mouse(int dx, int dy, int buttons)
{
	mouse_dx += dx;
	mouse_dy += dy;
	mouse_buttons = buttons;
}

void archi_glue_reload_config(void)
{
	loadconfig();
}

/* ---- joysticks (none on the host side) ---- */

void joystick_init(void) {}
void joystick_close(void) {}
void joystick_poll_host(void) {}
