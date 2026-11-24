/* PocketPlug - the Allegro 5 functions B-em uses, in memory (see allegro5/allegro.h)
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include "allegro5/allegro.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>

/* ---------------- paths ---------------- */
struct ALLEGRO_PATH { char s[1024]; };

ALLEGRO_PATH* al_create_path(const char* str)
{
    ALLEGRO_PATH* p = calloc(1, sizeof(*p));
    if (str) { strncpy(p->s, str, sizeof(p->s) - 1); }
    return p;
}

ALLEGRO_PATH* al_create_path_for_directory(const char* str)
{
    ALLEGRO_PATH* p = al_create_path(str);
    size_t n = strlen(p->s);
    if (n && p->s[n - 1] != '/' && n < sizeof(p->s) - 1) { p->s[n] = '/'; p->s[n + 1] = 0; }
    return p;
}

ALLEGRO_PATH* al_clone_path(const ALLEGRO_PATH* p) { return al_create_path(p ? p->s : NULL); }
void al_destroy_path(ALLEGRO_PATH* p) { free(p); }
const char* al_path_cstr(const ALLEGRO_PATH* p, char sep) { (void)sep; return p ? p->s : ""; }

const char* al_get_path_filename(const ALLEGRO_PATH* p)
{
    const char* s = strrchr(p->s, '/');
    return s ? s + 1 : p->s;
}

const char* al_get_path_extension(const ALLEGRO_PATH* p)
{
    const char* f = al_get_path_filename(p);
    const char* d = strrchr(f, '.');
    return d ? d : "";
}

bool al_set_path_extension(ALLEGRO_PATH* p, const char* ext)
{
    char* f = (char*)al_get_path_filename(p);
    char* d = strrchr(f, '.');
    if (d) *d = 0;
    strncat(p->s, ext, sizeof(p->s) - strlen(p->s) - 1);
    return true;
}

void al_set_path_filename(ALLEGRO_PATH* p, const char* fn)
{
    char* f = (char*)al_get_path_filename(p);
    *f = 0;
    if (fn) strncat(p->s, fn, sizeof(p->s) - strlen(p->s) - 1);
}

bool al_join_paths(ALLEGRO_PATH* path, const ALLEGRO_PATH* tail)
{
    if (tail->s[0] == '/') { strcpy(path->s, tail->s); return true; }
    al_set_path_filename(path, NULL);
    strncat(path->s, tail->s, sizeof(path->s) - strlen(path->s) - 1);
    return true;
}

void al_append_path_component(ALLEGRO_PATH* p, const char* s)
{
    char fn[256];
    strncpy(fn, al_get_path_filename(p), sizeof(fn) - 1); fn[sizeof(fn) - 1] = 0;
    al_set_path_filename(p, NULL);
    strncat(p->s, s, sizeof(p->s) - strlen(p->s) - 2);
    strcat(p->s, "/");
    strncat(p->s, fn, sizeof(p->s) - strlen(p->s) - 1);
}

bool al_filename_exists(const char* path) { struct stat st; return stat(path, &st) == 0; }

/* ---------------- config: sections of key = value ---------------- */
struct ALLEGRO_CONFIG_ENTRY { char* key; char* value; struct ALLEGRO_CONFIG_ENTRY* next; };
struct ALLEGRO_CONFIG_SECTION { char* name; struct ALLEGRO_CONFIG_ENTRY* head; struct ALLEGRO_CONFIG_SECTION* next; };
struct ALLEGRO_CONFIG { struct ALLEGRO_CONFIG_SECTION* head; };

static char* dupstr(const char* s) { char* d = malloc(strlen(s) + 1); strcpy(d, s); return d; }

static struct ALLEGRO_CONFIG_SECTION* find_section(const ALLEGRO_CONFIG* c, const char* name, bool create)
{
    if (!name) name = "";
    for (struct ALLEGRO_CONFIG_SECTION* s = c->head; s; s = s->next) if (!strcmp(s->name, name)) return s;
    if (!create) return NULL;
    struct ALLEGRO_CONFIG_SECTION* s = calloc(1, sizeof(*s));
    s->name = dupstr(name);
    struct ALLEGRO_CONFIG_SECTION** pp = (struct ALLEGRO_CONFIG_SECTION**)&c->head;
    while (*pp) pp = &(*pp)->next;
    *pp = s;
    return s;
}

ALLEGRO_CONFIG* al_create_config(void) { ALLEGRO_CONFIG* c = calloc(1, sizeof(*c)); find_section(c, "", true); return c; }

