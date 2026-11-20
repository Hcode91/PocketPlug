/* PocketPlug - which core's bus the shared Musashi 68k talks to (MIT) */
#ifndef POCKETPLUG_M68K_BUS_H
#define POCKETPLUG_M68K_BUS_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct M68kBus {
    unsigned int (*r8)(unsigned int a);
    unsigned int (*r16)(unsigned int a);
    unsigned int (*r32)(unsigned int a);
    void (*w8)(unsigned int a, unsigned int v);
    void (*w16)(unsigned int a, unsigned int v);
    void (*w32)(unsigned int a, unsigned int v);
    int (*intAck)(int level);   /* NULL = autovector */
} M68kBus;
/* NULL selects FreeBee's (the UNIX PC) */
void m68k_bus_set(const M68kBus* bus);
#ifdef __cplusplus
}
#endif
#endif
