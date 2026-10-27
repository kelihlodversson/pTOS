/* Standard GEM colour icons checked against independent VDI pen swatches. */
#include <gem.h>
#include <stddef.h>
#include "gem_draw_probe.h"
#include "colour_objects.h"
extern int memcmp(const void *, const void *, size_t);
extern void *memcpy(void *, const void *, size_t);
#define VERIFY(c) do { if (!(c)) return 0x7a00 | __LINE__; } while (0)
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
int gem_colour_checks(short handle)
{
    CICONBLK block = { 0 }, saved;
    CICON low = { 0 }, high = { 0 }, low_saved, high_saved;
    OBJECT icon = { 0 };
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
    return result;
}
