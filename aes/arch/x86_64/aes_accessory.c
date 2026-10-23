/* Private ring-3 accessories scheduled by the cooperative AES dispatcher.
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later.
 */
#include "emutos.h"
#include "struct.h"
#include "aesvars.h"
#include "gemlib.h"
#include "gemasm.h"
#include "gemasync.h"
#include "gempd.h"
#include "geminit.h"
#include "aes_service.h"
#include "aes_accessory.h"
#include "aes_window.h"
#include "aes_objects_kernel.h"
#include "process.h"
#include "gemerror.h"
#include "gemaplib.h"
#include "obdefs.h"
#include "biosext.h"
#include "string.h"
#include "geminput.h"
#include "gemdisp.h"
#include "gemevlib.h"
#include "gemgsxif.h"
#include "gemctrl.h"

#define MESSAGE_WAIT 0x8000
typedef struct {
    AESPD *aes;
    PD *owner;
    BOOL started;
    BOOL registration_revoked;
    LONG result;
    X86_64_PROCESS_CONTEXT context;
} ACCESSORY_CONTEXT;

/* AES PIDs index trusted storage, never a user basepage or client ID.  The
 * session drains it before gem_main frees UDA storage or clears THEGLO. */
static ACCESSORY_CONTEXT contexts[NUM_PDS];

#if CONF_WITH_X32_ACCESSORY_TEST
static void test_queued_button(PD *pd, const AES_EVENT_REQUEST *request);
static WORD test_arrows;
#endif

static ACCESSORY_CONTEXT *owned(PD *pd)
{
    WORD i;

    for (i = 2; i < NUM_PDS; i++)
        if (contexts[i].owner == pd && pd)
            return &contexts[i];
    return NULL;
}

void x86_64_aes_setup(AESPD *pd, PD *accessory)
{
    ACCESSORY_CONTEXT *c = &contexts[pd->p_pid];

    memset(c, 0, sizeof(*c));
    c->aes = pd;
    c->owner = accessory;
    pd->p_stat &= ~X32_AES_DEAD;
    if (accessory)
        pd->p_flags = 0;
    x86_64_process_save(&c->context);
    x86_64_process_restore(&c->context);
}

void x86_64_aes_save(void)
{
    x86_64_process_save(&contexts[rlr->p_pid].context);
}

void x86_64_aes_restore(void)
{
    x86_64_process_restore(&contexts[rlr->p_pid].context);
}

void x86_64_aes_enter(void)
{
    ACCESSORY_CONTEXT *c = &contexts[rlr->p_pid];
    PD *owner = c->owner;

    c->started = TRUE;
    c->result = proc_start_accessory(owner);
    proc_discard_accessory(owner); /* also unwind a refused start */
    /* The basepage may be reused once cleanup yields. Retire its task
     * association first; native window messages to this task are dropped. */
    c->owner = NULL;
    x86_64_aes_window_finished(owner, rlr);
    x86_64_aes_objects_finished(rlr);
    /* Pterm/fault unwound onto this UDA, and freed the user's syscall stack.
     * The dispatcher will remove this task without putting it back on a list. */
    rlr->p_flags |= AP_MESAG | AP_ACCLOSE;
    rlr->p_stat = X32_AES_DEAD;
    dsptch();
    panic("x86-64: resumed a terminated accessory\n");
}

BOOL x86_64_aes_associated(PD *pd)
{
    return owned(pd) != NULL;
}

BOOL x86_64_aes_owned(PD *pd)
{
    ACCESSORY_CONTEXT *c = owned(pd);

    return c && c->aes == rlr;
}

BOOL x86_64_aes_begin_call(PD *pd)
{
    ACCESSORY_CONTEXT *c = owned(pd);

    if (!c || c->aes != rlr)
        return FALSE;
    c->registration_revoked = FALSE;
    return TRUE;
}

BOOL x86_64_aes_call_cancelled(PD *pd)
{
    ACCESSORY_CONTEXT *c = owned(pd);

    return !c || c->aes != rlr || c->registration_revoked;
}

