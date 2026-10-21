/* Typed window operations use only kernel-owned native state and text.
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later.
 */
#include "emutos.h"
#include "struct.h"
#include "aesvars.h"
#include "gemlib.h"
#include "geminit.h"
#include "gemwmlib.h"
#include "obdefs.h"
#include "gsxdefs.h"
#include "biosext.h"
#include "aes_accessory.h"
#include "aes_window.h"
#include "gemerror.h"
#include "string.h"
#include "../../../bdos/kproc.h"
#include "../../../bdos/user_callback.h"
#if CONF_WITH_X32_ACCESSORY_TEST
#include "vdi_service.h"
#include "gemevlib.h"
#endif

typedef struct {
    PD *owner;
    AESPD *task;
    AESPD *busy; /* Coroutine holding this slot's continuation reservation. */
    BOOL retired, redraw;
    UWORD visible_next;
#if CONF_WITH_X32_ACCESSORY_TEST
    BOOL revoke_tested;
#endif
    GRECT dirty;
    char title[AES_WINDOW_TEXT_MAX + 1];
    char info[AES_WINDOW_TEXT_MAX + 1];
} CLIENT_WINDOW;

/* The native manager retains text pointers. Fixed slots keep those pointers
 * valid even across cancellation, deferred cleanup and native redraws. */
static CLIENT_WINDOW windows[NUM_WIN];
#if CONF_WITH_X32_ACCESSORY_TEST
static PD *test_revoking;
static PD *test_reusing;
static WORD test_replaced[2];
static WORD test_sync_cases, test_sync_first, test_sync_later;
static BOOL test_sync_held;
static PD *test_borrow_owner;
static AESPD *test_borrow_executor;
static WORD test_borrow_handle, test_borrow_phase;
static WORD test_cursor_handle, test_cursor_cases;

static void test_cursor_prepare(PD *pd, const AES_WINDOW_REQUEST *request)
{
    WORD h = request->handle;

    if (request->operation != AES_WIN_VISIBLE || h <= 0 || h >= NUM_WIN ||
        windows[h].owner != pd || strcmp(windows[h].title, "Cursor probe"))
        return;
    for (h = 1; h < NUM_WIN; h++)
        if (windows[h].owner == pd && !strcmp(windows[h].title, "Cursor cleanup")) {
            test_cursor_handle = h;
            windows[h].retired = TRUE;
            return;
        }
}

static void test_cursor_closed(WORD h, CLIENT_WINDOW *w)
{
    if (h != test_cursor_handle)
        return;
    test_cursor_handle = 0;
    /* Run a real ring-3 callback at cleanup's yielding close boundary. It
     * frees the already-validated output heap block without unregistering. */
    vdi_service_callback_event(VDI_CALLBACK_BUTTON, 629, 0);
    vdi_service_callback_work();
    if (!kproc_aes_get(w->owner))
        panic("accessory cursor cleanup registration lost FAIL\n");
    test_cursor_cases++;
}

static void test_borrow_prepare(PD *pd, const AES_WINDOW_REQUEST *request)
{
    WORD h = request->handle;

    if (request->operation != AES_WIN_GET || h <= 0 || h >= NUM_WIN || windows[h].owner != pd)
        return;
    if (test_borrow_phase == 2 && pd == test_borrow_owner) {
        if (h == test_borrow_handle || windows[test_borrow_handle].busy != test_borrow_executor)
            panic("accessory borrowed reservation lost FAIL\n");
        test_borrow_phase = 3;
        return;
    }
    if (test_borrow_phase || strcmp(windows[h].title, "Borrowed trigger"))
        return;
    for (h = 1; h < NUM_WIN; h++)
        if (windows[h].owner && windows[h].owner != pd &&
            !strcmp(windows[h].title, "Borrowed cleanup"))
            break;
    if (h == NUM_WIN)
        panic("accessory borrowed cleanup fixture missing FAIL\n");
    test_borrow_handle = h;
    test_borrow_owner = windows[h].owner;
    test_borrow_executor = rlr;
    test_borrow_phase = 1;
    /* The other client's real ring-3 callback unregisters and immediately
     * re-registers. Its window is now retired, with no owner call in flight. */
    vdi_service_callback_event(VDI_CALLBACK_BUTTON, 628, 0);
    vdi_service_callback_work();
    if (!windows[h].retired || !kproc_aes_get(test_borrow_owner))
        panic("accessory borrowed cleanup revocation FAIL\n");
}

