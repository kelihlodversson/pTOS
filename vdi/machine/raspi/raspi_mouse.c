/*
 * raspi_mouse.c Raspberry PI hw mouse sprite support
 *
 * Copyright (C) 2018-2026 The pTOS development team
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
#include "obdefs.h"
#include "gsxdefs.h"
#include "vdi_defs.h"
#include "raspi_io.h"
#include "../bios/raspi_mbox.h"
#include "../bios/raspi_screen.h"
#include "tosvars.h"
#include "lineavars.h"
#include "kprint.h"
#include "raspi_mouse.h"


static ULONG last_sprite_checksum;
static ULONG pointer_image[16][16];

static ULONG raspi_cur_calc_checksum(Mcdb *sprite);
static BOOL raspi_hw_cur_set_sprite(Mcdb *sprite);

BOOL raspi_hw_cur_display(Mcdb *sprite, WORD x, WORD y)
{
    ULONG checksum = raspi_cur_calc_checksum(sprite);
    prop_tag_cursor_state_t tag;

    if (checksum != last_sprite_checksum)
    {
        if (!raspi_hw_cur_set_sprite(sprite))
            return FALSE;
        last_sprite_checksum = checksum;
    }
    tag.enable = 1;
    tag.pos_x = x;
    tag.pos_y = y;
    tag.flags = CURSOR_FLAGS_FB_COORDS;
    return raspi_prop_get_tag(PROPTAG_SET_CURSOR_STATE, &tag, sizeof(prop_tag_cursor_state_t), 4*4);
}


static ULONG raspi_cur_calc_checksum(Mcdb *sprite)
{
    int i;
    ULONG res = (ULONG)sprite;
    UWORD *sprite_words = (UWORD*)sprite;
    for(i=0; i<(sizeof(*sprite)/2); i++)
    {
        res ^= ((ULONG)sprite_words[i]) << (i % 24);
    }
    return res;
}

static BOOL raspi_hw_cur_set_sprite(Mcdb *sprite)
{
    int x,y;
    ULONG fg = raspi_dflt_palette[sprite->fg_col] | 0xff000000;
    ULONG bg = raspi_dflt_palette[sprite->bg_col] | 0xff000000;
    ULONG clear = 0x00000000;
    UWORD* mask = sprite->maskdata;
    UWORD* data = sprite->maskdata+1;
    prop_tag_cursor_info_t tag;

    for(y=0; y<16; y++, mask += 2, data += 2)
    {
        UWORD pixel_mask = 0x8000;
        for(x=0; x<16; x++, pixel_mask >>= 1)
        {
            pointer_image[y][x] = ((*mask) & pixel_mask) ? (((*data) & pixel_mask) ? fg : bg) : clear;
        }
    }

    tag.width = tag.height = 16;
    tag.pixels = phys_to_bus((ULONG)pointer_image);
    tag.hotspot_x = sprite->xhot;
    tag.hotspot_y = sprite->yhot;
    return raspi_prop_get_tag(PROPTAG_SET_CURSOR_INFO, &tag, sizeof(prop_tag_cursor_info_t), 6*4);
}
