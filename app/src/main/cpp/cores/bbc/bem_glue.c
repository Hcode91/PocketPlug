/* PocketPlug - B-em (GPL-2+, Tom Walker and the B-em contributors) driven
 * frame by frame: this replaces B-em's main.c, vidalleg.c, sound.c and
 * linux.c, and stubs the peripherals PocketPlug doesn't build in.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include "bem/b-em.h"
#include <stdarg.h>
#include <sys/stat.h>
#include "bem/6502.h"
#include "bem/adc.h"
#include "bem/model.h"
#include "bem/cmos.h"
#include "bem/config.h"
#include "bem/disc.h"
#include "bem/i8271.h"
#include "bem/keyboard.h"
#include "bem/main.h"
#include "bem/mem.h"
#include "bem/sn76489.h"
#include "bem/sound.h"
#include "bem/sysacia.h"
#include "bem/serial.h"
#include "bem/tape.h"
#include "bem/tube.h"
#include "bem/via.h"
#include "bem/sysvia.h"
#include "bem/uservia.h"
#include "bem/video.h"
#include "bem/video_render.h"
#include "bem/wd1770.h"
#include "bem/sdf.h"
#include "bem/ide.h"
#include "bem/scsi.h"
#include "bem/mmccard.h"
#include "bem/music2000.h"
#include "bem/music5000.h"
#include "bem/sid_b-em.h"
#include "bem/paula.h"
#include "bem/vdfs.h"
#include "bem/led.h"
#include "bem_glue.h"

/* ---------------- main.c's globals ---------------- */
bool quitting = false, keydefining = false, autopause = false, autoskip = false, skipover = false;
unsigned hiresdisplay = 0;
int autoboot = 0;
int joybutton[4];
float joyaxes[4];
int emuspeed = 4;
bool tricky_sega_adapter = false;
int8_t shutdown_exit_code = 0;
ALLEGRO_EVENT_QUEUE* queue;
ALLEGRO_DISPLAY* tmp_display;
int mousecapture = 0;
static const emu_speed_t speeds[1] = { { "100%", 1, 1 } };
const emu_speed_t* emu_speeds = speeds;
int num_emu_speeds = 1;
int framesrun = 0;

static char g_datadir[512];

/* ---------------- files (linux.c) ---------------- */
bool is_relative_filename(const char* fn) { return fn && fn[0] != '/'; }

static ALLEGRO_PATH* find_in(const char* sub, const char* name, const char* ext)
{
    char buf[1100];
    struct stat st;
    snprintf(buf, sizeof(buf), "%s/%s%s%s", g_datadir, sub, name, ext ? ext : "");
    if (stat(buf, &st) == 0) return al_create_path(buf);
    snprintf(buf, sizeof(buf), "%s/%s%s", g_datadir, sub, name);
    if (stat(buf, &st) == 0) return al_create_path(buf);
    return NULL;
}

ALLEGRO_PATH* find_dat_file(ALLEGRO_PATH* dir, const char* name, const char* ext)
{
    if (!is_relative_filename(name)) return al_create_path(name);
    return find_in(al_path_cstr(dir, '/'), name, ext);
}

ALLEGRO_PATH* find_cfg_file(const char* name, const char* ext) { return find_in("", name, ext); }

ALLEGRO_PATH* find_cfg_dest(const char* name, const char* ext)
{
    char buf[1100];
    snprintf(buf, sizeof(buf), "%s/%s%s", g_datadir, name, ext ? ext : "");
    return al_create_path(buf);
}

/* ---------------- logging ---------------- */
static int g_verbose = -1;
static void vlog(const char* lvl, const char* fmt, va_list ap)
{
    if (g_verbose < 0) g_verbose = getenv("BEMLOG") != NULL;
    if (!g_verbose) return;
    fprintf(stderr, "bem %s: ", lvl);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
}
void log_open(const char* fn) { (void)fn; }
void log_close(void) {}
void log_fatal(const char* fmt, ...) { va_list ap; va_start(ap, fmt); g_verbose = 1; vlog("fatal", fmt, ap); va_end(ap); }
void log_error(const char* fmt, ...) { va_list ap; va_start(ap, fmt); vlog("error", fmt, ap); va_end(ap); }
void log_warn(const char* fmt, ...) { va_list ap; va_start(ap, fmt); vlog("warn", fmt, ap); va_end(ap); }
void log_info(const char* fmt, ...) { va_list ap; va_start(ap, fmt); vlog("info", fmt, ap); va_end(ap); }