static void test_borrow_closed(WORD h, CLIENT_WINDOW *w)
{
    WORD attempt, message[8] = { 638, 0, 0, 0, 0, 0, 0, 0 };

    if (test_borrow_phase != 1 || h != test_borrow_handle)
        return;
    if (rlr == w->task || w->busy != rlr)
        panic("accessory cleanup not borrowed FAIL\n");
    test_borrow_phase = 2;
    message[3] = h;
    if (aes_service_kernel_message(test_borrow_owner, message) != E_OK)
        panic("accessory borrowed cleanup wake FAIL\n");
    /* Suspend the borrower's actual AES continuation. The owner runs normal
     * appl_exit/init, creates a closed replacement and queries it meanwhile. */
    for (attempt = 0; attempt < 1000 && test_borrow_phase == 2; attempt++)
        ev_timer(10L);
    if (test_borrow_phase != 3 || w->busy != rlr)
        panic("accessory borrowed cleanup continuation FAIL\n");
}

/* At the close boundary, model the state left by a dispatched lock waiter
 * or another typed client acquiring one of the coordination tokens. */
static void test_sync_closed(WORD h, CLIENT_WINDOW *w)
{
    AESPD *saved = rlr, *foreign = D.w_win[0].w_owner;

    if (h != test_sync_first || strcmp(w->title, "Sync cleanup"))
        return;
    if (test_sync_held || test_sync_cases >= 3)
        panic("accessory cleanup synchronization setup FAIL\n");
    if (!test_sync_cases) {
        if (wind_spb.sy_tas || !foreign || foreign == rlr)
            panic("accessory native lock handoff setup FAIL\n");
        rlr = foreign;
        wm_update(BEG_UPDATE); /* Free lock: cannot yield on acquisition. */
        rlr = saved;
    } else if (aes_service_test_coordination(test_sync_cases == 1 ?
                                            AES_BEG_UPDATE : AES_BEG_MCTRL) != E_OK)
        panic("accessory coordination handoff setup FAIL\n");
    test_sync_held = TRUE;
}

static void test_sync_prepare(PD *pd, const AES_WINDOW_REQUEST *request)
{
    WORD h = request->handle;

    if (request->operation == AES_WIN_CREATE) {
        for (h = 1; h < NUM_WIN; h++)
            if (windows[h].owner == pd && !strcmp(windows[h].title, "Sync probe"))
                break;
    } else if (request->operation != AES_WIN_GET)
        return;
    if (h <= 0 || h >= NUM_WIN || windows[h].owner != pd ||
        strcmp(windows[h].title, "Sync probe"))
        return;
    if (test_sync_held) {
        if (!test_sync_cases) {
            /* Cleanup must have stopped before deleting the just-closed
             * window or closing a later retired window under a foreign lock. */
            if (!(D.w_win[test_sync_first].w_flags & VF_INUSE) ||
                (D.w_win[test_sync_first].w_flags & VF_ISOPEN) ||
                !(D.w_win[test_sync_later].w_flags & VF_ISOPEN) ||
                wind_spb.sy_owner != D.w_win[0].w_owner)
                panic("accessory cleanup accessed contended windows FAIL\n");
            wm_update(END_UPDATE);
        } else if (aes_service_test_coordination(test_sync_cases == 1 ?
                                                AES_END_UPDATE : AES_END_MCTRL) != E_OK)
            panic("accessory coordination release FAIL\n");
        test_sync_held = FALSE;
        test_sync_first = test_sync_later = 0;
        test_sync_cases++;
        return;
    }
    if (test_sync_cases >= 3)
        panic("accessory cleanup synchronization repeated FAIL\n");
    for (h = 1; h < NUM_WIN; h++)
        if (windows[h].owner == pd) {
            if (!strcmp(windows[h].title, "Sync cleanup"))
                test_sync_first = h;
            if (!strcmp(windows[h].title, "Sync later"))
                test_sync_later = h;
        }
    if (!test_sync_first || test_sync_first >= test_sync_later)
        panic("accessory retired synchronization windows setup FAIL\n");
    windows[test_sync_first].retired = windows[test_sync_later].retired = TRUE;
}

