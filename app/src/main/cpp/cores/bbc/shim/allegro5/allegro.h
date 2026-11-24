/* PocketPlug - the parts of Allegro 5's API that B-em uses, done without Allegro
 * (in memory: paths, an INI config, ARGB bitmaps; no display, input or audio devices).
 * SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef POCKETPLUG_ALLEGRO_SHIM_H
#define POCKETPLUG_ALLEGRO_SHIM_H
#ifndef VERSION
#define VERSION "2.2"
#endif
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include "keycodes.h"
#ifdef __cplusplus
extern "C" {
#endif

#define ALLEGRO_NATIVE_PATH_SEP '/'
#define ALLEGRO_PIXEL_FORMAT_ARGB_8888 0
#define ALLEGRO_LOCK_WRITEONLY 2
#define ALLEGRO_LOCK_READWRITE 0
#define ALLEGRO_LOCK_READONLY 1
#define ALLEGRO_WINDOWED 1
#define ALLEGRO_RESIZABLE 2
#define ALLEGRO_VSYNC 1
#define ALLEGRO_SUGGEST 2
#define ALLEGRO_VIDEO_BITMAP 0
#define ALLEGRO_MEMORY_BITMAP 1
#define ALLEGRO_NO_PRESERVE_TEXTURE 0
#define ALLEGRO_EVENT_KEY_CHAR 11
#define ALLEGRO_EVENT_KEY_DOWN 10
#define ALLEGRO_EVENT_KEY_UP 12
#define ALLEGRO_EVENT_TIMER 30
#define ALLEGRO_KEYMOD_SHIFT 1
#define ALLEGRO_KEYMOD_CTRL 2

typedef struct ALLEGRO_PATH ALLEGRO_PATH;
typedef struct ALLEGRO_CONFIG ALLEGRO_CONFIG;
typedef struct ALLEGRO_CONFIG_SECTION ALLEGRO_CONFIG_SECTION;
typedef struct ALLEGRO_CONFIG_ENTRY ALLEGRO_CONFIG_ENTRY;
typedef struct ALLEGRO_DISPLAY ALLEGRO_DISPLAY;
typedef struct ALLEGRO_EVENT_QUEUE ALLEGRO_EVENT_QUEUE;
typedef struct ALLEGRO_EVENT_SOURCE { int dummy; } ALLEGRO_EVENT_SOURCE;
typedef struct ALLEGRO_TIMER ALLEGRO_TIMER;
typedef struct ALLEGRO_USTR ALLEGRO_USTR;
typedef struct ALLEGRO_FILE ALLEGRO_FILE;
typedef struct ALLEGRO_FONT ALLEGRO_FONT;
typedef struct ALLEGRO_MENU ALLEGRO_MENU;
typedef struct ALLEGRO_THREAD ALLEGRO_THREAD;
typedef struct ALLEGRO_MUTEX ALLEGRO_MUTEX;

typedef struct ALLEGRO_COLOR { float r, g, b, a; } ALLEGRO_COLOR;
typedef struct ALLEGRO_LOCKED_REGION { void* data; int format; int pitch; int pixel_size; } ALLEGRO_LOCKED_REGION;
typedef struct ALLEGRO_BITMAP { int w, h; uint32_t* px; ALLEGRO_LOCKED_REGION lr; } ALLEGRO_BITMAP;
typedef struct ALLEGRO_KEYBOARD_STATE { unsigned int key[(ALLEGRO_KEY_MAX + 31) / 32]; } ALLEGRO_KEYBOARD_STATE;

typedef struct { unsigned int type; void* source; double timestamp; } ALLEGRO_ANY_EVENT;
typedef struct { unsigned int type; void* source; double timestamp; void* display; int keycode; int unichar; unsigned int modifiers; bool repeat; } ALLEGRO_KEYBOARD_EVENT;
typedef struct { unsigned int type; void* source; double timestamp; int64_t count; double error; } ALLEGRO_TIMER_EVENT;
typedef struct { unsigned int type; void* source; double timestamp; intptr_t data1, data2, data3, data4; } ALLEGRO_USER_EVENT;
typedef union ALLEGRO_EVENT {
    unsigned int type;
    ALLEGRO_ANY_EVENT any;
    ALLEGRO_KEYBOARD_EVENT keyboard;
    ALLEGRO_TIMER_EVENT timer;
    ALLEGRO_USER_EVENT user;
} ALLEGRO_EVENT;

/* paths */
ALLEGRO_PATH* al_create_path(const char* str);
ALLEGRO_PATH* al_create_path_for_directory(const char* str);
ALLEGRO_PATH* al_clone_path(const ALLEGRO_PATH* p);
void al_destroy_path(ALLEGRO_PATH* p);
const char* al_path_cstr(const ALLEGRO_PATH* p, char sep);
const char* al_get_path_extension(const ALLEGRO_PATH* p);
const char* al_get_path_filename(const ALLEGRO_PATH* p);
bool al_set_path_extension(ALLEGRO_PATH* p, const char* ext);
void al_set_path_filename(ALLEGRO_PATH* p, const char* fn);
bool al_join_paths(ALLEGRO_PATH* path, const ALLEGRO_PATH* tail);
void al_append_path_component(ALLEGRO_PATH* p, const char* s);
bool al_filename_exists(const char* path);