/* ---------------- debugger: off ---------------- */
int readc[65536], writec[65536], fetchc[65536];
void debug_memread(cpu_debug_t* cpu, uint32_t addr, uint32_t value, uint8_t size) { (void)cpu; (void)addr; (void)value; (void)size; }
void debug_memwrite(cpu_debug_t* cpu, uint32_t addr, uint32_t value, uint8_t size) { (void)cpu; (void)addr; (void)value; (void)size; }
void debug_ioread(cpu_debug_t* cpu, uint32_t addr, uint32_t value, uint8_t size) { (void)cpu; (void)addr; (void)value; (void)size; }
void debug_iowrite(cpu_debug_t* cpu, uint32_t addr, uint32_t value, uint8_t size) { (void)cpu; (void)addr; (void)value; (void)size; }
void debug_preexec(cpu_debug_t* cpu, uint32_t addr) { (void)cpu; (void)addr; }
void debug_trap(cpu_debug_t* cpu, uint32_t addr, int reason) { (void)cpu; (void)addr; (void)reason; }
size_t dbg6502_print_flags(PREG* pp, char* buf, size_t bufsize) { (void)pp; if (bufsize) *buf = 0; return 0; }
uint32_t dbg6502_disassemble(cpu_debug_t* cpu, uint32_t addr, char* buf, size_t bufsize, m6502_t model) { (void)cpu; (void)model; if (bufsize) *buf = 0; return addr + 1; }
const char* dbg6502_reg_names[] = { "A", "X", "Y", "S", "P", "PC", NULL };
size_t debug_print_8bit(uint32_t addr, char* buf, size_t bufsize) { return (size_t)snprintf(buf, bufsize, "%02X", addr & 0xff); }
size_t debug_print_16bit(uint32_t addr, char* buf, size_t bufsize) { return (size_t)snprintf(buf, bufsize, "%04X", addr & 0xffff); }
size_t debug_print_32bit(uint32_t addr, char* buf, size_t bufsize) { return (size_t)snprintf(buf, bufsize, "%08X", addr); }
size_t debug_print_addr16(cpu_debug_t* cpu, uint32_t addr, char* buf, size_t bufsize, bool s) { (void)cpu; (void)s; return debug_print_16bit(addr, buf, bufsize); }
size_t debug_print_addr32(cpu_debug_t* cpu, uint32_t addr, char* buf, size_t bufsize, bool s) { (void)cpu; (void)s; return debug_print_32bit(addr, buf, bufsize); }
uint32_t debug_parse_addr(cpu_debug_t* cpu, const char* arg, const char** end) { (void)cpu; return (uint32_t)strtoul(arg, (char**)end, 16); }
bool symbol_find_by_addr(symbol_table* symtab, uint32_t addr, const char** ret) { (void)symtab; (void)addr; (void)ret; return false; }

