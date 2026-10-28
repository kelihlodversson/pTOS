/* Wide-intermediate counterpart of native ob_actxywh's 3D adjustment. */
#ifndef GEMOBJBOUNDS_H
#define GEMOBJBOUNDS_H
#include "portab.h"
#include "obdefs.h"
void ob_3d_bounds(const OBJECT *object, WORD border, BOOL hit,
                  LONG *x, LONG *y, LONG *width, LONG *height);
#endif
