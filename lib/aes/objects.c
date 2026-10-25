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