/* Reproduce the native reset/recreate effect at the close/delete boundary.
 * Unlike typed CREATE, a native creator ignores the busy bookkeeping slot. */
static void test_replace_closed(WORD h, CLIENT_WINDOW *w)
{
    WORD i, n = 0, created[NUM_WIN], which;
    AESPD *saved = rlr, *foreign = D.w_win[0].w_owner;
    GRECT full = { 0, 0, 200, 100 };

    if (!strcmp(w->title, "Replace during delete")) which = 0;
    else if (!strcmp(w->title, "Replace during cleanup")) which = 1;
    else return;
    if (test_replaced[which] || !foreign || foreign == w->task || !wm_delete(h))
        panic("accessory native replacement setup FAIL\n");
    rlr = foreign;
    do {
        created[n] = wm_create(0, &full);
        if (created[n] <= 0)
            panic("accessory native replacement allocation FAIL\n");
    } while (created[n++] != h && n < NUM_WIN);
    rlr = saved;
    if (created[n - 1] != h)
        panic("accessory native replacement handle FAIL\n");
    for (i = 0; i < n - 1; i++)
        wm_delete(created[i]);
    test_replaced[which] = h;
}

WORD x86_64_aes_window_test_arrow(PD *pd)
{
    WORD h;

    for (h = 1; h < NUM_WIN; h++)
        if (windows[h].owner == pd &&
            !strcmp(windows[h].title, "Deferred arrow test"))
            return h;
    return 0;
}

BOOL x86_64_aes_window_test_revoke(PD *pd)
{
    return test_revoking == pd;
}

BOOL x86_64_aes_window_test_reuse(PD *pd)
{
    return test_reusing == pd;
}
#endif

static BOOL unlocked(void)
{
    return !wind_spb.sy_tas || wind_spb.sy_owner == rlr;
}

static BOOL same_window(WORD h, CLIENT_WINDOW *w, PD *owner, AESPD *task)
{
    return w->owner == owner && w->task == task &&
        (D.w_win[h].w_flags & VF_INUSE) && D.w_win[h].w_owner == task;
}

/* wm_close() can dispatch at END_UPDATE. Native reset/creation does not
 * consult typed busy slots, so never let wm_delete() perform that yielding
 * close internally: revalidate before deleting the still-owned closed slot. */
static LONG delete_window(WORD h, CLIENT_WINDOW *w)
{
    PD *owner = w->owner;
    AESPD *task = w->task;

    if (!unlocked())
        return EAGAIN;
    if (!same_window(h, w, owner, task))
        return EIHNDL;
    if (D.w_win[h].w_flags & VF_ISOPEN) {
        if (!wm_close(h))
            return EINVAL;
#if CONF_WITH_X32_ACCESSORY_TEST
        test_sync_closed(h, w);
        test_replace_closed(h, w);
        test_borrow_closed(h, w);
        test_cursor_closed(h, w);
#endif
        /* END_UPDATE may have handed the lock to a waiter and dispatched.
         * Leave this closed window for retry rather than delete under it. */
        if (!unlocked())
            return EAGAIN;
        if (!same_window(h, w, owner, task))
            return EIHNDL;
    }
    if (D.w_win[h].w_flags & VF_ISOPEN)
        return EINVAL;
    /* Closed deletion cannot yield; native code normalizes its dimensions
     * before reclaiming rectangle nodes, including never-opened windows. */
    return wm_delete(h) ? E_OK : EINVAL;
}

