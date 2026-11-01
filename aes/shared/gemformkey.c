/* Original AES form field search and keyboard decisions.
 * Copyright 1999, Caldera Thin Clients, Inc.
 * Copyright 2002-2026 The EmuTOS development team. GPL version 2 or later. */
#include "emutos.h"
#include "obdefs.h"
#include "scancode.h"
#include "gemformkey.h"
#ifdef AES_FORMKEY_BINDINGS
#include AES_FORMKEY_BINDINGS
#else
#include "gemfmlib.h"
#include "gemoblib.h"
#define FM_KEYBD_NAME fm_keybd
#define FM_SELECT_DEFAULT(tree,obj) ob_change(tree,obj,tree[obj].ob_state | SELECTED,TRUE)
#endif

/*
 *  Routine to find the next editable text field, or a field that
 *  is marked as a default return field.
 */
WORD fm_find_field(const OBJECT *tree, WORD start_obj, WORD which)
{
    WORD obj, flag, state, inc;
    WORD            theflag;

    /* Forward from the last record has no successor to inspect. */
    if (which == FM_FORWARD && (tree[start_obj].ob_flags & LASTOB)) return start_obj;
    obj = 0;
    flag = EDITABLE;
    inc = 1;
    switch(which)
    {
    case FM_BACKWARD:
        inc = -1;
        FALLTHROUGH;
    case FM_FORWARD:
        obj = start_obj + inc;
        break;
    case FM_DEFLT:
        flag = DEFAULT;
        break;
    }

    while (obj >= 0)
    {
        state = tree[obj].ob_state;
        theflag = tree[obj].ob_flags;
        if (!(theflag & HIDETREE) && !(state & DISABLED))
        {
            if (theflag & flag)
                return(obj);
        }
        if (theflag & LASTOB)
            obj = -1;
        else
            obj += inc;
    }

    return start_obj;
}


WORD FM_KEYBD_NAME(OBJECT *tree, WORD obj, WORD *pchar, WORD *pnew_obj)
{
    WORD    direction;

    /* handle character */
    direction = -1;
    switch(*pchar)
    {
    case RETURN:
    case ENTER:
        obj = 0;
        direction = FM_DEFLT;
        break;
    case ARROW_UP:
        direction = FM_BACKWARD;
        break;
    case TAB:       /* shift-tab, ctrl-tab, alt-tab have the same scancode */
    case ARROW_DOWN:
        direction = FM_FORWARD;
        break;
    }

    if (direction != -1)
    {
        *pchar = 0x00;
        *pnew_obj = fm_find_field(tree, obj, direction);
        if ((direction == FM_DEFLT) && (*pnew_obj != 0))
        {
            FM_SELECT_DEFAULT(tree,*pnew_obj);
            return FALSE;
        }
    }

    return TRUE;
}
