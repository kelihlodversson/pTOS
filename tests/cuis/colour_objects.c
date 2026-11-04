/* Standard GEM colour icons checked against independent VDI pen swatches. */
#include <gem.h>
#include <stddef.h>
#include "gem_draw_probe.h"
#include "colour_objects.h"
extern int memcmp(const void *, const void *, size_t);
extern void *memcpy(void *, const void *, size_t);
#define VERIFY(c) do { if (!(c)) return 0x7a00 | __LINE__; } while (0)
static CICON *mutation_variant;
static short mutate_colour(PARMBLK *pb)
{
    (void)pb;
    mutation_variant->num_planes = 0;
    return 0;
}
static int pen_pixel(short handle, short pen)
{
    OBJECT image = { 0 };
    BITBLK bit = { 0 };
    short word = (short)0xffff;
    image.ob_next = image.ob_head = image.ob_tail = -1;
    image.ob_type = G_IMAGE; image.ob_spec.bitblk = &bit;
    image.ob_x = image.ob_y = 80; image.ob_width = 16; image.ob_height = 1;
    bit.bi_pdata = &word; bit.bi_wb = 2; bit.bi_hl = 1; bit.bi_color = pen;
    if (!objc_draw(&image,0,0,0,0,100,100)) return -1;
    return gem_probe_pixel(handle,80,80);
}
static int colour_pixels(short handle, CICONBLK *block)
{
    static const short pens[16] = { 0,2,3,6,4,7,5,8,9,10,11,14,12,15,13,255 };
    OBJECT icon = { 0 }, backdrop = { 0 };
    int expected[16];
    short x, y;
    for (x = 0; x < 16; x++) { expected[x] = pen_pixel(handle,pens[x]); VERIFY(expected[x] >= 0); }
    backdrop.ob_next = backdrop.ob_head = backdrop.ob_tail = -1;
    backdrop.ob_type = G_BOX; backdrop.ob_spec.index = 0x1173;
    backdrop.ob_width = backdrop.ob_height = 100;
    icon.ob_next = icon.ob_head = icon.ob_tail = -1;
    icon.ob_type = G_CICON; icon.ob_spec.ciconblk = block;
    icon.ob_x = icon.ob_y = 10; icon.ob_width = 16; icon.ob_height = 4;
    VERIFY(objc_draw(&backdrop,0,0,0,0,100,100) == 1);
    VERIFY(objc_draw(&icon,0,0,0,0,100,100) == 1);
    for (y = 0; y < 4; y++) for (x = 0; x < 16; x++)
        VERIFY(gem_probe_pixel(handle,x+10,y+10) == expected[x]);
    VERIFY(objc_change(&icon,0,0,0,0,100,100,OS_SELECTED,1) == 1);
    for (y = 0; y < 4; y++) for (x = 0; x < 16; x++)
        VERIFY(gem_probe_pixel(handle,x+10,y+10) == expected[15-x]);
    VERIFY(objc_change(&icon,0,0,0,0,100,100,0,1) == 1);
    VERIFY(objc_draw(&backdrop,0,0,0,0,100,100) == 1);
    VERIFY(objc_draw(&icon,0,0,11,11,2,2) == 1);
    VERIFY(gem_probe_pixel(handle,10,10) == expected[2]); /* backdrop pen 3 */
    VERIFY(gem_probe_pixel(handle,11,11) == expected[1] && gem_probe_pixel(handle,12,12) == expected[2]);
    VERIFY(gem_probe_pixel(handle,13,12) == expected[2]);
    /* Dither darkening is tested separately by removing selected data. */
    return 0;
}
/* A separate eight-plane image covers every raw palette index. Reference
 * swatches use 256 one-pixel GEM images in one tree/workstation, independent
 * of the colour-plane decoder and without assuming unique RGB palette values. */
