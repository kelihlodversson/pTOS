/* Popup interaction derived from handle_popup()/setup_moblk() in gemmnext.c.
 * Copyright 1999, Caldera Thin Clients, Inc.
 * Copyright 2002-2026 The EmuTOS development team. GPL version 2 or later. */
#include "gemmenupopup.h"
#include "aesdefs.h"

static WORD selectable(OBJECT *tree, WORD root, WORD item)
{
    return item >= 0 && item != root && !(tree[item].ob_state & DISABLED) && !(tree[item].ob_flags & HIDETREE) ? item : NIL;
}

WORD mn_popup_track(OBJECT *tree, WORD root, WORD start, const POPUP_TRACK_OPS *ops,
                     void *context, WORD *modifiers, BOOL *ok)
{
    WORD x, y, item, active, next, events, mouse[4], result = NIL;
    BOOL outside;
    GRECT rect;

    *ok = FALSE; *modifiers = 0;
    active = selectable(tree,root,start);
    /* Match native GEM's initially highlighted starting item. */
    if (active != NIL) tree[active].ob_state |= SELECTED;
    if (!ops->draw(context,tree,root) || !ops->pointer(context,&x,&y)) goto done;
    for (;;) {
        item = ops->find(context,tree,root,x,y);
        outside = TRUE;
        if (item < 0) {
            outside = FALSE;
            if (!ops->rect(context,tree,root,&rect)) goto done;
        } else {
            /* Any motion can change the topmost hit in overlapping siblings
             * (including expanded 3D rows), even inside the old hit area. */
            rect.g_x = x; rect.g_y = y; rect.g_w = rect.g_h = 1;
        }
        events = ops->wait(context,outside,&rect,mouse);
        if (!(events & (MU_M1 | MU_BUTTON))) goto done;
        x = mouse[0]; y = mouse[1];
        *modifiers = mouse[3];
        /* A click can arrive without a separate motion event. Its trusted
         * coordinates decide selection, including outside/disabled clicks. */
        next = selectable(tree,root,ops->find(context,tree,root,x,y));
        if (next != active) {
            if (active != NIL && !ops->change(context,tree,active,tree[active].ob_state & ~SELECTED)) goto done;
            active = NIL;
            if (next != NIL) {
                if (!ops->change(context,tree,next,tree[next].ob_state | SELECTED)) goto done;
                active = next;
            }
        }
        if (events & MU_BUTTON) {
            result = next;
            *ok = TRUE;
            break;
        }
    }
done:
    /* Never index NIL on outside cancellation. The display adapter restores
     * the saved background; removing transient selection needs no redraw. */
    if (active != NIL) tree[active].ob_state &= ~SELECTED;
    if (!*ok) *modifiers = 0;
    return result;
}
