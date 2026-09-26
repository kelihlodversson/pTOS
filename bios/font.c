/*
 * font.c - bios part of font loading and initialization
 *
 * Copyright (C) 2004-2014 by Authors (see below)
 * Copyright (C) 2016-2020 The EmuTOS development team
 *
 * Authors:
 *  MAD  Martin Doering
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

/* #define ENABLE_KDEBUG */

#include "emutos.h"
#include "font.h"
#include "country.h"
#include "string.h"
#include "lineavars.h"

/* RAM-copies of the ROM-fontheaders */
Fonthead fon8x16;
Fonthead fon8x8;
Fonthead fon6x6;

/* system font array used by linea0 */
const Fonthead *sysfonts[4];    /* all three fonts and NULL */

/*
 * font_init - set default font to linea, font ring initialization
 */

void font_init(void)
{
    const Fonthead *f6x6, *f8x8, *f8x16;

    /* ask country.c for the right fonts */

    get_fonts(&f6x6, &f8x8, &f8x16);

    /* copy the ROM-fontheaders of 3 system fonts to RAM */

    fon6x6 = *f6x6;
    fon8x8 = *f8x8;
    fon8x16 = *f8x16;

    /*
     * now in RAM, chain only the 8x8 & 8x16 font headers in a linked list.
     * this is how Atari TOS does it, and is required for text_init()
     * [in vdi_text.c] to set up font_ring[] correctly, and subsequently
     * calculate DEV_TAB[5] and font_count correctly.
     */
    fon8x8.next_font = &fon8x16;
    linea_vars.font_count = 1;     /* number of different font ids in font_ring[] */

    /* Initialize the system font array for linea */

    sysfonts[0] = f6x6;
    sysfonts[1] = f8x8;
    sysfonts[2] = f8x16;
    sysfonts[3] = NULL;
}

/*
 * font_set_default
 *
 * choose default font depending on screen height, and set
 * linea variables according to chosen font configuration
 */

void font_set_default(void)
{
    Fonthead *font;

    font = (linea_vars.V_REZ_VT < 400) ? &fon8x8 : &fon8x16;

    linea_vars.v_cel_ht = font->form_height;
    linea_vars.v_cel_wr = linea_vars.v_lin_wr * font->form_height;
    linea_vars.v_cel_mx = (linea_vars.V_REZ_HZ / font->max_cell_width) - 1;
    linea_vars.v_cel_my = (linea_vars.V_REZ_VT / font->form_height) - 1;

    linea_vars.v_fnt_wr = font->form_width;
    linea_vars.v_fnt_st = font->first_ade;
    linea_vars.v_fnt_nd = font->last_ade;
    /*
     * font->dat_table/off_table are compile-time-initialized pointers
     * (baked in by each fnt_*.c source's own Fonthead initializer,
     * pointing at const arrays in that same translation unit). On
     * x86-64, startup.c's x86_64_apply_higher_half_relocations() (#343)
     * has already rebased them (and everything else the PE loader's own
     * base relocation table covers) from their low, pre-higher-half-jump
     * load address to the higher-half virtual alias, before font_init()'s
     * "fon8x8 = *f8x8" struct copy below carries the value forward -- no
     * per-arch handling needed here.
     */
    linea_vars.v_fnt_ad = (const UWORD *)font->dat_table;
    linea_vars.v_off_ad = font->off_table;
}