LONG x86_64_aes_wait(PD *pd)
{
    ACCESSORY_CONTEXT *c = owned(pd);

    if (!c || c->aes != rlr)
        return EACCDN;
    c->registration_revoked = FALSE;
    rlr->p_flags |= AP_MESAG;
    mwait(MESSAGE_WAIT);
    rlr->p_evwait &= ~MESSAGE_WAIT;
    rlr->p_evflg &= ~MESSAGE_WAIT;
    return c->registration_revoked ? EACCDN : E_OK;
}

LONG x86_64_aes_yield(PD *pd)
{
#if CONF_WITH_X32_ACCESSORY_TEST
    WORD h;
    ACCESSORY_CONTEXT *c = owned(pd);
#endif

    if (!x86_64_aes_owned(pd))
        return EACCDN;
#if CONF_WITH_X32_ACCESSORY_TEST
    h = x86_64_aes_window_test_arrow(pd);
    if (h) {
        if (test_arrows >= 4 || (c->aes->p_stat & WAITIN) || c->aes->p_msg.action >= 0)
            panic("accessory deferred arrow setup FAIL\n");
        /* Same deferred slot as a short native arrow/track click while its
         * owner is runnable, rather than waiting in an event call. */
        c->aes->p_msg.wh = h;
        c->aes->p_msg.action = test_arrows++;
    }
#endif
    dsptch();
    return E_OK;
}

LONG x86_64_aes_events(PD *pd, const AES_EVENT_REQUEST *request,
                       AES_INPUT_EVENT *event, BOOL message_ready)
{
    EVSPEC flags = request->mask & ~AES_EVENT_MESSAGE;
    EVSPEC bit, completed;
    MOBLK mouse1, mouse2;
    EVB *e;
    WORD available = 0, needed = 0;
    BOOL immediate = (flags & MU_TIMER) && !request->timer_ms;
    BOOL previous;
    ACCESSORY_CONTEXT *c = owned(pd);

    if (!c || c->aes != rlr)
        return EACCDN;
    c->registration_revoked = FALSE;
    if (immediate || (request->flags & AES_EVENT_POLL))
        flags &= ~MU_TIMER;
    for (bit = 1; bit <= MU_TIMER; bit <<= 1)
        if (flags & bit)
            needed++;
    for (e = eul; e && available < needed; e = e->e_nextp)
        available++;
    if (available < needed)
        return ENSMEM;
#if CONF_WITH_X32_ACCESSORY_TEST
    test_queued_button(pd, request);
#endif
    chkkbd();
    forker();
    mouse1.m_out = request->mouse1.outside;
    mouse1.m_gr = request->mouse1.rectangle;
    mouse2.m_out = request->mouse2.outside;
    mouse2.m_gr = request->mouse2.rectangle;
    if (flags & MU_KEYBD)
        iasync_owned(MU_KEYBD, 0);
    if (flags & MU_BUTTON)
        iasync_owned(MU_BUTTON, combine_cms(request->clicks,
                      request->button_mask, request->button_state));
    if (flags & MU_M1)
        iasync_owned(MU_M1, (intptr_t)&mouse1);
    if (flags & MU_M2)
        iasync_owned(MU_M2, (intptr_t)&mouse2);
    if (flags & MU_TIMER)
        iasync_owned(MU_TIMER, (request->timer_ms + gl_ticktime - 1) / gl_ticktime);
    /* A stale message wake may survive the previous wait. No producer can
     * run between this reset and mwait() except through this dispatcher. */
    rlr->p_evflg &= ~MESSAGE_WAIT;
    if (request->mask & AES_EVENT_MESSAGE)
        rlr->p_flags |= AP_MESAG;
    if (!(request->flags & AES_EVENT_POLL) && !immediate && !message_ready)
        /* Registration can be revoked by a callback during any input wait. */
        mwait(flags | MESSAGE_WAIT);
    completed = acancel(flags);
    previous = mtrans > 1;
    event->mouse_x = previous ? pr_xrat : xrat;
    event->mouse_y = previous ? pr_yrat : yrat;
    event->buttons = previous ? pr_button : button;
    /* A completed button event owns its snapshot. Later transitions or a
     * native ev_rets() may have overwritten the shared mouse history. Read
     * the event before apret() puts its block back on the free list. */
    if (completed & MU_BUTTON)
        for (e = rlr->p_evlist; e; e = e->e_nextp)
            if (e->e_mask == MU_BUTTON) {
                event->mouse_x = HIWORD(e->e_parm);
                event->mouse_y = LOWORD(e->e_parm);
                event->buttons = HIWORD(e->e_return);
                break;
            }
    for (bit = 1; bit <= MU_TIMER; bit <<= 1)
        if (completed & bit) {
            UWORD value = apret(bit);
            if (bit == MU_KEYBD)
                event->key = value;
            if (bit == MU_BUTTON)
                event->clicks = value;
        }
    rlr->p_evwait &= ~MESSAGE_WAIT;
    rlr->p_evflg &= ~MESSAGE_WAIT;
    event->events = completed | (immediate ? MU_TIMER : 0);
    event->modifiers = kstate;
    if (rlr == gl_mowner)
        mtrans = 0;
    return c->registration_revoked ? EACCDN : E_OK;
}

