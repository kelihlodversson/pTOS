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
#include "process.h"
#include "gemerror.h"
#include "gemaplib.h"
#include "obdefs.h"
#include "biosext.h"
#include "string.h"

#define MESSAGE_WAIT 0x8000
typedef struct {
    AESPD *aes;
    PD *owner;
    BOOL started;
    LONG result;
    X86_64_PROCESS_CONTEXT context;
} ACCESSORY_CONTEXT;

/* AES PIDs index trusted storage, never a user basepage or client ID.  The
 * session drains it before gem_main frees UDA storage or clears THEGLO. */
static ACCESSORY_CONTEXT contexts[NUM_PDS];

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

    c->started = TRUE;
    c->result = proc_start_accessory(c->owner);
    proc_discard_accessory(c->owner); /* also unwind a refused start */
    /* Pterm/fault unwound onto this UDA, and freed the user's syscall stack.
     * The dispatcher will remove this task without putting it back on a list. */
    c->owner = NULL;
    rlr->p_flags |= AP_MESAG | AP_ACCLOSE;
    rlr->p_stat = X32_AES_DEAD;
    dsptch();
    panic("x86-64: resumed a terminated accessory\n");
}

BOOL x86_64_aes_owned(PD *pd)
{
    ACCESSORY_CONTEXT *c = owned(pd);

    return c && c->aes == rlr;
}

LONG x86_64_aes_wait(PD *pd)
{
    if (!x86_64_aes_owned(pd))
        return EACCDN;
    rlr->p_flags |= AP_MESAG;
    mwait(MESSAGE_WAIT);
    rlr->p_evwait &= ~MESSAGE_WAIT;
    rlr->p_evflg &= ~MESSAGE_WAIT;
    return E_OK;
}

LONG x86_64_aes_yield(PD *pd)
{
    if (!x86_64_aes_owned(pd))
        return EACCDN;
    dsptch();
    return E_OK;
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
        return (pd->p_stat & X32_AES_DEAD) ? EACCDN : EINVFN;
    return aes_service_kernel_message(c->owner, message);
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

    for (i = 2; i < NUM_PDS; i++) {
        c = &contexts[i];
        if (!c->aes)
            continue;
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
        if (c->owner) {
            if (c->started)
                x86_64_process_cancel(&c->context, c->owner);
            else
                proc_discard_accessory(c->owner);
        }
        memset(c, 0, sizeof(*c));
    }
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
#include "gemevlib.h"

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
    snapshot(&before_session);
}

BOOL x86_64_aes_test_run(void)
{
    WORD i, n, dead = 0, waiting = 0;
    WORD close_message[8] = { 41, 0, 0, 0, 0, 0, 0, 0 };
    ACCESSORY_CONTEXT *c;

    if (totpds != 7 || curpid != 6)
        panic("accessory refused-load publication FAIL\n");
    for (i = 0; i < 50; i++)
        dsptch();
    for (i = 2; i < NUM_PDS; i++) {
        c = &contexts[i];
        if (!c->aes)
            continue;
        if (!memcmp(c->aes->p_name, "ACCONE  ", 8) &&
            !c->owner && c->result == 17)
            dead++;
        if (!memcmp(c->aes->p_name, "ACCFAULT", 8) &&
            !c->owner && c->result == 0xffff)
            dead++;
        if (c->owner && (c->aes->p_evwait & MESSAGE_WAIT) &&
            (c->aes->p_flags & AP_MESAG))
            waiting++;
    }
    if (dead != 2 || waiting != 2)
        panic("accessory lifecycle FAIL: dead=%d waiting=%d\n", dead, waiting);
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
    WORD window;

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
    /* Both surviving accessories and the screen manager are waiting. A real
     * timer event must wake this task from an empty dispatcher ready list. */
    idle_seen = FALSE;
    ev_timer(100L);
    if (!idle_seen)
        panic("accessory scheduler idle path not exercised\n");
    kcprintf("x32 accessory idle wake: PASS\n");
}
#endif
