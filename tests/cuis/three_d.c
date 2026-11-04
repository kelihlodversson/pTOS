/* Standard GEM 3D objects, preferences and native-style hit bounds. */
#include <gem.h>
#include "gem_draw_probe.h"
#include "three_d.h"
#define VERIFY(c) do { if (!(c)) return 0x7b00 | __LINE__; } while (0)
static int swatch(short handle, short colour)
{
    OBJECT o = { 0 };
    BITBLK bit = { 0 };
    short word = -1;
    o.ob_next = o.ob_head = o.ob_tail = -1; o.ob_type = G_IMAGE;
    o.ob_x = o.ob_y = 80; o.ob_width = 16; o.ob_height = 1; o.ob_spec.bitblk = &bit;
    bit.bi_pdata = &word; bit.bi_wb = 2; bit.bi_hl = 1; bit.bi_color = colour;
    if (!objc_draw(&o,0,0,0,0,100,100)) return -1;
    return gem_probe_pixel(handle,80,80);
}
static OBJECT *moved_object;
static short move_sibling(PARMBLK *parameters)
{
    (void)parameters;
    moved_object->ob_x = -32700;
    return 0;
}
int gem_3d_checks(short handle)
{
    OBJECT box = { 0 }, tree[2] = { { 0 } };
    short a = -1, b = -1, global[15], i;
    int white, black, light, dark, red;
    white = swatch(handle,0); black = swatch(handle,1);
    light = swatch(handle,8); dark = swatch(handle,9); red = swatch(handle,2);
    VERIFY(white >= 0 && black >= 0 && light >= 0 && dark >= 0 && red >= 0);
    VERIFY(objc_sysvar(SV_INQUIRE,LK3DIND,0,0,&a,&b) == 1 && a == 0 && b == 1);
    VERIFY(objc_sysvar(SV_INQUIRE,LK3DACT,0,0,&a,&b) == 1 && a == 1 && b == 0);
    VERIFY(objc_sysvar(SV_INQUIRE,AD3DVAL,0,0,&a,&b) == 1 && a == 2 && b == 2);
    box.ob_next = box.ob_head = box.ob_tail = -1;
    box.ob_type = G_BOX; box.ob_spec.index = 0x1100;
    box.ob_x = box.ob_y = 20; box.ob_width = 20; box.ob_height = 16;
    box.ob_flags = 0x200; /* FL3DIND, standard GEM flag bits. */
    VERIFY(objc_draw(&box,0,0,0,0,100,100) == 1);
    VERIFY(gem_probe_pixel(handle,25,25) == light && gem_probe_pixel(handle,20,18) == white);
    VERIFY(gem_probe_pixel(handle,20,37) == dark);
    VERIFY(objc_change(&box,0,0,0,0,100,100,OS_SELECTED,1) == 1);
    VERIFY(gem_probe_pixel(handle,25,25) == dark && gem_probe_pixel(handle,20,18) == black);
    VERIFY(gem_probe_pixel(handle,20,37) == light);
    VERIFY(objc_sysvar(SV_SET,ACTBUTCOL,2,0,0,0) == 1);
    box.ob_flags = 0x600; box.ob_state = 0;
    VERIFY(objc_draw(&box,0,0,0,0,100,100) == 1 && gem_probe_pixel(handle,25,25) == red);
    VERIFY(objc_change(&box,0,0,0,0,100,100,OS_SELECTED,1) == 1 && gem_probe_pixel(handle,25,25) == red);
    box.ob_flags = 0x400; box.ob_state = OS_OUTLINED;
    VERIFY(objc_draw(&box,0,0,0,0,100,100) == 1);
    VERIFY(gem_probe_pixel(handle,25,25) == light && gem_probe_pixel(handle,17,25) == dark);
    VERIFY(gem_probe_pixel(handle,18,25) == white);
    VERIFY(objc_sysvar(SV_SET,ACTBUTCOL,8,0,0,0) == 1);
    a = 123; b = 456;
    VERIFY(objc_sysvar(SV_INQUIRE,32767,0,0,&a,&b) == 0 && a == 123 && b == 456);
    VERIFY(objc_sysvar(SV_SET,AD3DVAL,99,99,&a,&b) == 0 && a == 123 && b == 456);
    VERIFY(objc_sysvar(SV_SET,ACTBUTCOL,256,0,&a,&b) == 0 && a == 123 && b == 456);
    VERIFY(objc_sysvar(SV_SET,INDBUTCOL,-1,0,&a,&b) == 0 && a == 123 && b == 456);
    VERIFY(objc_sysvar(2,INDBUTCOL,2,0,&a,&b) == 0 && a == 123 && b == 456);
    for (i = 0; i < 15; i++) global[i] = aes_global[i];
    VERIFY(mt_objc_sysvar(SV_INQUIRE,ACTBUTCOL,0,0,&a,&b,global) == 1 && a == 8 && b == 456);
    VERIFY(mt_objc_sysvar(SV_SET,LK3DIND,-1,-1,0,0,global) == 1);
    global[2] = -1;
    VERIFY(mt_objc_sysvar(SV_INQUIRE,ACTBUTCOL,0,0,&a,&b,global) == 0 && a == 8);
    tree[0].ob_next = tree[0].ob_head = tree[0].ob_tail = -1;
    tree[0].ob_type = G_IBOX; tree[0].ob_width = tree[0].ob_height = 100;
    tree[1] = box; tree[1].ob_flags = 0x200; tree[1].ob_state = 0;
    VERIFY(objc_add(tree,0,1) == 1);
    VERIFY(objc_find(tree,0,1,18,18) == 1 && objc_find(tree,0,1,17,18) == 0);
    VERIFY(objc_find(tree,0,1,41,37) == 1 && objc_find(tree,0,1,42,37) == 0);
    tree[1].ob_state = OS_OUTLINED;
    VERIFY(objc_find(tree,0,1,15,15) == 1 && objc_find(tree,0,1,14,15) == 0);
    tree[1].ob_state = OS_SHADOWED;
    VERIFY(objc_find(tree,0,1,19,20) == 0 && objc_find(tree,0,1,20,20) == 1);
    tree[1].ob_flags = 0x400; tree[1].ob_state = 0;
    VERIFY(objc_find(tree,0,1,19,20) == 0 && objc_find(tree,0,1,20,20) == 1);
    box.ob_flags = 0x200; box.ob_state = 0; box.ob_x = -32768;
    VERIFY(objc_change(&box,0,0,0,0,100,100,OS_SELECTED,1) == 0 && !box.ob_state);
    box.ob_x = 20; box.ob_width = 32767;
    VERIFY(objc_draw(&box,0,0,0,0,100,100) == 0);
    box.ob_width = 20; box.ob_type = G_TEXT; box.ob_spec.tedinfo = 0;
    VERIFY(objc_draw(&box,0,0,0,0,100,100) == 0);
    /* Parent/child sums must be checked before any WORD narrowing, both
     * when traversing a whole tree and when starting directly at a child. */
    tree[0].ob_x = -32760; tree[0].ob_y = 0;
    tree[1].ob_x = -32700; tree[1].ob_y = 20; tree[1].ob_flags = 0x200;
    tree[1].ob_state = 0;
    VERIFY(objc_draw(tree,0,1,0,0,100,100) == 0);
    VERIFY(objc_draw(tree,1,0,0,0,100,100) == 0);
    VERIFY(objc_change(tree,1,0,0,0,100,100,OS_SELECTED,1) == 0 && !tree[1].ob_state);
    tree[0].ob_x = 0; tree[0].ob_y = -32760;
    tree[1].ob_x = 20; tree[1].ob_y = -32700;
    VERIFY(objc_draw(tree,0,1,0,0,100,100) == 0);
    VERIFY(objc_change(tree,1,0,0,0,100,100,OS_SELECTED,1) == 0 && !tree[1].ob_state);
    {
        CICONBLK colour = { 0 };
        ICONBLK *icon = &colour.monoblk;
        short data[1] = { 0 }, axis, part, kind;
        icon->ib_pdata = icon->ib_pmask = data;
        icon->ib_ptext = "X"; icon->ib_wicon = 16; icon->ib_hicon = 1;
        icon->ib_wtext = 8; icon->ib_htext = 16; icon->ib_char = 'X';
        box.ob_x = box.ob_y = 0; box.ob_width = box.ob_height = 20;
        box.ob_spec.ciconblk = &colour;
        for (kind = 0; kind < 2; kind++) {
            box.ob_type = kind ? G_CICON : G_ICON;
            for (axis = 0; axis < 2; axis++) for (part = 0; part < 3; part++) {
                short *offset = part == 0 ? (axis ? &icon->ib_yicon : &icon->ib_xicon) :
                    part == 1 ? (axis ? &icon->ib_ytext : &icon->ib_xtext) :
                                (axis ? &icon->ib_ychar : &icon->ib_xchar);
                *offset = -32767;
                VERIFY(objc_draw(&box,0,0,0,0,100,100) == 0);
                VERIFY(objc_change(&box,0,0,0,0,100,100,OS_SELECTED,1) == 0 && !box.ob_state);
                *offset = 0;
            }
        }
    }
    {
        OBJECT siblings[3] = { { 0 } };
        USERBLK user;
        int before = gem_probe_pixel(handle,76,25);
        user.ub_code = move_sibling; user.ub_parm = 0;
        siblings[0].ob_next = siblings[0].ob_head = siblings[0].ob_tail = -1;
        siblings[0].ob_type = G_IBOX; siblings[0].ob_x = -32760;
        siblings[0].ob_width = 100; siblings[0].ob_height = 100;
        siblings[1].ob_next = siblings[1].ob_head = siblings[1].ob_tail = -1;
        siblings[1].ob_type = G_USERDEF; siblings[1].ob_spec.userblk = &user;
        siblings[1].ob_x = 32760; siblings[1].ob_y = 20;
        siblings[1].ob_width = siblings[1].ob_height = 20;
        siblings[2] = siblings[1]; siblings[2].ob_type = G_BOX;
        siblings[2].ob_spec.index = 0x1172; siblings[2].ob_flags = 0x200;
        VERIFY(objc_add(siblings,0,1) == 1 && objc_add(siblings,0,2) == 1);
        moved_object = siblings+2;
        VERIFY(objc_draw(siblings,0,1,0,0,100,100) == 0);
        VERIFY(siblings[2].ob_x == -32700 && gem_probe_pixel(handle,76,25) == before);
        moved_object = 0;
    }
    return 0;
}
