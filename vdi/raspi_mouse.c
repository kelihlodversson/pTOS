/*
 * raspi_mouse.c Raspberry PI hw mouse sprite support
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
#include "asm.h"
#include "biosbind.h"
#include "xbiosbind.h"
#include "aespub.h"
#include "obdefs.h"
#include "gsxdefs.h"
#include "vdi_defs.h"
#include "raspi_io.h"
#include "../bios/raspi_mbox.h"
#include "../bios/raspi_screen.h"
#include "../bios/tosvars.h"
#include "../bios/lineavars.h"
#include "kprint.h"
#include "raspi_mouse.h"


ULONG last_sprite_checksum;
static ULONG pointer_image[16][16];

static ULONG raspi_cur_calc_checksum(Mcdb *sprite);
static void raspi_hw_cur_set_sprite(Mcdb *sprite);

void raspi_hw_cur_display(Mcdb *sprite, WORD x, WORD y)
{
    ULONG checksum = raspi_cur_calc_checksum(sprite);
    if (checksum != last_sprite_checksum)
    {
        raspi_hw_cur_set_sprite(sprite);
        last_sprite_checksum = checksum;
    }
    prop_tag_cursor_state_t tag;
    tag.enable = 1;
    tag.pos_x = x;
    tag.pos_y = y;
    tag.flags = CURSOR_FLAGS_FB_COORDS;
    raspi_prop_get_tag(PROPTAG_SET_CURSOR_STATE, &tag, sizeof(prop_tag_cursor_state_t), 4*4);
}


static ULONG raspi_cur_calc_checksum(Mcdb *sprite)
{
    int i;
    ULONG res = (ULONG)sprite;
    UWORD *sprite_words = (UWORD*)sprite;
    for(i=0; i<(sizeof(Mcdb)/2); i++)
    {
        res ^= ((ULONG)sprite_words[i]) << (i % 24);
    }
    return res;
}

static void raspi_hw_cur_set_sprite(Mcdb *sprite)
{
    int x,y;
    ULONG fg = raspi_dflt_palette[sprite->fg_col] | 0xff000000;
    ULONG bg = raspi_dflt_palette[sprite->bg_col] | 0xff000000;
    ULONG clear = 0x00000000;
    UWORD* mask = sprite->maskdata;
    UWORD* data = sprite->maskdata+1;
    for(y=0; y<16; y++, mask += 2, data += 2)
    {
        UWORD pixel_mask = 0x8000;
        for(x=0; x<16; x++, pixel_mask >>= 1)
        {
            pointer_image[x][y] = ((*mask) & pixel_mask) ? (((*data) & pixel_mask) ? fg : bg) : clear;
        }
    }

    prop_tag_cursor_info_t tag;
    tag.width = tag.height = 16;
    tag.pixels = GPU_MEM_BASE + (ULONG)pointer_image;
    tag.hotspot_x = sprite->xhot;
    tag.hotspot_y = sprite->yhot;
    raspi_prop_get_tag(PROPTAG_SET_CURSOR_INFO, &tag, sizeof(prop_tag_cursor_info_t), 6*4);
}