void x86_64_aes_wake(PD *pd)
{
    ACCESSORY_CONTEXT *c = owned(pd);
    AESPD **link;

    if (!c)
        return;
    c->aes->p_evflg |= MESSAGE_WAIT;
    for (link = &nrl; *link; link = &(*link)->p_link)
        if (*link == c->aes && (c->aes->p_evwait & MESSAGE_WAIT)) {
            *link = c->aes->p_link;
            c->aes->p_link = drl;
            drl = c->aes;
            break;
        }
}

void x86_64_aes_registration(PD *pd, BOOL active)
{
    ACCESSORY_CONTEXT *c = owned(pd);

    if (!c)
        return;
    if (active) {
        c->aes->p_flags |= AP_OPEN;
        c->aes->p_flags &= ~AP_ACCLOSE;
    } else {
        /* Do not erase cancellation if the callback registers again before
         * the suspended wait resumes. Only a fresh wait clears this flag. */
        c->registration_revoked = TRUE;
        c->aes->p_msg.action = -1;
        c->aes->p_flags &= ~AP_OPEN;
        c->aes->p_flags |= AP_MESAG | AP_ACCLOSE;
        x86_64_aes_wake(pd);
    }
}

void x86_64_aes_flags(PD *pd, UWORD flags)
{
    ACCESSORY_CONTEXT *c = owned(pd);

    if (c)
        c->aes->p_flags |= flags;
}

LONG x86_64_aes_message(AESPD *pd, const WORD *message)
{
    ACCESSORY_CONTEXT *c = &contexts[pd->p_pid];

    if (!c->owner)
        return (c->started || (pd->p_stat & X32_AES_DEAD)) ? EACCDN : EINVFN;
    return aes_service_kernel_message(c->owner, message);
}

void x86_64_aes_pending_message(PD *pd)
{
    ACCESSORY_CONTEXT *c = owned(pd);
    WORD message[8] = { WM_ARROWED, 0, 0, 0, 0, 0, 0, 0 };

    if (!c || c->aes->p_msg.action < 0)
        return;
    message[3] = c->aes->p_msg.wh;
    message[4] = c->aes->p_msg.action;
    /* No yield between enqueue and commit. A full FIFO leaves the native
     * deferred slot intact for the next typed read, poll or message wait. */
    if (aes_service_kernel_message(pd, message) == E_OK)
        c->aes->p_msg.action = -1;
}

static void unlink_task(AESPD **head, AESPD *pd)
{
    AESPD **link;

    for (link = head; *link; link = &(*link)->p_link)
        if (*link == pd) {
            *link = pd->p_link;
            break;
        }
}

