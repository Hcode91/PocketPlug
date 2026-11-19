/*
 * PocketPlug - Acorn A7000 / A7000+
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * The host side RPCEmu expects from its Qt front end (rpc-qt6.cpp, paths.cpp,
 * settings.cpp, plt-sound.cpp, platform-linux.cpp), written for PocketPlug:
 * no GUI, no threads of its own. Video and sound land in buffers the
 * AcornCore reads; the IOMD and vsync timers follow a clock that the core
 * advances.
 */

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/statvfs.h>
#include <time.h>

#include "rpcemu/rpcemu.h"
#include "rpcemu/iomd.h"
#include "rpcemu/sound.h"
#include "rpcemu/vidc20.h"
#include "rpcemu/platform/platform-common.h"
#include "rpcemu_glue.h"


static char g_datadir[PATH_MAX];

/* ---- set by the core ---- */
void acorn_glue_set_datadir(const char *dir)
{
    snprintf(g_datadir, sizeof(g_datadir), "%s", dir);
}

/* ---- the clock: nanoseconds of emulated time, moved on by the core ---- */
static uint64_t g_now_ns;
static uint64_t g_iomd_next = 2000000, g_video_next;
static uint64_t g_video_interval = 1000000000ull / 60;

extern int drawscre;
static void vblupdate(void) { drawscre++; }

/* Time moves with the instructions executed (inscount steps by 200 per
   arm_exec() call), so timer reads inside a run of code see it advance. */
extern uint32_t inscount;
static uint32_t g_ins_base;
static uint32_t g_ps_per_ins = 31250;   /* picoseconds: 32 MHz, one instruction a cycle */

void acorn_glue_set_mhz(int mhz) { g_ps_per_ins = 1000000u / (uint32_t) mhz; }

uint64_t rpcemu_nsec_timer_ticks(void)
{
    return g_now_ns + (uint64_t) (uint32_t) (inscount - g_ins_base) * g_ps_per_ins / 1000;
}

void acorn_glue_sync(void)
{
    g_now_ns = rpcemu_nsec_timer_ticks();
    g_ins_base = inscount;
}

void acorn_glue_timers(void)
{
    acorn_glue_sync();
    while (g_now_ns >= g_iomd_next) {
        gentimerirq((int64_t) g_iomd_next);
        g_iomd_next += 2000000;     /* 500 Hz, as RPCEmu */
    }
    if (g_video_next == 0) g_video_next = g_video_interval;
    while (g_now_ns >= g_video_next) {
        vblupdate();
        g_video_next += g_video_interval;
    }
}

void acorn_glue_advance(uint64_t ns)
{
    acorn_glue_sync();
    g_now_ns += ns;
}

/* While RISC OS idles (Portable_Idle), time still has to pass: the core
   sets how much the idle loop may use up before it returns to the frame. */
static uint64_t g_idle_stop_ns;
int g_acorn_idle_break;

void acorn_glue_set_frame_end(uint64_t ns) { g_idle_stop_ns = ns; }

void rpcemu_idle_process_events(void)
{
    /* each pass through rpcemu_idle() stands for ~1 ms */
    acorn_glue_advance(1000000);
    acorn_glue_timers();
    if (g_now_ns >= g_idle_stop_ns) g_acorn_idle_break = 1;
}

/* ---- video: vidc20.c's display thread, run inline ---- */
void vidcstartthread(void) {}
void vidcendthread(void) {}
/* RPCEmu's Qt front end raises flyback when a redrawn picture has been
   shown; here the picture is "shown" at once, and flyback comes every frame
   (RISC OS counts vsyncs and waits on them). */
void vidcwakeupthread(void) { vidcthread(); iomd_flyback(1); }
int vidctrymutex(void) { return 1; }
void vidcreleasemutex(void) {}

void rpcemu_video_update(const uint32_t *buffer, int xsize, int ysize, int yl, int yh,
                         int double_size, int host_xsize, int host_ysize)
{
    acorn_core_video(buffer, xsize, ysize, yl, yh, double_size, host_xsize, host_ysize);
}

