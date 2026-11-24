/* PocketPlug - B-em parts not built into PocketPlug: no-op stand-ins
 * (disc noise, tape noise, mouse, VDFS, Music 4000/5000, Paula, MIDI, MMB,
 * the debugger, the FDI / HFE / IMD disc formats).
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stddef.h>

void cataddname(char *s) { (void)s; }
int ddnoise_type = 0, ddnoise_vol = 0;
void ddnoise_seek(int len) { (void)len; }
void ddnoise_spindown(void) {}
void ddnoise_spinup(void) {}
int debug_core = 0, debug_tube = 0, debug_step = 0;
int fdi_load(int drive, const char *fn) { (void)drive; (void)fn; return -1; }
int hfe_load(int drive, const char *fn) { (void)drive; (void)fn; return -1; }
int imd_load(int drive, const char *fn) { (void)drive; (void)fn; return -1; }
void gui_allegro_set_eject_text(int drive, void *path) { (void)drive; (void)path; }
void log_debug(const char *format, ...) { (void)format; }
void midi_load_config(void) {}
void midi_save_config(void) {}
char *mmb_fn = NULL, *mmccard_fn = NULL;
bool mouse_amx = false, mouse_stick = false;
uint8_t mouse_portb = 0xff;
void mouse_poll(void) {}
uint8_t music4000_read(void) { return 0xff; }
void music4000_shift(int value) { (void)value; }
int music5000_fno = 0;
void music5000_loadstate(FILE *f) { (void)f; }
void music5000_savestate(FILE *f) { (void)f; }
void paula_loadstate(FILE *f) { (void)f; }
void paula_savestate(FILE *f) { (void)f; }
void tapenoise_adddat(uint8_t dat) { (void)dat; }
void tapenoise_addhigh(void) {}
void toggle_fullscreen_menu(void) {}
const char *vdfs_cfg_root = NULL;
bool vdfs_enabled = false;
const char *vdfs_get_root(void) { return ""; }
void vdfs_loadstate(FILE *f) { (void)f; }
void vdfs_savestate(FILE *f) { (void)f; }
int i386_dasm_one(char *buffer, uint32_t eip, int addr_size, int op_size) { (void)eip; (void)addr_size; (void)op_size; if (buffer) *buffer = 0; return 1; }
void *al_ustr_append_chr(void *us, int c) { (void)c; return us; }
void tapenoise_motorchange(int stat) { (void)stat; }
int mcount = 6;
