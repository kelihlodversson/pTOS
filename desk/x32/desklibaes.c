/* EmuDesk's historical binding arrays over the standard GEM library API.
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later. */
#include "emutos.h"
#include "gem.h"
#include "aesdefs.h"
#include "string.h"
#include "gemrslib.h"
#include "cuis.h"
#include "syscall.h"
#include "shutdown.h"

LONG desk_reap_child(WORD pid);
LONG desk_reap_child(WORD pid)
{
    /* EmuDesk is the launching parent. AES notification does not reap for it.
     * A stale message must never turn into wait-any or a blocking wait. */
    return pid > 0 ? desk_syscall(1,0x13a,pid,1,0,0) : -33;
}

void desk_poweroff(void);
void desk_poweroff(void)
{
    (void)desk_syscall(1,GEMDOS_SHUTDOWN,SHUT_POWER,0,0,0);
}
LONG desk_shutdown_token(void);
LONG desk_shutdown_token(void)
{
    LONG session = desk_syscall(CUIS_SERVICE_CLASS,CUIS_SESSION_START,0,0,0,0);
    return session < 0 ? session : desk_syscall(CUIS_SERVICE_CLASS,CUIS_COHORT_QUERY,
        session,CUIS_COHORT_ID,0,0);
}
BOOL desk_shutdown_refused(LONG token);
BOOL desk_shutdown_refused(LONG token)
{
    LONG session = desk_syscall(CUIS_SERVICE_CLASS,CUIS_SESSION_START,0,0,0,0);
    return token > 0 && session >= 0 && desk_syscall(CUIS_SERVICE_CLASS,CUIS_COHORT_QUERY,
        session,CUIS_COHORT_RESULT_ID,0,0) == token &&
        desk_syscall(CUIS_SERVICE_CLASS,CUIS_COHORT_QUERY,session,CUIS_COHORT_RESULT_OFFSET,0,0) == 0 &&
        desk_syscall(CUIS_SERVICE_CLASS,CUIS_COHORT_QUERY,session,CUIS_COHORT_RESULT_BYTES,0,0) == 16;
}
BOOL desk_termination_ready(void);
BOOL desk_termination_ready(void)
{
    LONG session = desk_syscall(CUIS_SERVICE_CLASS,CUIS_SESSION_START,0,0,0,0), offset;
    if (session < 0) return FALSE;
    offset = desk_syscall(CUIS_SERVICE_CLASS,CUIS_COHORT_QUERY,session,CUIS_COHORT_READ_OFFSET,0,0);
    return offset >= 0 && offset % 16 == 0 && desk_syscall(CUIS_SERVICE_CLASS,CUIS_COHORT_QUERY,
        session,CUIS_COHORT_READ_BYTES,0,0) == 16;
}
BOOL desk_shutdown_ready(void);
BOOL desk_shutdown_ready(void)
{
    LONG session = desk_syscall(CUIS_SERVICE_CLASS,CUIS_SESSION_START,0,0,0,0);
    return session >= 0 && desk_syscall(CUIS_SERVICE_CLASS,CUIS_COHORT_QUERY,
        session,CUIS_COHORT_STATE,0,0) == CUIS_COHORT_FINISHED;
}

WORD desk_shifted_key(WORD scancode);
WORD desk_shifted_key(WORD scancode)
{
    return desk_syscall(CUIS_SERVICE_CLASS,CUIS_KEYMAP_LOOKUP,scancode,1,0,0);
}

WORD desk_aes_dispatch(UWORD opcode, UWORD *global, UWORD *input, UWORD *out,
                       intptr_t *addresses, intptr_t *results);
#if CONF_WITH_X32_DESKTOP_TEST
void desk_test_record(const char *message);
extern WORD gl_width, gl_height;
static BOOL ready;
#endif

/* The desktop's native AESGLOBAL has alignment/pointer fields of its own.
 * Translate only the resource fields from GEM's packed two-WORD pointers. */
static void resource_globals(UWORD *global)
{
    AESGLOBAL *legacy = (AESGLOBAL *)global;
    ULONG pointer;
    memcpy(&pointer,aes_global+5,sizeof(pointer));
    legacy->ap_ptree = (OBJECT **)(uintptr_t)pointer;
    memcpy(&pointer,aes_global+7,sizeof(pointer));
    legacy->ap_rscmem = (RSHDR *)(uintptr_t)pointer;
    legacy->ap_rsclen = (UWORD)aes_global[9];
}