void rpcemu_move_host_mouse(uint16_t x, uint16_t y) { (void) x; (void) y; }

/* ---- sound: no thread; blocks are handed over as they fill ---- */
void sound_thread_start(void) {}
void sound_thread_close(void) {}
void sound_thread_wakeup(void) { sound_buffer_update(); }
void plt_sound_init(uint32_t bufferlen) { (void) bufferlen; }
void plt_sound_restart(void) {}
void plt_sound_pause(void) {}
int32_t plt_sound_buffer_free(void) { return 1 << 20; }
void plt_sound_buffer_play(uint32_t samplerate, const char *buffer, uint32_t length)
{
    acorn_core_sound(samplerate, (const int16_t *) buffer, length / 4);
}

/* ---- logging and errors ---- */
void fatal(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    char msg[512];
    vsnprintf(msg, sizeof(msg), format, ap);
    va_end(ap);
    acorn_core_fatal(msg);   /* does not return */
    abort();
}

void error(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    char msg[512];
    vsnprintf(msg, sizeof(msg), format, ap);
    va_end(ap);
    fprintf(stderr, "RPCEmu: %s\n", msg);
}

void rpcemu_log_os(void) {}
void rpcemu_log_platform(void) {}
const char *rpcemu_get_log_path(void) { const char *e = getenv("RPCLOG"); return e ? e : "/dev/null"; }
const char *rpcemu_get_datadir(void) { return g_datadir; }

bool rpcemu_platform_startup(int modifierMask) { (void) modifierMask; return true; }

int path_disk_info(const char *path, disk_info *d)
{
    struct statvfs s;
    if (statvfs(path, &s) != 0) return 0;
    d->size = (uint64_t) s.f_blocks * (uint64_t) s.f_frsize;
    d->free = (uint64_t) s.f_bavail * (uint64_t) s.f_frsize;
    return 1;
}

/* ---- paths (qt6/paths.cpp) ---- */
void path_join(char *buffer, const char *left, const char *right)
{
    if (right[0] == '/') { strcpy(buffer, right); return; }
    size_t n = strlen(left);
    if (n > 0 && left[n - 1] == '/') sprintf(buffer, "%s%s", left, right);
    else sprintf(buffer, "%s/%s", left, right);
}

void path_append(char *buffer, const char *path)
{
    char tmp[PATH_MAX];
    path_join(tmp, buffer, path);
    strcpy(buffer, tmp);
}

void path_append_data(char *buffer, const char *path)
{
    path_join(buffer, g_datadir, path);
}

void path_join_data(char *buffer, const char *left, const char *right)
{
    char tmp[PATH_MAX];
    path_join(tmp, g_datadir, left);
    path_join(buffer, tmp, right);
}

bool path_directory_exists(const char *path)
{
    struct statvfs s;
    return statvfs(path, &s) == 0;
}

const char *path_get_extension(const char *path)
{
    const char *slash = strrchr(path, '/');
    const char *dot = strrchr(path, '.');
    if (!dot || (slash && dot < slash)) return strdup("");
    return strdup(dot + 1);
}

const char *path_get_filename(const char *path)
{
    const char *slash = strrchr(path, '/');
    return strdup(slash ? slash + 1 : path);
}

/* ---- configuration (qt6/settings.cpp): fixed for PocketPlug ---- */
void config_load(Config *c)
{
    memset(c, 0, sizeof(*c));
    c->mem_size = 32;           /* MB */
    c->vram_size = 0;           /* the A7000 has no VRAM */
    c->refresh = 60;
    c->soundenabled = 1;
    c->cdromenabled = 0;
    c->cdromtype = 0;
    c->mousehackon = 0;
    c->mousetwobutton = 0;
    c->network_type = NetworkType_Off;
    c->cpu_idle = 1;
    c->username = strdup("");
    c->ipaddress = strdup("");
    c->macaddress = strdup("");
    c->bridgename = strdup("");
}

void config_save(Config *c) { (void) c; }
