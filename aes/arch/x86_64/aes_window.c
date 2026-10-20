/* Typed window operations use only kernel-owned native state and text.
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later.
 */
#include "emutos.h"
#include "struct.h"
#include "aesvars.h"
#include "gemlib.h"
#include "geminit.h"
#include "gemwmlib.h"
#include "gsxdefs.h"
#include "biosext.h"
#include "aes_accessory.h"
#include "aes_window.h"
#include "gemerror.h"
#include "string.h"
#include "../../../bdos/kproc.h"
#include "../../../bdos/user_callback.h"

typedef struct {
    PD *owner;
    AESPD *task;
    BOOL busy, retired, redraw;
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

static void reap(void)
{
    WORD h;
    CLIENT_WINDOW *w;

#if CONF_WITH_USER_CALLBACKS
    if (user_callback_active())
        return;
#endif
    if (!unlocked())
        return;
    for (h = 1; h < NUM_WIN; h++) {
        w = &windows[h];
        if (!w->owner || !w->retired || w->busy)
            continue;
        w->busy = TRUE;
        if ((D.w_win[h].w_flags & VF_INUSE) && D.w_win[h].w_owner == w->task)
            wm_delete(h);
        memset(w, 0, sizeof(*w));
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

void x86_64_aes_window_finished(PD *pd)
{
    WORD h;
    AESPD *task = NULL;

    for (h = 1; h < NUM_WIN; h++)
        if (windows[h].owner == pd) {
            windows[h].retired = TRUE;
            windows[h].busy = FALSE;
            task = windows[h].task;
        }
    if (!task)
        return;
    /* This owner's process and every suspended kernel continuation have
     * unwound. Release any abandoned native update nesting before deleting
     * its windows on the still-live AES UDA (or the session-drain UDA). */
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
        x86_64_aes_window_finished(pd);
}

/* Called after all accessory continuations and native EVBs are cancelled. */
void x86_64_aes_window_drain(void)
{
    WORD h;

#if CONF_WITH_X32_ACCESSORY_TEST
    test_revoking = NULL;
    test_reusing = NULL;
#endif
    for (h = 1; h < NUM_WIN; h++)
        if (windows[h].owner)
            x86_64_aes_window_finished(windows[h].owner);
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
 * when combined with the screen or another allocated window's old bounds. */
static BOOL native_bounds(const GRECT *input)
{
    WORD i;
    GRECT current;
    LONG left = input->g_x < 0 ? input->g_x : 0;
    LONG top = input->g_y < 0 ? input->g_y : 0;
    LONG right = (LONG)input->g_x + input->g_w + 2;
    LONG bottom = (LONG)input->g_y + input->g_h + 2;
    LONG end;

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
        end = (LONG)current.g_x + current.g_w + 2;
        if (end > right) right = end;
        end = (LONG)current.g_y + current.g_h + 2;
        if (end > bottom) bottom = end;
    }
    return right - left <= 32767L && bottom - top <= 32767L;
}

/* Keep an ordinal, never the native w_rnext pointer: any window mutation
 * can recycle native ORECT nodes between two user calls. Rescan the current
 * owned list, bounded by its global pool, without retaining a freed node. */
static LONG visible_rectangle(CLIENT_WINDOW *w, WINDOW *native, BOOL next, GRECT *out)
{
    ORECT *node;
    UWORD index = 0, walked = 0;
    GRECT rectangle;

    if (!next)
        w->visible_next = 0;
    memset(out, 0, sizeof(*out));
    if (!(native->w_flags & VF_ISOPEN))
        return E_OK;
    for (node = native->w_rlist; node && walked++ < NUM_ORECT; node = node->o_link) {
        rectangle = node->o_gr;
        if (rc_intersect(&native->w_work, &rectangle) && index++ == w->visible_next) {
            *out = rectangle;
            w->visible_next++;
            return E_OK;
        }
    }
    return node ? EINTRN : E_OK;
}

LONG x86_64_aes_window(PD *pd, const AES_WINDOW_REQUEST *r,
                       const char *text, AES_WINDOW_INFO *info, GRECT *rectangle)
{
    CLIENT_WINDOW *w = NULL;
    WINDOW *native;
    GRECT calculated, input = r->rectangle;
    WORD h = r->handle, i, fields[4] = { 0 };
    char *saved_text;
    LONG result = E_OK;
    static const WORD sliders[4] = { WF_HSLIDE, WF_VSLIDE, WF_HSLSIZ, WF_VSLSIZ };

    if (!x86_64_aes_begin_call(pd))
        return EACCDN;
    if (!unlocked())
        return EAGAIN;
    reap();
    if (x86_64_aes_call_cancelled(pd))
        return EACCDN;
    if (r->operation == AES_WIN_FIND)
        return wm_find(input.g_x, input.g_y);
    if (r->operation == AES_WIN_CALC) {
        if (!geometry(r->kind, &input, r->selector == AES_WINDOW_WORK, rectangle))
            return EINVAL;
        return E_OK;
    }
    if (r->operation == AES_WIN_CREATE) {
        if (!geometry(r->kind, &input, TRUE, &calculated) || !native_bounds(&input))
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
    w->busy = TRUE;
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
            w->busy = FALSE;
            reap();
            return EACCDN;
        }
    }
#endif
    switch (r->operation) {
    case AES_WIN_OPEN:
    case AES_WIN_RECT:
        if (!geometry(native->w_kind, &input, TRUE, &calculated) || !native_bounds(&input))
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
        result = wm_delete(h) ? E_OK : EINVAL;
        break;
    case AES_WIN_GET:
        information(h, pd, info);
        break;
    case AES_WIN_TEXT:
        saved_text = r->selector == AES_WINDOW_NAME ? w->title : w->info;
        memcpy(saved_text, text, AES_WINDOW_TEXT_MAX + 1);
        memcpy(fields, &saved_text, sizeof(saved_text));
        result = wm_set(h, r->selector == AES_WINDOW_NAME ? WF_NAME : WF_INFO, fields) ? E_OK : EINVAL;
        break;
    case AES_WIN_TOP:
        if (!(native->w_flags & VF_ISOPEN))
            result = EINVAL;
        else
            result = wm_set(h, WF_TOP, fields) ? E_OK : EINVAL;
        break;
    case AES_WIN_SLIDER:
        fields[0] = r->value;
        result = wm_set(h, sliders[r->selector], fields) ? E_OK : EINVAL;
        break;
    case AES_WIN_VISIBLE:
        result = visible_rectangle(w, native, r->selector, rectangle);
        break;
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
    w->busy = FALSE;
    if (w->retired || x86_64_aes_call_cancelled(pd)) {
        w->retired = TRUE;
        result = EACCDN;
    } else if (r->operation == AES_WIN_DELETE)
        w->retired = TRUE;
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
    WORD h, count = 0;
    CLIENT_WINDOW *w;

    reap();
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
