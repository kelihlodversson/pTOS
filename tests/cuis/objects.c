/* GEM object contracts run unchanged on the host and inside ring 3. */
#include "gem.h"
int libaes_object_checks(void);

static void fixture(OBJECT *tree)
{
    static const OBJECT initial[6] = {
        { -1, 1, 3, G_IBOX, 0, 0, { 0 }, 10, 20, 200, 150 },
        { 2, -1, -1, G_STRING, 0, 0, { 1 }, 5, 6, 10, 10 },
        { 3, 4, 5, G_BOX, 0, 0, { 0 }, 20, 30, 80, 60 },
        { 0, -1, -1, G_BOX, 0, 0, { 0 }, 30, 40, 100, 80 },
        { 5, -1, -1, G_USERDEF, 0, 0, { 1 }, -2, -3, 10, 10 },
        { 2, -1, -1, G_STRING, OF_LASTOB, 0, { 1 }, 3, 4, 10, 10 }
    };
    int i;
    for (i = 0; i < 6; i++) tree[i] = initial[i];
}
#define OBJECT_CHECK(condition) do { if (!(condition)) return 0x2000 | __LINE__; } while (0)
int libaes_object_checks(void)
{
    OBJECT tree[6];
    OBJECT only[2] = {
        { -1, 1, 1, G_IBOX, 0, 0, { 0 }, 0, 0, 100, 100 },
        { 0, -1, -1, G_BOX, OF_LASTOB, 0, { 0 }, 0, 0, 10, 10 }
    };
    static OBJECT wide[257];
    short x, y, i;
    /* A wide list exercises first/last matches, overlapping siblings and
     * hidden candidates without a backwards predecessor scan. */
    wide[0].ob_head = 1;
    wide[0].ob_tail = 256;
    wide[0].ob_width = wide[0].ob_height = 100;
    for (i = 1; i <= 256; i++) {
        wide[i].ob_next = i == 256 ? 0 : i + 1;
        wide[i].ob_head = wide[i].ob_tail = -1;
        wide[i].ob_x = i == 1 ? 0 : 50;
        wide[i].ob_width = wide[i].ob_height = 10;
    }
    OBJECT_CHECK(objc_find(wide, 0, 1, 1, 1) == 1);
    OBJECT_CHECK(objc_find(wide, 0, 1, 51, 1) == 256);
    wide[256].ob_flags = OF_HIDETREE;
    OBJECT_CHECK(objc_find(wide, 0, 1, 51, 1) == 255);
    OBJECT_CHECK(objc_find(wide, 0, 32767, 30, 30) == 0);
    fixture(tree);
    /* Root/subtree origins, depth and half-open edges. */
    OBJECT_CHECK(objc_find(tree, 0, 0, 10, 20) == 0);
    OBJECT_CHECK(objc_find(tree, 0, 8, 9, 20) == -1);
    OBJECT_CHECK(objc_find(tree, 0, 8, 210, 20) == -1);
    OBJECT_CHECK(objc_find(tree, 0, 8, 10, 170) == -1);
    OBJECT_CHECK(objc_find(tree, 0, 8, 15, 26) == 1);
    OBJECT_CHECK(objc_find(tree, 0, 8, 25, 26) == 0);
    OBJECT_CHECK(objc_find(tree, 0, 1, 34, 55) == 2);
    OBJECT_CHECK(objc_find(tree, 0, 8, 34, 55) == 5);
    OBJECT_CHECK(objc_find(tree, 2, 0, 34, 55) == 2);
    OBJECT_CHECK(objc_find(tree, 2, 1, 34, 55) == 5);
    OBJECT_CHECK(objc_find(tree, 0, 8, 45, 65) == 3);
    tree[3].ob_flags |= OF_HIDETREE;
    OBJECT_CHECK(objc_find(tree, 0, 8, 45, 65) == 2);
    tree[2].ob_flags |= OF_HIDETREE;
    OBJECT_CHECK(objc_find(tree, 0, 8, 34, 55) == 0);
    tree[0].ob_flags |= OF_HIDETREE;
    OBJECT_CHECK(objc_find(tree, 0, 8, 34, 55) == -1);
    fixture(tree);
    OBJECT_CHECK(objc_find(tree, -1, 8, 10, 20) == -1);
    OBJECT_CHECK(objc_find(tree, 0, -1, 10, 20) == -1);
    /* Visual states do not make an otherwise visible object unclickable. */
    tree[5].ob_state = OS_DISABLED | OS_SHADOWED;
    OBJECT_CHECK(objc_find(tree, 0, 8, 34, 55) == 5);
    fixture(tree);
    OBJECT_CHECK(mt_objc_find(tree, 0, 32767, 34, 55, 0) == 5);
    OBJECT_CHECK(tree[4].ob_spec.index == 1 && tree[4].ob_type == G_USERDEF);
    OBJECT_CHECK(mt_objc_offset(tree, 4, &x, &y, 0) == 1 && x == 28 && y == 47);
    OBJECT_CHECK(mt_objc_delete(tree, 3, 0) == 1);
    OBJECT_CHECK(mt_objc_add(tree, 0, 3, 0) == 1);
    OBJECT_CHECK(mt_objc_order(tree, 3, 0, 0) == 1);
    OBJECT_CHECK(objc_find(tree, 0, 8, 45, 65) == 2);
    fixture(tree);
    OBJECT_CHECK(objc_offset(tree, 0, &x, &y) == 1 && x == 10 && y == 20);
    OBJECT_CHECK(objc_offset(tree, 4, &x, &y) == 1 && x == 28 && y == 47);
    OBJECT_CHECK(objc_offset(tree, 1, &tree[1].ob_x, &tree[1].ob_y) == 1);
    OBJECT_CHECK(tree[1].ob_x == 15 && tree[1].ob_y == 26);
    fixture(tree);
    OBJECT_CHECK(objc_offset(tree, 4, &tree[0].ob_x, &tree[0].ob_y) == 1);
    OBJECT_CHECK(tree[0].ob_x == 28 && tree[0].ob_y == 47);
    fixture(tree);
    OBJECT_CHECK(objc_offset(tree, 4, &x, &x) == 1 && x == 47);
    OBJECT_CHECK(objc_delete(tree, 2) == 1);
    OBJECT_CHECK(tree[0].ob_head == 1 && tree[0].ob_tail == 3 && tree[1].ob_next == 3);
    OBJECT_CHECK(tree[2].ob_head == 4 && tree[2].ob_tail == 5 && tree[5].ob_next == 2);
    OBJECT_CHECK(objc_add(tree, 3, 2) == 1);
    OBJECT_CHECK(tree[3].ob_head == 2 && tree[3].ob_tail == 2 && tree[2].ob_next == 3);
    OBJECT_CHECK(objc_offset(tree, 4, &x, &y) == 1 && x == 58 && y == 87);
    OBJECT_CHECK(tree[4].ob_spec.index == 1 && tree[4].ob_type == G_USERDEF);
    fixture(tree);
    OBJECT_CHECK(objc_order(tree, 3, 0) == 1);
    OBJECT_CHECK(tree[0].ob_head == 3 && tree[0].ob_tail == 2 &&
          tree[3].ob_next == 1 && tree[1].ob_next == 2 && tree[2].ob_next == 0);
    OBJECT_CHECK(objc_order(tree, 3, -1) == 1);
    OBJECT_CHECK(tree[0].ob_head == 1 && tree[0].ob_tail == 3 && tree[2].ob_next == 3);
    OBJECT_CHECK(objc_order(tree, 1, 1) == 1);
    OBJECT_CHECK(tree[0].ob_head == 2 && tree[2].ob_next == 1 && tree[1].ob_next == 3);
    OBJECT_CHECK(objc_order(tree, 1, 2) == 1);
    OBJECT_CHECK(tree[0].ob_tail == 1 && tree[3].ob_next == 1 && tree[1].ob_next == 0);
    OBJECT_CHECK(objc_order(tree, 1, -2) == 0 && objc_order(tree, 1, 3) == 0);
    OBJECT_CHECK(tree[0].ob_head == 2 && tree[0].ob_tail == 1 &&
          tree[2].ob_next == 3 && tree[3].ob_next == 1 && tree[1].ob_next == 0);
    OBJECT_CHECK(tree[2].ob_head == 4 && tree[2].ob_tail == 5);
    OBJECT_CHECK(objc_delete(tree, 2) == 1 && tree[0].ob_head == 3);
    OBJECT_CHECK(objc_delete(tree, 1) == 1 && tree[0].ob_tail == 3 && tree[3].ob_next == 0);
    OBJECT_CHECK(objc_delete(tree, 3) == 1 && tree[0].ob_head == -1 && tree[0].ob_tail == -1);
    OBJECT_CHECK(objc_add(tree, 0, 3) == 1 && tree[0].ob_head == 3 && tree[0].ob_tail == 3);
    OBJECT_CHECK(objc_order(only, 1, 0) == 1);
    OBJECT_CHECK(only[0].ob_head == 1 && only[0].ob_tail == 1 && only[1].ob_next == 0);
    OBJECT_CHECK(objc_order(only, 1, -1) == 1);
    OBJECT_CHECK(only[0].ob_head == 1 && only[0].ob_tail == 1 && only[1].ob_next == 0);
    OBJECT_CHECK(objc_order(only, 1, 1) == 0 && only[0].ob_head == 1 && only[0].ob_tail == 1);
    OBJECT_CHECK(objc_delete(only, 0) == 0 && objc_order(only, 0, 0) == 0);
    OBJECT_CHECK(objc_add(only, -1, 1) == 0 && objc_add(only, 0, 0) == 0);
    x = 12; y = 34;
    OBJECT_CHECK(objc_offset(only, -1, &x, &y) == 0 && x == 12 && y == 34);
    return 0;
}
#ifdef LIBAES_OBJECT_HOST_TEST
int main(void) { return libaes_object_checks() ? 1 : 0; }
#endif
