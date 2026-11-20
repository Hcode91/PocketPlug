/* PocketPlug - routes Musashi's bus callbacks to the running core (MIT) */
#include "m68k_bus.h"
#include "musashi/m68k.h"

uint32_t freebee_read_memory_32(uint32_t a);
uint32_t freebee_read_memory_16(uint32_t a);
uint32_t freebee_read_memory_8(uint32_t a);
void freebee_write_memory_32(uint32_t a, uint32_t v);
void freebee_write_memory_16(uint32_t a, uint32_t v);
void freebee_write_memory_8(uint32_t a, uint32_t v);

static unsigned int fb_r8(unsigned int a) { return freebee_read_memory_8(a); }
static unsigned int fb_r16(unsigned int a) { return freebee_read_memory_16(a); }
static unsigned int fb_r32(unsigned int a) { return freebee_read_memory_32(a); }
static void fb_w8(unsigned int a, unsigned int v) { freebee_write_memory_8(a, v); }
static void fb_w16(unsigned int a, unsigned int v) { freebee_write_memory_16(a, v); }
static void fb_w32(unsigned int a, unsigned int v) { freebee_write_memory_32(a, v); }

static const M68kBus freebee_bus = { fb_r8, fb_r16, fb_r32, fb_w8, fb_w16, fb_w32, 0 };
static const M68kBus* bus = &freebee_bus;

void m68k_bus_set(const M68kBus* b) { bus = b ? b : &freebee_bus; }

unsigned int m68k_read_memory_8(unsigned int a) { return bus->r8(a); }
unsigned int m68k_read_memory_16(unsigned int a) { return bus->r16(a); }
unsigned int m68k_read_memory_32(unsigned int a) { return bus->r32(a); }
void m68k_write_memory_8(unsigned int a, unsigned int v) { bus->w8(a, v); }
void m68k_write_memory_16(unsigned int a, unsigned int v) { bus->w16(a, v); }
void m68k_write_memory_32(unsigned int a, unsigned int v) { bus->w32(a, v); }

int pocketplug_m68k_int_ack(int level)
{
    return bus->intAck ? bus->intAck(level) : (int)M68K_INT_ACK_AUTOVECTOR;
}
