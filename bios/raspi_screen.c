/*
 * raspi_screen.h Raspberry PI framebuffer support
 *
 * Copyright (C) 2013-2018 The EmuTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#ifndef MACHINE_RPI
#error This file must only be compiled for raspberry PI targets
#endif

#include "portab.h"
#include "raspi_io.h"
#include "raspi_int.h"
#include "raspi_mbox.h"
#include "raspi_screen.h"
#include "tosvars.h"
#include "string.h"
#include "screen.h"
#include "videl.h"
#include "asm.h"
#include "lineavars.h"
#include "font.h"
#include "conout.h"


#define PRGB_BLACK     0x00000000       /* "Falcon" palette */
#define PRGB_BLUE      0x00ff0000
#define PRGB_GREEN     0x0000ff00
#define PRGB_CYAN      0x00ffff00
#define PRGB_RED       0x000000ff
#define PRGB_MAGENTA   0x00ff00ff
#define PRGB_LTGRAY    0x00bbbbbb
#define PRGB_GRAY      0x00888888
#define PRGB_LTBLUE    0x00aa0000
#define PRGB_LTGREEN   0x0000aa00
#define PRGB_LTCYAN    0x00aaaa00
#define PRGB_LTRED     0x000000aa
#define PRGB_LTMAGENTA 0x00aa00aa
#define PRGB_YELLOW    0x0000ffff
#define PRGB_LTYELLOW  0x0000aaaa
#define PRGB_WHITE     0x00ffffff

