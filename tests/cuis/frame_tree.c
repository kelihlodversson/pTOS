/* Shared native frame construction: independent state and bounded topology. */
#include "gemframe.h"
#include "aesdefs.h"
#define VERIFY(c) do { if (!(c)) return __LINE__; } while (0)
static int walk(OBJECT *tree, WORD obj, ULONG *seen)
{
    WORD child, tail;
    if (obj < 0 || obj >= NUM_ELEM || (*seen & (1UL<<obj))) return 0;
    *seen |= 1UL<<obj;
    child = tree[obj].ob_head;
    if (child == NIL) return tree[obj].ob_tail == NIL;
    tail = NIL;
    while (child != obj) {
        if (!walk(tree,child,seen)) return 0;
        tail = child; child = tree[child].ob_next;
    }
    return tail == tree[obj].ob_tail;
}
int main(void)
{
    WM_FRAME first, second;
    WM_FRAME_STATE state = { { 10,20,320,200 },NAME|INFO,TRUE,19,19,23,23,
                            0,0,-1,-1,"Title","Info",NULL };
    ULONG seen;
    UWORD kind;
    wm_frame_init(&first); wm_frame_init(&second);
    VERIFY(first.tree[W_NAME].ob_spec.tedinfo == &first.name);
    VERIFY(second.tree[W_NAME].ob_spec.tedinfo == &second.name);
    wm_frame_build(first.tree,&first.name,&first.info,&state);
    VERIFY(first.name.te_ptext == state.name && first.info.te_ptext == state.info);
    VERIFY(first.tree[W_BOX].ob_x == 10 && first.tree[W_BOX].ob_y == 20);
    VERIFY(first.tree[W_TITLE].ob_height == 23 && first.tree[W_INFO].ob_y == 23);
    VERIFY(first.tree[W_DATA].ob_y == 42 && first.tree[W_WORK].ob_y == 1);
    VERIFY(first.tree[W_WORK].ob_width == 318 && first.tree[W_WORK].ob_height == 156);
    VERIFY(!second.name.te_ptext && second.tree[W_BOX].ob_head == NIL);
    state.topped = FALSE;
    wm_frame_build(second.tree,&second.name,&second.info,&state);
    VERIFY(second.tree[W_NAME].ob_state == DISABLED && !first.tree[W_NAME].ob_state);
    for (kind = 0; kind < 4096; kind++) {
        state.kind = kind;
        wm_frame_build(first.tree,&first.name,&first.info,&state);
        seen = 0;
        VERIFY(walk(first.tree,ROOT,&seen));
        VERIFY(((seen & (1UL<<W_TITLE)) != 0) == ((kind & (NAME|CLOSER|FULLER|MOVER)) != 0));
        VERIFY(((seen & (1UL<<W_INFO)) != 0) == ((kind & INFO) != 0));
    }
    return 0;
}
