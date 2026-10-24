/* GEM object contracts run unchanged on the host and inside ring 3. */
#include "libaes.h"
int libaes_object_checks(void);

static void fixture(OBJECT *tree)
{
    static const OBJECT initial[6] = {
        { -1, 1, 3, G_IBOX, 0, 0, { 0 }, 10, 20, 200, 150 },
        { 2, -1, -1, G_STRING, 0, 0, { 1 }, 5, 6, 10, 10 },
        { 3, 4, 5, G_BOX, 0, 0, { 0 }, 20, 30, 80, 60 },
        { 0, -1, -1, G_BOX, 0, 0, { 0 }, 30, 40, 100, 80 },
        { 5, -1, -1, G_USERDEF, 0, 0, { 1 }, -2, -3, 10, 10 },
        { 2, -1, -1, G_STRING, LASTOB, 0, { 1 }, 3, 4, 10, 10 }
    };
    int i;
    for (i = 0; i < 6; i++) tree[i] = initial[i];
}
#define CHECK(condition) do { if (!(condition)) return 0x2000 | __LINE__; } while (0)
int libaes_object_checks(void)
{
    OBJECT tree[6];
    OBJECT only[2] = {
        { -1, 1, 1, G_IBOX, 0, 0, { 0 }, 0, 0, 100, 100 },
        { 0, -1, -1, G_BOX, LASTOB, 0, { 0 }, 0, 0, 10, 10 }
    };
    WORD x, y;
    fixture(tree);
    CHECK(objc_offset(tree, 0, &x, &y) == 1 && x == 10 && y == 20);
    CHECK(objc_offset(tree, 4, &x, &y) == 1 && x == 28 && y == 47);
    CHECK(objc_delete(tree, 2) == 1);
    CHECK(tree[0].ob_head == 1 && tree[0].ob_tail == 3 && tree[1].ob_next == 3);
    CHECK(tree[2].ob_head == 4 && tree[2].ob_tail == 5 && tree[5].ob_next == 2);
    CHECK(objc_add(tree, 3, 2) == 1);
    CHECK(tree[3].ob_head == 2 && tree[3].ob_tail == 2 && tree[2].ob_next == 3);
    CHECK(objc_offset(tree, 4, &x, &y) == 1 && x == 58 && y == 87);
    CHECK(tree[4].ob_spec.index == 1 && tree[4].ob_type == G_USERDEF);
    fixture(tree);
    CHECK(objc_order(tree, 3, 0) == 1);
    CHECK(tree[0].ob_head == 3 && tree[0].ob_tail == 2 &&
          tree[3].ob_next == 1 && tree[1].ob_next == 2 && tree[2].ob_next == 0);
    CHECK(objc_order(tree, 3, -1) == 1);
    CHECK(tree[0].ob_head == 1 && tree[0].ob_tail == 3 && tree[2].ob_next == 3);
    CHECK(objc_order(tree, 1, 1) == 1);
    CHECK(tree[0].ob_head == 2 && tree[2].ob_next == 1 && tree[1].ob_next == 3);
    CHECK(objc_order(tree, 1, 2) == 1);
    CHECK(tree[0].ob_tail == 1 && tree[3].ob_next == 1 && tree[1].ob_next == 0);
    CHECK(objc_order(tree, 1, -2) == 0 && objc_order(tree, 1, 3) == 0);
    CHECK(tree[0].ob_head == 2 && tree[0].ob_tail == 1 &&
          tree[2].ob_next == 3 && tree[3].ob_next == 1 && tree[1].ob_next == 0);
    CHECK(tree[2].ob_head == 4 && tree[2].ob_tail == 5);
    CHECK(objc_delete(tree, 2) == 1 && tree[0].ob_head == 3);
    CHECK(objc_delete(tree, 1) == 1 && tree[0].ob_tail == 3 && tree[3].ob_next == 0);
    CHECK(objc_delete(tree, 3) == 1 && tree[0].ob_head == -1 && tree[0].ob_tail == -1);
    CHECK(objc_add(tree, 0, 3) == 1 && tree[0].ob_head == 3 && tree[0].ob_tail == 3);
    CHECK(objc_order(only, 1, 0) == 1);
    CHECK(only[0].ob_head == 1 && only[0].ob_tail == 1 && only[1].ob_next == 0);
    CHECK(objc_order(only, 1, -1) == 1);
    CHECK(only[0].ob_head == 1 && only[0].ob_tail == 1 && only[1].ob_next == 0);
    CHECK(objc_order(only, 1, 1) == 0 && only[0].ob_head == 1 && only[0].ob_tail == 1);
    CHECK(objc_delete(only, 0) == 0 && objc_order(only, 0, 0) == 0);
    CHECK(objc_add(only, -1, 1) == 0 && objc_add(only, 0, 0) == 0);
    x = 12; y = 34;
    CHECK(objc_offset(only, -1, &x, &y) == 0 && x == 12 && y == 34);
    return 0;
}
#ifdef LIBAES_OBJECT_HOST_TEST
int main(void) { return libaes_object_checks() ? 1 : 0; }
#endif