static int full_palette_pixels(short handle, CICONBLK *block)
{
    static const short low_pens[16] = { 0,2,3,6,4,7,5,8,9,10,11,14,12,15,13,255 };
    OBJECT swatches[257] = { { 0 } }, icon = { 0 };
    BITBLK bits[256] = { { 0 } };
    short one = 1, index, x, y;
    int expected[256];
    swatches[0].ob_next = -1; swatches[0].ob_head = 1; swatches[0].ob_tail = 256;
    swatches[0].ob_type = G_IBOX; swatches[0].ob_width = swatches[0].ob_height = 100;
    for (index = 0; index < 256; index++) {
        OBJECT *o = swatches+index+1;
        o->ob_next = index == 255 ? 0 : index+2;
        o->ob_head = o->ob_tail = -1; o->ob_type = G_IMAGE;
        o->ob_x = 80+(index&15); o->ob_y = 80+(index>>4);
        o->ob_width = o->ob_height = 1; o->ob_spec.bitblk = bits+index;
        bits[index].bi_pdata = &one; bits[index].bi_wb = 2; bits[index].bi_hl = 1;
        bits[index].bi_x = 15;
        bits[index].bi_color = index < 16 ? low_pens[index] : index == 255 ? 1 : index;
    }
    VERIFY(objc_draw(swatches,0,1,0,0,100,100) == 1);
    for (index = 0; index < 256; index++) {
        expected[index] = gem_probe_pixel(handle,80+(index&15),80+(index>>4));
        VERIFY(expected[index] >= 0);
    }
    icon.ob_next = icon.ob_head = icon.ob_tail = -1;
    icon.ob_type = G_CICON; icon.ob_spec.ciconblk = block;
    icon.ob_x = icon.ob_y = 10; icon.ob_width = icon.ob_height = 16;
    VERIFY(objc_draw(&icon,0,0,0,0,100,100) == 1);
    for (y = 0; y < 16; y++) for (x = 0; x < 16; x++)
        VERIFY(gem_probe_pixel(handle,x+10,y+10) == expected[y*16+x]);
    VERIFY(objc_change(&icon,0,0,0,0,100,100,OS_SELECTED,1) == 1);
    for (y = 0; y < 16; y++) for (x = 0; x < 16; x++)
        VERIFY(gem_probe_pixel(handle,x+10,y+10) == expected[255-y*16-x]);
    return 0;
}
static int native_full_palette(short handle)
{
    CICONBLK block = { 0 };
    CICON variant = { 0 };
    short data[128] = { 0 }, selected[128], mask[16], x, y, plane;
    for (plane = 0; plane < 8; plane++) for (y = 0; y < 16; y++) {
        for (x = 0; x < 16; x++)
            if ((y*16+x) & (1<<plane)) data[plane*16+y] |= (unsigned short)0x8000 >> x;
        selected[plane*16+y] = (short)~data[plane*16+y];
    }
    for (y = 0; y < 16; y++) mask[y] = -1;
    block.monoblk.ib_pdata = mask; block.monoblk.ib_pmask = mask;
    block.monoblk.ib_ptext = ""; block.monoblk.ib_wicon = block.monoblk.ib_hicon = 16;
    block.mainlist = &variant; variant.num_planes = 8;
    variant.col_data = data; variant.col_mask = mask;
    variant.sel_data = selected; variant.sel_mask = mask;
    return full_palette_pixels(handle,&block);
}
int gem_colour_checks(short handle)
{
    CICONBLK block = { 0 }, saved;
    CICON low = { 0 }, high = { 0 }, low_saved, high_saved;
    OBJECT icon = { 0 };
    OBSPEC indirect;
    short mono[4] = { (short)0xa000,(short)0xa000,(short)0xa000,(short)0xa000 };
    short mask[4] = { -1,-1,-1,-1 }, data[16], selected[16], data_saved[16], selected_saved[16];
    short low_data[8] = { 0 }, x, y;
    int result, black, white, red, blue;
    for (x = 0; x < 4; x++) for (y = 0; y < 4; y++) {
        static const unsigned short words[4] = { 0x5555,0x3333,0x0f0f,0x00ff };
        data[x*4+y] = words[x]; selected[x*4+y] = (short)~words[x];
    }
    low.num_planes = 2; low.col_data = low_data; low.col_mask = mask; low.next_res = &high;
    high.num_planes = 4; high.col_data = data; high.col_mask = mask;
    high.sel_data = selected; high.sel_mask = mask;
    block.monoblk.ib_pdata = mono; block.monoblk.ib_pmask = mask; block.monoblk.ib_ptext = "";
    block.monoblk.ib_char = 0x2400; block.monoblk.ib_wicon = 16; block.monoblk.ib_hicon = 4;
    block.mainlist = &low;
    memcpy(&saved,&block,sizeof(block)); low_saved = low; high_saved = high;
    memcpy(data_saved,data,sizeof(data)); memcpy(selected_saved,selected,sizeof(selected));
    result = native_full_palette(handle); if (result) return result;
    result = colour_pixels(handle,&block); if (result) return result;
    VERIFY(!memcmp(&saved,&block,sizeof(block)) && !memcmp(&low,&low_saved,sizeof(low)) &&
        !memcmp(&high,&high_saved,sizeof(high)) && !memcmp(data,data_saved,sizeof(data)) &&
        !memcmp(selected,selected_saved,sizeof(selected)));
    icon.ob_next = icon.ob_head = icon.ob_tail = -1;
    icon.ob_type = G_CICON; icon.ob_spec.ciconblk = &block;
    icon.ob_x = icon.ob_y = 10; icon.ob_width = 16; icon.ob_height = 4;
    high.sel_data = high.sel_mask = 0; icon.ob_state = OS_SELECTED;
    black = pen_pixel(handle,1); white = pen_pixel(handle,0); red = pen_pixel(handle,2); blue = pen_pixel(handle,4);
    VERIFY(objc_draw(&icon,0,0,0,0,100,100) == 1);
    VERIFY(gem_probe_pixel(handle,10,10) == white && gem_probe_pixel(handle,11,10) == black);
    VERIFY(gem_probe_pixel(handle,10,11) == black && gem_probe_pixel(handle,11,11) == red);
    for (y = 0; y < 4; y++) VERIFY(mask[y] == -1 && (unsigned short)mono[y] == 0xa000);
    VERIFY(!memcmp(data,data_saved,sizeof(data)) && !memcmp(selected,selected_saved,sizeof(selected)));
    indirect.ciconblk = &block; icon.ob_spec.indirect = &indirect;
    icon.ob_flags = 0x100; icon.ob_state = 0;
    VERIFY(objc_draw(&icon,0,0,0,0,100,100) == 1 && indirect.ciconblk == &block);
    VERIFY(gem_probe_pixel(handle,11,10) == red);
    icon.ob_flags = 0; icon.ob_spec.ciconblk = &block;
    {
        OBJECT tree[3] = { { 0 } };
        USERBLK user;
        tree[0].ob_next = tree[0].ob_head = tree[0].ob_tail = -1;
        tree[0].ob_type = G_IBOX; tree[0].ob_width = tree[0].ob_height = 100;
        tree[1].ob_next = tree[1].ob_head = tree[1].ob_tail = -1;
        tree[1].ob_type = G_USERDEF; tree[1].ob_spec.userblk = &user;
        tree[1].ob_width = tree[1].ob_height = 1;
        tree[2] = icon; user.ub_code = mutate_colour; user.ub_parm = 0;
        VERIFY(objc_add(tree,0,1) == 1 && objc_add(tree,0,2) == 1);
        mutation_variant = &high;
        VERIFY(objc_draw(tree,0,1,0,0,100,100) == 0);
        VERIFY(gem_probe_pixel(handle,11,10) == red);
        high.num_planes = 4; mutation_variant = 0;
    }
    /* Empty colour lists use the original mono painter, including selection. */
    block.mainlist = 0; icon.ob_state = 0;
    VERIFY(objc_draw(&icon,0,0,0,0,100,100) == 1);
    VERIFY(gem_probe_pixel(handle,10,10) == red);
    VERIFY(objc_change(&icon,0,0,0,0,100,100,OS_SELECTED,1) == 1);
    VERIFY(gem_probe_pixel(handle,10,10) == blue);
    block.mainlist = &low; icon.ob_state = 0;
    low.next_res = &low;
    VERIFY(objc_draw(&icon,0,0,0,0,100,100) == 0);
    low.next_res = &high; high.sel_data = selected; high.sel_mask = 0;
    VERIFY(objc_draw(&icon,0,0,0,0,100,100) == 0);
    high.sel_mask = mask; high.num_planes = 0;
    VERIFY(objc_draw(&icon,0,0,0,0,100,100) == 0);
    return 0;
}
int gem_colour_resource_checks(short handle)
{
    OBJECT *tree;
    CICONBLK *block;
    int result;
    VERIFY(rsrc_load("GEMCOLOR.RSC") == 1 && rsrc_gaddr(R_TREE,0,&tree) == 1);
    VERIFY(tree[2].ob_spec.indirect == tree[3].ob_spec.indirect);
    block = tree[2].ob_spec.indirect->ciconblk;
    VERIFY(block && block->mainlist && block->mainlist->next_res);
    result = colour_pixels(handle,block);
    VERIFY(rsrc_free() == 1);
    if (result) return result;
    VERIFY(rsrc_load("GEM256.RSC") == 1 && rsrc_gaddr(R_TREE,0,&tree) == 1);
    VERIFY(tree[2].ob_spec.indirect == tree[3].ob_spec.indirect);
    block = tree[2].ob_spec.indirect->ciconblk;
    VERIFY(block && block->mainlist && block->monoblk.ib_hicon == 16);
    result = full_palette_pixels(handle,block);
    VERIFY(rsrc_free() == 1);
    return result;
}
