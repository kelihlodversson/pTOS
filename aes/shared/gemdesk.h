/* Desk menu construction derived from the original menu_fixup().
 * Copyright 1999, Caldera Thin Clients, Inc.
 * Copyright 2002-2026 The EmuTOS development team. GPL version 2 or later. */
#ifndef GEMDESK_H
#define GEMDESK_H
#include "portab.h"
#include "obdefs.h"

/* Desk menu construction shared with the original menu_fixup(). Inlining
 * lets the fixed native layout avoid argument/frame overhead in 256 KB ROMs.
 * The accessory objects immediately follow the separator; labels may have
 * empty slots, while the linked menu items are compact. Indices and storage
 * have already been admitted by the caller. */
static __inline__ __attribute__((always_inline)) void mn_desk_items(OBJECT *tree, WORD box, WORD about, WORD separator,
                   const char *const *labels, WORD slots, WORD hchar)
{
    WORD count = 0, first = separator+1;
    OBJECT *parent = tree+box, *previous = tree+about, *item = tree+first;
    const char *label;
    parent->ob_head = about;
    while (slots-- > 0) {
        label = *labels++;
        if (!label) continue;
        if (!count) {
            previous->ob_next = separator;
            previous = tree+separator;
        }
        previous->ob_next = first++;
        item->ob_spec.free_string = (char *)label;
        previous = item++;
        count++;
    }
    previous->ob_next = box;
    parent->ob_tail = count ? first-1 : about;
    parent->ob_height = (count ? count+2 : 1)*hchar;
}

#endif
