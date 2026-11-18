// PocketPlug: fixed settings in place of FreeBee's TOML configuration file
#ifndef _FBCONFIG_H
#define _FBCONFIG_H
#include <stdbool.h>
#include <string.h>
static inline int fbc_get_int(const char *s, const char *k) { (void)s; return strcmp(k, "volume") == 0 ? 50 : 0; }
static inline bool fbc_get_bool(const char *s, const char *k) { (void)s; (void)k; return false; }
static inline const char *fbc_get_string(const char *s, const char *k) { (void)s; (void)k; return ""; }
static inline double fbc_get_double(const char *s, const char *k) { (void)s; (void)k; return 1.0; }
#endif
