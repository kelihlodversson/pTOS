/* Shared original AES implementation; included by native AES and libaes.
 * Copyright 1999, Caldera Thin Clients, Inc.
 * Copyright 2002-2026 The EmuTOS development team. GPL version 2 or later. */
void gsx_cline(UWORD x1, UWORD y1, UWORD x2, UWORD y2)
{
    WORD pxy[4] = { x1, y1, x2, y2 };

    gsx_moff();
    v_pline(2, pxy);
    gsx_mon();
}


/*
 *  Routine to set the text, writing mode, and color attributes
 */
void gsx_attr(UWORD text, UWORD mode, UWORD color)
{
    WORD    tmp;

    tmp = intin[0];
    contrl.nptsin = 0;
    contrl.nintin = 1;
    contrl.handle = gl_handle;
    if (mode != gl_mode)
    {
        contrl.code = SET_WRITING_MODE;
        intin[0] = gl_mode = mode;
        gsx2();
    }

    contrl.code = 0;
    if (text)
    {
        if (color != gl_tcolor)
        {
            contrl.code = SET_TEXT_COLOR;
            gl_tcolor = color;
        }
    }
    else
    {
        if (color != gl_lcolor)
        {
            contrl.code = SET_LINE_COLOR;
            gl_lcolor = color;
        }
    }

    if (contrl.code)
    {
        intin[0] = color;
        gsx2();
    }
    intin[0] = tmp;
}


/*
 *  Routine to draw a box using the current attributes
 */
static void gsx_box(GRECT *pt)
{
    gsx_bxpts(pt);
    v_pline(5, ptsin);
}
