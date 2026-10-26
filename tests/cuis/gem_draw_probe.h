/* Pixel oracle kept separate from the normal GEM application headers. */
#ifndef GEM_DRAW_PROBE_H
#define GEM_DRAW_PROBE_H
#include "portab.h"
LONG gem_probe_open(WORD *handle, WORD *width, WORD *height);
LONG gem_probe_close(WORD handle);
int gem_probe_pixel(WORD handle, WORD x, WORD y);
LONG gem_probe_update_begin(void);
LONG gem_probe_update_end(LONG window);
#endif
