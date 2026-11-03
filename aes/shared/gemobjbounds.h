/* Wide-intermediate counterpart of native ob_actxywh's 3D adjustment. */
#ifndef GEMOBJBOUNDS_H
#define GEMOBJBOUNDS_H
#include "portab.h"
#include "obdefs.h"
/* WORD geometry scratch margin used by the shared 3D renderer. */
static inline LONG ob_3d_padding(WORD border)
{
    return 8+3L*(border < 0 ? -(LONG)border : border);
}
void ob_3d_bounds(const OBJECT *object, WORD border, BOOL hit,
                  LONG *x, LONG *y, LONG *width, LONG *height);
#endif