static void reap(void)
{
    WORD h;
    CLIENT_WINDOW *w;
    PD *owner;
    AESPD *task;
    LONG result;

#if CONF_WITH_USER_CALLBACKS
    if (user_callback_active())
        return;
#endif
    for (h = 1; h < NUM_WIN; h++) {
        /* An earlier deletion may have yielded and lost the native lock. */
        if (!unlocked())
            return;
        w = &windows[h];
        if (!w->owner || w->busy)
            continue;
        /* A native reset can remove live windows without a typed DELETE.
         * Reclaim stale records even when later CREATE chooses a different
         * handle; in-flight records remain reserved until they unwind. */
        if (!same_window(h, w, w->owner, w->task)) {
            memset(w, 0, sizeof(*w));
            continue;
        }
        if (!w->retired)
            continue;
        owner = w->owner;
        task = w->task;
        w->busy = rlr;
        result = delete_window(h, w);
        if (w->owner == owner && w->task == task) {
            if (result == E_OK || result == EIHNDL)
                memset(w, 0, sizeof(*w));
            else
                w->busy = NULL;
        }
    }
}

void x86_64_aes_window_exit(PD *pd)
{
    WORD h;

    for (h = 1; h < NUM_WIN; h++)
        if (windows[h].owner == pd)
            windows[h].retired = TRUE;
    /* KPROC teardown and user callbacks must remain bounded: native drawing
     * and synchronization run later, outside those critical continuations. */
}

void x86_64_aes_window_finished(PD *pd, AESPD *executor)
{
    WORD h;
    AESPD *task = executor;
    BOOL pending = FALSE;

    for (h = 1; h < NUM_WIN; h++) {
        /* Only this executor's continuations have unwound. A different
         * coroutine may still be closing this owner's retired windows. */
        if (executor && windows[h].busy == executor) {
            windows[h].busy = NULL;
            pending = TRUE;
        }
        if (windows[h].owner == pd) {
            windows[h].retired = TRUE;
            if (!task)
                task = windows[h].task;
            pending = TRUE;
        }
    }
    if (!task || (!pending && wind_spb.sy_owner != task))
        return;
    /* The supplied executor has unwound; NULL is reserved for session drain
     * after all executors were cancelled. Other executors' reservations stay
     * pinned. Release abandoned update nesting before safe native cleanup. */
    while (wind_spb.sy_tas && wind_spb.sy_owner == task)
        wm_update(END_UPDATE);
    wm_update(BEG_UPDATE);
    reap();
    wm_update(END_UPDATE);
}

void x86_64_aes_window_unregister(PD *pd)
{
#if CONF_WITH_USER_CALLBACKS
    if (user_callback_active())
        return;
#endif
    /* A normal appl_exit has no suspended window continuation. Complete
     * cleanup outside KPROC teardown, including builds without callbacks
     * (which have no callback return-work hook to perform deferred work). */
    if (x86_64_aes_owned(pd))
        x86_64_aes_window_finished(pd, rlr);
}

/* Called after all accessory continuations and native EVBs are cancelled. */
void x86_64_aes_window_drain(void)
{
    WORD h;

#if CONF_WITH_X32_ACCESSORY_TEST
    test_revoking = NULL;
    test_reusing = NULL;
    memset(test_replaced, 0, sizeof(test_replaced));
    test_sync_cases = test_sync_first = test_sync_later = 0;
    test_sync_held = FALSE;
    test_borrow_owner = NULL;
    test_borrow_executor = NULL;
    test_borrow_handle = test_borrow_phase = 0;
    test_cursor_handle = test_cursor_cases = 0;
#endif
    /* Every accessory continuation, including borrowed cleanup, has now
     * been cancelled. No executor can resume these reservations. */
    for (h = 1; h < NUM_WIN; h++)
        windows[h].busy = NULL;
    for (h = 1; h < NUM_WIN; h++)
        if (windows[h].owner)
            x86_64_aes_window_finished(windows[h].owner, NULL);
#if CONF_WITH_X32_ACCESSORY_TEST
    for (h = 1; h < NUM_WIN; h++)
        if (windows[h].owner)
            panic("accessory window drain FAIL\n");
#endif
}

/* Native window drawing must never wait for its own caller's full queue.
 * Keep one union per window; transfer it into the typed queue when it has
 * room. Other control messages retain normal FIFO/backpressure behavior. */