void x86_64_aes_drain(void)
{
    WORD i;
    AESPD *saved = rlr;
    ACCESSORY_CONTEXT *c;
#if CONF_WITH_X32_ACCESSORY_TEST
    AESPD *cancelled[NUM_PDS] = { 0 };
    WORD n, message[8] = { WM_REDRAW, 0, 0, 0, 0, 0, 100, 100 };
#endif

    for (i = 2; i < NUM_PDS; i++) {
        c = &contexts[i];
        if (!c->aes)
            continue;
#if CONF_WITH_X32_ACCESSORY_TEST
        cancelled[i] = c->aes;
#endif
        unlink_task(&rlr, c->aes);
        unlink_task(&drl, c->aes);
        unlink_task(&nrl, c->aes);
        /* Cancel incomplete legacy kernel events (e.g. a disk-error dialog)
         * and collect completed events before the UDA/EVB array goes away. */
        rlr = c->aes;
        acancel(0xffff);
        while (rlr->p_evlist)
            apret(rlr->p_evlist->e_mask);
        rlr = saved;
        /* These tasks will never consume another queue entry. Keep that
         * fact on the AESPD after its context is erased, before any native
         * window cleanup can send notifications during desktop restart. */
        c->aes->p_stat = X32_AES_DEAD;
        c->aes->p_msg.action = -1;
        if (c->owner) {
            if (c->started)
                x86_64_process_cancel(&c->context, c->owner);
            else
                proc_discard_accessory(c->owner);
        }
        memset(c, 0, sizeof(*c));
    }
#if CONF_WITH_X32_ACCESSORY_TEST
    for (i = 2; i < NUM_PDS; i++)
        if (cancelled[i])
            for (n = 0; n < 16; n++)
                if (x86_64_aes_message(cancelled[i], message) != EACCDN || cancelled[i]->p_qindex)
                    panic("accessory cancelled native message drop FAIL\n");
    kcprintf("x32 cancelled task messages dropped: PASS\n");
#endif
    x86_64_aes_window_drain();
    x86_64_aes_objects_drain();
    /* Internal screen manager and process zero are rebuilt on restart. */
    memset(contexts, 0, sizeof(contexts));
}

#if CONF_WITH_X32_ACCESSORY_TEST
#include "kheap.h"
#include "procmem.h"
#include "bdosbind.h"
#include "../../../bdos/kproc.h"
#include "../../../desk/aesbind.h"
#include "gemwmlib.h"
#include "gemmnlib.h"
#include "gemevlib.h"
#include "vdi_service.h"

extern UQUAD pmem_free_bytes(void);
extern UQUAD x86_64_pmem_bad_frees(void);
extern void x86_64_pmem_test_fail_after(LONG count);

typedef struct {
    KHEAP_STATS heap;
    PROCMEM_STATS window;
    UQUAD physical;
    ULONG records;
    UQUAD bad_physical;
} ACCESSORY_SNAPSHOT;

static ACCESSORY_SNAPSHOT before_session;
static WORD test_sessions;
static BOOL idle_seen;
static WORD queued_button_cases;
static WORD button_snapshot_cases;

/* Queue both transitions before the user-mode poll's forker() runs, with
 * a click count above the request to check capping as well as preservation. */
