/* Source-compatible resource/dialog geometry and real pixel checks.
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later. */
#include <gem.h>
#include "gem_layout.h"
#include "gem_draw_probe.h"
#define VERIFY(c) do { if (!(c)) return 0x7600 | __LINE__; } while (0)
int gem_layout_checks(short handle, short width, short height)
{
    OBJECT tree[2] = { { 0 } }, saved;
    GRECT rect, previous;
    short wchar, hchar, x, y, w, h, global[15];
    OBSPEC spec;
    TEDINFO ted = { 0 };
    int i;
    VERIFY(gem_probe_cell(handle,&wchar,&hchar) == 0);
    for (i = 0; i < 15; i++) global[i] = aes_global[i];
    tree[0].ob_next = tree[0].ob_head = tree[0].ob_tail = -1;
    tree[0].ob_type = G_BOX; tree[0].ob_spec.index = 0x1172;
    tree[0].ob_x = 0x0203; tree[0].ob_y = (short)0xff02;
    tree[0].ob_width = 0x020a; tree[0].ob_height = (short)0xfd03;
    tree[1] = tree[0]; tree[1].ob_x = (short)0x8001;
    tree[1].ob_width = 0x0350;
    saved = tree[0];
    VERIFY(mt_rsrc_obfix(tree,1,global) == 1);
    VERIFY(tree[1].ob_x == wchar+128 && tree[1].ob_y == 2*hchar-1);
    VERIFY(tree[1].ob_width == width+3 && tree[1].ob_height == 3*hchar-3);
    VERIFY(tree[0].ob_x == saved.ob_x && tree[0].ob_width == saved.ob_width);
    VERIFY(rsrc_obfix(tree,0) == 1);
    VERIFY(tree[0].ob_x == 3*wchar+2 && tree[0].ob_y == 2*hchar-1);
    VERIFY(tree[0].ob_width == 10*wchar+2 && tree[0].ob_height == 3*hchar-3);
    VERIFY(form_center(tree,&x,&y,&w,&h) == 1);
    VERIFY(tree[0].ob_x == (width-w)/2 && tree[0].ob_y == hchar+3+(height-hchar-3-h)/2);
    VERIFY(x == tree[0].ob_x && y == tree[0].ob_y && w == tree[0].ob_width && h == tree[0].ob_height);
    VERIFY(objc_draw(tree,0,0,0,0,width,height) == 1);
    VERIFY(gem_probe_pixel(handle,x+1,y+1) == 2);
    tree[0].ob_state = OS_OUTLINED|OS_SHADOWED; tree[0].ob_spec.index = 0xfe1172;
    VERIFY(mt_form_center_grect(tree,&rect,global) == 1);
    VERIFY(rect.g_x == (tree[0].ob_x < 3 ? 0 : tree[0].ob_x-3));
    VERIFY(rect.g_y == (tree[0].ob_y < 3 ? 0 : tree[0].ob_y-3));
    VERIFY(rect.g_w == w+10 && rect.g_h == h+10);
    spec = tree[0].ob_spec; tree[0].ob_flags = OF_INDIRECT; tree[0].ob_spec.indirect = &spec;
    VERIFY(mt_form_center(tree,&x,&y,&w,&h,global) == 1);
    VERIFY(x == rect.g_x && y == rect.g_y && w == rect.g_w && h == rect.g_h);
    tree[0].ob_flags = 0; tree[0].ob_spec = spec;
    tree[0].ob_width = width; tree[0].ob_height = height;
    VERIFY(form_center_grect(tree,&rect) == 1 && rect.g_x == 0);
    /* Large invalid expansion must neither wrap nor mutate caller outputs. */
    tree[0].ob_width = 32767; saved = tree[0]; previous = rect;
    VERIFY(form_center_grect(tree,&rect) == 0);
    VERIFY(tree[0].ob_x == saved.ob_x && tree[0].ob_y == saved.ob_y);
    VERIFY(rect.g_x == previous.g_x && rect.g_y == previous.g_y && rect.g_w == previous.g_w && rect.g_h == previous.g_h);
    tree[0].ob_type = G_BOXTEXT; tree[0].ob_spec.tedinfo = &ted;
    tree[0].ob_width = 40; tree[0].ob_height = 20; tree[0].ob_state = OS_SHADOWED;
    ted.te_thickness = -32768;
    VERIFY(form_center_grect(tree,&rect) == 0);
    VERIFY(tree[0].ob_x == saved.ob_x && tree[0].ob_y == saved.ob_y);
    VERIFY(mt_rsrc_obfix(tree,0,0) == 0 && rsrc_obfix(tree,-1) == 0);
    VERIFY(mt_form_center_grect(tree,&rect,0) == 0 && form_center_grect(0,&rect) == 0);
    VERIFY(form_center(tree,0,&y,&w,&h) == 0);
    return 0;
}
