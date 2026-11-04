/* Original GEM form/button interaction shared with caller-local libaes.
 * Copyright 1999, Caldera Thin Clients, Inc.
 * Copyright 2002-2026 The EmuTOS development team. GPL version 2 or later. */
#include "emutos.h"
#include "obdefs.h"
#include "aesext.h"
#include "gemobjop.h"
#include "gemoblib.h"
#include "gemgrlib.h"
#include "gemfmlib.h"
#include "gemgsxif.h"
#include "gemwatch.h"
#ifdef AES_FORM_BINDINGS
#define AES_FORM_BUTTON_UNIT
#include AES_FORM_BINDINGS
#else
#include "struct.h"
#include "gemlib.h"
#include "gemevlib.h"
#endif

WORD fm_button(OBJECT *tree, WORD new_obj, WORD clks, WORD *pnew_obj)
{
    WORD    tobj;
    WORD    orword;
    WORD    parent, state, flags;
    WORD    cont, tstate, tflags;
    WORD    rets[6];
    OBJECT  *objptr;

    cont = TRUE;
    orword = 0;

    state = ob_fs(tree, new_obj, &flags);

    /* handle touchexit case: if double click, then set high bit */
    if (flags & TOUCHEXIT)
    {
        if (clks == 2)
            orword = 0x8000;
        cont = FALSE;
    }

    /* handle selectable case */
    if ((flags & SELECTABLE) && !(state & DISABLED))
    {
        /* if it's a radio button */
        if (flags & RBUTTON)
        {
            /* check siblings to find and turn off the old RBUTTON */
            parent = get_par(tree, new_obj);
            objptr = tree + parent;
            tobj = objptr->ob_head;
            while (tobj != parent)
            {
                tstate = ob_fs(tree, tobj, &tflags);
                if ((tflags & RBUTTON) &&
                    ((tstate & SELECTED) || (tobj == new_obj)))
                {
                    if (tobj == new_obj)
                        state = tstate |= SELECTED;
                    else
                        tstate &= ~SELECTED;
                    ob_change(tree, tobj, tstate, TRUE);
                }
                objptr = tree + tobj;
                tobj = objptr->ob_next;
            }
        }
        else
        {   /* turn on new object   */
            if (gr_watchbox(tree, new_obj, state^SELECTED, state))
                state ^= SELECTED;
        }
        /* if not touchexit then wait for button up */
        if (cont && (flags & (SELECTABLE | EDITABLE)))
            ev_button(1, 0x00001, 0x0000, rets);
    }

    /* see if this selection gets us out */
    if ((state & SELECTED) && (flags & EXIT))
        cont = FALSE;

    /* handle click on another editable field */
    if (cont && !(flags & EDITABLE))
        new_obj = 0;

    *pnew_obj = new_obj | orword;

    return cont;
}