void al_add_config_section(ALLEGRO_CONFIG* c, const char* name) { find_section(c, name, true); }

const char* al_get_config_value(const ALLEGRO_CONFIG* c, const char* section, const char* key)
{
    if (!c) return NULL;
    struct ALLEGRO_CONFIG_SECTION* s = find_section(c, section, false);
    if (!s) return NULL;
    for (struct ALLEGRO_CONFIG_ENTRY* e = s->head; e; e = e->next) if (!strcmp(e->key, key)) return e->value;
    return NULL;
}

void al_set_config_value(ALLEGRO_CONFIG* c, const char* section, const char* key, const char* value)
{
    struct ALLEGRO_CONFIG_SECTION* s = find_section(c, section, true);
    struct ALLEGRO_CONFIG_ENTRY** pp = &s->head;
    for (; *pp; pp = &(*pp)->next)
        if (!strcmp((*pp)->key, key)) { free((*pp)->value); (*pp)->value = dupstr(value); return; }
    struct ALLEGRO_CONFIG_ENTRY* e = calloc(1, sizeof(*e));
    e->key = dupstr(key); e->value = dupstr(value);
    *pp = e;
}

bool al_remove_config_key(ALLEGRO_CONFIG* c, const char* section, const char* key)
{
    struct ALLEGRO_CONFIG_SECTION* s = find_section(c, section, false);
    if (!s) return false;
    for (struct ALLEGRO_CONFIG_ENTRY** pp = &s->head; *pp; pp = &(*pp)->next)
        if (!strcmp((*pp)->key, key)) {
            struct ALLEGRO_CONFIG_ENTRY* e = *pp; *pp = e->next;
            free(e->key); free(e->value); free(e);
            return true;
        }
    return false;
}

static char* trim(char* s)
{
    while (isspace((unsigned char)*s)) s++;
    char* e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    return s;
}

ALLEGRO_CONFIG* al_load_config_file(const char* filename)
{
    FILE* f = fopen(filename, "r");
    if (!f) return NULL;
    ALLEGRO_CONFIG* c = al_create_config();
    char line[1024], section[256] = "";
    while (fgets(line, sizeof(line), f)) {
        char* l = trim(line);
        if (!*l || *l == '#') continue;
        if (*l == '[') {
            char* e = strchr(l, ']');
            if (e) *e = 0;
            strncpy(section, l + 1, sizeof(section) - 1);
            find_section(c, section, true);
            continue;
        }
        char* eq = strchr(l, '=');
        if (!eq) continue;
        *eq = 0;
        al_set_config_value(c, section, trim(l), trim(eq + 1));
    }
    fclose(f);
    return c;
}

bool al_save_config_file(const char* filename, const ALLEGRO_CONFIG* config) { (void)filename; (void)config; return true; }

void al_destroy_config(ALLEGRO_CONFIG* c)
{
    if (!c) return;
    struct ALLEGRO_CONFIG_SECTION* s = c->head;
    while (s) {
        struct ALLEGRO_CONFIG_ENTRY* e = s->head;
        while (e) { struct ALLEGRO_CONFIG_ENTRY* n = e->next; free(e->key); free(e->value); free(e); e = n; }
        struct ALLEGRO_CONFIG_SECTION* n = s->next; free(s->name); free(s); s = n;
    }
    free(c);
}

const char* al_get_first_config_section(const ALLEGRO_CONFIG* c, ALLEGRO_CONFIG_SECTION** it)
{
    *it = c ? c->head : NULL;
    return *it ? (*it)->name : NULL;
}

const char* al_get_next_config_section(ALLEGRO_CONFIG_SECTION** it)
{
    if (!*it) return NULL;
    *it = (*it)->next;
    return *it ? (*it)->name : NULL;
}

const char* al_get_first_config_entry(const ALLEGRO_CONFIG* c, const char* section, ALLEGRO_CONFIG_ENTRY** it)
{
    struct ALLEGRO_CONFIG_SECTION* s = c ? find_section(c, section, false) : NULL;
    *it = s ? s->head : NULL;
    return *it ? (*it)->key : NULL;
}

const char* al_get_next_config_entry(ALLEGRO_CONFIG_ENTRY** it)
{
    if (!*it) return NULL;
    *it = (*it)->next;
    return *it ? (*it)->key : NULL;
}

/* ---------------- bitmaps ---------------- */
static ALLEGRO_BITMAP* g_target;

ALLEGRO_COLOR al_map_rgb(unsigned char r, unsigned char g, unsigned char b)
{
    ALLEGRO_COLOR c = { r / 255.0f, g / 255.0f, b / 255.0f, 1.0f };
    return c;
}

