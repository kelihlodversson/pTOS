/* Scripted popup tracking, independent of display and input services. */
#include "gemmenupopup.h"
#include "aesdefs.h"
#define VERIFY(c) do { if (!(c)) return __LINE__; } while (0)
typedef struct {
    WORD initial_x, initial_y, events[8][5], count, cursor, draws, changes;
    BOOL fail_draw, fail_change, fail_pointer, fail_rect;
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
    (void)context; (void)tree; (void)root;
    if (x < 10 || x >= 90 || y < 10 || y >= 70) return NIL;
    if (x < 20) return 0;
    return y < 40 ? 1 : 2;
}
static BOOL rectangle(void *context, OBJECT *tree, WORD item, GRECT *rect)
{
    SCRIPT *s = context;
    (void)tree;
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
    s.fail_rect = TRUE;
    VERIFY(mn_popup_track(tree,0,1,&ops,&s,&modifiers,&ok) == NIL && !ok && tree[1].ob_state == CHECKED);
    initialise(&s,tree);
    s.fail_change = TRUE;
    s.events[0][0] = MU_M1; s.events[0][1] = 25; s.events[0][2] = 50; s.count = 1;
    VERIFY(mn_popup_track(tree,0,1,&ops,&s,&modifiers,&ok) == NIL && !ok && tree[1].ob_state == CHECKED);
    return 0;
}
