/* Shared original AES implementation; included by native AES and libaes.
 * Copyright 1999, Caldera Thin Clients, Inc.
 * Copyright 2002-2026 The EmuTOS development team. GPL version 2 or later. */
static void ob_change_native(OBJECT *tree, WORD obj, UWORD new_state, WORD redraw)
{
    WORD flags, obtype, th;
    GRECT t;
    UWORD curr_state;
    OBSPEC spec;
    OBJECT *objptr;

    ob_sst(tree, obj, &spec, (WORD*)&curr_state, &obtype, &flags, &t, &th);

    if ((curr_state == new_state) || (spec.index == -1L))
        return;

    objptr = tree + obj;
    objptr->ob_state = new_state;

    /* if no redraw, we're done */
    if (!redraw)
        return;

    ob_offset(tree, obj, &t.g_x, &t.g_y);

    gsx_moff();

    if (th < 0)
        th = 0;

    switch(obtype)
    {
    case G_USERDEF: /* just call the user's drawing routine */
        ob_user(tree, obj, &t, spec, curr_state, new_state);
        redraw = FALSE;
        break;
    case G_ICON:    /* never use XOR for icons */
        break;
    default:        /* others: handle change of SELECTED state */
#if OB_RENDER_3D
        /*
         * if we can toggle selection via XOR, we use XOR and don't redraw.
         * if we have a G_IMAGE object that we're deselecting, we use XOR
         * but force a redraw (xor_is_ok() returns FALSE for G_IMAGE).
         */
        if ((new_state ^ curr_state) & SELECTED)
        {
            BOOL ok = xor_is_ok(obtype,flags, spec);
            if (ok || ((obtype == G_IMAGE) && !(new_state & SELECTED)))
            {
                bb_fill(MD_XOR, FIS_SOLID, IP_SOLID, t.g_x+th, t.g_y+th,
                        t.g_w-(2*th), t.g_h-(2*th));
                redraw = !ok;
            }
        }
#else
        /* no 3D support: always use XOR if SELECTED state has changed */
        if ((new_state ^ curr_state) & SELECTED)
        {
            bb_fill(MD_XOR, FIS_SOLID, IP_SOLID, t.g_x+th, t.g_y+th,
                    t.g_w-(2*th), t.g_h-(2*th));
            redraw = FALSE;
        }
#endif
    }

    if (redraw)
        just_draw(tree, obj, t.g_x, t.g_y);

    gsx_mon();
}