const ULONG raspi_dflt_palette[] = {
    PRGB_WHITE, PRGB_RED, PRGB_GREEN, PRGB_YELLOW,
    PRGB_BLUE, PRGB_MAGENTA, PRGB_CYAN, PRGB_LTGRAY,
    PRGB_GRAY, PRGB_LTRED, PRGB_LTGREEN, PRGB_LTYELLOW,
    PRGB_LTBLUE, PRGB_LTMAGENTA, PRGB_LTCYAN, PRGB_BLACK,
    0x00ffffff, 0x00ededed, 0x00dddddd, 0x00cccccc,
    0x00bababa, 0x00aaaaaa, 0x00999999, 0x00878787,
    0x00777777, 0x00666666, 0x00545454, 0x00444444,
    0x00333333, 0x00212121, 0x00111111, 0x00000000,
    0x000000ff, 0x001100ff, 0x002100ff, 0x003300ff,
    0x004400ff, 0x005400ff, 0x006600ff, 0x007700ff,
    0x008700ff, 0x009900ff, 0x00aa00ff, 0x00ba00ff,
    0x00cc00ff, 0x00dd00ff, 0x00ed00ff, 0x00ff00ff,
    0x00ff00ed, 0x00ff00dd, 0x00ff00cc, 0x00ff00ba,
    0x00ff00aa, 0x00ff0099, 0x00ff0087, 0x00ff0077,
    0x00ff0066, 0x00ff0054, 0x00ff0044, 0x00ff0033,
    0x00ff0021, 0x00ff0011, 0x00ff0000, 0x00ff1100,
    0x00ff2100, 0x00ff3300, 0x00ff4400, 0x00ff5400,
    0x00ff6600, 0x00ff7700, 0x00ff8700, 0x00ff9900,
    0x00ffaa00, 0x00ffba00, 0x00ffcc00, 0x00ffdd00,
    0x00ffed00, 0x00ffff00, 0x00edff00, 0x00ddff00,
    0x00ccff00, 0x00baff00, 0x00aaff00, 0x0099ff00,
    0x0087ff00, 0x0077ff00, 0x0066ff00, 0x0054ff00,
    0x0044ff00, 0x0033ff00, 0x0021ff00, 0x0011ff00,
    0x0000ff00, 0x0000ff11, 0x0000ff21, 0x0000ff33,
    0x0000ff44, 0x0000ff54, 0x0000ff66, 0x0000ff77,
    0x0000ff87, 0x0000ff99, 0x0000ffaa, 0x0000ffba,
    0x0000ffcc, 0x0000ffdd, 0x0000ffed, 0x0000ffff,
    0x0000edff, 0x0000ddff, 0x0000ccff, 0x0000baff,
    0x0000aaff, 0x000099ff, 0x000087ff, 0x000077ff,
    0x000066ff, 0x000054ff, 0x000044ff, 0x000033ff,
    0x000021ff, 0x000011ff, 0x000000ba, 0x001100ba,
    0x002100ba, 0x003300ba, 0x004400ba, 0x005400ba,
    0x006600ba, 0x007700ba, 0x008700ba, 0x009900ba,
    0x00aa00ba, 0x00ba00ba, 0x00ba00aa, 0x00ba0099,
    0x00ba0087, 0x00ba0077, 0x00ba0066, 0x00ba0054,
    0x00ba0044, 0x00ba0033, 0x00ba0021, 0x00ba0011,
    0x00ba0000, 0x00ba1100, 0x00ba2100, 0x00ba3300,
    0x00ba4400, 0x00ba5400, 0x00ba6600, 0x00ba7700,
    0x00ba8700, 0x00ba9900, 0x00baaa00, 0x00baba00,
    0x00aaba00, 0x0099ba00, 0x0087ba00, 0x0077ba00,
    0x0066ba00, 0x0054ba00, 0x0044ba00, 0x0033ba00,
    0x0021ba00, 0x0011ba00, 0x0000ba00, 0x0000ba11,
    0x0000ba21, 0x0000ba33, 0x0000ba44, 0x0000ba54,
    0x0000ba66, 0x0000ba77, 0x0000ba87, 0x0000ba99,
    0x0000baaa, 0x0000baba, 0x0000aaba, 0x000099ba,
    0x000087ba, 0x000077ba, 0x000066ba, 0x000054ba,
    0x000044ba, 0x000033ba, 0x000021ba, 0x000011ba,
    0x00000077, 0x00110077, 0x00210077, 0x00330077,
    0x00440077, 0x00540077, 0x00660077, 0x00770077,
    0x00770066, 0x00770054, 0x00770044, 0x00770033,
    0x00770021, 0x00770011, 0x00770000, 0x00771100,
    0x00772100, 0x00773300, 0x00774400, 0x00775400,
    0x00776600, 0x00777700, 0x00667700, 0x00547700,
    0x00447700, 0x00337700, 0x00217700, 0x00117700,
    0x00007700, 0x00007711, 0x00007721, 0x00007733,
    0x00007744, 0x00007754, 0x00007766, 0x00007777,
    0x00006677, 0x00005477, 0x00004477, 0x00003377,
    0x00002177, 0x00001177, 0x00000044, 0x00110044,
    0x00210044, 0x00330044, 0x00440044, 0x00440033,
    0x00440021, 0x00440011, 0x00440000, 0x00441100,
    0x00442100, 0x00443300, 0x00444400, 0x00334400,
    0x00214400, 0x00114400, 0x00004400, 0x00004411,
    0x00004421, 0x00004433, 0x00004444, 0x00003344,
    0x00002144, 0x00001144, PRGB_WHITE, PRGB_BLACK
};


static UWORD raspi_screen_width;
static UWORD raspi_screen_width_in_bytes;
static UWORD raspi_screen_height;
static UBYTE *raspi_screenbase;
static ULONG raspi_screen_size;