/* config */
ALLEGRO_CONFIG* al_create_config(void);
ALLEGRO_CONFIG* al_load_config_file(const char* filename);
bool al_save_config_file(const char* filename, const ALLEGRO_CONFIG* config);
void al_destroy_config(ALLEGRO_CONFIG* config);
const char* al_get_config_value(const ALLEGRO_CONFIG* config, const char* section, const char* key);
void al_set_config_value(ALLEGRO_CONFIG* config, const char* section, const char* key, const char* value);
bool al_remove_config_key(ALLEGRO_CONFIG* config, const char* section, const char* key);
const char* al_get_first_config_section(const ALLEGRO_CONFIG* config, ALLEGRO_CONFIG_SECTION** iterator);
const char* al_get_next_config_section(ALLEGRO_CONFIG_SECTION** iterator);
const char* al_get_first_config_entry(const ALLEGRO_CONFIG* config, const char* section, ALLEGRO_CONFIG_ENTRY** iterator);
const char* al_get_next_config_entry(ALLEGRO_CONFIG_ENTRY** iterator);
void al_add_config_section(ALLEGRO_CONFIG* config, const char* name);

/* bitmaps (memory only) */
ALLEGRO_COLOR al_map_rgb(unsigned char r, unsigned char g, unsigned char b);
ALLEGRO_COLOR al_map_rgba(unsigned char r, unsigned char g, unsigned char b, unsigned char a);
void al_unmap_rgb(ALLEGRO_COLOR c, unsigned char* r, unsigned char* g, unsigned char* b);
ALLEGRO_BITMAP* al_create_bitmap(int w, int h);
void al_destroy_bitmap(ALLEGRO_BITMAP* b);
void al_set_target_bitmap(ALLEGRO_BITMAP* b);
void al_clear_to_color(ALLEGRO_COLOR c);
ALLEGRO_LOCKED_REGION* al_lock_bitmap(ALLEGRO_BITMAP* b, int format, int flags);
void al_unlock_bitmap(ALLEGRO_BITMAP* b);
void al_put_pixel(int x, int y, ALLEGRO_COLOR c);
ALLEGRO_COLOR al_get_pixel(ALLEGRO_BITMAP* b, int x, int y);
int al_get_bitmap_width(ALLEGRO_BITMAP* b);
int al_get_bitmap_height(ALLEGRO_BITMAP* b);
void al_set_new_bitmap_flags(int flags);

/* display, keyboard, misc: no devices */
void al_set_new_display_flags(int flags);
void al_set_new_display_option(int option, int value, int importance);
int al_get_new_display_option(int option, int* importance);
ALLEGRO_DISPLAY* al_create_display(int w, int h);
void al_destroy_display(ALLEGRO_DISPLAY* d);
const char* al_keycode_to_name(int keycode);
void al_get_keyboard_state(ALLEGRO_KEYBOARD_STATE* s);
bool al_key_down(const ALLEGRO_KEYBOARD_STATE* s, int keycode);
bool al_install_joystick(void);
double al_get_time(void);
void al_free(void* p);
void* al_malloc(size_t n);
void al_rest(double seconds);
void al_set_window_title(ALLEGRO_DISPLAY* d, const char* title);
void al_emit_user_event(ALLEGRO_EVENT_SOURCE* src, ALLEGRO_EVENT* ev, void* dtor);
bool al_init(void);

#ifdef __cplusplus
}
#endif
#endif
