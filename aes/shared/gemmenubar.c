/* Standard GEM title/dropdown correspondence, derived from menu_sub().
 * Copyright 1999, Caldera Thin Clients, Inc.
 * Copyright 2002-2026 The EmuTOS development team. GPL version 2 or later. */
#include "gemmenubar.h"

/* Native menu trees reserve objects 0/1/2 for screen/bar/active titles.
 * Unlike the original title-number arithmetic, sibling traversal also
 * handles non-contiguous title and dropdown IDs without quadratic scans. */
#define MENU_BAR 1
#define MENU_ACTIVE 2
static BOOL node(WORD object, WORD count)
{
    return object >= 0 && object < count;
}
static BOOL box(const OBJECT *object)
{
    WORD type = object->ob_type & 255;
    return type == G_BOX || type == G_IBOX;
}
static WORD pairs(const OBJECT *tree, WORD count)
{
    WORD menus, title, menu, title_next, menu_next;
    LONG steps;
    if (!tree || count <= MENU_ACTIVE || tree[0].ob_head != MENU_BAR ||
        tree[MENU_BAR].ob_head != MENU_ACTIVE || tree[MENU_BAR].ob_tail != MENU_ACTIVE ||
        tree[MENU_ACTIVE].ob_next != MENU_BAR) return 0;
    menus = tree[0].ob_tail;
    if (!node(menus,count) || menus <= MENU_ACTIVE || tree[MENU_BAR].ob_next != menus ||
        tree[menus].ob_next != ROOT || !box(tree+menus)) return 0;
    title = tree[MENU_ACTIVE].ob_head; menu = tree[menus].ob_head;
    /* Every pair consumes distinct title/box nodes. The count bound also
     * guarantees termination for malformed cyclic sibling chains. */
    for (steps = 1; steps <= count; steps++) {
        if (!node(title,count) || title <= MENU_ACTIVE || title == menus ||
            !node(menu,count) || menu <= MENU_ACTIVE || menu == menus ||
            (tree[title].ob_type & 255) != G_TITLE || !box(tree+menu) ||
            tree[title].ob_head != NIL || tree[title].ob_tail != NIL) return 0;
        title_next = tree[title].ob_next; menu_next = tree[menu].ob_next;
        if (title_next == MENU_ACTIVE || menu_next == menus) {
            if (title_next != MENU_ACTIVE || menu_next != menus ||
                tree[MENU_ACTIVE].ob_tail != title || tree[menus].ob_tail != menu) return 0;
            return steps;
        }
        title = title_next; menu = menu_next;
    }
    return 0;
}
WORD mn_bar_index(const OBJECT *tree, WORD count, MENU_BAR_ENTRY *output, WORD capacity)
{
    WORD total = pairs(tree,count), title, menu, i;
    if (!total || capacity < 0 || (output && capacity < total)) return 0;
    if (!output) return total;
    title = tree[MENU_ACTIVE].ob_head;
    menu = tree[tree[ROOT].ob_tail].ob_head;
    for (i = 0; i < total; i++) {
        output[i].title = title; output[i].menu = menu;
        title = tree[title].ob_next; menu = tree[menu].ob_next;
    }
    return total;
}

/* A bounded bitset makes each reachable node unique without rescanning
 * parents or siblings. Orphan objects are allowed, as with objc_delete(). */
static BOOL claim(UBYTE *seen, WORD object, WORD count)
{
    UBYTE bit;
    if (!node(object,count)) return FALSE;
    bit = 1U << (object & 7);
    if (seen[object >> 3] & bit) return FALSE;
    seen[object >> 3] |= bit;
    return TRUE;
}
static BOOL text(const OBJECT *object)
{
    OBSPEC spec;
    WORD n;
    /* Native drawing skips hidden objects, including their payloads. */
    if (object->ob_flags & HIDETREE) return TRUE;
    if ((object->ob_flags & INDIRECT) && !object->ob_spec.indirect) return FALSE;
    spec = object->ob_flags & INDIRECT ? *object->ob_spec.indirect : object->ob_spec;
    if (!spec.free_string || spec.index == -1L) return FALSE;
    for (n = 0; n <= 127; n++) if (!spec.free_string[n]) return TRUE;
    return FALSE;
}
static BOOL box_spec(const OBJECT *object)
{
    return (object->ob_flags & HIDETREE) || !(object->ob_flags & INDIRECT) || object->ob_spec.indirect;
}
WORD mn_bar_admit(const OBJECT *tree, WORD count, UBYTE *scratch, ULONG bytes)
{
    WORD total, title, menu, container, i, item, last, type;
    ULONG needed, n;
    if (!scratch || count <= MENU_ACTIVE) return 0;
    needed = ((ULONG)count+7)/8;
    if (bytes < needed || !(total = pairs(tree,count)) ||
        !box(tree) || !box(tree+MENU_BAR) || !box(tree+MENU_ACTIVE) ||
        !box_spec(tree) || !box_spec(tree+MENU_BAR) || !box_spec(tree+MENU_ACTIVE) ||
        !box_spec(tree+tree[ROOT].ob_tail)) return 0;
    for (n = 0; n < needed; n++) scratch[n] = 0;
    container = tree[ROOT].ob_tail;
    claim(scratch,ROOT,count); claim(scratch,MENU_BAR,count);
    claim(scratch,MENU_ACTIVE,count); claim(scratch,container,count);
    title = tree[MENU_ACTIVE].ob_head; menu = tree[container].ob_head;
    for (i = 0; i < total; i++) {
        if (!claim(scratch,title,count) || !claim(scratch,menu,count) || !box_spec(tree+menu) || !text(tree+title) ||
            (tree[title].ob_flags & SUBMENU)) return 0;
        item = tree[menu].ob_head; last = NIL;
        while (item != menu && item != NIL) {
            if (!claim(scratch,item,count)) return 0;
            type = tree[item].ob_type & 255;
            if ((type != G_STRING && type != G_TITLE) || tree[item].ob_head != NIL ||
                tree[item].ob_tail != NIL || (tree[item].ob_flags & SUBMENU) || !text(tree+item)) return 0;
            last = item; item = tree[item].ob_next;
        }
        /* Empty boxes have NIL/NIL children. Nonempty lists must terminate
         * at their parent and agree with its tail, exactly as GEM requires. */
        if (tree[menu].ob_tail != last || (last != NIL && item != menu) ||
            (last == NIL && item != NIL)) return 0;
        title = tree[title].ob_next; menu = tree[menu].ob_next;
    }
    return total;
}
