/* Native GEM mover constraints with wide intermediate coordinates. */
#ifndef GEMMOVEGEOM_H
#define GEMMOVEGEOM_H
#include "portab.h"
#include "grect.h"
BOOL wm_move_position(const GRECT *start, UWORD kind, LONG press_x, LONG press_y,
                      LONG x, LONG y, WORD screen_width, WORD screen_height, WORD wbox, WORD hbox,
                      GRECT *result);
#endif
