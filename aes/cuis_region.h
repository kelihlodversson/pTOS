/* Native 32-bit geometry, independent of GEM and user address layouts. */
#ifndef CUIS_REGION_H
#define CUIS_REGION_H
#include "portab.h"
#define CUIS_CLIP_LIMIT 256UL
typedef struct {
    LONG x, y, width, height;
} CUIS_RECT;
LONG cuis_region_subtract(const CUIS_RECT *input, ULONG count,
                          const CUIS_RECT *cover, CUIS_RECT *output,
                          ULONG capacity);
#endif
