/* Standard form_keybd semantics against GEM source and real pixels.
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later. */
#include <gem.h>
#include "form_keys.h"
#include "gem_draw_probe.h"
#define VERIFY(c) do { if (!(c)) return 0x7500 | __LINE__; } while (0)
int gem_form_key_checks(short handle)
{
    OBJECT tree[6] = { { 0 } }, single = { 0 }, pixel = { 0 };
    short next, key, global[15];
    int i, before, above, below;
    for (i = 0; i < 6; i++) {
        tree[i].ob_next = tree[i].ob_head = tree[i].ob_tail = -1;
        tree[i].ob_type = G_IBOX; tree[i].ob_width = 50; tree[i].ob_height = 24;
        tree[i].ob_x = 10; tree[i].ob_y = 40;
    }
    tree[0].ob_x = tree[0].ob_y = 0;
    tree[0].ob_width = 160; tree[0].ob_height = 120;
    for (i = 1; i < 6; i++) VERIFY(objc_add(tree,0,i) == 1);
    for (i = 1; i < 5; i++) tree[i].ob_flags = OF_EDITABLE;
    tree[2].ob_state = OS_DISABLED; tree[3].ob_flags |= OF_HIDETREE;
    tree[5].ob_type = G_BUTTON; tree[5].ob_spec.free_string = "OK";
    tree[5].ob_flags = OF_LASTOB|OF_DEFAULT|OF_SELECTABLE;
    for (i = 0; i < 15; i++) global[i] = aes_global[i];
    VERIFY(form_keybd(tree,1,88,0x0f09,&next,&key) == 1 && next == 4 && key == 0);
    VERIFY(mt_form_keybd(tree,1,88,0x5000,&next,&key,global) == 1 && next == 4 && key == 0);
    VERIFY(form_keybd(tree,4,88,0x4800,&next,&key) == 1 && next == 1 && key == 0);
    VERIFY(form_keybd(tree,4,88,0x0f09,&next,&key) == 1 && next == 4 && key == 0);
    VERIFY(form_keybd(tree,5,88,0x0f09,&next,&key) == 1 && next == 5 && key == 0);
    VERIFY(form_keybd(tree,1,88,0x1e61,&next,&key) == 1 && next == 88 && key == 0x1e61);
    VERIFY(form_keybd(tree,0,88,0x4800,&next,&key) == 1 && next == 0 && key == 0);
    tree[5].ob_state = OS_DISABLED;
    VERIFY(form_keybd(tree,1,88,0x1c0d,&next,&key) == 1 && next == 0 && key == 0);
    tree[5].ob_state = 0; tree[5].ob_flags |= OF_HIDETREE;
    VERIFY(form_keybd(tree,1,88,0x720d,&next,&key) == 1 && next == 0 && key == 0);
    tree[5].ob_flags &= ~OF_HIDETREE;
    tree[5].ob_flags &= ~OF_DEFAULT;
    VERIFY(form_keybd(tree,1,88,0x1c0d,&next,&key) == 1 && next == 0 && key == 0);
    tree[5].ob_flags |= OF_DEFAULT;
    single.ob_flags = OF_LASTOB|OF_EDITABLE;
    VERIFY(form_keybd(&single,0,88,0x0f09,&next,&key) == 1 && next == 0 && key == 0);
    next = 91; key = 92;
    VERIFY(mt_form_keybd(tree,1,88,0x0f09,&next,&key,0) == 0 && next == 91 && key == 92);
    VERIFY(form_keybd(tree,-1,88,0x0f09,&next,&key) == 0 && next == 91 && key == 92);
    VERIFY(form_keybd(tree,1,88,0x0f09,0,&key) == 0 && key == 92);
    if (handle < 0) {
        VERIFY(form_keybd(tree,1,88,0x1c0d,&next,&key) == 0 && next == 91 && key == 92);
        VERIFY(tree[5].ob_state == 0);
        /* An already selected default needs no graphics operation. */
        tree[5].ob_state = OS_SELECTED;
        VERIFY(form_keybd(tree,1,88,0x1c0d,&next,&key) == 0 && next == 5 && key == 0);
        return 0;
    }
    VERIFY(objc_draw(tree,5,0,0,0,160,120) == 1);
    /* Truecolor palette readback may map inverted white back to pen 0.
     * Red test points make the native XOR observable without assuming a
     * particular packed representation of black or an alpha byte. */
    pixel.ob_next = pixel.ob_head = pixel.ob_tail = -1;
    pixel.ob_type = G_BOX; pixel.ob_spec.index = 0x1172;
    pixel.ob_x = 11; pixel.ob_y = 41; pixel.ob_width = pixel.ob_height = 1;
    VERIFY(objc_draw(&pixel,0,0,0,0,160,120) == 1);
    before = gem_probe_pixel(handle,11,41); VERIFY(before == 2);
    VERIFY(form_keybd(tree,1,88,0x1c0d,&next,&key) == 0 && next == 5 && key == 0);
    VERIFY((tree[5].ob_state & OS_SELECTED) && gem_probe_pixel(handle,11,41) != before);
    VERIFY(objc_change(tree,5,0,0,0,160,120,0,1) == 1 && gem_probe_pixel(handle,11,41) == before);
    VERIFY(mt_form_keybd(tree,1,88,0x720d,&next,&key,global) == 0 && next == 5 && key == 0);
    VERIFY(tree[5].ob_state & OS_SELECTED);
    VERIFY(objc_change(tree,5,0,0,0,160,120,0,1) == 1);
    /* Native FORM_KEYBD clips selection to the area below the menu bar. */
    tree[5].ob_y = 8;
    VERIFY(objc_draw(tree,5,0,0,0,160,120) == 1);
    pixel.ob_y = 10;
    VERIFY(objc_draw(&pixel,0,0,0,0,160,120) == 1);
    pixel.ob_y = 21;
    VERIFY(objc_draw(&pixel,0,0,0,0,160,120) == 1);
    above = gem_probe_pixel(handle,11,10); below = gem_probe_pixel(handle,11,21);
    VERIFY(form_keybd(tree,1,88,0x1c0d,&next,&key) == 0 && next == 5 && key == 0);
    VERIFY(gem_probe_pixel(handle,11,10) == above && gem_probe_pixel(handle,11,21) != below);
    tree[5].ob_state = 0;
    VERIFY(objc_draw(tree,5,0,0,0,160,120) == 1);
    return 0;
}
