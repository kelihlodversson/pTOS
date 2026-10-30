/* Standard GEM VDI pen -> device palette-index ordering. */
#ifndef VDI_COLMAP_H
#define VDI_COLMAP_H
#include "portab.h"
static const WORD vdi_pen_order[16] =
    { 0, 15, 1, 2, 4, 6, 3, 5, 7, 8, 9, 10, 12, 14, 11, 13 };
#endif