BOOL x86_64_aes_window_redraw(PD *pd, const WORD *message)
{
    WORD h = message[3];
    CLIENT_WINDOW *w;
    GRECT dirty;

    if (message[0] != WM_REDRAW || h <= 0 || h >= NUM_WIN)
        return FALSE;
    w = &windows[h];
    if (w->owner != pd)
        return FALSE;
    if (w->retired)
        return TRUE;
    memcpy(&dirty, message + 4, sizeof(dirty));
    if (w->redraw)
        rc_union(&dirty, &w->dirty);
    else
        w->dirty = dirty;
    w->redraw = TRUE;
    x86_64_aes_wake(pd);
    return TRUE;
}

BOOL x86_64_aes_window_pending(PD *pd, AES_MESSAGE *message)
{
    WORD h;
    CLIENT_WINDOW *w;

    for (h = 1; h < NUM_WIN; h++) {
        w = &windows[h];
        if (w->owner == pd && !w->retired && w->redraw) {
            memset(message, 0, sizeof(*message));
            message->type = WM_REDRAW;
            message->data[0] = h;
            memcpy(message->data + 1, &w->dirty, sizeof(w->dirty));
            w->redraw = FALSE;
            return TRUE;
        }
    }
    return FALSE;
}

static BOOL geometry(UWORD kind, const GRECT *r, BOOL work, GRECT *result)
{
    GRECT border;
    LONG x, y, width, height;
    LONG sign = work ? -1 : 1;

    wm_calc(WC_BORDER, kind, 0, 0, 0, 0,
            &border.g_x, &border.g_y, &border.g_w, &border.g_h);
    x = (LONG)r->g_x + sign * border.g_x;
    y = (LONG)r->g_y + sign * border.g_y;
    width = (LONG)r->g_w + sign * border.g_w;
    height = (LONG)r->g_h + sign * border.g_h;
    if (width <= 0 || height <= 0 || width > 32765L || height > 32765L ||
        x < -32765L || y < -32765L || x + width > 32765L || y + height > 32765L)
        return FALSE;
    result->g_x = x; result->g_y = y;
    result->g_w = width; result->g_h = height;
    return TRUE;
}

static void information(WORD h, PD *pd, AES_WINDOW_INFO *info)
{
    WINDOW *w = &D.w_win[h];

    memset(info, 0, sizeof(*info));
    info->handle = h;
    info->owner = h ? kproc_pid(pd) : 0;
    info->kind = w->w_kind;
    info->flags = (w->w_flags & VF_ISOPEN) ? AES_WINDOW_OPEN : 0;
    info->full = w->w_full;
    info->work = w->w_work;
    info->previous = w->w_prev;
    w_getsize(WS_CURR, h, &info->current);
    info->hslide = w->w_hslide; info->vslide = w->w_vslide;
    info->hslsiz = w->w_hslsiz; info->vslsiz = w->w_vslsiz;
    info->top = gl_wtop == NIL ? 0 : gl_wtop;
}

/* Native move/union arithmetic is WORD-sized too. Individually valid
 * rectangles at opposite signed extremes must not overflow that arithmetic
 * when combined with the screen or another allocated window's old bounds.
 * draw_change() adds the two-pixel shadow twice for same-origin resizes;
 * reserve all four pixels for both new bounds and old bounds on shrink. */
static BOOL native_bounds(UWORD kind, const GRECT *input)
{
    WORD i;
    GRECT current;
    LONG left = input->g_x < 0 ? input->g_x : 0;
    LONG top = input->g_y < 0 ? input->g_y : 0;
    LONG right = (LONG)input->g_x + input->g_w + 4;
    LONG bottom = (LONG)input->g_y + input->g_h + 4;
    LONG end;

    if (kind & MOVER) {
        /* hctl_window() stores this title-drag constraint in a WORD.
         * Check the complete expression in LONG before native dragging. */
        end = (LONG)gl_rscreen.g_w + input->g_w - gl_wbox - 6;
#if CONF_WITH_3D_OBJECTS
        end -= 2 * ADJ3DSTD;
#endif
        if (end <= 0 || end > 32767L)
            return FALSE;
    }
    if (right < gl_width) right = gl_width;
    if (bottom < gl_height) bottom = gl_height;
    for (i = 1; i < NUM_WIN; i++) {
        if (!(D.w_win[i].w_flags & VF_INUSE))
            continue;
        w_getsize(WS_CURR, i, &current);
        if (current.g_w <= 0 || current.g_h <= 0)
            continue;
        if (current.g_x < left) left = current.g_x;
        if (current.g_y < top) top = current.g_y;
        end = (LONG)current.g_x + current.g_w + 4;
        if (end > right) right = end;
        end = (LONG)current.g_y + current.g_h + 4;
        if (end > bottom) bottom = end;
    }
    return right - left <= 32767L && bottom - top <= 32767L;
}

