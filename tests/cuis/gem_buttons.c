/* Standard GEM flags, return values, passive release and mouse ownership.
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later. */
#include <gem.h>
#include "gem_buttons.h"
#define VERIFY(c) do { if (!(c)) return 0x7300 | __LINE__; } while (0)
static OBJECT nested_watch;
static short callback_mode, callback_result;
static short watch_callback(PARMBLK *parameters)
{
    (void)parameters;
    if (callback_mode == 1) callback_result = graf_watchbox(&nested_watch,0,OS_SELECTED,0);
    else aes_global[2] = -1;
    return 0;
}
int gem_button_checks(short graphics)
{
    OBJECT tree[4] = { { 0 } }, watch = { 0 };
    short next, mx, my, buttons, modifiers, global[15], saved_id;
    USERBLK user;
    int i;
    for (i = 0; i < 4; i++) {
        tree[i].ob_next = tree[i].ob_head = tree[i].ob_tail = -1;
        tree[i].ob_type = G_BOX; tree[i].ob_spec.index = 0x1172;
        tree[i].ob_width = tree[i].ob_height = 16;
    }
    tree[0].ob_type = G_IBOX; tree[0].ob_width = 160; tree[0].ob_height = 120;
    for (i = 1; i < 4; i++) VERIFY(objc_add(tree,0,i) == 1);
    for (i = 0; i < 15; i++) global[i] = aes_global[i];
    tree[1].ob_flags = OF_EDITABLE;
    VERIFY(form_button(tree,1,1,&next) == 1 && next == 1);
    tree[1].ob_flags = OF_TOUCHEXIT;
    VERIFY(mt_form_button(tree,1,2,&next,global) == 0 && (unsigned short)next == 0x8001);
    VERIFY(form_button(tree,1,1,&next) == 0 && next == 1);
    tree[1].ob_flags = OF_SELECTABLE|OF_EXIT; tree[1].ob_state = OS_DISABLED;
    VERIFY(form_button(tree,1,1,&next) == 1 && next == 0 && tree[1].ob_state == OS_DISABLED);
    next = 99;
    VERIFY(mt_form_button(tree,1,1,&next,0) == 0 && next == 99);
    VERIFY(form_button(tree,-1,1,&next) == 0 && next == 99);
    VERIFY(form_button(tree,1,1,0) == 0);
    VERIFY(mt_graf_watchbox(tree,1,OS_SELECTED,0,0) == 0);
    tree[1].ob_flags = OF_SELECTABLE|OF_RBUTTON; tree[1].ob_state = 0;
    tree[2].ob_flags = OF_SELECTABLE|OF_RBUTTON; tree[2].ob_state = OS_SELECTED;
    tree[3].ob_flags = OF_SELECTABLE; tree[3].ob_state = OS_SELECTED;
    if (!graphics) {
        VERIFY(form_button(tree,1,1,&next) == 0 && next == 99);
        VERIFY(!tree[1].ob_state && tree[2].ob_state == OS_SELECTED);
        VERIFY(graf_watchbox(tree,1,OS_SELECTED,0) == 0 && !tree[1].ob_state);
        VERIFY(wind_update(BEG_MCTRL|NO_BLOCK) == 1 && wind_update(END_MCTRL) == 1);
        return 0;
    }
    VERIFY(graf_mkstate(&mx,&my,&buttons,&modifiers) == 1 && !buttons);
    for (i = 1; i < 4; i++) { tree[i].ob_x = mx-8; tree[i].ob_y = my-8; }
    VERIFY(form_button(tree,1,1,&next) == 1 && next == 0);
    VERIFY((tree[1].ob_state & OS_SELECTED) && !tree[2].ob_state && tree[3].ob_state == OS_SELECTED);
    tree[1].ob_flags = OF_SELECTABLE|OF_EXIT; tree[1].ob_state = 0;
    VERIFY(mt_form_button(tree,1,1,&next,global) == 0 && next == 1 && (tree[1].ob_state & OS_SELECTED));
    tree[1].ob_flags = OF_SELECTABLE|OF_TOUCHEXIT; tree[1].ob_state = 0;
    VERIFY(form_button(tree,1,2,&next) == 0 && (unsigned short)next == 0x8001);
    watch = tree[1]; watch.ob_flags = 0; watch.ob_state = 0;
    watch.ob_next = watch.ob_head = watch.ob_tail = -1;
    VERIFY(wind_update(BEG_MCTRL) == 1);
    VERIFY(mt_graf_watchbox(&watch,0,OS_SELECTED,0,global) == 1 && watch.ob_state == OS_SELECTED);
    VERIFY(wind_update(END_MCTRL) == 1 && wind_update(END_MCTRL) == 0);
    nested_watch = watch; nested_watch.ob_state = 0;
    user.ub_code = watch_callback; user.ub_parm = 0;
    watch.ob_type = G_USERDEF; watch.ob_spec.userblk = &user; watch.ob_state = 0;
    callback_mode = 1; callback_result = 0;
    VERIFY(graf_watchbox(&watch,0,OS_SELECTED,0) == 1 && callback_result == 1);
    VERIFY(nested_watch.ob_state == OS_SELECTED);
    callback_mode = 2; watch.ob_state = 0; saved_id = aes_global[2];
    next = graf_watchbox(&watch,0,OS_SELECTED,0);
    aes_global[2] = saved_id;
    VERIFY(next == 0);
    VERIFY(wind_update(BEG_MCTRL|NO_BLOCK) == 1 && wind_update(END_MCTRL) == 1);
    watch.ob_type = G_BOX; watch.ob_spec.index = 0x1172;
    watch.ob_x = mx+20; watch.ob_state = 0;
    VERIFY(graf_watchbox(&watch,0,OS_SELECTED,0) == 0 && !watch.ob_state);
    VERIFY(wind_update(BEG_MCTRL|NO_BLOCK) == 1 && wind_update(END_MCTRL) == 1);
    return 0;
}
