/* Bounded rectangle subtraction for kernel CUIS visible-region snapshots. */
#include "emutos.h"
#include "cuis_region.h"
#include "gemerror.h"

static BOOL append_rect(CUIS_RECT *output, ULONG capacity, ULONG *count,
                        LONG x, LONG y, LONG width, LONG height)
{
    CUIS_RECT *rect;
    if (!width || !height) return TRUE;
    if (*count == capacity) return FALSE;
    rect = &output[(*count)++];
    rect->x = x; rect->y = y;
    rect->width = width; rect->height = height;
    return TRUE;
}

/* Inputs are validated window rectangles or subsets of them: positive
 * extents and representable exclusive edges. Pieces are disjoint subsets,
 * so differences cannot exceed the original signed 32-bit extents. */
LONG cuis_region_subtract(const CUIS_RECT *input, ULONG count,
                          const CUIS_RECT *cover, CUIS_RECT *output,
                          ULONG capacity)
{
    ULONG i, used = 0;
    LONG right = cover->x + cover->width, bottom = cover->y + cover->height;
    for (i = 0; i < count; i++) {
        const CUIS_RECT *r = &input[i];
        LONG rr = r->x + r->width, rb = r->y + r->height;
        LONG x = r->x > cover->x ? r->x : cover->x;
        LONG y = r->y > cover->y ? r->y : cover->y;
        LONG end_x = rr < right ? rr : right;
        LONG end_y = rb < bottom ? rb : bottom;
        if (x >= end_x || y >= end_y) {
            if (!append_rect(output, capacity, &used, r->x, r->y,
                             r->width, r->height)) return ENSMEM;
            continue;
        }
        if (!append_rect(output, capacity, &used, r->x, r->y, r->width, y - r->y) ||
            !append_rect(output, capacity, &used, r->x, end_y, r->width, rb - end_y) ||
            !append_rect(output, capacity, &used, r->x, y, x - r->x, end_y - y) ||
            !append_rect(output, capacity, &used, end_x, y, rr - end_x, end_y - y))
            return ENSMEM;
    }
    return (LONG)used;
}
