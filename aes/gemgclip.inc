/* Shared original AES implementation; included by native AES and libaes.
 * Copyright 1999, Caldera Thin Clients, Inc.
 * Copyright 2002-2026 The EmuTOS development team. GPL version 2 or later. */
void gsx_sclip(const GRECT *pt)
{
    gl_clip = *pt;

    if (gl_clip.g_w && gl_clip.g_h)
    {
        ptsin[0] = gl_clip.g_x;
        ptsin[1] = gl_clip.g_y;
        ptsin[2] = gl_clip.g_x + gl_clip.g_w - 1;
        ptsin[3] = gl_clip.g_y + gl_clip.g_h - 1;
        vs_clip( TRUE, ptsin);
    }
    else
        vs_clip( FALSE, ptsin);
}


/*
 *  Routine to get the current clip setting
 */
void gsx_gclip(GRECT *pt)
{
    *pt = gl_clip;
}


/*
 *  Routine to return TRUE iff the specified rectangle intersects the
 *  current clip rectangle ... or clipping is off (?)
 */
WORD gsx_chkclip(GRECT *pt)
{
    /* if clipping is on */
    if (gl_clip.g_w && gl_clip.g_h)
    {
        if ((pt->g_y + pt->g_h) < gl_clip.g_y)
            return FALSE;                   /* all above    */
        if ((pt->g_x + pt->g_w) < gl_clip.g_x)
            return FALSE;                   /* all left     */
        if ((gl_clip.g_y + gl_clip.g_h) <= pt->g_y)
            return FALSE;                   /* all below    */
        if ((gl_clip.g_x + gl_clip.g_w) <= pt->g_x)
            return FALSE;                   /* all right    */
    }

    return TRUE;
}
