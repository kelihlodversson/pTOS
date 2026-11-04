/* Pixel oracle kept separate from the normal GEM application headers. */
#ifndef GEM_DRAW_PROBE_H
#define GEM_DRAW_PROBE_H
#include "portab.h"
LONG gem_probe_open(WORD *handle, WORD *width, WORD *height);
LONG gem_probe_close(WORD handle);
LONG gem_probe_cell(WORD handle, WORD *width, WORD *height);
int gem_probe_pixel(WORD handle, WORD x, WORD y);
LONG gem_probe_update_begin(void);
LONG gem_probe_update_end(LONG window);
LONG gem_probe_swatch(WORD handle, WORD x, WORD y, WORD pen, WORD *pixel);
void v_opnvwk(WORD *input, WORD *handle, WORD *output);
void v_clsvwk(WORD handle);
#endif