/* ---------------- peripherals not built in ---------------- */
bool sound_internal = true, sound_beebsid = false, sound_dac = false, sound_ddnoise = false, sound_tape = false;
bool sound_music5000 = false, sound_filter = true, sound_paula = false;
size_t buflen_m5 = 750;
sound_rec_t sound_rec;
void sid_write(uint16_t addr, uint8_t val) { (void)addr; (void)val; }
uint8_t sid_read(uint16_t addr) { (void)addr; return 0xff; }
void music2000_poll(void) {}
uint8_t music2000_read(uint32_t addr) { (void)addr; return 0xff; }
void music2000_write(uint32_t addr, uint8_t val) { (void)addr; (void)val; }
void music5000_poll(int cycles) { (void)cycles; }
void music5000_write(uint16_t addr, uint8_t val) { (void)addr; (void)val; }
bool paula_read(uint16_t addr, uint8_t* r) { (void)addr; (void)r; return false; }
void paula_write(uint16_t addr, uint8_t val) { (void)addr; (void)val; }
bool scsi_enabled = false;
uint8_t scsi_read(uint16_t addr) { (void)addr; return 0xff; }
void scsi_write(uint16_t addr, uint8_t val) { (void)addr; (void)val; }
bool ide_enable = false;
int ide_count = 0;
void ide_callback(void) {}
uint8_t ide_read(uint16_t addr) { (void)addr; return 0xff; }
void ide_write(uint16_t addr, uint8_t val) { (void)addr; (void)val; }
uint8_t vdfs_read(uint16_t addr) { (void)addr; return 0xff; }
void vdfs_write(uint16_t addr, uint8_t val) { (void)addr; (void)val; }
uint8_t mmccard_read(void) { return 0xff; }
void mmccard_write(uint8_t data) { (void)data; }

/* ---------------- sound: SN76489 at 125 kHz, down to 44.1 kHz ---------------- */
static int16_t g_snbuf[1];
static int g_sndCycles = 0;
static uint32_t g_resAcc = 0;
static int32_t g_sum = 0, g_cnt = 0;
static float g_iirY = 0;
#define AUD_MAX 16384
static int16_t g_aud[AUD_MAX];
static int g_audN = 0;

void sound_init(void) { g_sndCycles = 0; }

void sound_poll(int cycles)
{
    g_sndCycles -= cycles;
    while (g_sndCycles < 0) {
        g_sndCycles += 16;           /* 2 MHz / 16 = 125 kHz */
        g_snbuf[0] = 0;
        sn_fillbuf(g_snbuf, 1);
        g_sum += g_snbuf[0]; g_cnt++;
        g_resAcc += 44100;
        if (g_resAcc >= 125000) {
            g_resAcc -= 125000;
            float v = g_cnt ? (float)g_sum / g_cnt : 0;
            g_sum = g_cnt = 0;
            g_iirY += (v - g_iirY) * 0.6f;      /* a gentle low-pass, as B-em's filter */
            int s = (int)g_iirY;
            if (s > 32767) s = 32767; if (s < -32768) s = -32768;
            if (g_audN < AUD_MAX) g_aud[g_audN++] = (int16_t)s;
        }
    }
}

bool sound_start_rec(sound_rec_t* rec, const char* filename) { (void)rec; (void)filename; return false; }
void sound_stop_rec(sound_rec_t* rec) { (void)rec; }

int bem_read_audio(int16_t* out, int max)
{
    int n = g_audN < max ? g_audN : max;
    memcpy(out, g_aud, (size_t)n * 2);
    memmove(g_aud, g_aud + n, (size_t)(g_audN - n) * 2);
    g_audN -= n;
    return n;
}

/* ---------------- video: B-em draws into bitmap b; take the picture area ---------------- */
int scr_x_start, scr_x_size, scr_y_start, scr_y_size;
int winsizex = 640, winsizey = 512, vid_win_multiplier = 1;
enum vid_disptype vid_dtype_user = VDT_SCALE, vid_dtype_intern = VDT_SCALE;
enum vid_coltype vid_colour_out = VDC_RGB;
int vid_fskipmax = 1, vid_fullborders = 0;
int vid_ledlocation = 0, vid_ledvisibility = 0;
bool vid_print_mode = false;
int vid_lock_type = 0;
int vid_savescrshot = 0;
char vid_scrshotname[260];
ALLEGRO_COLOR mono_green_col, mono_amber_col, mono_white_col;

static uint32_t g_frame[BEM_W * BEM_H];
static int g_frameReady = 0;

