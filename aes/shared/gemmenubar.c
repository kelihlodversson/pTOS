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
