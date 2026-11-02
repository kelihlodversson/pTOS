/* Real refresh helper: retained dialog nodes, selections and window context. */
#include <stdio.h>
#include <stdlib.h>
#include "emutos.h"
#include "obdefs.h"
#include "aesdefs.h"
#include "deskbind.h"
#include "deskglob.h"
#include "deskrefresh.h"
#include "desksupp.h"
#include "deskfun.h"
#include "aesbind.h"
#include "string.h"
#define VERIFY(c) do { if (!(c)) { fprintf(stderr,"refresh:%d: %s\n",__LINE__,#c); abort(); } } while (0)
GLOBES G;
static WNODE windows[3];
static unsigned scans, draws, allocations, fail_alloc, again;
static const unsigned files = 1025;
static ANODE association;

static void fill(PNODE *path, BOOL initial)
{
    unsigned i, number;
    FNODE *nodes = calloc(files,sizeof(*nodes));
    VERIFY(nodes);
    for (i = 0; i < files; i++) {
        /* Reorder by a permutation; one selected file disappears and a new
         * file takes its place, rather than inheriting its array position. */
        number = initial ? i : (i*37)%files;
        if (!initial && number == 4) number = files;
        sprintf(nodes[i].f_name,"F%04u.TXT",number);
        nodes[i].f_selected = initial && !(number%2);
        nodes[i].f_next = i+1 < files ? &nodes[i+1] : NULL;
    }
    path->p_fbase = path->p_flist = nodes;
}
void *dos_alloc_anyram(LONG size)
{
    allocations++;
    if (fail_alloc) { fail_alloc--; return NULL; }
    return malloc(size);
}
WORD dos_free(void *p) { free(p); return 0; }
WORD pn_active(PNODE *path, BOOL folders)
{
    VERIFY(folders);
    scans++;
    free(path->p_fbase); fill(path,FALSE);
    if (again) { again = 0; desk_refresh_request(); }
    return 0;
}
WNODE *win_find(WORD handle)
{
    unsigned i;
    for (i = 0; i < 3; i++) if (windows[i].w_id == handle) return &windows[i];
    return NULL;
}
WNODE *win_ontop(void) { return &windows[1]; }
void win_classify_selection(WNODE *window)
{
    FNODE *node;
    for (node = window->w_pnode.p_flist; node; node = node->f_next)
        if (node->f_selected) { node->f_pa = &association; node->f_isap = TRUE; }
}
void desk_verify(WORD handle, WORD rebuild)
{
    FNODE *node;
    WNODE *window = win_find(handle);
    unsigned number;
    VERIFY(window);
    if (rebuild)
        for (node = window->w_pnode.p_flist; node; node = node->f_next) {
            VERIFY(sscanf(node->f_name,"F%u.TXT",&number) == 1);
            VERIFY(!!node->f_selected == (number < files && !(number%2)));
            /* No view calculation here: all 1025 entries, including those
             * outside the viewport, must already have valid associations. */
            if (node->f_selected) VERIFY(node->f_pa == &association && node->f_isap);
        }
    G.g_cwin = G.g_wlastsel = handle; G.g_croot = window->w_root;
}
void win_sinfo(WNODE *window, BOOL selected) { VERIFY(selected && G.g_cwin == window->w_id); }
WORD wind_get_grect(WORD handle, WORD field, GRECT *rect)
{
    VERIFY(win_find(handle) && field == WF_WXYWH); memset(rect,0,sizeof(*rect)); return 1;
}
void fun_msg(WORD type, WORD handle, WORD a, WORD b, WORD c, WORD d)
{
    (void)a; (void)b; (void)c; (void)d;
    VERIFY(type == WM_REDRAW && G.g_cwin == handle); draws++;
}
void desk_busy_on(void) { }
void desk_busy_off(void) { }
int main(void)
{
    unsigned i;
    FNODE *held;
    for (i = 0; i < 3; i++) {
        windows[i].w_id = i+1; windows[i].w_root = i+11;
        windows[i].w_next = i+1 < 3 ? &windows[i+1] : NULL;
        fill(&windows[i].w_pnode,TRUE);
    }
    G.g_wfirst = windows; G.g_cwin = 2; G.g_croot = 12; G.g_wlastsel = 1;
    held = windows[0].w_pnode.p_flist;
    desk_refresh_request(); desk_refresh_request();
    VERIFY(!scans && !allocations && !strcmp(held->f_name,"F0000.TXT"));
    fail_alloc = 1;
    desk_refresh_run();
    VERIFY(!strcmp(held->f_name,"F0000.TXT") && scans == 2 && draws == 2);
    VERIFY(G.g_cwin == 2 && G.g_croot == 12 && G.g_wlastsel == 1);
    again = 1;
    desk_refresh_run(); /* Retries the refused refresh, preserving a reentrant request. */
    VERIFY(scans == 5 && draws == 5);
    desk_refresh_run(); VERIFY(scans == 8 && draws == 8);
    desk_refresh_run(); VERIFY(scans == 8 && draws == 8);
    VERIFY(G.g_cwin == 2 && G.g_croot == 12 && G.g_wlastsel == 1);
    for (i = 0; i < 3; i++) free(windows[i].w_pnode.p_fbase);
    puts("Deferred desktop refresh preserves selections, dialog nodes and active context: PASS");
    return 0;
}
