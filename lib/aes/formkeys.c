/* Standard GEM form keyboard navigation in application memory.
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later. */
#include "gem.h"
#include "application.h"
#include "layout.h"
#include "gemformkey.h"

short mt_form_keybd(OBJECT *tree, short object, short proposed, short key,
                    short *next, short *remaining, short *global)
{
    WORD target = proposed, character = key;
    BOOL cont;
    LAYOUT_METRICS m;
    if (!tree || object < 0 || !next || !remaining || aes_application_session(global) < 0) return 0;
    cont = fm_keybd_step(tree,object,&character,&target);
    if (!cont && !(tree[target].ob_state & SELECTED)) {
        if (!aes_layout_metrics(&m,global)) return 0;
        /* The native FORM_KEYBD entry clips default activation to gl_rfull,
         * preserving the menu bar. No update/mouse lock spans keyboard work. */
        if (!mt_objc_change(tree,target,0,0,m.hbox,m.width,m.height-m.hbox,
                            tree[target].ob_state | SELECTED,1,global)) return 0;
    }
    *next = target; *remaining = character;
    return cont;
}
short form_keybd(OBJECT *tree, short object, short proposed, short key,
                 short *next, short *remaining)
{
    return mt_form_keybd(tree,object,proposed,key,next,remaining,aes_global);
}
