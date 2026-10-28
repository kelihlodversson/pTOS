/* Shared caller-local 3D geometry, retaining native GEM expansion rules.
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later. */
#include "gemobjbounds.h"

void ob_3d_bounds(const OBJECT *object, WORD border, BOOL hit,
                  LONG *x, LONG *y, LONG *width, LONG *height)
{
    WORD adjust;
    *width = object->ob_width; *height = object->ob_height;
    /* Native ob_find tests shadowed objects against their logical rectangle. */
    if (!(object->ob_flags & FL3DOBJ) || (hit && (object->ob_state & SHADOWED))) return;
    adjust = ADJ3DSTD;
    if (object->ob_state & OUTLINED) adjust += ADJ3DOUT;
    *x -= adjust; *y -= adjust; *width += 2*adjust; *height += 2*adjust;
    if ((object->ob_state & SHADOWED) && border < 0) {
        *width -= (LONG)border*ADJ3DSHA;
        *height -= (LONG)border*ADJ3DSHA;
    }
}
