/* Shared native colour-icon resolution selection.
 * Copyright (C) 2002-2026 The EmuTOS development team. GPL version 2 or later. */
#include "gemcicon.h"

/* Exact resolution wins; otherwise choose the largest lower plane count.
 * Ties preserve the original resource order. No suitable entry means mono.
 * GEM variants have positive plane counts. Tracking the best count avoids a
 * pointer dereference/null check and a separate exact-match return path. */
CICON *rs_cicon_best(CICONBLK *start, WORD planes)
{
    CICON *p, *found = NULL;
    WORD best = 0;
    for (p = start->mainlist; p; p = p->next_res) {
        WORD count = p->num_planes;
        if (count > planes || count <= best) continue;
        found = p;
        best = count;
    }
    return found;
}
