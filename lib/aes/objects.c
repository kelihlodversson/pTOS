/* Ordinary GEM C calls over the application's own OBJECT array.
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later.
 */
#include "libaes.h"
#include "../../aes/gemobjtree.h"

WORD objc_add(OBJECT *tree, WORD parent, WORD child)
{
    if (parent < ROOT || child <= ROOT || parent == child) return 0;
    ob_add(tree, parent, child);
    return 1;
}
WORD objc_delete(OBJECT *tree, WORD object)
{
    if (object <= ROOT) return 0;
    return ob_delete_local(tree, object);
}
WORD objc_order(OBJECT *tree, WORD object, WORD position)
{
    return ob_order(tree, object, position);
}
WORD objc_offset(OBJECT *tree, WORD object, WORD *x, WORD *y)
{
    WORD local_x, local_y;

    if (object < ROOT) return 0;
    ob_offset(tree, object, &local_x, &local_y);
    *x = local_x;
    *y = local_y;
    return 1;
}

/* Search the last sibling first, as GEM draws later siblings on top.
 * Coordinates are logical object bounds; rendering effects are not hit areas.
 * No recursion: a caller-provided depth cannot exhaust the application stack. */
WORD objc_find(OBJECT *tree, WORD start, WORD depth, WORD mx, WORD my)
{
    WORD object = start, found = NIL, x, y, child, previous;
    LONG right, bottom;

    if (start < ROOT || depth < 0) return NIL;
    ob_offset(tree, start, &x, &y);
    for (;;) {
        right = (LONG)x + tree[object].ob_width;
        bottom = (LONG)y + tree[object].ob_height;
        if (!(tree[object].ob_flags & HIDETREE) &&
            mx >= x && my >= y && mx < right && my < bottom) {
            found = object;
            if (!depth || tree[object].ob_tail == NIL) break;
            object = tree[object].ob_tail;
            x += tree[object].ob_x;
            y += tree[object].ob_y;
            depth--;
        } else {
            if (found == NIL) break;
            /* Only siblings inside the last containing parent are candidates.
             * Once one contains the point, its subtree wins over earlier ones. */
            previous = NIL;
            child = tree[found].ob_head;
            while (child != object) {
                previous = child;
                child = tree[child].ob_next;
            }
            if (previous == NIL) break;
            x -= tree[object].ob_x;
            y -= tree[object].ob_y;
            object = previous;
            x += tree[object].ob_x;
            y += tree[object].ob_y;
        }
    }
    return found;
}

WORD mt_objc_add(OBJECT *tree, WORD parent, WORD child, WORD *global)
{
    (void)global;
    return objc_add(tree, parent, child);
}
WORD mt_objc_delete(OBJECT *tree, WORD object, WORD *global)
{
    (void)global;
    return objc_delete(tree, object);
}
WORD mt_objc_order(OBJECT *tree, WORD object, WORD position, WORD *global)
{
    (void)global;
    return objc_order(tree, object, position);
}
WORD mt_objc_offset(OBJECT *tree, WORD object, WORD *x, WORD *y, WORD *global)
{
    (void)global;
    return objc_offset(tree, object, x, y);
}
WORD mt_objc_find(OBJECT *tree, WORD start, WORD depth, WORD mx, WORD my, WORD *global)
{
    (void)global;
    return objc_find(tree, start, depth, mx, my);
}
