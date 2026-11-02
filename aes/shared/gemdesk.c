/* Desk menu construction derived from the original menu_fixup().
 * Copyright 1999, Caldera Thin Clients, Inc.
 * Copyright 2002-2026 The EmuTOS development team. GPL version 2 or later. */
#include "gemmenubar.h"

/* Desk menu construction shared with the original menu_fixup(). */
void mn_desk_items(OBJECT *tree, WORD box, WORD about, WORD separator,
                   const WORD *items, const char *const *labels, WORD count, WORD hchar)
{
    WORD i, previous = about;
    tree[box].ob_head = about;
    if (count) {
        tree[previous].ob_next = separator;
        previous = separator;
        for (i = 0; i < count; i++) {
            tree[previous].ob_next = items[i];
            tree[items[i]].ob_spec.free_string = (char *)labels[i];
            previous = items[i];
        }
    }
    tree[previous].ob_next = box;
    tree[box].ob_tail = previous;
    tree[box].ob_height = (count ? count+2 : 1)*hchar;
}

