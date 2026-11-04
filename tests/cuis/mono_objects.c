/* Standard GEM monochrome objects against independent pixel probes.
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later. */
#include <gem.h>
#include "gem_draw_probe.h"
#include "mono_objects.h"
#define VERIFY(c) do { if (!(c)) return 0x7800 | __LINE__; } while (0)
static BITBLK *callback_bitmap;
static ICONBLK *callback_icon;
static char long_label[160];
static int mutate_geometry;
static short large_form[32752];
static short mutate_mono(PARMBLK *parameters)
{
    (void)parameters;
    if (callback_bitmap) callback_bitmap->bi_wb = 1;
    if (callback_icon) {
        if (mutate_geometry) callback_icon->ib_xicon = 32760;
        else callback_icon->ib_ptext = long_label;
    }
    return 0;
}
int gem_mono_checks(short handle)
{
    OBJECT backdrop = { 0 }, image = { 0 };
    BITBLK bitmap = { 0 };
    OBJECT tree[3] = { { 0 } };
    USERBLK user;
    ICONBLK icon = { 0 };
    short bits[4] = { (short)0xa000,0x5000,(short)0xffff,0 };
    short mask[4] = { (short)0xf000,(short)0xf000,(short)0xf000,(short)0xf000 };
    short saved[4][16], x, y;
    int axis, part, sign;
    backdrop.ob_next = backdrop.ob_head = backdrop.ob_tail = -1;
    backdrop.ob_type = G_BOX; backdrop.ob_spec.index = 0x1173;
    backdrop.ob_width = backdrop.ob_height = 100;
    image.ob_next = image.ob_head = image.ob_tail = -1;
    image.ob_type = G_IMAGE; image.ob_spec.bitblk = &bitmap;
    image.ob_x = image.ob_y = 10; image.ob_width = 16; image.ob_height = 4;
    bitmap.bi_pdata = bits; bitmap.bi_wb = 2; bitmap.bi_hl = 4; bitmap.bi_color = 2;
    VERIFY(objc_draw(&backdrop,0,0,0,0,100,100) == 1);
    VERIFY(objc_draw(&image,0,0,0,0,100,100) == 1);
    VERIFY(gem_probe_pixel(handle,10,10) == 2 && gem_probe_pixel(handle,11,10) == 3);
    VERIFY(gem_probe_pixel(handle,12,10) == 2 && gem_probe_pixel(handle,10,11) == 3);
    VERIFY(gem_probe_pixel(handle,11,11) == 2 && gem_probe_pixel(handle,25,12) == 2);
    VERIFY(gem_probe_pixel(handle,10,13) == 3);
    for (y = 0; y < 4; y++) for (x = 0; x < 16; x++) saved[y][x] = gem_probe_pixel(handle,x+10,y+10);
    VERIFY(objc_change(&image,0,0,0,0,100,100,OS_SELECTED,1) == 1);
    VERIFY(objc_change(&image,0,0,0,0,100,100,0,1) == 1);
    for (y = 0; y < 4; y++) for (x = 0; x < 16; x++)
        VERIFY(gem_probe_pixel(handle,x+10,y+10) == saved[y][x]);
    VERIFY(objc_draw(&backdrop,0,0,0,0,100,100) == 1);
    VERIFY(objc_draw(&image,0,0,11,10,2,1) == 1);
    VERIFY(gem_probe_pixel(handle,10,10) == 3 && gem_probe_pixel(handle,11,10) == 3);
    VERIFY(gem_probe_pixel(handle,12,10) == 2 && gem_probe_pixel(handle,12,11) == 3);
    bitmap.bi_x = 1; bitmap.bi_y = 1;
    VERIFY(objc_draw(&backdrop,0,0,0,0,100,100) == 1);
    VERIFY(objc_draw(&image,0,0,0,0,100,100) == 1);
    VERIFY(gem_probe_pixel(handle,10,10) == 2 && gem_probe_pixel(handle,11,10) == 3);
    VERIFY(gem_probe_pixel(handle,24,10) == 3 && gem_probe_pixel(handle,25,11) == 3);
    bitmap.bi_x = bitmap.bi_y = 0;
    image.ob_x = -1; image.ob_y = -1;
    VERIFY(objc_draw(&backdrop,0,0,0,0,100,100) == 1);
    VERIFY(objc_draw(&image,0,0,0,0,100,100) == 1);
    VERIFY(gem_probe_pixel(handle,0,0) == 2 && gem_probe_pixel(handle,1,0) == 3);
    image.ob_x = image.ob_y = 10;
    bitmap.bi_wb = 1;
    VERIFY(objc_draw(&image,0,0,0,0,100,100) == 0);
    image.ob_flags = OF_HIDETREE; bitmap.bi_pdata = 0;
    VERIFY(objc_draw(&image,0,0,0,0,100,100) == 1);
    image.ob_flags = 0; image.ob_type = G_ICON; image.ob_spec.iconblk = &icon;
    icon.ib_pdata = bits; icon.ib_pmask = mask; icon.ib_ptext = "";
    icon.ib_char = 0x2400; icon.ib_wicon = 16; icon.ib_hicon = 4;
    VERIFY(objc_draw(&backdrop,0,0,0,0,100,100) == 1);
    VERIFY(objc_draw(&image,0,0,0,0,100,100) == 1);
    VERIFY(gem_probe_pixel(handle,10,10) == 2 && gem_probe_pixel(handle,11,10) == 4);
    VERIFY(gem_probe_pixel(handle,12,10) == 2 && gem_probe_pixel(handle,14,10) == 3);
    VERIFY(objc_change(&image,0,0,0,0,100,100,OS_SELECTED,1) == 1);
    VERIFY(gem_probe_pixel(handle,10,10) == 4 && gem_probe_pixel(handle,11,10) == 2);
    VERIFY(objc_change(&image,0,0,0,0,100,100,0,1) == 1);
    VERIFY(gem_probe_pixel(handle,10,10) == 2 && gem_probe_pixel(handle,11,10) == 4);
    icon.ib_char = 0x2000; image.ob_state = OS_WHITEBAK;
    VERIFY(objc_draw(&backdrop,0,0,0,0,100,100) == 1);
    VERIFY(objc_draw(&image,0,0,0,0,100,100) == 1);
    VERIFY(gem_probe_pixel(handle,10,10) == 2 && gem_probe_pixel(handle,11,10) == 3);
    image.ob_state = 0; icon.ib_char = 0x2441;
    icon.ib_ptext = "Icon"; icon.ib_xtext = 20; icon.ib_ytext = 20;
    icon.ib_wtext = 40; icon.ib_htext = 16;
    VERIFY(objc_draw(&image,0,0,0,0,100,100) == 1);
    VERIFY(gem_probe_pixel(handle,30,45) == 4);
    icon.ib_pmask = 0;
    VERIFY(objc_draw(&image,0,0,0,0,100,100) == 0);
    image.ob_flags = OF_HIDETREE; image.ob_spec.iconblk = 0;
    VERIFY(objc_draw(&image,0,0,0,0,100,100) == 1);
    image.ob_flags = 0; image.ob_type = G_CICON;
    VERIFY(objc_draw(&image,0,0,0,0,100,100) == 0);
    image.ob_type = G_ICON; image.ob_spec.iconblk = &icon;
    icon.ib_pmask = mask; icon.ib_wicon = 15;
    VERIFY(objc_draw(&image,0,0,0,0,100,100) == 0);
    icon.ib_wicon = 16;
    /* Translated bitmap, label and character origins must not wrap. */
    backdrop.ob_width = 160;
    VERIFY(objc_draw(&backdrop,0,0,0,0,160,100) == 1);
    for (x = 0; x < 32752; x++) large_form[x] = (short)0xffff;
    image.ob_x = image.ob_y = 100;
    for (axis = 0; axis < 2; axis++) for (part = 0; part < 3; part++) for (sign = -1; sign <= 1; sign += 2) {
        ICONBLK overflow = { 0 };
        overflow.ib_pdata = overflow.ib_pmask = large_form;
        overflow.ib_wicon = axis ? 16 : 32752;
        overflow.ib_hicon = axis ? 32752 : 1;
        overflow.ib_ptext = part == 1 ? "Label" : "";
        overflow.ib_char = 0x2400 | (part == 2 ? 'A' : 0);
        if (part == 0) {
            overflow.ib_xicon = axis ? 0 : sign*32760;
            overflow.ib_yicon = axis ? sign*32760 : 0;
        } else if (part == 1) {
            overflow.ib_xtext = axis ? 0 : sign*32760;
            overflow.ib_ytext = axis ? sign*32760 : 0;
            overflow.ib_wtext = 40; overflow.ib_htext = 16;
        } else {
            overflow.ib_xicon = axis ? 0 : sign*32600;
            overflow.ib_yicon = axis ? sign*32600 : 0;
            overflow.ib_xchar = axis ? 0 : sign*200;
            overflow.ib_ychar = axis ? sign*200 : 0;
        }
        image.ob_x = image.ob_y = sign*100;
        image.ob_spec.iconblk = &overflow;
        VERIFY(objc_draw(&image,0,0,0,0,160,120) == 0);
        VERIFY(gem_probe_pixel(handle,0,0) == 3 && gem_probe_pixel(handle,75,75) == 3);
    }
    image.ob_x = image.ob_y = 10; image.ob_spec.iconblk = &icon;
    icon.ib_xtext = 32600; icon.ib_wtext = 200; image.ob_x = 100;
    VERIFY(objc_draw(&image,0,0,0,0,160,120) == 0);
    icon.ib_xtext = 20; icon.ib_wtext = 40; image.ob_x = 10;
    /* Revalidate descriptors and labels changed by an earlier USERDEF. */
    tree[0] = backdrop; tree[0].ob_type = G_IBOX;
    tree[1] = image; tree[1].ob_type = G_USERDEF;
    tree[1].ob_spec.userblk = &user; user.ub_code = mutate_mono; user.ub_parm = 0;
    tree[2] = image;
    VERIFY(objc_add(tree,0,1) == 1 && objc_add(tree,0,2) == 1);
    for (x = 0; x < (short)sizeof(long_label)-1; x++) long_label[x] = 'X';
    callback_icon = &icon;
    VERIFY(objc_draw(tree,0,1,0,0,100,100) == 0);
    icon.ib_ptext = "Icon"; mutate_geometry = 1;
    VERIFY(objc_draw(tree,0,1,0,0,100,100) == 0);
    mutate_geometry = 0; callback_icon = 0; icon.ib_xicon = 0;
    bitmap.bi_pdata = bits; bitmap.bi_wb = 2;
    tree[2].ob_type = G_IMAGE; tree[2].ob_spec.bitblk = &bitmap;
    callback_bitmap = &bitmap;
    VERIFY(objc_draw(tree,0,1,0,0,100,100) == 0);
    callback_bitmap = 0;
    return 0;
}
