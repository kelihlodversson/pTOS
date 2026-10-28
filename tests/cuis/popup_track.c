/* Scripted popup tracking, independent of display and input services. */
#include "gemmenupopup.h"
#include "gem.h"
#include "gemobjbounds.h"
#include "aesdefs.h"
#define VERIFY(c) do { if (!(c)) return __LINE__; } while (0)
typedef struct {
    WORD initial_x, initial_y, events[8][5], count, cursor, draws, changes;
    BOOL fail_draw, fail_change, fail_pointer, fail_rect, real_hits, hovered_second;
    WORD rectangles;
    GRECT last_rect;
    BOOL last_outside;
} SCRIPT;
static BOOL draw(void *context, OBJECT *tree, WORD root)
{
    SCRIPT *s = context;
    (void)tree; (void)root;
    s->draws++;
    return !s->fail_draw;
}
static BOOL change(void *context, OBJECT *tree, WORD item, UWORD state)
{
    SCRIPT *s = context;
    s->changes++;
    if (s->fail_change) return FALSE;
    tree[item].ob_state = state;
    if (s->real_hits && item == 2 && (state & SELECTED) && s->cursor == 1) s->hovered_second = TRUE;
    return TRUE;
}
static BOOL pointer(void *context, WORD *x, WORD *y)
{
    SCRIPT *s = context;
    *x = s->initial_x; *y = s->initial_y;
    return !s->fail_pointer;
}
static WORD find(void *context, OBJECT *tree, WORD root, WORD x, WORD y)
{
    SCRIPT *s = context;
    if (s->real_hits) return objc_find(tree,root,1,x,y);
    if (x < 10 || x >= 90 || y < 10 || y >= 70) return NIL;
    if (x < 20) return 0;
    return y < 40 ? 1 : 2;
}
static BOOL rectangle(void *context, OBJECT *tree, WORD item, GRECT *rect)
{
    SCRIPT *s = context;
    s->rectangles++;
    if (s->real_hits) {
        WORD x, y;
        LONG bx, by, w, h;
        objc_offset(tree,item,&x,&y); bx = x; by = y;
        ob_3d_bounds(tree+item,0,TRUE,&bx,&by,&w,&h);
        rect->g_x = bx; rect->g_y = by; rect->g_w = w; rect->g_h = h;
        return !s->fail_rect;
    }
    rect->g_x = item ? 20 : 10; rect->g_y = item == 2 ? 40 : 10;
    rect->g_w = item ? 70 : 80; rect->g_h = item ? 30 : 60;
    return !s->fail_rect;
}
static WORD wait_event(void *context, BOOL outside, const GRECT *rect, WORD *mouse)
{
    SCRIPT *s = context;
    WORD *event;
    s->last_rect = *rect; s->last_outside = outside;
    if (s->cursor == s->count) return 0;
    event = s->events[s->cursor++];
    /* A scripted motion must actually satisfy the registered predicate. */
    if (event[0] & MU_M1) {
        BOOL inside = event[1] >= rect->g_x && event[2] >= rect->g_y &&
            (LONG)event[1] < (LONG)rect->g_x+rect->g_w && (LONG)event[2] < (LONG)rect->g_y+rect->g_h;
        if (inside == outside) return 0;
    }
    mouse[0] = event[1]; mouse[1] = event[2]; mouse[2] = event[3]; mouse[3] = event[4];
    return event[0];
}
static const POPUP_TRACK_OPS ops = { draw, change, pointer, find, rectangle, wait_event };
static void initialise(SCRIPT *s, OBJECT *tree)
{
    static const SCRIPT empty = { 0 };
    static const OBJECT objects[3] = { { 0 } };
    int i;
    *s = empty;
    for (i = 0; i < 3; i++) tree[i] = objects[i];
    tree[1].ob_state = CHECKED;
    s->initial_x = 25; s->initial_y = 20;
}
int main(void)
{
    SCRIPT s;
    OBJECT tree[3];
    WORD modifiers, result;
    BOOL ok;
    initialise(&s,tree);
    s.events[0][0] = MU_BUTTON; s.events[0][1] = 25; s.events[0][2] = 50;
    s.events[0][3] = 1; s.events[0][4] = 3; s.count = 1;
    result = mn_popup_track(tree,0,1,&ops,&s,&modifiers,&ok);
    VERIFY(ok && result == 2 && modifiers == 3 && s.changes == 2);
    VERIFY(tree[1].ob_state == CHECKED && !tree[2].ob_state);
    initialise(&s,tree);
    s.events[0][0] = MU_BUTTON; s.events[0][1] = 100; s.count = 1;
    VERIFY(mn_popup_track(tree,0,1,&ops,&s,&modifiers,&ok) == NIL && ok);
    VERIFY(tree[1].ob_state == CHECKED && !tree[2].ob_state);
    initialise(&s,tree);
    tree[2].ob_state = DISABLED;
    s.events[0][0] = MU_BUTTON; s.events[0][1] = 25; s.events[0][2] = 50; s.count = 1;
    VERIFY(mn_popup_track(tree,0,1,&ops,&s,&modifiers,&ok) == NIL && ok);
    VERIFY(tree[1].ob_state == CHECKED && tree[2].ob_state == DISABLED);
    initialise(&s,tree);
    tree[1].ob_state |= DISABLED;
    s.events[0][0] = MU_BUTTON; s.events[0][1] = 25; s.events[0][2] = 20; s.count = 1;
    VERIFY(mn_popup_track(tree,0,1,&ops,&s,&modifiers,&ok) == NIL && ok);
    VERIFY(tree[1].ob_state == (CHECKED|DISABLED));
    initialise(&s,tree);
    tree[1].ob_flags = HIDETREE;
    s.events[0][0] = MU_BUTTON; s.events[0][1] = 25; s.events[0][2] = 20; s.count = 1;
    VERIFY(mn_popup_track(tree,0,1,&ops,&s,&modifiers,&ok) == NIL && ok);
    VERIFY(tree[1].ob_state == CHECKED);
    initialise(&s,tree);
    s.events[0][0] = MU_BUTTON; s.events[0][1] = 15; s.events[0][2] = 15; s.count = 1;
    VERIFY(mn_popup_track(tree,0,1,&ops,&s,&modifiers,&ok) == NIL && ok);
    VERIFY(tree[1].ob_state == CHECKED && !tree[0].ob_state);
    initialise(&s,tree);
    s.initial_x = 15; s.initial_y = 15;
    s.events[0][0] = MU_M1; s.events[0][1] = 25; s.events[0][2] = 20;
    s.events[1][0] = MU_BUTTON; s.events[1][1] = 25; s.events[1][2] = 20; s.count = 2;
    VERIFY(mn_popup_track(tree,0,1,&ops,&s,&modifiers,&ok) == 1 && ok);
    initialise(&s,tree);
    s.initial_x = s.initial_y = 15;
    VERIFY(mn_popup_track(tree,0,1,&ops,&s,&modifiers,&ok) == NIL && !ok);
    VERIFY(s.last_outside && s.last_rect.g_w == 1 && s.last_rect.g_h == 1);
    VERIFY(tree[1].ob_state == CHECKED && modifiers == 0);
    initialise(&s,tree);
    s.fail_draw = TRUE;
    VERIFY(mn_popup_track(tree,0,1,&ops,&s,&modifiers,&ok) == NIL && !ok && tree[1].ob_state == CHECKED);
    initialise(&s,tree);
    s.fail_pointer = TRUE;
    VERIFY(mn_popup_track(tree,0,1,&ops,&s,&modifiers,&ok) == NIL && !ok && tree[1].ob_state == CHECKED);
    initialise(&s,tree);
    s.fail_rect = TRUE; s.initial_x = 100;
    VERIFY(mn_popup_track(tree,0,1,&ops,&s,&modifiers,&ok) == NIL && !ok && tree[1].ob_state == CHECKED);
    VERIFY(s.rectangles == 1);
    initialise(&s,tree);
    s.fail_change = TRUE;
    s.events[0][0] = MU_M1; s.events[0][1] = 25; s.events[0][2] = 50; s.count = 1;
    VERIFY(mn_popup_track(tree,0,1,&ops,&s,&modifiers,&ok) == NIL && !ok && tree[1].ob_state == CHECKED);
    initialise(&s,tree);
    s.real_hits = TRUE; s.initial_x = 25; s.initial_y = 40;
    tree[0].ob_next = NIL; tree[0].ob_head = 1; tree[0].ob_tail = 2;
    tree[0].ob_type = G_IBOX; tree[0].ob_x = 20; tree[0].ob_y = 30;
    tree[0].ob_width = 80; tree[0].ob_height = 40;
    tree[1].ob_next = 2; tree[2].ob_next = 0;
    tree[1].ob_head = tree[1].ob_tail = tree[2].ob_head = tree[2].ob_tail = NIL;
    tree[1].ob_type = tree[2].ob_type = G_STRING;
    tree[1].ob_flags = tree[2].ob_flags = FL3DIND;
    tree[1].ob_width = tree[2].ob_width = 80;
    tree[1].ob_height = tree[2].ob_height = 20; tree[2].ob_y = 20;
    VERIFY(objc_find(tree,0,1,25,40) == 1 && objc_find(tree,0,1,25,49) == 2);
    s.events[0][0] = MU_M1; s.events[0][1] = 25; s.events[0][2] = 49;
    s.events[1][0] = MU_BUTTON; s.events[1][1] = 25; s.events[1][2] = 49; s.count = 2;
    VERIFY(mn_popup_track(tree,0,1,&ops,&s,&modifiers,&ok) == 2 && ok && s.changes == 2 && s.hovered_second);
    VERIFY(tree[1].ob_state == CHECKED && !tree[2].ob_state);
    return 0;
}
