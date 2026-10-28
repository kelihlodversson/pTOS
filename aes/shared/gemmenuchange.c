/* Native GEM menu state changes, shared with caller-local libaes.
 * Copyright 1999, Caldera Thin Clients, Inc.
 * Copyright 2002-2026 The EmuTOS development team. GPL version 2 or later. */
#include "emutos.h"
#include "obdefs.h"
#ifdef AES_MENU_BINDINGS
#include AES_MENU_BINDINGS
#else
#include "struct.h"
#include "aesvars.h"
#include "gemgsxif.h"
#include "gemoblib.h"
#include "gemmnlib.h"
#define MENU_APPLY(tree,item,state,draw) (ob_change(tree,item,state,draw), TRUE)
#endif

/*
 *  Routine to change the state of a particular object.  The change in
 *  state will not occur if the object is disabled and the chkdisabled
 *  parameter is set.  The object will be drawn with its new state only
 *  if the dodraw parameter is set.
 */
BOOL do_chg(OBJECT *tree, WORD iitem, UWORD chgvalue,
             WORD dochg, WORD dodraw, WORD chkdisabled)
/* tree:         tree that holds item */
/* iitem:        item to affect       */
/* chgvalue:     bit value to change  */
/* dochg:        set or reset value   */
/* dodraw:       draw resulting change*/
/* chkdisabled:  only if item enabled */
{
    UWORD   curr_state;
    OBJECT  *obj;

    obj = tree + iitem;
    curr_state = obj->ob_state;
    if (chkdisabled && (curr_state & DISABLED) )
        return FALSE;

    if (dochg)
        curr_state |= chgvalue;
    else
        curr_state &= ~chgvalue;

    if (dodraw)
        gsx_sclip(&gl_rzero);

    return MENU_APPLY(tree, iitem, curr_state, dodraw);
}


