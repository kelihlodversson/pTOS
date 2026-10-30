/* Native rubber-window dimensions with frame admission and WORD bounds. */
#ifndef GEMSIZEGEOM_H
#define GEMSIZEGEOM_H
#include "portab.h"
#include "grect.h"
BOOL wm_size_position(const GRECT *start, UWORD kind, LONG x, LONG y,
                      WORD wchar, WORD hchar, WORD wbox, WORD hbox,
                      WORD screen_width, WORD screen_height, GRECT *result);
#endif
