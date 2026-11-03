/* Native frame slider rectangles and gr_slidebox proposal geometry. */
#ifndef GEMSLIDEGEOM_H
#define GEMSLIDEGEOM_H
#include "gemframe.h"
typedef struct { GRECT track, thumb; BOOL vertical; } WM_SLIDER_GEOMETRY;
BOOL wm_slider_geometry(const WM_FRAME_STATE *state, WORD object, WM_SLIDER_GEOMETRY *out);
BOOL wm_slider_position(const WM_SLIDER_GEOMETRY *slider, LONG press_x, LONG press_y,
                        LONG x, LONG y, WORD *value, GRECT *outline);
WORD wm_slider_page(const WM_FRAME_STATE *state, WORD object, LONG x, LONG y);
#endif
