/* Shared native colour-icon resolution selection.
 * Copyright (C) 2002-2026 The EmuTOS development team. GPL version 2 or later. */
#include "gemcicon.h"

/* Exact resolution wins; otherwise choose the largest lower plane count.
 * Ties preserve the original resource order. No suitable entry means mono. */
CICON *rs_cicon_best(CICONBLK *start, WORD planes)
{
    CICON *p, *found = NULL;
    for (p = start->mainlist; p; p = p->next_res) {
        if (p->num_planes > planes) continue;
        if (p->num_planes == planes) return p;
        if (!found || p->num_planes > found->num_planes) found = p;
    }
    return found;
}