void raspi_screen_init(void)
{
    struct
    {
        prop_tag_2u32_t    set_physical_dim;
        prop_tag_2u32_t    set_virtual_dim;
        prop_tag_1u32_t    set_depth;
        prop_tag_2u32_t    set_virtual_off;
        prop_tag_2u32_t    allocate_buffer;
        prop_tag_1u32_t    get_pitch;
    }
    init_tags =
    {
        {{PROPTAG_SET_PHYS_WIDTH_HEIGHT, 8, 8}},
        {{PROPTAG_SET_VIRT_WIDTH_HEIGHT, 8, 8}},
        {{PROPTAG_SET_DEPTH,             4, 4}, 8},
        {{PROPTAG_SET_VIRTUAL_OFFSET,    8, 8}, 0, 0},
        {{PROPTAG_ALLOCATE_BUFFER,       8, 4}, 0},
        {{PROPTAG_GET_PITCH,             4, 0}}
    };
    raspi_screen_width = init_tags.set_physical_dim.value1 = init_tags.set_virtual_dim.value1 = 1280;
    raspi_screen_height = init_tags.set_physical_dim.value2 = init_tags.set_virtual_dim.value2 = 720;

    raspi_prop_get_tags(&init_tags, sizeof(init_tags));

    raspi_screenbase = (UBYTE*) (init_tags.allocate_buffer.value1 & 0x3fffffff);
    raspi_screen_size = init_tags.allocate_buffer.value2;
    raspi_screen_width_in_bytes = init_tags.get_pitch.value;
}

/*
 * Initialise Raspberry PI palette
 */
void initialise_raspi_palette(WORD mode)
{
    prop_tag_palette_t palette;
    palette.offset = 0;
    palette.length = 256;
    memcpy(palette.palette, raspi_dflt_palette, sizeof(ULONG)*256);
    raspi_prop_get_tag(PROPTAG_SET_PALETTE, &palette, sizeof(prop_tag_palette_t), sizeof(ULONG)*258);
}

#if 0
// Rough debug routines for swapping the background color at various checkpoints
// plus a more advanced one useful for early exceptions.

void raspi_screen_debug(void)
{
    static int counter = 0;
    prop_tag_palette_t palette;
    palette.offset = 0;
    palette.length = 1;
    palette.palette[0] = raspi_dflt_palette[(counter++ % 16) + 2];
    raspi_prop_get_tag(PROPTAG_SET_PALETTE, &palette, sizeof(prop_tag_palette_t), sizeof(ULONG)*3);
}

void raspi_screen_err(ULONG num, ULONG addr, ULONG pc)
{
    prop_tag_palette_t palette;
    palette.offset = 0;
    palette.length = 1;
    palette.palette[0] = raspi_dflt_palette[2];
    raspi_prop_get_tag(PROPTAG_SET_PALETTE, &palette, sizeof(prop_tag_palette_t), sizeof(ULONG)*3);
    int x,y;
    UBYTE c1,c2,c3;
    for(x = 0; x < 640; x ++)
    {
        if (x % 20 == 19)
        {
            num <<= 1;
            addr <<= 1;
            pc <<= 1;
            c1 = c2 = c3 = 15;
        }
        else if (x % 80 == 78)
        {
            c1 = c2 = c3 = 15;
        }
        else
        {
            c1 = (num & 0x80000000) ? 1 : 254;
            c2 = (addr & 0x80000000) ? 2 : 254;
            c3 = (pc & 0x80000000) ? 4 : 254;
        }
        for(y = 100; y < 150; y++)
        {
            raspi_screenbase[y*raspi_screen_width_in_bytes + x] = c1;
        }
        for(y = 150; y < 200; y++)
        {
            raspi_screenbase[y*raspi_screen_width_in_bytes + x] = c2;
        }
        for(y = 200; y < 250; y++)
        {
            raspi_screenbase[y*raspi_screen_width_in_bytes + x] = c3;
        }
    }
}
#endif

WORD raspi_check_moderez(WORD moderez)
{
    WORD current_mode, return_mode;

    if (moderez == (WORD)0xff02) /* ST High */
        moderez = VIDEL_COMPAT|VIDEL_1BPP|VIDEL_80COL|VIDEL_VERTICAL;

    if (moderez < 0)                /* ignore other ST video modes */
        return 0;

    current_mode = raspi_vgetmode();
    return_mode = moderez;          /* assume always valid */
    return (return_mode==current_mode)?0:return_mode;

}