static void test_queued_button(PD *pd, const AES_EVENT_REQUEST *request)
{
    ACCESSORY_CONTEXT *c = owned(pd);
    UQUAD flags;

    if (queued_button_cases >= 2 || !c || memcmp(c->aes->p_name, "ACCTWO  ", 8) ||
        request->mask != AES_EVENT_BUTTON ||
        request->flags != (queued_button_cases ? 0 : AES_EVENT_POLL) ||
        request->clicks != 2 || request->button_mask != 1 || request->button_state != 1)
        return;
    queued_button_cases++;
    __asm__ volatile ("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
    if (forkq(mchange, MAKE_ULONG(101, 121)) < 0 ||
        forkq(bchange, MAKE_ULONG(1, 3)) < 0 ||
        forkq(mchange, MAKE_ULONG(111, 131)) < 0 ||
        forkq(bchange, MAKE_ULONG(0, 1)) < 0 ||
        forkq(mchange, MAKE_ULONG(121, 141)) < 0)
        panic("accessory queued button injection FAIL\n");
    if (flags & 0x200)
        __asm__ volatile ("sti" ::: "memory");
}

/* Complete an already suspended button wait, then disturb the shared
 * history before the accessory can run: a native event return clears it,
 * or a third transition overwrites it with a right-button press. */
static void test_button_snapshot(ACCESSORY_CONTEXT *c)
{
    MOBLK mouse = { 0, { 0, 0, gl_width, gl_height } };
    WORD rets[4];
    UQUAD flags;

    if (button_snapshot_cases >= 3 || !c->owner ||
        memcmp(c->aes->p_name, "ACCTWO  ", 8) ||
        !(c->aes->p_stat & WAITIN) ||
        (c->aes->p_evwait & ~MESSAGE_WAIT) != MU_BUTTON)
        return;
    if (!c->aes->p_evlist || c->aes->p_evlist->e_nextp ||
        c->aes->p_evlist->e_mask != MU_BUTTON ||
        (c->aes->p_evlist->e_flag & COMPLETE) || gl_mowner != c->aes)
        panic("accessory button snapshot wait FAIL\n");
    __asm__ volatile ("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
    if (button_snapshot_cases < 2) {
        if (forkq(mchange, MAKE_ULONG(101, 121)) < 0 ||
            forkq(bchange, MAKE_ULONG(1, 1)) < 0 ||
            forkq(mchange, MAKE_ULONG(111, 131)) < 0 ||
            forkq(bchange, MAKE_ULONG(0, 1)) < 0)
            panic("accessory button snapshot injection FAIL\n");
        if (button_snapshot_cases == 1 &&
            (forkq(mchange, MAKE_ULONG(121, 141)) < 0 ||
             forkq(bchange, MAKE_ULONG(2, 1)) < 0))
            panic("accessory third button injection FAIL\n");
    } else if (forkq(bchange, MAKE_ULONG(0, 1)) < 0)
        panic("accessory button release injection FAIL\n");
    if (flags & 0x200)
        __asm__ volatile ("sti" ::: "memory");
    forker();
    if (!button_snapshot_cases) {
        /* This calls native ev_rets() without yielding to the ready client. */
        ev_mouse(&mouse, rets);
        if (mtrans || rets[2])
            panic("accessory intervening native event return FAIL\n");
    }
    button_snapshot_cases++;
}

static void snapshot(ACCESSORY_SNAPSHOT *s)
{
    kheap_stats(&s->heap);
    procmem_stats(&s->window);
    s->physical = pmem_free_bytes();
    s->records = kproc_count();
    s->bad_physical = x86_64_pmem_bad_frees();
}

static void check_snapshot(const ACCESSORY_SNAPSHOT *before)
{
    ACCESSORY_SNAPSHOT after;

    snapshot(&after);
    if (before->physical != after.physical ||
        before->records != after.records ||
        before->heap.live_blocks != after.heap.live_blocks ||
        before->heap.pages != after.heap.pages ||
        before->heap.bad_frees != after.heap.bad_frees ||
        before->window.live_allocs != after.window.live_allocs ||
        before->window.free_pages != after.window.free_pages ||
        before->window.bad_frees != after.window.bad_frees ||
        before->bad_physical != after.bad_physical)
        panic("accessory resources FAIL: phys %ld/%ld records %ld/%ld heap %ld/%ld pages %ld/%ld window %ld/%ld\n",
              (long)before->physical, (long)after.physical,
              (long)before->records, (long)after.records,
              (long)before->heap.live_blocks, (long)after.heap.live_blocks,
              (long)before->heap.pages, (long)after.heap.pages,
              (long)before->window.live_allocs, (long)after.window.live_allocs);
}

void x86_64_aes_test_begin(void)
{
    ACCESSORY_SNAPSHOT before;
    LONG pd;
    WORD n;

    /* Failure before publication must unwind the load and all inherited
     * references as well as a partially prepared address space/kernel stack. */
    if (!test_sessions) {
        for (n = 1; n <= 48; n++) {
            snapshot(&before);
            pd = Pexec(PE_LOAD, "ACCWAIT.ACC", "", NULL);
            if (pd < 0)
                panic("accessory fixture missing\n");
            x86_64_pmem_test_fail_after(n);
            proc_prepare_accessory((PD *)(uintptr_t)(ULONG)pd);
            x86_64_pmem_test_fail_after(-1);
            proc_discard_accessory((PD *)(uintptr_t)(ULONG)pd);
            check_snapshot(&before);
        }
        kcprintf("x32 accessory preparation failures: PASS\n");
    }
    queued_button_cases = 0;
    button_snapshot_cases = 0;
    test_arrows = 0;
    snapshot(&before_session);
}

BOOL x86_64_aes_test_run(void)
{
    WORD i, j, n, attempt, dead = 0, waiting = 0;
    WORD close_message[8] = { 41, 0, 0, 0, 0, 0, 0, 0 };
    WORD reuse_message[8] = { 634, 0, 0, 0, 0, 0, 0, 0 };
    ACCESSORY_CONTEXT *c;
    AESPD *old_owner;
    GRECT old_control, control = { 0, 0, gl_width, gl_height };
    BOOL old_ctmown = gl_ctmown;
    WORD revocations = 0;
    BOOL reuse_sent = FALSE;
    BOOL objects_sent = FALSE;
    BOOL fault_mode_sent = FALSE, foreign_fault_done = FALSE;
    BOOL disabled_cancelled = FALSE;
    const EVSPEC revoke_masks[3] = { MU_KEYBD, MU_M1, MU_TIMER };
    AESPD *task;

    if (totpds != 7 || curpid != 6)
        panic("accessory refused-load publication FAIL\n");
    get_mown(&old_owner);
    get_ctrl(&old_control);
    set_ctrl(&control);
    gl_ctmown = TRUE;
    for (i = 2; i < NUM_PDS; i++)
        if (contexts[i].aes && !memcmp(contexts[i].aes->p_name, "ACCTWO  ", 8))
            set_mown(contexts[i].aes);
    for (attempt = 0; attempt < 1000; attempt++) {
        ev_timer(10L);
        if (!disabled_cancelled && mn_test_disabled_waiting()) {
            /* Both the disabled-title tracker and its owner are asleep.
             * Cancel from this foreign task, then wake native tracking by
             * retiring its menu while its saved continuation stays pinned. */
            vdi_service_callback_event(VDI_CALLBACK_BUTTON, 1, 0);
            vdi_service_callback_work();
            x86_64_aes_objects_reap();
            disabled_cancelled = TRUE;
        }
        dead = waiting = 0;
        for (i = 2; i < NUM_PDS; i++) {
            c = &contexts[i];
            if (!c->aes)
                continue;
            if (c->owner && !memcmp(c->aes->p_name, "ACCFAULT", 8) &&
                (c->aes->p_stat & WAITIN) && c->aes->p_evwait == MESSAGE_WAIT) {
                if (!fault_mode_sent && x86_64_aes_objects_test_fault_ready(c->owner, FALSE)) {
                    WORD fault_message[8] = { 665, 0, 0, 0, 0, 0, 0, 0 };
                    fault_message[3] = test_sessions & 1;
                    if (!ap_rdwr(MU_SDMSG, c->aes, sizeof(fault_message), fault_message))
                        panic("accessory fault mode wake FAIL\n");
                    fault_mode_sent = TRUE;
                } else if ((test_sessions & 1) && !foreign_fault_done &&
                    x86_64_aes_objects_test_fault_ready(c->owner, TRUE)) {
                    PD *failed_owner = c->owner;
                    wm_update(BEG_UPDATE);
                    w_drawdesk(&gl_rscreen);
                    /* Still on the foreign drawing task, before the owner
                     * resumes: cancellation must have retired its trees
                     * and moved its indefinite wait off the sleeping list. */
                    if (kproc_aes_get(failed_owner) ||
                        x86_64_aes_objects_test_fault_ready(failed_owner, FALSE))
                        panic("accessory foreign callback retirement FAIL\n");
                    for (task = nrl; task; task = task->p_link)
                        if (task == c->aes)
                            panic("accessory foreign callback stayed asleep FAIL\n");
                    foreign_fault_done = TRUE;
                    wm_update(END_UPDATE);
                    kcprintf("x32 foreign USERDEF fault retirement/wake: PASS\n");
                }
            }
            test_button_snapshot(c);
            if (c->owner && !reuse_sent && x86_64_aes_window_test_reuse(c->owner)) {
                reuse_sent = TRUE;
                for (j = 2; j < NUM_PDS; j++)
                    if (contexts[j].owner &&
                        !memcmp(contexts[j].aes->p_name, "ACCWAIT ", 8) &&
                        !ap_rdwr(MU_SDMSG, contexts[j].aes, sizeof(reuse_message), reuse_message))
                        panic("accessory window reuse message FAIL\n");
            }
            if (c->owner && x86_64_aes_window_test_revoke(c->owner)) {
                vdi_service_callback_event(VDI_CALLBACK_BUTTON, 626, 0);
                vdi_service_callback_work();
            }
            if (c->owner && revocations < 3 &&
                !memcmp(c->aes->p_name, "ACCTWO  ", 8) &&
                (c->aes->p_stat & WAITIN) &&
                (c->aes->p_evwait & ~MESSAGE_WAIT) == revoke_masks[revocations]) {
                if (!c->aes->p_evlist || c->aes->p_evlist->e_nextp ||
                    c->aes->p_evlist->e_mask != revoke_masks[revocations])
                    panic("accessory revoked input event cleanup FAIL\n");
                /* The registered handler calls aes_appl_exit() in ring 3
                 * while its owner's syscall is suspended on another stack. */
                vdi_service_callback_event(VDI_CALLBACK_BUTTON, 613 + revocations, 0);
                vdi_service_callback_work();
                revocations++;
                if (revocations == 3) {
                    set_ctrl(&old_control);
                    set_mown(old_owner);
                    gl_ctmown = old_ctmown;
                }
                for (task = nrl; task; task = task->p_link)
                    if (task == c->aes)
                        panic("accessory revoked input wait stayed asleep\n");
            }
            if (!memcmp(c->aes->p_name, "ACCONE  ", 8) &&
                !c->owner && (c->aes->p_stat & X32_AES_DEAD) && c->result == 17)
                dead++;
            if (!memcmp(c->aes->p_name, "ACCFAULT", 8) &&
                !c->owner && (c->aes->p_stat & X32_AES_DEAD) && c->result == 0xffff)
                dead++;
            if (c->owner && !x86_64_aes_window_test_revoke(c->owner) &&
                c->aes->p_evwait == MESSAGE_WAIT &&
                (c->aes->p_flags & AP_MESAG)) {
                if (c->aes->p_evlist)
                    panic("accessory revoked input events leaked\n");
                waiting++;
            }
        }
        /* Message wakeups can leave p_evwait set on a runnable task. Wait
         * for both clients to have finished their windows and really slept. */
        if (dead == 2 && waiting == 2 && x86_64_aes_window_test_ready()) {
            if (!objects_sent) {
                WORD objects_message[8] = { 662, 0, 0, 0, 0, 0, 0, 0 };
                for (i = 2; i < NUM_PDS; i++)
                    if (contexts[i].owner && !memcmp(contexts[i].aes->p_name, "ACCTWO  ", 8))
                        if (!ap_rdwr(MU_SDMSG, contexts[i].aes, sizeof(objects_message), objects_message))
                            panic("accessory object test wake FAIL\n");
                objects_sent = TRUE;
            } else if (x86_64_aes_window_objects_done())
                break;
        }
    }
    if (revocations != 3) {
        set_ctrl(&old_control);
        set_mown(old_owner);
        gl_ctmown = old_ctmown;
    }
    if (attempt == 1000 || dead != 2 || waiting != 2 || revocations != 3 || button_snapshot_cases != 3 || !reuse_sent || test_arrows != 4 ||
        !fault_mode_sent || !disabled_cancelled || ((test_sessions & 1) && !foreign_fault_done))
        panic("accessory lifecycle FAIL: dead=%d waiting=%d\n", dead, waiting);
    x86_64_aes_window_test();
    /* Exercise the native AES message bridge and receive acknowledgement. */
    for (i = 2; i < NUM_PDS; i++) {
        if (!contexts[i].owner)
            continue;
        if (!memcmp(contexts[i].aes->p_name, "ACCWAIT ", 8)) {
            /* Fill the typed queue without yielding. The following AC_CLOSE
             * must let its receiver run and drain all eight ordered messages. */
            close_message[0] = 548;
            for (n = 0; n < AES_MESSAGE_QUEUE; n++) {
                close_message[3] = n;
                if (!ap_rdwr(MU_SDMSG, contexts[i].aes, sizeof(close_message), close_message))
                    panic("accessory native queue fill FAIL\n");
            }
            close_message[0] = 41;
            close_message[3] = 0;
        }
        if (!ap_rdwr(MU_SDMSG, contexts[i].aes, sizeof(close_message), close_message))
            panic("accessory native message bridge FAIL\n");
    }
    for (i = 0; i < 50; i++)
        dsptch();
    for (i = 2; i < NUM_PDS; i++)
        if (contexts[i].owner && !(contexts[i].aes->p_flags & AP_ACCLOSE))
            panic("accessory native message acknowledgement FAIL\n");
    kcprintf("x32 accessory refused load: PASS\n");
    kcprintf("x32 accessory native queue backpressure: PASS\n");
    kcprintf("x32 accessory message/exit/fault: PASS\n");
    return test_sessions < 3;
}

void x86_64_aes_test_finish(void)
{
    check_snapshot(&before_session);
    test_sessions++;
    kcprintf("x32 accessory restart %d: PASS\n", test_sessions);
    if (test_sessions == 3)
        kcprintf("x32 accessory lifecycle: PASS\n");
}

void x86_64_aes_test_idle(void)
{
    UQUAD flags;

    __asm__ volatile ("pushfq; popq %0" : "=r"(flags));
    if (!(flags & 0x200))
        panic("accessory scheduler idle interrupts FAIL\n");
    idle_seen = TRUE;
}

void x86_64_aes_test_desktop(OBJECT *desktop, WORD root)
{
    static char name[] = "Native window title";
    static char info[] = "Native window information";
    GRECT rect = { 20, 20, 240, 120 };
    WORD window, i, attempt;
    WORD reopen[8] = { 631, 0, 0, 0, 0, 0, 0, 0 };

    if ((uintptr_t)desktop <= 0xffffffffULL ||
        !wm_test_desktop(desktop, root))
        panic("accessory native desktop pointer/root FAIL\n");
    window = wind_create_grect(NAME | INFO, &rect);
    if (window < 1 ||
        !wind_set(window, WF_NAME, (void *)name, 0x1234, 0x5678) ||
        !wind_set(window, WF_INFO, (void *)info, 0x4321, 0x7654) ||
        D.w_win[window].w_pname != name || D.w_win[window].w_pinfo != info ||
        !wind_set(window, WF_HSLIDE, 321, 0, 0, 0) ||
        D.w_win[window].w_hslide != 321 ||
        !wind_open_grect(window, &rect) || !wind_close(window) || !wind_delete(window))
        panic("accessory native window pointer/scalar FAIL\n");
    kcprintf("x32 accessory native window pointers: PASS\n");
    /* EmuDesk startup calls wind_new(). Reopen the surviving fixtures after
     * that global reset, then wait for their clipped VDI redraws to finish. */
    for (i = 2; i < NUM_PDS; i++)
        if (contexts[i].owner &&
            !ap_rdwr(MU_SDMSG, contexts[i].aes, sizeof(reopen), reopen))
            panic("accessory live window reopen message FAIL\n");
    for (attempt = 0; attempt < 100; attempt++) {
        ev_timer(10L);
        if (x86_64_aes_window_test_ready())
            break;
    }
    if (attempt == 100)
        panic("accessory live window reopen FAIL\n");
    x86_64_aes_window_test();
    /* Both surviving accessories and the screen manager are waiting. A real
     * timer event must wake this task from an empty dispatcher ready list. */
    idle_seen = FALSE;
    ev_timer(100L);
    if (!idle_seen)
        panic("accessory scheduler idle path not exercised\n");
    kcprintf("x32 accessory idle wake: PASS\n");
}
#endif