void video_doblit(bool non_ttx, uint8_t vtotal)
{
    ++framesrun;
    /* the medium-border window B-em shows (video_render.h): room for games that
     * widen the screen */
    int x0 = non_ttx ? BORDER_MED_X_START_GRA : BORDER_MED_X_START_TTX;
    int y0 = vtotal > 30 ? BORDER_MED_Y_START_GRA : BORDER_MED_Y_START_TXT;
    const uint32_t* src = (const uint32_t*)region->data;
    int pitch = region->pitch / 4;
    /* interlaced teletext is drawn on 2 lines per scan line (B-em's VDT_INTERLACE); the
     * other modes are line doubled to match */
    bool inter = vid_dtype_intern == VDT_INTERLACE;
    for (int y = 0; y < BEM_H; y++) {
        int sy = inter ? y0 * 2 + y : y0 + y / 2;
        uint32_t* d = g_frame + (size_t)y * BEM_W;
        if (sy < 0 || sy >= 800) { memset(d, 0, BEM_W * 4); continue; }
        const uint32_t* s = src + (size_t)sy * pitch + x0;
        for (int x = 0; x < BEM_W; x++) d[x] = s[x] | 0xff000000u;
    }
    g_frameReady = 1;
    firstx = firsty = 65535;
    lastx = lasty = 0;
}

void video_enterfullscreen(void) {}
void video_leavefullscreen(void) {}
void video_set_window_size(bool fudge) { (void)fudge; }
void video_update_window_size(ALLEGRO_EVENT* event) { (void)event; }
void video_set_borders(int borders) { (void)borders; }
void video_set_multipier(int m) { (void)m; }
void video_set_led_location(int l) { (void)l; }
void video_set_led_visibility(int v) { (void)v; }
void clearscreen(void) {}
void led_init(void) {}
static int g_discLed;
void led_update(led_name_t led, bool b, int ticks) { (void)ticks; if (led == LED_DRIVE_0 || led == LED_DRIVE_1) g_discLed = b; }
void led_close(void) {}
bool led_any_transient_led_on(void) { return false; }
int bem_disc_led(void) { return g_discLed; }
void led_timer_fired(void) {}

const uint32_t* bem_frame(void) { return g_frame; }

/* ---------------- the machine ---------------- */
void main_reset(void)
{
    m6502_reset();
    crtc_reset();
    video_reset();
    sysvia_reset();
    uservia_reset();
    serial_reset();
    wd1770_reset();
    i8271_reset();
    sn_init();
    if (curtube != -1) tubes[curtube].cpu->reset();
    else tube_exec = NULL;
    tube_reset();
}

void main_softreset(void) { m6502_reset(); }
void main_restart(void) {}
void main_pause(const char* why) { (void)why; }
void main_resume(void) {}
void main_setspeed(int speed) { (void)speed; }
void main_start_fullspeed(void) {}
void main_stop_fullspeed(bool hostshift) { (void)hostshift; }
void main_key_break(void) { m6502_reset(); }
void main_key_pause(void) {}
void main_cleardrawit(void) {}
void main_setmouse(void) {}
void set_quit(void) {}
void setquit(void) {}
void set_shutdown_exit_code(uint8_t c) { (void)c; }

int bem_init(const char* datadir, int model)
{
    snprintf(g_datadir, sizeof(g_datadir), "%s", datadir);
    char cfg[600];
    snprintf(cfg, sizeof(cfg), "%s/b-em.cfg", datadir);
    key_init();
    config_load(al_create_path(cfg));
    model_loadcfg();
    curmodel = model;
    video_init();
    mode7_makechars();
    mem_init();
    sound_init();
    adc_init();
    disc_init();
    model_init();
    main_reset();
    return 0;
}

void bem_run_frame(void)
{
    /* 2 MHz, 50 frames a second */
    g_frameReady = 0;
    if (x65c02) m65c02_exec(40000);
    else m6502_exec(40000);
}

int bem_load_disc(int drive, const char* path)
{
    disc_close(drive);
    return disc_load(drive, al_create_path(path));
}

void bem_eject_disc(int drive) { disc_close(drive); }

void bem_load_tape(const char* path) { tape_load(al_create_path(path)); }

void bem_key(int bbckey, int down) { if (down) key_down((uint8_t)bbckey); else key_up((uint8_t)bbckey); }
void bem_break(void) { m6502_reset(); }
int bem_frame_ready(void) { return g_frameReady; }
