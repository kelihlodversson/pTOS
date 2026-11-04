/*      GEMFMALT.C              09/01/84 - 06/20/85     Lee Lorenzen    */
/*      merge High C vers. w. 2.2 & 3.0         8/20/87         mdf     */

/*
*       Copyright 1999, Caldera Thin Clients, Inc.
*                 2002-2022 The EmuTOS development team
*
*       This software is licenced under the GNU Public License.
*       Please see LICENSE.TXT for further information.
*
*                  Historical Copyright
*       -------------------------------------------------------------
*       GEM Application Environment Services              Version 2.3
*       Serial No.  XXXX-0000-654321              All Rights Reserved
*       Copyright (C) 1987                      Digital Research Inc.
*       -------------------------------------------------------------
*/

/* #define ENABLE_KDEBUG */

#include "emutos.h"
#include "struct.h"
#include "obdefs.h"
#include "intmath.h"
#include "aesdefs.h"
#include "aesext.h"
#include "aesvars.h"
#include "gemlib.h"
#include "gem_rsc.h"

#include "gemgsxif.h"
#include "gemctrl.h"
#include "gemoblib.h"
#include "gemobed.h"
#include "geminit.h"
#include "geminput.h"
#include "gemrslib.h"
#include "gemfmlib.h"
#include "gemwmlib.h"
#include "rectfunc.h"
#include "gemfmalt.h"
#include "geminput.h"

#if !defined(X32_USERLAND)
WORD fm_alert(WORD defbut, char *palstr)
{
    WORD i;
    OBJECT *tree = rs_trees[DIALERT];
    GRECT d, t;
#if CONF_WITH_3D_OBJECTS
    WORD color;

    color = (backgrcol < gl_ws.ws_ncolors) ? backgrcol : WHITE;
    tree[ROOT].ob_spec.index = (tree[ROOT].ob_spec.index & 0xffffff80L) | 0x70L | (color & 0x000f);
#endif

    set_mouse_to_arrow();
    tree = fm_alert_tree(defbut, palstr, &d);

    /* save screen underneath the alert    */
    wm_update(BEG_UPDATE);
    gsx_gclip(&t);
    bb_save(&d);

    /* draw the alert       */
    gsx_sclip(&d);
    ob_draw(tree, ROOT, MAX_DEPTH);

    /*
     * turn on the mouse and set the mouse owner.  the latter is required
     * for DAs to be able to issue form_alert()s.
     *
     * if we don't update mouse ownership, the desktop will remain the
     * mouse owner, and the system will queue any mouse clicks to the
     * desktop's evnt_multi(), rather than the evnt_multi() issued by
     * the fm_do() below.  this will result in the DA (and then the whole
     * system) hanging.
     *
     * ct_mouse(TRUE) no longer does this itself as of #146/803fe7a8: it
     * is also called from GRAF_MOUSE handling now, where reassigning
     * ownership on every mouse-form toggle would steal it from whoever
     * actually holds it.
     */
    ct_mouse(TRUE);
    gl_mowner = rlr;

    /* let user pick button */
    i = fm_do(tree, 0);

    /* turn off mouse if necessary */
    ct_mouse(FALSE);

    /* restore saved screen */
    gsx_sclip(&d);
    bb_restore(&d);
    gsx_sclip(&t);
    wm_update(END_UPDATE);

    /* return selection     */
    return i - BUTOFF + 1;
}
#endif /* !X32_USERLAND */
