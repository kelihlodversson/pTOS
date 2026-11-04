/* Original modal GEM form loop, shared with the userspace bindings.
 * Copyright 1999, Caldera Thin Clients, Inc.
 * Copyright 2002-2026 The EmuTOS development team. GPL version 2 or later. */
#include "emutos.h"
#include "obdefs.h"
#include "gemformkey.h"
#ifdef AES_MODAL_BINDINGS
#include AES_MODAL_BINDINGS
#else
#include "struct.h"
#include "aesdefs.h"
#include "aesext.h"
#include "gemlib.h"
#include "gemfmlib.h"
#include "gemoblib.h"
#include "gemobed.h"
#include "gemevlib.h"
#include "geminput.h"
#include "gemgraf.h"
#include "biosbind.h"
#define FM_DO_ENTER() (fm_own(TRUE), TRUE)
#define FM_DO_LEAVE() fm_own(FALSE)
#define FM_DO_FLUSH() fq()
#define FM_DO_CLIP() gsx_sclip(&gl_rfull)
#define FM_DO_BELL() Bconout(2,0x07)
#define FM_DO_OK() TRUE
#if CONF_WITH_MENU_EXTENSION
#define FM_DO_WAIT(rets) ev_multi(MU_KEYBD|MU_BUTTON,NULL,NULL,NULL,0L,0x0002ff01L,NULL,rets)
#else
#define FM_DO_WAIT(rets) ev_multi(MU_KEYBD|MU_BUTTON,NULL,NULL,0L,0x0002ff01L,NULL,rets)
#endif
#endif

static WORD fm_inifld(OBJECT *tree, WORD start_fld)
{
    /* position cursor on the starting field */
    if (start_fld == 0)
        start_fld = fm_find_field(tree,0,FM_FORWARD);

    return start_fld;
}






/*
 *  ForM DO routine to allow the user to interactively fill out a
 *  form.  The cursor is placed at the starting field.  This routine
 *  returns the object that caused the exit to occur
 */
WORD fm_do(OBJECT *tree, WORD start_fld)
{
    BOOL editing = FALSE;
    WORD    edit_obj;
    WORD    next_obj;
    WORD    which, cont;
    WORD    idx;
    WORD    rets[6];

    /* grab ownership of screen and mouse */
    if (!FM_DO_ENTER()) return 0;

    /* flush keyboard */
    FM_DO_FLUSH();

    /* set clip so we can draw chars, and invert buttons */
    FM_DO_CLIP();

    /* determine which is the starting field to edit */
    next_obj = fm_inifld(tree, start_fld);
    edit_obj = 0;

    /* interact with user */
    cont = TRUE;
    while(cont && FM_DO_OK())
    {
        /* position cursor on the selected editing field */
        if ((next_obj != 0) && (edit_obj != next_obj))
        {
            edit_obj = next_obj;
            next_obj = 0;
            ob_edit(tree, edit_obj, 0, &idx, EDINIT);
            editing = FM_DO_OK();
        }
        /* wait for mouse or key */
        which = FM_DO_WAIT(rets);
        if (!FM_DO_OK()) { cont = FALSE; next_obj = 0; break; }

        /* handle keyboard event */
        if (which & MU_KEYBD)
        {
            cont = fm_keybd(tree, edit_obj, &rets[4], &next_obj);
            if (rets[4])
              ob_edit(tree, edit_obj, rets[4], &idx, EDCHAR);
        }

        /* handle button event */
        if (which & MU_BUTTON)
        {
            next_obj = ob_find(tree, ROOT, MAX_DEPTH, rets[0], rets[1]);
            if (next_obj == NIL)
            {
                FM_DO_BELL();   /* bell sound */
                next_obj = 0;
            }
            else
                cont = fm_button(tree, next_obj, rets[5], &next_obj);
        }

        /* handle end of field clean up */
        if (!cont ||
            ((next_obj != 0) && (next_obj != edit_obj)))
        {
            ob_edit(tree, edit_obj, 0, &idx, EDEND);
            editing = FALSE;
        }
    }

    if (!FM_DO_OK() && editing) ob_edit(tree,edit_obj,0,&idx,EDEND);

    /* give up mouse and screen ownership */
    FM_DO_LEAVE();

    /* return exit object */
    return FM_DO_OK() ? next_obj : 0;
}


