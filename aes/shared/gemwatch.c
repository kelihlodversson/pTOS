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
#include AES_FORM_BINDINGS
#else
#include "struct.h"
#include "gemlib.h"
#include "gemevlib.h"
#endif

WORD gr_watchbox(OBJECT *tree, WORD obj, WORD instate, WORD outstate)
{
    BOOL    out;
    WORD    state;
    GRECT   t;

    gsx_sclip(&gl_rscreen);
    ob_actxywh(tree, obj, &t);

    out = FALSE;
    do
    {
        state = (out) ? outstate : instate;
        ob_change(tree, obj, state, TRUE);
        out = !out;
    } while (gr_stilldn(out, t.g_x, t.g_y, t.g_w, t.g_h));

    return out;
}