WORD desk_aes_dispatch(UWORD opcode, UWORD *global, UWORD *input, UWORD *out,
                       intptr_t *addresses, intptr_t *results)
{
    WORD in[16], r[7] = { 0 }, *g = aes_global;
    intptr_t a[3];
    OBJECT *tree;
    void *pointer;

    /* USERDEF callbacks can reuse the historical binding's static arrays. */
    memcpy(in,input,sizeof(in)); memcpy(a,addresses,sizeof(a));
    tree = (OBJECT *)a[0];
    (void)results;
    switch (opcode) {
    case APPL_INIT:
        r[0] = mt_appl_init(g);
        ((AESGLOBAL *)global)->ap_version = g[0];
        ((AESGLOBAL *)global)->ap_count = g[1];
        ((AESGLOBAL *)global)->ap_id = g[2];
        resource_globals(global);
#if CONF_WITH_X32_DESKTOP_TEST
        ready = FALSE;
        if (r[0] >= 0) desk_test_record("x32 desktop LIBAES PASS\r\n");
#endif
        break;
    case APPL_EXIT: r[0] = mt_appl_exit(g); resource_globals(global); break;
    case APPL_WRITE: r[0] = mt_appl_write(in[0],in[1],(void *)a[0],g); break;
    case APPL_FIND: r[0] = mt_appl_find((char *)a[0],g); break;
    case EVNT_MULTI:
#if CONF_WITH_X32_DESKTOP_TEST
        if ((in[0] & (MU_KEYBD|MU_BUTTON|MU_MESAG)) == (MU_KEYBD|MU_BUTTON|MU_MESAG)) {
            char message[100];
            WORD x, y, buttons, modifiers;
            if (mt_graf_mkstate(&x,&y,&buttons,&modifiers,g)) {
                sprintf(message,"x32 desktop %s %d %d %d %d\r\n",
                        ready ? "IDLE" : "READY",x,y,gl_width,gl_height);
                desk_test_record(message); ready = TRUE;
            }
        }
#endif
        r[0] = mt_evnt_multi(in[0],in[1],in[2],in[3],in[4],in[5],in[6],in[7],in[8],
                            in[9],in[10],in[11],in[12],in[13],(WORD *)a[0],
                            (ULONG)(UWORD)in[14] | ((ULONG)(UWORD)in[15] << 16),
                            r+1,r+2,r+3,r+4,r+5,r+6,g);
        break;
    case EVNT_BUTTON: r[0] = mt_evnt_button(in[0],in[1],in[2],r+1,r+2,r+3,r+4,g); break;
    case EVNT_TIMER: r[0] = mt_evnt_timer((ULONG)(UWORD)in[0] | ((ULONG)(UWORD)in[1] << 16),g); break;
    case EVNT_DCLICK: r[0] = mt_evnt_dclick(in[0],in[1],g); break;
    case MENU_BAR: r[0] = mt_menu_bar(tree,in[0],g); break;
    case MENU_ICHECK: r[0] = mt_menu_icheck(tree,in[0],in[1],g); break;
    case MENU_IENABLE: r[0] = mt_menu_ienable(tree,in[0],in[1],g); break;
    case MENU_TNORMAL: r[0] = mt_menu_tnormal(tree,in[0],in[1],g); break;
    case OBJC_ADD: r[0] = mt_objc_add(tree,in[0],in[1],g); break;
    case OBJC_DRAW: r[0] = mt_objc_draw(tree,in[0],in[1],in[2],in[3],in[4],in[5],g); break;
    case OBJC_FIND: r[0] = mt_objc_find(tree,in[0],in[1],in[2],in[3],g); break;
    case OBJC_OFFSET: r[0] = mt_objc_offset(tree,in[0],r+1,r+2,g); break;
    case OBJC_ORDER: r[0] = mt_objc_order(tree,in[0],in[1],g); break;
    case OBJC_CHANGE: r[0] = mt_objc_change(tree,in[0],in[1],in[2],in[3],in[4],in[5],in[6],in[7],g); break;
    case OBJC_EDIT:
        r[1] = in[2]; r[0] = mt_objc_edit(tree,in[0],in[1],r+1,in[3],g); break;
    case FORM_DO:
#if CONF_WITH_X32_DESKTOP_TEST
        desk_test_record("x32 desktop form WAIT\r\n");
#endif
        r[0] = mt_form_do(tree,in[0],g); break;
    case FORM_CENTER: r[0] = mt_form_center(tree,r+1,r+2,r+3,r+4,g); break;
    case FORM_DIAL: r[0] = mt_form_dial(in[0],in[1],in[2],in[3],in[4],in[5],in[6],in[7],in[8],g); break;
    case FORM_ALERT: r[0] = mt_form_alert(in[0],(char *)a[0],g); break;
    case FORM_ERROR: r[0] = mt_form_error(in[0],g); break;
    case FORM_BUTTON: r[0] = mt_form_button(tree,in[0],in[1],r+1,g); break;
    case FORM_KEYBD: r[0] = mt_form_keybd(tree,in[0],in[1],in[2],r+1,r+2,g); break;
    case GRAF_HANDLE: r[0] = mt_graf_handle(r+1,r+2,r+3,r+4,g); break;
    case GRAF_MOUSE: r[0] = mt_graf_mouse(in[0],(MFORM *)a[0],g); break;
    case GRAF_MKSTATE: r[0] = mt_graf_mkstate(r+1,r+2,r+3,r+4,g); break;
    case GRAF_RUBBOX: r[0] = mt_graf_rubberbox(in[0],in[1],in[2],in[3],r+1,r+2,g); break;
    case GRAF_DRAGBOX: r[0] = mt_graf_dragbox(in[0],in[1],in[2],in[3],in[4],in[5],in[6],in[7],r+1,r+2,g); break;
    case GRAF_MBOX: r[0] = mt_graf_mbox(in[0],in[1],in[2],in[3],in[4],in[5],g); break;
    case GRAF_WATCHBOX: r[0] = mt_graf_watchbox(tree,in[1],in[2],in[3],g); break;
    case GRAF_SLIDEBOX: r[0] = mt_graf_slidebox(tree,in[0],in[1],in[2],g); break;
    case GRAF_GROWBOX: r[0] = mt_graf_growbox_grect((GRECT *)in,(GRECT *)(in+4),g); break;
    case GRAF_SHRINKBOX: r[0] = mt_graf_shrinkbox_grect((GRECT *)in,(GRECT *)(in+4),g); break;
    case FSEL_EXINPUT: r[0] = mt_fsel_exinput((char *)a[0],(char *)a[1],r+1,(char *)a[2],g); break;
    case FSEL_INPUT: r[0] = mt_fsel_input((char *)a[0],(char *)a[1],r+1,g); break;
    case WIND_CREATE: r[0] = mt_wind_create_grect(in[0],(GRECT *)(in+1),g); break;
    case WIND_OPEN:
        r[0] = mt_wind_open_grect(in[0],(GRECT *)(in+1),g);
#if CONF_WITH_X32_DESKTOP_TEST
        if (r[0]) {
            char message[100];
            sprintf(message,"x32 desktop window OPEN %d\r\n",in[0]);
            desk_test_record(message);
        }
#endif
        break;
    case WIND_CLOSE: r[0] = mt_wind_close(in[0],g); break;
    case WIND_DELETE: r[0] = mt_wind_delete(in[0],g); break;
    case WIND_FIND: r[0] = mt_wind_find(in[0],in[1],g); break;
    case WIND_GET: r[0] = mt_wind_get(in[0],in[1],r+1,r+2,r+3,r+4,g); break;
    case WIND_CALC: r[0] = mt_wind_calc_grect(in[0],in[1],(GRECT *)(in+2),(GRECT *)(r+1),g); break;
    case WIND_SET:
        memcpy(&pointer,in+2,sizeof(pointer));
        if (in[1] == WF_NAME || in[1] == WF_INFO)
            r[0] = mt_wind_set_str(in[0],in[1],pointer,g);
        else if (in[1] == WF_NEWDESK)
            r[0] = mt_wind_set_ptr_int(in[0],in[1],pointer,in[4],g);
        else r[0] = mt_wind_set(in[0],in[1],in[2],in[3],in[4],in[5],g);
        break;
    case WIND_UPDATE: r[0] = mt_wind_update(in[0],g); break;
    case WIND_NEW: r[0] = mt_wind_new(g); break;
    case RSRC_LOAD: r[0] = mt_rsrc_load((char *)a[0],g); resource_globals(global); break;
    case RSRC_FREE: r[0] = mt_rsrc_free(g); resource_globals(global); break;
    case RSRC_OBFIX: r[0] = mt_rsrc_obfix(tree,in[0],g); break;
    case SHEL_WRITE: r[0] = mt_shel_write(in[0],in[1],in[2],(void *)a[0],(char *)a[1],g); break;
    case SHEL_GET: r[0] = mt_shel_get((char *)a[0],in[0],g); break;
    case SHEL_PUT: r[0] = mt_shel_put((char *)a[0],in[0],g); break;
    case SHEL_WDEF: mt_shel_wdef((char *)a[0],(char *)a[1],g); r[0] = 1; break;
    }
    memcpy(out,r,sizeof(r)); return r[0];
}
