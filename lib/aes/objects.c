/* Ordinary GEM C calls over the application's own OBJECT array.
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later.
 */
#include "libaes.h"
#include "../../aes/shared/gemobjbounds.h"
#include "../../aes/shared/gemobjtree.h"

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

static BOOL object_contains(const OBJECT *object, WORD x, WORD y, WORD mx, WORD my)
{
    LONG bx = x, by = y, width, height;
    ob_3d_bounds(object,0,TRUE,&bx,&by,&width,&height);
    return !(object->ob_flags & HIDETREE) && mx >= bx && my >= by &&
           mx < bx+width && my < by+height;
}

/* Scan siblings forward once and descend into the last matching child:
 * later siblings are drawn on top. No recursion or per-object scratch space.
 * Native 3D/outline expansion applies; shadowed objects keep logical hits. */
WORD objc_find(OBJECT *tree, WORD start, WORD depth, WORD mx, WORD my)
{
    WORD object = start, child, found, x, y, child_x, child_y;

    if (start < ROOT || depth < 0) return NIL;
    ob_offset(tree, start, &x, &y);
    if (!object_contains(&tree[object], x, y, mx, my)) return NIL;
    while (depth && tree[object].ob_head != NIL) {
        found = NIL;
        for (child = tree[object].ob_head; child != object; child = tree[child].ob_next) {
            child_x = x + tree[child].ob_x;
            child_y = y + tree[child].ob_y;
            if (object_contains(&tree[child], child_x, child_y, mx, my)) found = child;
        }
        if (found == NIL) break;
        object = found;
        x += tree[object].ob_x;
        y += tree[object].ob_y;
        depth--;
    }
    return object;
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