ALLEGRO_COLOR al_map_rgba(unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
    ALLEGRO_COLOR c = { r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f };
    return c;
}

void al_unmap_rgb(ALLEGRO_COLOR c, unsigned char* r, unsigned char* g, unsigned char* b)
{
    *r = (unsigned char)(c.r * 255.0f + 0.5f); *g = (unsigned char)(c.g * 255.0f + 0.5f); *b = (unsigned char)(c.b * 255.0f + 0.5f);
}

static uint32_t argb(ALLEGRO_COLOR c)
{
    unsigned char r, g, b;
    al_unmap_rgb(c, &r, &g, &b);
    return 0xff000000u | (uint32_t)r << 16 | (uint32_t)g << 8 | b;
}

ALLEGRO_BITMAP* al_create_bitmap(int w, int h)
{
    ALLEGRO_BITMAP* b = calloc(1, sizeof(*b));
    b->w = w; b->h = h;
    b->px = calloc((size_t)w * h, 4);
    b->lr.data = b->px; b->lr.pitch = w * 4; b->lr.pixel_size = 4; b->lr.format = 0;
    return b;
}

void al_destroy_bitmap(ALLEGRO_BITMAP* b) { if (b) { if (g_target == b) g_target = NULL; free(b->px); free(b); } }
void al_set_target_bitmap(ALLEGRO_BITMAP* b) { g_target = b; }

void al_clear_to_color(ALLEGRO_COLOR c)
{
    if (!g_target) return;
    uint32_t v = argb(c);
    for (size_t i = 0; i < (size_t)g_target->w * g_target->h; i++) g_target->px[i] = v;
}

ALLEGRO_LOCKED_REGION* al_lock_bitmap(ALLEGRO_BITMAP* b, int format, int flags) { (void)format; (void)flags; return b ? &b->lr : NULL; }
void al_unlock_bitmap(ALLEGRO_BITMAP* b) { (void)b; }

void al_put_pixel(int x, int y, ALLEGRO_COLOR c)
{
    if (g_target && x >= 0 && y >= 0 && x < g_target->w && y < g_target->h) g_target->px[(size_t)y * g_target->w + x] = argb(c);
}

ALLEGRO_COLOR al_get_pixel(ALLEGRO_BITMAP* b, int x, int y)
{
    uint32_t v = (b && x >= 0 && y >= 0 && x < b->w && y < b->h) ? b->px[(size_t)y * b->w + x] : 0;
    return al_map_rgb((v >> 16) & 255, (v >> 8) & 255, v & 255);
}

int al_get_bitmap_width(ALLEGRO_BITMAP* b) { return b ? b->w : 0; }
int al_get_bitmap_height(ALLEGRO_BITMAP* b) { return b ? b->h : 0; }
void al_set_new_bitmap_flags(int flags) { (void)flags; }

/* ---------------- no devices ---------------- */
struct ALLEGRO_DISPLAY { int dummy; };
static struct ALLEGRO_DISPLAY g_display;
void al_set_new_display_flags(int flags) { (void)flags; }
void al_set_new_display_option(int option, int value, int importance) { (void)option; (void)value; (void)importance; }
int al_get_new_display_option(int option, int* importance) { (void)option; if (importance) *importance = 0; return 0; }
ALLEGRO_DISPLAY* al_create_display(int w, int h) { (void)w; (void)h; return &g_display; }
void al_destroy_display(ALLEGRO_DISPLAY* d) { (void)d; }

const char* al_keycode_to_name(int keycode)
{
    static char buf[16];
    snprintf(buf, sizeof(buf), "KEY%d", keycode);
    return buf;
}

void al_get_keyboard_state(ALLEGRO_KEYBOARD_STATE* s) { memset(s, 0, sizeof(*s)); }
bool al_key_down(const ALLEGRO_KEYBOARD_STATE* s, int keycode) { (void)s; (void)keycode; return false; }
bool al_install_joystick(void) { return false; }
double al_get_time(void) { return 0.0; }
void al_free(void* p) { free(p); }
void* al_malloc(size_t n) { return malloc(n); }
void al_rest(double seconds) { (void)seconds; }
void al_set_window_title(ALLEGRO_DISPLAY* d, const char* title) { (void)d; (void)title; }
void al_emit_user_event(ALLEGRO_EVENT_SOURCE* src, ALLEGRO_EVENT* ev, void* dtor) { (void)src; (void)ev; (void)dtor; }
bool al_init(void) { return true; }