void raspi_get_current_mode_info(UWORD *planes, UWORD *hz_rez, UWORD *vt_rez)
{
    *planes = 8;
    *hz_rez = raspi_screen_width;
    *vt_rez = raspi_screen_height;
}

void raspi_setphys(const UBYTE *addr)
{

}

UBYTE *raspi_physbase(void)
{
    return raspi_screenbase;
}

WORD raspi_setcolor(WORD colorNum, WORD color)
{
    if (colorNum == 0)
        return 0x777;
    else
        return 0x000;

}

void raspi_setrez(WORD rez, WORD videlmode)
{

}

WORD raspi_vgetmode(void)
{
    WORD mode = VIDEL_8BPP;

    if (raspi_screen_width >= 640)
        mode |= VIDEL_80COL;

    if (raspi_screen_height == 240)
        mode |= VIDEL_VGA | VIDEL_VERTICAL;
    else if (raspi_screen_height == 480)
        mode |= VIDEL_VGA;
    else if (raspi_screen_height == 512)
        mode |= VIDEL_PAL | VIDEL_VERTICAL;
    else if (raspi_screen_height == 256)
        mode |= VIDEL_PAL;
    else if (raspi_screen_height == 400)
        mode |= VIDEL_VERTICAL;

    if (raspi_screen_width == 640 && raspi_screen_height == 400)
        mode |= VIDEL_COMPAT;

    return mode;

}


UBYTE * raspi_cell_addr(int x, int y)
{
    ULONG cell_wr = linea_vars.v_cel_wr;
     /* check bounds against screen limits */
    if ( x >= linea_vars.v_cel_mx )
        x = linea_vars.v_cel_mx;           /* clipped x */

    if ( y >= linea_vars.v_cel_my )
        y = linea_vars.v_cel_my;           /* clipped y */

    return raspi_screenbase + x*8 + (cell_wr * y);
}

void raspi_blank_out (int topx, int topy, int botx, int boty)
{
    UWORD color = linea_vars.v_col_bg;             /* bg color value */
    int width, height, row;
    width = (botx - topx + 1) * 8;
    height = (boty - topy + 1) * linea_vars.v_cel_ht;
    UBYTE * addr = raspi_cell_addr(topx, topy);

    if (width >= raspi_screen_width_in_bytes)
    {
        memset(addr, color, height * raspi_screen_width_in_bytes);
    }
    else
    {
        for (row = 0; row < height; row++)
        {
            memset(addr+(row *  raspi_screen_width_in_bytes), color, width);
        }
    }

    color = (color+1) % 64;
}

void raspi_cell_xfer(UBYTE * src, UBYTE * dst)
{
    UWORD fg;
    UWORD bg;
    int fnt_wr, line_wr, y;

    fnt_wr = linea_vars.v_fnt_wr;
    line_wr = linea_vars.v_lin_wr;

    /* check for reversed foreground and background colors */
    if ( linea_vars.v_stat_0 & M_REVID ) {
        fg = linea_vars.v_col_bg;
        bg = linea_vars.v_col_fg;
    }
    else {
        fg = linea_vars.v_col_fg;
        bg = linea_vars.v_col_bg;
    }

    for(y = 0; y < linea_vars.v_cel_ht; y++)
    {
        UBYTE cel = *src;//[fnt_wr*y];
        int pixel;
        for(pixel = 0; pixel < 8; pixel++) {
            dst[pixel + line_wr*y] = (cel & (256>>pixel))?fg:bg;
        }
        src+=fnt_wr;
    }

}

void raspi_neg_cell(UBYTE * cell)
{
    int len;
    linea_vars.v_stat_0 |= M_CRIT;                 /* start of critical section. */
    for(len = 0; len < linea_vars.v_cel_ht; len++)
    {
        *cell = ~*cell;
        cell += linea_vars.v_lin_wr;
    }
    linea_vars.v_stat_0 &= ~M_CRIT;                /* end of critical section. */
}