/* Keep an ordinal, never the native w_rnext pointer: any window mutation
 * can recycle native ORECT nodes between two user calls. Rescan the current
 * owned list, bounded by its global pool, without retaining a freed node. */
static LONG visible_rectangle(const CLIENT_WINDOW *w, WINDOW *native, BOOL next,
                              GRECT *out, UWORD *cursor)
{
    ORECT *node;
    UWORD index = 0, walked = 0;
    GRECT rectangle;

    *cursor = next ? w->visible_next : 0;
    memset(out, 0, sizeof(*out));
    if (!(native->w_flags & VF_ISOPEN))
        return E_OK;
    for (node = native->w_rlist; node && walked++ < NUM_ORECT; node = node->o_link) {
        rectangle = node->o_gr;
        if (rc_intersect(&native->w_work, &rectangle) && index++ == *cursor) {
            *out = rectangle;
            (*cursor)++;
            return E_OK;
        }
    }
    return node ? EINTRN : E_OK;
}

LONG x86_64_aes_window(PD *pd, const AES_WINDOW_REQUEST *r,
                       const char *text, AES_WINDOW_INFO *info, GRECT *rectangle,
                       UQUAD output)
{
    CLIENT_WINDOW *w = NULL;
    WINDOW *native;
    GRECT calculated, input = r->rectangle;
    WORD h = r->handle, i, fields[4] = { 0 };
    UWORD cursor;
    char *saved_text;
    LONG result = E_OK;
    static const WORD sliders[4] = { WF_HSLIDE, WF_VSLIDE, WF_HSLSIZ, WF_VSLSIZ };

    if (!x86_64_aes_begin_call(pd))
        return EACCDN;
#if CONF_WITH_X32_ACCESSORY_TEST
    test_sync_prepare(pd, r);
    test_borrow_prepare(pd, r);
    test_cursor_prepare(pd, r);
#endif
    if (!unlocked() || !aes_service_window_unlocked(pd))
        return EAGAIN;
    reap();
    if (x86_64_aes_call_cancelled(pd))
        return EACCDN;
    /* Cleanup can dispatch: refresh both native synchronization and typed
     * coordination ownership before validating handles or entering AES. */
    if (!unlocked() || !aes_service_window_unlocked(pd))
        return EAGAIN;
    if (r->operation == AES_WIN_FIND)
        return wm_find(input.g_x, input.g_y);
    if (r->operation == AES_WIN_CALC) {
        if (!geometry(r->kind, &input, r->selector == AES_WINDOW_WORK, rectangle))
            return EINVAL;
        return E_OK;
    }
    if (r->operation == AES_WIN_CREATE) {
        if (!geometry(r->kind, &input, TRUE, &calculated) || !native_bounds(r->kind, &input))
            return EINVAL;
        /* A native delete/reset can free a handle before its yielding call
         * unwinds. Keep that slot reserved until the old call releases its
         * bookkeeping; another client must not overwrite a busy record. */
        for (i = 1; i < NUM_WIN; i++)
            if (windows[i].busy && !(D.w_win[i].w_flags & VF_INUSE))
                return EAGAIN;
#if CONF_WITH_USER_CALLBACKS
        if (!user_callback_work_register(reap))
            return ENSMEM;
#endif
        h = wm_create(r->kind, &input);
        if (h <= 0)
            return ENSMEM;
        w = &windows[h];
        memset(w, 0, sizeof(*w));
        w->owner = pd;
        w->task = rlr;
        D.w_win[h].w_pname = w->title;
        D.w_win[h].w_pinfo = w->info;
        memset(&D.w_win[h].w_work, 0, sizeof(D.w_win[h].w_work));
        D.w_win[h].w_rlist = D.w_win[h].w_rnext = NULL;
        return h;
    }
    if (h < 0 || h >= NUM_WIN || !(D.w_win[h].w_flags & VF_INUSE))
        return EIHNDL;
    if (h == 0) {
        if (r->operation != AES_WIN_GET)
            return EACCDN;
        information(h, pd, info);
        return E_OK;
    }
    w = &windows[h];
    native = &D.w_win[h];
    if (w->owner != pd || w->retired || native->w_owner != w->task)
        return EACCDN;
    if (w->busy)
        return EAGAIN;
    w->busy = rlr;
#if CONF_WITH_X32_ACCESSORY_TEST
    if (r->operation == AES_WIN_GET && !w->revoke_tested &&
        !strcmp(w->title, "Revoke this window")) {
        AES_EVENT_REQUEST pause = { AES_EVENT_TIMER, 0, 0, 0, 0, 0,
                                    { 0 }, { 0 }, 60000 };
        AES_INPUT_EVENT event = { 0 };

        w->revoke_tested = TRUE;
        test_revoking = pd;
        x86_64_aes_events(pd, &pause, &event, FALSE);
        test_revoking = NULL;
        if (x86_64_aes_call_cancelled(pd)) {
            w->busy = NULL;
            reap();
            return EACCDN;
        }
    }
#endif
    switch (r->operation) {
    case AES_WIN_OPEN:
    case AES_WIN_RECT:
        if (!geometry(native->w_kind, &input, TRUE, &calculated) || !native_bounds(native->w_kind, &input))
            result = EINVAL;
        else if (r->operation == AES_WIN_OPEN)
            result = wm_open(h, &input) ? E_OK : EINVAL;
        else {
            memcpy(fields, &input, sizeof(input));
            result = wm_set(h, WF_CXYWH, fields) ? E_OK : EINVAL;
        }
        break;
    case AES_WIN_CLOSE:
        result = wm_close(h) ? E_OK : EINVAL;
        break;
    case AES_WIN_DELETE:
        result = delete_window(h, w);
        break;
    case AES_WIN_GET:
        information(h, pd, info);
        break;
    case AES_WIN_TEXT:
        saved_text = r->selector == AES_WINDOW_NAME ? w->title : w->info;
        memcpy(saved_text, text, AES_WINDOW_TEXT_MAX + 1);
        /* Native text redraw assumes the gadget exists in its object tree.
         * The retained pointers already address these copied kernel strings. */
        if (native->w_kind & (r->selector == AES_WINDOW_NAME ? NAME : INFO)) {
            memcpy(fields, &saved_text, sizeof(saved_text));
            result = wm_set(h, r->selector == AES_WINDOW_NAME ? WF_NAME : WF_INFO, fields) ? E_OK : EINVAL;
        }
        break;
    case AES_WIN_TOP:
        if (!(native->w_flags & VF_ISOPEN))
            result = EINVAL;
        else
            result = wm_set(h, WF_TOP, fields) ? E_OK : EINVAL;
        break;
    case AES_WIN_SLIDER:
        /* Native slider redraw walks the gadget's object-tree links.
         * Arrow buttons and a sizer do not imply that the slider exists. */
        if (!(native->w_kind & (r->selector == AES_WINDOW_HSLIDE ||
                               r->selector == AES_WINDOW_HSLSIZ ? HSLIDE : VSLIDE)))
            result = EINVAL;
        else {
            fields[0] = r->value;
            result = wm_set(h, sliders[r->selector], fields) ? E_OK : EINVAL;
        }
        break;
    case AES_WIN_VISIBLE:
        /* Selection, copyout and cursor commit are bounded and cannot
         * dispatch. Do not reap after committing: cleanup could invalidate
         * the delivered output or cancel this call before it returns. */
        result = visible_rectangle(w, native, r->selector, rectangle, &cursor);
        if (result == E_OK) {
            if (!kproc_copy_to_user(output, rectangle, sizeof(*rectangle)))
                result = EIMBA;
            else
                w->visible_next = cursor;
        }
        w->busy = NULL;
        return result;
    default:
        result = EINVFN;
    }
#if CONF_WITH_X32_ACCESSORY_TEST
    if (r->operation == AES_WIN_DELETE && !strcmp(w->title, "Recycle this window")) {
        AES_EVENT_REQUEST pause = { AES_EVENT_TIMER, 0, 0, 0, 0, 0,
                                    { 0 }, { 0 }, 60000 };
        AES_INPUT_EVENT event = { 0 };

        test_reusing = pd;
        x86_64_aes_events(pd, &pause, &event, FALSE);
        test_reusing = NULL;
    }
#endif
    /* Never change a replacement slot after a native operation yielded. */
    if (w->owner != pd || w->task != rlr)
        return EACCDN;
    if (result == E_OK && r->operation != AES_WIN_GET && r->operation != AES_WIN_VISIBLE)
        w->visible_next = 0;
    w->busy = NULL;
    if (w->retired || x86_64_aes_call_cancelled(pd)) {
        w->retired = TRUE;
        result = EACCDN;
    } else if (r->operation == AES_WIN_DELETE && result != EAGAIN)
        w->retired = TRUE;
    else if (!unlocked())
        result = EAGAIN;
    else if (!(native->w_flags & VF_INUSE) || native->w_owner != w->task) {
        w->retired = TRUE;
        result = EIHNDL;
    } else if (r->operation == AES_WIN_CLOSE && result == E_OK)
        w->redraw = FALSE;
    reap();
    if (x86_64_aes_call_cancelled(pd))
        return EACCDN;
    return result;
}

