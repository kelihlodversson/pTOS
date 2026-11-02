/* Child completion must not invalidate file nodes held by nested operations.
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later. */
#include "emutos.h"
#include "obdefs.h"
#include "aesdefs.h"
#include "deskbind.h"
#include "deskglob.h"
#include "desksupp.h"
#include "deskfun.h"
#include "deskrefresh.h"
#include "gemdos.h"
#include "aesbind.h"
#include "string.h"

typedef struct { char name[LEN_ZFNAME]; } SELECTED_NAME;
static BOOL pending;

void desk_refresh_request(void) { pending = TRUE; }

static void exchange(SELECTED_NAME *a, SELECTED_NAME *b)
{
    SELECTED_NAME temp = *a; *a = *b; *b = temp;
}
static void sift(SELECTED_NAME *names, LONG root, LONG count)
{
    LONG child;
    while (root < count/2) {
        child = root*2+1;
        if (child+1 < count && strcmp(names[child].name,names[child+1].name) < 0) child++;
        if (strcmp(names[root].name,names[child].name) >= 0) return;
        exchange(names+root,names+child); root = child;
    }
}
static void sort(SELECTED_NAME *names, LONG count)
{
    LONG i;
    for (i = count/2; i > 0; ) sift(names,--i,count);
    for (i = count-1; i > 0; i--) {
        exchange(names,names+i); sift(names,0,i);
    }
}
static BOOL selected(const SELECTED_NAME *names, LONG count, const char *name)
{
    LONG first = 0, middle, comparison;
    while (first < count) {
        middle = first+(count-first)/2;
        comparison = strcmp(name,names[middle].name);
        if (!comparison) return TRUE;
        if (comparison < 0) count = middle;
        else first = middle+1;
    }
    return FALSE;
}
static BOOL refresh(WNODE *window)
{
    FNODE *node;
    LONG count = 0, at = 0;
    SELECTED_NAME *names = NULL;
    GRECT rectangle;

    for (node = window->w_pnode.p_flist; node; node = node->f_next)
        if (node->f_selected) count++;
    if (count) {
        names = dos_alloc_anyram(count*sizeof(*names));
        if (!names) return FALSE; /* Keep the old nodes/selections on OOM. */
        for (node = window->w_pnode.p_flist; node; node = node->f_next)
            if (node->f_selected) strcpy(names[at++].name,node->f_name);
        sort(names,count);
    }

    pn_active(&window->w_pnode,TRUE);
    for (node = window->w_pnode.p_flist; node; node = node->f_next)
        node->f_selected = selected(names,count,node->f_name);
    if (names) dos_free(names);
    win_classify_selection(window);
    /* Restore FNODE selection before rebuilding OBJECTs and info text. */
    desk_verify(window->w_id,TRUE);
    win_sinfo(window,TRUE);
    wind_get_grect(window->w_id,WF_WXYWH,&rectangle);
    fun_msg(WM_REDRAW,window->w_id,rectangle.g_x,rectangle.g_y,rectangle.g_w,rectangle.g_h);
    return TRUE;
}

/* Only the outer desktop loop calls this, after dialogs and file operations
 * have returned. Reentrant messages request another pass rather than freeing
 * any of their caller's nodes. Updating views must not change keyboard/menu
 * selection context or top another window. */
void desk_refresh_run(void)
{
    WNODE *window;
    WORD active, last;
    if (!pending) return;
    pending = FALSE;
    active = G.g_cwin; last = G.g_wlastsel;
    desk_busy_on();
    for (window = G.g_wfirst; window; window = window->w_next)
        if (window->w_id && !refresh(window)) pending = TRUE;
    if (active != DESKWH && !win_find(active)) {
        window = win_ontop(); active = window ? window->w_id : DESKWH;
    }
    desk_verify(active,FALSE);
    G.g_wlastsel = last == DESKWH || win_find(last) ? last : active;
    desk_busy_off();
}