#if CONF_WITH_X32_ACCESSORY_TEST
BOOL x86_64_aes_window_test_ready(void)
{
    WORD h, count = 0;
    AESPD *task;
    CLIENT_WINDOW *w;

    for (h = 1; h < NUM_WIN; h++) {
        w = &windows[h];
        if (!w->owner)
            continue;
        if (w->retired || w->busy || !(D.w_win[h].w_flags & VF_ISOPEN) ||
            D.w_win[h].w_owner != w->task)
            return FALSE;
        for (task = nrl; task && task != w->task; task = task->p_link)
            ;
        if (!task)
            return FALSE;
        count++;
    }
    return count == 2;
}

void x86_64_aes_window_test(void)
{
    WORD h, i, count = 0;
    CLIENT_WINDOW *w;

    reap();
    if (test_sync_cases != 3 || test_sync_held)
        panic("accessory cleanup synchronization cases missing FAIL\n");
    if (test_borrow_phase != 3)
        panic("accessory borrowed cleanup case missing FAIL\n");
    if (test_cursor_cases != 2 || test_cursor_handle)
        panic("accessory cursor copyout cases missing FAIL\n");
    for (i = 0; i < 2; i++) {
        h = test_replaced[i];
        if (!h)
            continue; /* Already verified before the final desktop reopen. */
        if (windows[h].owner || !(D.w_win[h].w_flags & VF_INUSE) ||
            D.w_win[h].w_owner != D.w_win[0].w_owner)
            panic("accessory replacement window deleted FAIL\n");
        wm_delete(h);
        test_replaced[i] = 0;
        count++;
    }
    if (count)
        kcprintf("x32 native replacement windows preserved: PASS\n");
    count = 0;
    for (h = 1; h < NUM_WIN; h++) {
        w = &windows[h];
        if (w->owner) {
            count++;
            if (!kproc_aes_get(w->owner) || w->busy || w->retired ||
                D.w_win[h].w_owner != w->task ||
                !(D.w_win[h].w_flags & VF_ISOPEN) ||
                D.w_win[h].w_pname != w->title || D.w_win[h].w_pinfo != w->info ||
                strcmp(w->title, "Typed x32 window") ||
                strcmp(w->info, "Copied window information") ||
                (uintptr_t)w->title <= 0xffffffffULL)
                panic("accessory window ownership/text/exit cleanup FAIL\n");
        }
        if ((D.w_win[h].w_flags & VF_INUSE) && D.w_win[h].w_owner &&
            (D.w_win[h].w_owner->p_stat & X32_AES_DEAD))
            panic("accessory dead task window leaked\n");
    }
    if (count != 2)
        panic("accessory live window count FAIL: %d\n", count);
    kcprintf("x32 window copied text/exit/fault cleanup: PASS\n");
}
#endif
