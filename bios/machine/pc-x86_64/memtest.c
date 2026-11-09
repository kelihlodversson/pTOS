/*
 * memtest.c - boot-time memory-management self-test (x86-64)
 *
 * Exercises, on every boot of a CONF_WITH_X86_64_MEMTEST build, the
 * tracked-allocation machinery the x32 process lifecycle depends on
 * (issue #362): the physical page allocator's free path, the growable
 * kernel heap, the process window, per-process address spaces, KPROC
 * records and the real Pexec()/teardown paths in bdos/proc.c.
 *
 * Every test takes a snapshot of each allocator's counters, runs a cycle
 * many times -- more than the former fixed object pool could hold -- and
 * requires the counters to return exactly to the snapshot: a leaked
 * mapping, table page, backing page, KPROC record or heap block shows up
 * as a difference.  Failure paths are exercised with the page allocator's
 * failure injection.  The result is one serial line the CI boot test looks
 * for:  "x86-64 memtest: PASS"  or  "x86-64 memtest: FAIL (n)".
 *
 * Copyright (C) 2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#ifndef MACHINE_PC_X86_64
#error This file must only be compiled for the pc-x86_64 machine
#endif

#if CONF_WITH_X86_64_MEMTEST

#include "emutos.h"
#include "biosext.h"
#include "string.h"
#include "bdosdefs.h"
#include "bdosbind.h"
#include "gemdos.h"
#include "gemerror.h"
#include "kheap.h"
#include "procmem.h"
#include "kprint.h"
#include "setjmp.h"
#include "pgtable.h"
#include "pmem.h"
#include "pc_x86_64_memory.h"
#include "progimage.h"
#include "io.h"
#include "trap.h"
#include "gdt.h"
#include "pc_x86_64_memtest.h"
#include "tosvars.h"
#include "../../../bdos/kproc.h"
#include "../../../bdos/kthread_context.h"
#include "../../../bdos/fs.h"
#include "../../../bdos/mem.h"
#include "../../../bdos/kthread.h"
#include "../../../bdos/proc_clone.h"
#include "../../../bdos/process_observer.h"
#include "../../../bdos/process_exec_internal.h"
#if CONF_WITH_NATIVE_SCHED
#include "../../../bdos/arch/x86_64/thread_startup_x32.h"
#include "../../../bdos/arch/x86_64/thread_return_x32.h"
#include "../../../bdos/thread_startup_backend.h"
#endif
#include "../../../bdos/proc.h"
#if CONF_WITH_CUIS
#include "../../../aes/cuis.h"
#include "ksched.h"
#include "process.h"
#include "ikbd.h"
#include "iorec.h"
#endif
#if CONF_WITH_X32_VDI
#include "lineavars.h"
#include "../../../vdi/vdi_defs.h"
#include "screen_mode.h"
#include "font.h"
#include "vdi_service.h"
#endif

#if CONF_WITH_CUIS
static BOOL cuis_input_watching, cuis_input_armed, cuis_input_legacy_clean;
static ULONG cuis_input_injections, cuis_pointer_injections;
static BOOL cuis_pointer_armed;
#if CONF_WITH_X32_VDI
static ULONG cuis_pointer_legacy_calls, cuis_pointer_cursor_calls;
static BOOL cuis_pointer_callbacks_clean, cuis_pointer_cursor_updated, cuis_pointer_guard_clean;
#if CONF_WITH_USER_CALLBACKS
static BOOL cuis_pointer_stale_clean;
#endif
static void (*cuis_saved_cursor)(WORD, WORD);
static void cuis_test_button(WORD buttons) { (void)buttons; cuis_pointer_legacy_calls++; }
static ULONG cuis_test_motion(WORD x, WORD y)
{
    cuis_pointer_legacy_calls++;
    return MAKE_ULONG(x, y);
}
static void cuis_test_cursor(WORD x, WORD y)
{
    cuis_pointer_cursor_calls++;
    cuis_saved_cursor(x, y);
}
#endif
void x86_64_memtest_cuis_wait(ULONG events)
{
    if (!cuis_input_watching) return;
    if ((events & CUIS_EVENT_POINTER) && !cuis_pointer_injections) cuis_pointer_armed = TRUE;
    else if ((events & CUIS_EVENT_KEY) && !cuis_input_injections) cuis_input_armed = TRUE;
}
void x86_64_memtest_cuis_tick(void)
{
    if (cuis_input_watching && cuis_pointer_armed) {
        SBYTE press[3] = { (SBYTE)0xfa, 1, -1 }, release[3] = { (SBYTE)0xf8, 0, 0 };
#if CONF_WITH_X32_VDI
        void (*button)(WORD) = linea_vars.user_but;
        ULONG (*motion)(WORD, WORD) = linea_vars.user_mot;
        WORD old_x = linea_vars.GCURX, old_y = linea_vars.GCURY;
        cuis_saved_cursor = linea_vars.user_cur;
        cuis_pointer_legacy_calls = cuis_pointer_cursor_calls = 0;
        linea_vars.user_but = cuis_test_button;
        linea_vars.user_mot = cuis_test_motion;
        linea_vars.user_cur = cuis_test_cursor;
#endif
        cuis_pointer_armed = FALSE;
        cuis_pointer_injections++;
#if CONF_WITH_X32_VDI
#if CONF_WITH_USER_CALLBACKS
        vdi_service_test_motion_begin();
#endif
        linea_vars.mouse_flag++;
#endif
        call_mousevec(press);
        call_mousevec(release);
#if CONF_WITH_X32_VDI
        cuis_pointer_guard_clean = !cuis_pointer_cursor_calls &&
            linea_vars.GCURX == (old_x < xres ? old_x + 1 : old_x) &&
            linea_vars.GCURY == (old_y > 0 ? old_y - 1 : 0);
        /* Show/hide may clear this after the packet. Flush must re-arm it. */
        linea_vars.draw_flag = FALSE;
        linea_vars.mouse_flag--;
        vdi_cuis_cursor_flush();
#if CONF_WITH_USER_CALLBACKS
        cuis_pointer_stale_clean = vdi_service_test_motion_end();
#endif
#endif
#if CONF_WITH_X32_VDI
        linea_vars.user_but = button;
        linea_vars.user_mot = motion;
        linea_vars.user_cur = cuis_saved_cursor;
        cuis_pointer_callbacks_clean = !cuis_pointer_legacy_calls;
        cuis_pointer_cursor_updated = cuis_pointer_cursor_calls != 0;
#endif
    }
    if (cuis_input_watching && cuis_input_armed) {
        WORD tail = ikbdiorec.tail;
        cuis_input_armed = FALSE;
        cuis_input_injections++;
        /* Deliver through the real translated keyboard producer in an IRQ,
         * not a user-callable CUIS injection or a syscall polling hook. */
        kbd_int(0x25);
        kbd_int(0xa5);
        if (ikbdiorec.tail != tail) cuis_input_legacy_clean = FALSE;
    }
}
#endif

extern void set_owner(void *addr, PD *p);
extern void proc_free_owned(PD *p);
extern void proc_make_resident(PD *p, ULONG keep_bytes);
extern PD *run;

#define PAGE            X86_64_PAGE_SIZE

static int failures;

#define CHECK(cond, what) \
    do { \
        if (!(cond)) { \
            kcprintf("memtest FAIL line %d: %s\n", __LINE__, what); \
            failures++; \
        } \
    } while (0)

static BOOL exit_handoff_watch;
static ULONG exit_handoff_stages[4];
static UQUAD exit_handoff_free_before;

static unsigned fallback_observer_stages;
void x86_64_memtest_fallback_observer(KPROC *process, BOOL ready)
{
    PROCESS_COMPLETION completion;
    CHECK(kproc_ref_terminal(process) == ready, "fallback readiness follows syscall stack reclamation");
    CHECK(kproc_ref_has_basepage(process) != ready, "fallback identity does not retain retired basepage");
    CHECK(kproc_ref_completion(process, &completion), "fallback canonical completion snapshot");
    if (ready)
        CHECK(completion.state == PROCESS_COMPLETED &&
              (completion.order_low || completion.order_high), "fallback completion gets terminal order");
    fallback_observer_stages++;
}

void x86_64_memtest_exit_handoff(KPROC *process, PD *pd, UQUAD stack_phys,
                                UWORD status, UWORD stage)
{
    UQUAD rsp;
    UWORD recorded;
    if (!exit_handoff_watch) return;
    CHECK(stage < ARRAY_SIZE(exit_handoff_stages), "valid exit handoff stage");
    if (stage >= ARRAY_SIZE(exit_handoff_stages)) return;
    __asm__ volatile ("movq %%rsp,%0" : "=r"(rsp));
    CHECK(x86_64_memtest_kstack_contains(stack_phys, rsp) == (stage == 0),
          "exit finalization runs away from the child syscall stack");
    if (stage == 1) exit_handoff_free_before = pmem_free_bytes();
    if (stage == 2)
        CHECK(pmem_free_bytes() == exit_handoff_free_before + x86_64_memtest_kstack_bytes(),
              "the child syscall stack is released exactly once");
    if (stage < 3) {
        CHECK(kproc_ref_has_basepage(process) && kproc_user_aspace(pd),
              "exit handoff retains process and mappings until finalization");
        if (!stage) {
            CHECK(!kthread_create(pd), "exit handoff rejects new thread associations");
            proc_dispose_process(process);
            CHECK(kproc_ref_has_basepage(process) && kproc_user_aspace(pd),
                  "reentrant disposal cannot commit a pending exit handoff");
        }
    } else {
        CHECK(!kproc_ref_has_basepage(process) &&
              kproc_ref_status(process, &recorded) && recorded == status,
              "exit finalization publishes the retained status once");
    }
    exit_handoff_stages[stage]++;
}

#if CONF_WITH_NATIVE_SCHED
static ULONG task_thread_stages[8];
static ULONG live_process_thread_reaps;
void x86_64_memtest_task_thread(KTHREAD *thread, UQUAD stack_phys, UWORD stage)
{
    UQUAD rsp;
    CHECK(stage < ARRAY_SIZE(task_thread_stages), "valid task THREAD ownership stage");
    if (stage >= ARRAY_SIZE(task_thread_stages)) return;
    __asm__ volatile ("movq %%rsp,%0" : "=r"(rsp));
    CHECK(x86_64_memtest_kstack_contains(stack_phys, rsp) == (stage == 2 || stage == 3),
          "only task entry and launcher handoff execute on the scheduler stack");
    if (stage != 7) {
        CHECK(thread && kthread_tid(thread) && kthread_process(thread) && kthread_aspace(thread),
              "each managed execution retains independent THREAD/PROCESS/ASPACE identity");
        CHECK(kthread_is_executing(thread) == (stage == 1 || stage == 2),
              "task dispatch and generic THREAD execution lifetime agree");
        CHECK(kthread_is_stopped(thread) == (stage == 3 || stage == 4),
              "task can only be reaped after a stopped launcher handoff");
        if (stage == 4 && kproc_ref_has_basepage(kthread_process(thread))) {
            UWORD status;
            if (kthread_exit_status(thread, &status)) {
                CHECK(!kproc_ref_status(kthread_process(thread), NULL), "sibling reap does not complete live PROCESS");
                live_process_thread_reaps++;
            }
        }
        if (stage == 3)
            CHECK(kproc_ref_has_basepage(kthread_process(thread)),
                  "launcher stops THREAD before final PROCESS cleanup");
    }
    task_thread_stages[stage]++;
}
#endif

/* Everything that must be exactly the same before and after a cycle. */
typedef struct {
    UQUAD pmem_free;
    UQUAD pmem_bad;
    KHEAP_STATS heap;
    PROCMEM_STATS pm;
    ULONG kprocs;
    ULONG dir_refs;             /* current-directory references run holds + children took */
} SNAP;

/* Sum of the use counts of every directory run has a reference to: a basepage
 * that inherits and fails to give one back shows up here, which the allocator
 * counters cannot see. */
static ULONG dir_refs(void)
{
    ULONG sum = 0;
    int d;

    for (d = 0; d < NUMCURDIR; d++)
        if (PD_CURDIR(run)[d])
            sum += dirtbl[PD_CURDIR(run)[d]].use;
    return sum;
}

static void snap(SNAP *s)
{
    s->dir_refs = dir_refs();
    s->pmem_free = pmem_free_bytes();
    s->pmem_bad = x86_64_pmem_bad_frees();
    kheap_stats(&s->heap);
    procmem_stats(&s->pm);
    s->kprocs = kproc_count();
}

/* Compares with a snapshot, ignoring bad_frees (tests that provoke
 * rejected frees account for them separately). */
static BOOL same(const SNAP *a, const char *what)
{
    SNAP b;
    BOOL ok;

    snap(&b);
    ok = a->pmem_free == b.pmem_free &&
         a->heap.live_blocks == b.heap.live_blocks &&
         a->heap.pages == b.heap.pages &&
         a->pm.live_allocs == b.pm.live_allocs &&
         a->pm.free_pages == b.pm.free_pages &&
         a->kprocs == b.kprocs && a->dir_refs == b.dir_refs;
    if (!ok) {
        kcprintf("memtest FAIL: %s leaked: pmem %ld->%ld heap blocks %ld->%ld "
                 "pages %ld->%ld window allocs %ld->%ld free %ld->%ld kprocs %ld->%ld "
                 "dir refs %ld->%ld\n",
                 what, (long)a->pmem_free, (long)b.pmem_free,
                 (long)a->heap.live_blocks, (long)b.heap.live_blocks,
                 (long)a->heap.pages, (long)b.heap.pages,
                 (long)a->pm.live_allocs, (long)b.pm.live_allocs,
                 (long)a->pm.free_pages, (long)b.pm.free_pages,
                 (long)a->kprocs, (long)b.kprocs,
                 (long)a->dir_refs, (long)b.dir_refs);
        failures++;
    }
    return ok;
}

/* An address space the test cannot go on without: failing to get one is a
 * reported failure, never a silent skip of what follows. */
static ASPACE *must_create(const char *what)
{
    ASPACE *as = aspace_create();

    CHECK(as != NULL, what);
    return as;
}

/* ---- physical page allocator ------------------------------------- */

#if CONF_WITH_LIBAES && CONF_WITH_X32_VDI
/* Exercise cursor composition with actual ring-3 VDI writes and the normal
 * native mouse-movement/VBL path; no user-facing cursor test API is needed. */
static WORD renderer_vdi(WORD code, WORD value)
{
    VDICONTROL control = { 0 };
    WORD input = value, points[12] = { 50,50 }, output[45] = { 0 };
    VDIPB saved = linea_vars.local_pb;
    VDIPB pb = { &control, &input, points, output, points };
    control.code = code; control.handle = 1;
    if (code == 105) control.nptsin = 1;
    else control.nintin = 1;
    GSX_ENTRY(0x73,&pb);
    linea_vars.local_pb = saved;
    return output[1];
}
static void test_renderer_cursor(void)
{
    WORD hidden = linea_vars.HIDE_CNT, x = linea_vars.GCURX, y = linea_vars.GCURY;
    char tail[3] = { 1,'c',0 };
    LONG result;
    renderer_vdi(123,1);
    linea_vars.GCURX = 50; linea_vars.GCURY = 50;
    renderer_vdi(122,0);
    CHECK(linea_vars.HIDE_CNT == 0, "renderer cursor test starts visible");
    result = Pexec(PE_LOADGO,"C:\\GEMDRAW.TOS",tail,NULL);
    CHECK(result == 0 && linea_vars.HIDE_CNT == 0, "object painting preserves visible cursor state");
    linea_vars.GCURX = 120; linea_vars.GCURY = 120;
    linea_vars.user_cur(120,120);
    if (vblqueue[0]) vblqueue[0]();
    CHECK(renderer_vdi(105,0) == 2, "cursor movement restores repainted object pixels");
    renderer_vdi(123,1); renderer_vdi(123,1);
    CHECK(linea_vars.HIDE_CNT == 2, "renderer cursor test starts with nested hides");
    result = Pexec(PE_LOADGO,"C:\\GEMDRAW.TOS",tail,NULL);
    CHECK(result == 0 && linea_vars.HIDE_CNT == 2, "object painting preserves nested cursor hides");
    /* Both test passes have removed the cursor at this point. */
    linea_vars.GCURX = x; linea_vars.GCURY = y;
    linea_vars.HIDE_CNT = hidden ? hidden : 1;
    if (!hidden) renderer_vdi(122,1);
}
#endif

static void test_pmem(void)
{
    SNAP s;
    UQUAD a, b, c, limited;
    UQUAD bad, n, i;
    UQUAD *runs;

    snap(&s);

    a = x86_64_pmem_try_alloc_pages(4, 0);
    CHECK(a != X86_64_PMEM_NONE && !(a & (PAGE - 1)), "pmem alloc");
    CHECK(pmem_free_bytes() == s.pmem_free - 4 * PAGE, "pmem accounting");

    /* free out of order: the pieces must coalesce back, not fragment */
    CHECK(x86_64_pmem_free_pages(a + 2 * PAGE, 1), "free middle");
    CHECK(x86_64_pmem_free_pages(a, 1), "free first");
    CHECK(x86_64_pmem_free_pages(a + PAGE, 1), "free second");
    CHECK(x86_64_pmem_free_pages(a + 3 * PAGE, 1), "free last");
    CHECK(pmem_free_bytes() == s.pmem_free, "pmem restored");
    /* first-fit is by list slot, not address, so a need not come back;
     * but a 4-page run must exist at or below the end of the freed one */
    b = x86_64_pmem_try_alloc_pages(4, a + 4 * PAGE);
    CHECK(b != X86_64_PMEM_NONE && b + 4 * PAGE <= a + 4 * PAGE, "coalesced run reusable");
    if (b != X86_64_PMEM_NONE)
        x86_64_pmem_free_pages(b, 4);

    /* a double free, a misaligned and an out-of-range free are refused */
    bad = x86_64_pmem_bad_frees();
    CHECK(!x86_64_pmem_free_pages(a, 1), "double free refused");
    CHECK(!x86_64_pmem_free_pages(a + 1, 1), "misaligned free refused");
    CHECK(!x86_64_pmem_free_pages(x86_64_pmem_highest_addr(), 1), "beyond-RAM free refused");
    CHECK(x86_64_pmem_bad_frees() == bad + 3, "bad frees counted");
    /* memory the allocator never owned -- here this image's own page-table
     * pages, and physical page 0 -- is refused even though it is below the
     * top of RAM and not on any free list */
    bad = x86_64_pmem_bad_frees();
    CHECK(!x86_64_pmem_free_pages(x86_64_kernel_pml4_phys() & ~(PAGE - 1), 1),
          "kernel image page refused");
    CHECK(!x86_64_pmem_free_pages(0, 1), "unmanaged page refused");
    CHECK(x86_64_pmem_bad_frees() == bad + 2, "unmanaged frees counted");
    CHECK(pmem_free_bytes() == s.pmem_free, "refused frees changed nothing");

    /* runtime fragmentation: free every other page of a large run, then the
     * rest -- far more separate holes than the boot-time region list could
     * name -- and everything must come back */
    n = pmem_free_bytes() / PAGE / 4;    /* a quarter of free RAM, */
    if (n > 20000)                              /* up to 20000 pages */
        n = 20000;
    CHECK(n >= 1000, "enough free memory for the fragmentation test");
    runs = kalloc(n * sizeof(UQUAD));
    CHECK(runs != NULL, "page vector for the fragmentation test");
    if (runs) {
        for (i = 0; i < n; i++) {
            runs[i] = x86_64_pmem_try_alloc_pages(1, 0);
            if (runs[i] == X86_64_PMEM_NONE)
                break;
        }
        CHECK(i == n, "all single pages allocated");
        for (n = i, i = 0; i < n; i += 2)
            x86_64_pmem_free_pages(runs[i], 1);
        for (i = 1; i < n; i += 2)
            x86_64_pmem_free_pages(runs[i], 1);
        kfree(runs);
    }
    CHECK(pmem_free_bytes() == s.pmem_free, "fragmented frees all reclaimed");

    /* the PHYSICAL limit is honoured, and independent of any virtual one */
    c = x86_64_pmem_try_alloc_pages(1, 0);
    limited = x86_64_pmem_try_alloc_pages(1, c);   /* must end at or below c */
    CHECK(limited == X86_64_PMEM_NONE || limited + PAGE <= c, "physical limit");
    if (limited != X86_64_PMEM_NONE)
        x86_64_pmem_free_pages(limited, 1);
    x86_64_pmem_free_pages(c, 1);

    /* failure injection: exactly one allocation fails, then it recovers */
    x86_64_pmem_test_fail_after(1);
    CHECK(x86_64_pmem_try_alloc_pages(1, 0) == X86_64_PMEM_NONE, "injected failure");
    c = x86_64_pmem_try_alloc_pages(1, 0);
    CHECK(c != X86_64_PMEM_NONE, "recovers after failure");
    x86_64_pmem_free_pages(c, 1);

    same(&s, "pmem");
}

/* ---- kernel heap --------------------------------------------------- */

#define HEAP_BLOCKS 2000        /* far more than the old 118-entry pool */

static void test_kheap(void)
{
    SNAP s;
    void **v;
    KHEAP_STATS before, after;
    ULONG i, j;
    static const ULONG sizes[] = { 1, 17, 48, 49, 400, 2032, 2033, 5000, 100000 };
    UBYTE *p;
    void *q;
    BOOL zeroed = TRUE;

    snap(&s);
    v = kalloc(HEAP_BLOCKS * sizeof(void *));      /* itself a large block */
    CHECK(v != NULL, "kalloc vector");
    if (!v)
        return;

    for (i = 0; i < HEAP_BLOCKS; i++) {
        v[i] = kalloc(72);                          /* KPROC-sized */
        if (!v[i] || ((UQUAD)(uintptr_t)v[i] & 15))
            break;
        p = v[i];
        for (j = 0; j < 72; j++)
            if (p[j])
                zeroed = FALSE;
        memset(p, 0xA5, 72);                        /* dirty it for the next owner */
    }
    CHECK(i == HEAP_BLOCKS, "heap grows past the fixed pool");
    CHECK(zeroed, "kalloc returns zeroed memory");
    while (i--)
        kfree(v[i]);

    /* every size class and the large path, allocated together */
    for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        v[i] = kalloc(sizes[i]);
        CHECK(v[i] != NULL, "size class alloc");
        if (v[i])
            memset(v[i], 0x5A, sizes[i]);
    }
    for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++)
        kfree(v[i]);

    /* defined failure: no memory -> NULL, heap unchanged */
    kheap_stats(&before);
    x86_64_pmem_test_fail_after(1);
    CHECK(kalloc(200000) == NULL, "kalloc fails cleanly");
    kheap_stats(&after);
    CHECK(before.live_blocks == after.live_blocks && before.pages == after.pages,
          "failed kalloc changed nothing");
    CHECK(kalloc(0) == NULL, "kalloc(0)");
    CHECK(kalloc(0x7fffffffUL) == NULL, "kalloc huge");
    /* the whole image window fits in one block: the ELF loader builds a program
     * there (bdos/elfld.c) */
    q = kalloc(USER_IMAGE_SIZE);
    CHECK(q != NULL, "a block as big as the image window");
    kfree(q);

    /* ownership: freed twice, foreign pointers and process memory refused */
    q = kalloc(64);
    kheap_stats(&before);
    kfree(q);
    kfree(q);                                       /* double free */
    kfree(&before);                                 /* not in the heap */
    p = procmem_alloc(1, 0);
    kfree(p);                                       /* user memory */
    procmem_free(p);
    kheap_stats(&after);
    CHECK(after.bad_frees == before.bad_frees + 3, "bad kfree()s rejected");

    kfree(v);
    same(&s, "kheap");
}

/* ---- process window ------------------------------------------------ */

static ULONG pre_free_calls;
static void *callback_unpin_block;
static void *callback_replacement;
static void *callback_replacement_owner;
static jmp_buf cleanup_test_resume;
static void count_pre_free(void *base)
{
    (void)base;
    pre_free_calls++;
}
static void unpin_during_cleanup(void *base)
{
    (void)base;
    procmem_pin((VADDR)(uintptr_t)callback_unpin_block, PAGE, -1);
    CHECK(!procmem_size(callback_unpin_block), "callback unpin reclaims deferred allocation");
    pre_free_calls++;
}
static void jump_during_cleanup(void *base)
{
    (void)base;
    longjmp(cleanup_test_resume, 1);
}
static void replace_during_keep(void *base)
{
    CHECK(procmem_free(base), "keep callback frees prior allocation");
    callback_replacement = procmem_alloc(PAGE, 0);
    CHECK(callback_replacement == base, "keep callback reuses address");
    procmem_set_owner(callback_replacement, callback_replacement_owner);
}

static void test_procmem(void)
{
    SNAP s;
    PROCMEM_STATS st, st2;
    UBYTE *p, *q;
    void **v;
    ULONG i, n = 0;
    int owner_a, owner_b;

    snap(&s);

    /* repeated alloc/free reclaims the window */
    for (i = 0; i < 1000; i++) {
        p = procmem_alloc(5000, PROCMEM_ZERO);
        if (!p)
            break;
        CHECK(procmem_contains(p), "inside window");
        CHECK((UQUAD)(uintptr_t)p + 2 * PAGE <= USER_VA_LIMIT, "below 4 GiB");
        CHECK(!((UQUAD)(uintptr_t)p & (PAGE - 1)), "page aligned");
        memset(p, 0xAA, 5000);
        CHECK(procmem_free(p), "free");
    }
    CHECK(i == 1000, "1000 alloc/free cycles");
    same(&s, "procmem cycles");

    /* zeroing is the caller's explicit choice */
    p = procmem_alloc(PAGE, 0);
    memset(p, 0xAA, PAGE);
    procmem_free(p);
    q = procmem_alloc(PAGE, 0);
    CHECK(q == p && q[100] == 0xAA, "no PROCMEM_ZERO leaves old contents");
    procmem_free(q);
    q = procmem_alloc(PAGE, PROCMEM_ZERO);
    CHECK(q == p && q[100] == 0 && q[PAGE - 1] == 0, "PROCMEM_ZERO zeroes");
    CHECK(procmem_free(q), "free zeroed");

    /* double free, interior and foreign pointers are refused */
    procmem_stats(&st);
    CHECK(!procmem_free(q), "double free refused");
    p = procmem_alloc(2 * PAGE, 0);
    CHECK(!procmem_free(p + PAGE), "interior pointer refused");
    CHECK(!procmem_free(&st), "foreign pointer refused");
    CHECK(procmem_free(p), "free");
    procmem_stats(&st2);
    CHECK(st2.bad_frees == st.bad_frees + 3, "refused releases counted");

    /* exhaustion: allocation fails cleanly and everything comes back */
    v = kalloc(st.total_pages * sizeof(void *));
    for (i = 0; v && i < st.total_pages + 1; i++) {
        v[i] = procmem_alloc(PAGE, 0);
        if (!v[i])
            break;
        n++;
    }
    CHECK(v && n == st.free_pages && i == st.free_pages, "window fills exactly");
    CHECK(procmem_alloc(PAGE, 0) == NULL, "exhausted window returns NULL");
    CHECK(!procmem_range_live(X86_64_LOW_TPA_VIRT_BASE - PAGE, PAGE) &&
          !procmem_range_live(X86_64_LOW_TPA_VIRT_BASE + X86_64_LOW_TPA_BYTES, PAGE),
          "range check rejects outside the window");
    while (n--)
        procmem_free(v[n]);
    kfree(v);
    p = procmem_alloc(PAGE, 0);
    CHECK(p != NULL, "recovers after exhaustion");
    procmem_free(p);

    /* ownership: owned blocks die with their owner, others survive */
    {
        void *a1 = procmem_alloc(PAGE, 0), *a2 = procmem_alloc(PAGE, 0);
        void *b1 = procmem_alloc(PAGE, 0), *perm = procmem_alloc(PAGE, 0);

        procmem_set_owner(a1, &owner_a);
        procmem_set_owner(a2, &owner_a);
        procmem_set_owner(b1, &owner_b);
        pre_free_calls = 0;
        procmem_free_owned(&owner_a, count_pre_free);
        CHECK(pre_free_calls == 2, "pre_free per owned block");
        CHECK(!procmem_size(a1) && !procmem_size(a2), "owned blocks freed");
        CHECK(procmem_size(b1) && procmem_size(perm), "others survive");
        procmem_free_owned(&owner_a, NULL);      /* again: nothing */
        procmem_free_owned(NULL, NULL);          /* NULL is not "all" */
        CHECK(procmem_size(perm), "permanent blocks never owned-freed");
        procmem_keep(&owner_b, 0, NULL);
        procmem_free_owned(&owner_b, NULL);
        CHECK(procmem_size(b1), "kept block is resident");
        procmem_free(b1);
        procmem_free(perm);
    }

    /* A deferred block carries its own decision, not a reusable owner PD. */
    {
        void *owner = procmem_alloc(PAGE, 0);
        void *held = procmem_alloc(PAGE, 0);
        void *replacement;
        UQUAD generation = procmem_gen(owner);

        procmem_set_owner(owner, owner);
        procmem_set_owner(held, owner);
        procmem_pin((VADDR)(uintptr_t)held, PAGE, 1);
        procmem_free_owned(owner, NULL);
        CHECK(!procmem_size(owner) && procmem_size(held), "pinned owned block defers release");
        replacement = procmem_alloc(PAGE, 0);
        CHECK(replacement == owner && procmem_gen(replacement) != generation,
              "former owner address is reused with a new generation");
        procmem_set_owner(replacement, replacement);
        procmem_pin((VADDR)(uintptr_t)held, PAGE, -1);
        CHECK(!procmem_size(held) && procmem_size(replacement),
              "last unpin reclaims only the original allocation");
        procmem_free_owned(replacement, NULL);
    }
    {
        void *held = procmem_alloc(PAGE, 0);
        procmem_set_owner(held, &owner_a);
        procmem_pin((VADDR)(uintptr_t)held, PAGE, 1);
        procmem_free_owned(&owner_a, NULL);
        procmem_set_owner(held, &owner_b);
        procmem_pin((VADDR)(uintptr_t)held, PAGE, -1);
        CHECK(procmem_size(held) && procmem_owner(held) == &owner_b,
              "ownership transfer cancels deferred reclamation");
        procmem_free_owned(&owner_b, NULL);
    }
    {
        void *block = procmem_alloc(PAGE, 0);
        procmem_set_owner(block, &owner_a);
        callback_replacement = NULL;
        callback_replacement_owner = &owner_b;
        procmem_keep(&owner_a, 0, replace_during_keep);
        CHECK(callback_replacement == block && procmem_owner(block) == &owner_b,
              "keep ignores a replacement allocation with a new generation");
        procmem_free_owned(&owner_b, NULL);
    }
    {
        void *held = procmem_alloc(PAGE, 0);
        void *other = procmem_alloc(PAGE, 0);
        procmem_set_owner(held, &owner_a);
        procmem_pin((VADDR)(uintptr_t)held, PAGE, 1);
        procmem_free_owned(&owner_a, NULL);
        procmem_set_owner(other, &owner_b);
        callback_unpin_block = held;
        pre_free_calls = 0;
        procmem_free_owned(&owner_b, unpin_during_cleanup);
        CHECK(pre_free_calls == 1 && !procmem_size(held) && !procmem_size(other),
              "cleanup traversal tolerates reclamation during callback");
    }
    {
        void *held = procmem_alloc(PAGE, 0);
        void *other = procmem_alloc(PAGE, 0);
        procmem_set_owner(held, &owner_a);
        procmem_pin((VADDR)(uintptr_t)held, PAGE, 1);
        procmem_free_owned(&owner_a, NULL);
        procmem_set_owner(other, &owner_b);
        if (!setjmp(cleanup_test_resume))
            procmem_free_owned(&owner_b, jump_during_cleanup);
        procmem_pin((VADDR)(uintptr_t)held, PAGE, -1);
        CHECK(!procmem_size(held), "non-local callback exit does not inhibit reclamation");
        procmem_free_owned(&owner_b, NULL);
    }

    /* Ptermres: the owner's block is cut back to the requested pages and
     * the tail returned; the rest of what it owns is kept, not freed */
    {
        UBYTE *blk = procmem_alloc(4 * PAGE, 0);
        void *other = procmem_alloc(PAGE, 0);
        PROCMEM_STATS before, after;

        procmem_set_owner(blk, blk);
        procmem_set_owner(other, blk);
        procmem_stats(&before);
        pre_free_calls = 0;
        procmem_keep(blk, PAGE + 1, count_pre_free);
        procmem_stats(&after);
        CHECK(procmem_size(blk) == 2 * PAGE, "Ptermres keeps whole pages of the prefix");
        CHECK(after.free_pages == before.free_pages + 2, "unused tail returned");
        CHECK(pre_free_calls == 1 && procmem_size(other), "other owned block kept");
        procmem_free_owned(blk, NULL);
        CHECK(procmem_size(blk) && procmem_size(other), "resident blocks survive owner cleanup");
        procmem_free(blk);
        procmem_free(other);
    }

    same(&s, "procmem");
}

/* ---- address spaces ------------------------------------------------ */

static void test_aspace(void)
{
    SNAP s, s2;
    ASPACE *as;
    UBYTE *mem;
    ULONG i, k, failed = 0;
    UQUAD flags;
    UQUAD scratch;
    UBYTE buf_dummy[16];

    snap(&s);
    mem = procmem_alloc(3 * PAGE, PROCMEM_ZERO);
    CHECK(mem != NULL, "window block for mapping");
    if (!mem)
        return;
    snap(&s2);      /* with the block live: what each cycle must return to */

    /* create / map / destroy, far more often than any fixed pool */
    for (i = 0; i < 500; i++) {
        as = aspace_create();
        if (!as)
            break;
        if (!aspace_map_procmem(as, (UQUAD)(uintptr_t)mem, 3 * PAGE,
                                       ASPACE_PROT_WRITE | ASPACE_PROT_USER)) {
            aspace_destroy(as);
            break;
        }
        if (i == 0)
            CHECK(aspace_table_pages(as) >= 4, "PML4 + 3 table levels owned");
        aspace_destroy(as);
    }
    CHECK(i == 500, "500 address-space cycles");
    same(&s2, "aspace cycles");

    /* a block mapped into a live address space cannot be freed or reused */
    as = must_create("address space for the pin test");
    if (as) {
        CHECK(aspace_map_procmem(as, (UQUAD)(uintptr_t)mem, 3 * PAGE,
                                        ASPACE_PROT_WRITE | ASPACE_PROT_USER), "map for pin");
        CHECK(procmem_pinned(mem), "mapped block is pinned");
        CHECK(!procmem_free(mem), "pinned block cannot be freed");
        procmem_set_owner(mem, &failed);
        procmem_free_owned(&failed, NULL);
        CHECK(procmem_size(mem), "owned-free skips a pinned block");
        procmem_set_owner(mem, NULL);
        aspace_destroy(as);
        CHECK(!procmem_pinned(mem), "destroy unpins");
    }

    /* failed setup at every allocation point unwinds completely */
    for (k = 1; k <= 8; k++) {
        x86_64_pmem_test_fail_after(k);
        as = aspace_create();
        if (as && !aspace_map_procmem(as, (UQUAD)(uintptr_t)mem, 3 * PAGE,
                                             ASPACE_PROT_WRITE | ASPACE_PROT_USER))
            failed++;
        else if (!as)
            failed++;
        x86_64_pmem_test_fail_after(0);
        aspace_destroy(as);              /* NULL-safe */
        same(&s2, "aspace failure unwind");
    }
    CHECK(failed >= 4, "injected failures were exercised");

    /* virtual (32-bit ABI) and physical limits are separate constraints */
    as = must_create("address space for the constraint tests");
    if (as) {
        scratch = x86_64_pmem_try_alloc_pages(1, 0);
        flags = ASPACE_PROT_WRITE | ASPACE_PROT_USER;
        CHECK(aspace_map_page(as, USER_VA_LIMIT - PAGE, scratch, flags),
              "last page below 4 GiB maps");
        CHECK(!aspace_map_page(as, USER_VA_LIMIT, scratch, flags),
              "va at 4 GiB refused");
        CHECK(!aspace_map_page(as, USER_VA_LIMIT + PAGE, scratch, flags),
              "va above 4 GiB refused");
        CHECK(!aspace_map_page(as, 0xFFFFFFFF80000000ULL, scratch, flags),
              "kernel va refused");
        CHECK(!aspace_map_page(as, USER_VA_LIMIT - 2 * PAGE + 16, scratch, flags),
              "unaligned va refused");
        /* physical backing above 4 GiB is fine for a low virtual address */
        CHECK(aspace_map_page(as, 0x40000000ULL, 0x123456000ULL, flags),
              "phys above 4 GiB at low va");
        CHECK(aspace_map_page(as, 0x40001000ULL, 0xFFFFF000000ULL, ASPACE_PROT_USER),
              "phys far above 4 GiB at low va");
        /* ring 3 may be given a device or bogus physical page, but the kernel
         * must refuse to copy through the direct map to it, not fault */
        CHECK(aspace_user_range_ok(as, 0x40000000ULL, 8, TRUE), "device-style page is valid for ring 3");
        CHECK(!aspace_copy_from_user(as, buf_dummy, 0x40000000ULL, 8) &&
              !aspace_copy_to_user(as, 0x40000000ULL, buf_dummy, 8) &&
              !aspace_copy_from_user(as, buf_dummy, 0x40000FFCULL, 8),
              "copy to or from a page that is not owned RAM is refused");
        /* only live process memory can be mapped */
        CHECK(!aspace_map_procmem(as, 0x500000, PAGE, flags), "outside window refused");
        x86_64_pmem_free_pages(scratch, 1);
        aspace_destroy(as);
    }

    /* tearing down the address space that is currently loaded is safe */
    as = must_create("address space to tear down while loaded");
    if (as) {
        UQUAD flagsave;

        __asm__ volatile ("pushfq; popq %0; cli" : "=r"(flagsave) :: "memory");
        x86_64_write_cr3(aspace_root(as));
        aspace_destroy(as);
        CHECK(x86_64_read_cr3() == x86_64_kernel_pml4_phys(), "CR3 back on kernel tables");
        if (flagsave & 0x200)
            x86_64_sti();
    }

    procmem_free(mem);
    same(&s, "aspace");
}

/* A basepage as Pexec(PE_BASEPAGEFLAGS) creates it; 0 on failure. */
static PD *new_basepage(void)
{
    long rc = Pexec(PE_BASEPAGEFLAGS, (char *)PF_STANDARD, "", NULL);

    CHECK(rc > 0, "basepage creation");     /* never a silent skip */
    return (rc > 0) ? (PD *)(uintptr_t)rc : NULL;
}

/* ---- isolated address spaces (#401) -------------------------------- */

#define ISO_VA 0x400000ULL      /* where the linked-in x32 images live */

/* Runs fn with interrupts off and returns the byte it read under `as`'s
 * page tables at user address va: a real MMU access, not a software walk. */
static UBYTE read_byte_under(ASPACE *as, UQUAD va)
{
    UQUAD flags;
    UBYTE value;

    __asm__ volatile ("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
    x86_64_write_cr3(aspace_root(as));
    value = *(volatile UBYTE *)(uintptr_t)va;
    x86_64_write_cr3(x86_64_kernel_pml4_phys());
    if (flags & 0x200)
        x86_64_sti();
    return value;
}

static void test_isolation(void)
{
    SNAP s;
    ASPACE *a, *b;
    UQUAD pa, pb, probe;
    UWORD prot;
    UBYTE buf[128], big[200];
    ULONG i, k, failed = 0;

    snap(&s);
    a = must_create("first isolation address space");
    b = must_create("second isolation address space");
    if (!a || !b) {
        aspace_destroy(a);
        aspace_destroy(b);
        return;
    }

    /* the same virtual addresses in both, with different backing */
    CHECK(aspace_map_private(a, ISO_VA, 3 * PAGE + 1, ASPACE_PROT_WRITE | ASPACE_PROT_EXEC | ASPACE_PROT_USER),
          "private mapping in A");
    CHECK(aspace_map_private(b, ISO_VA, 2 * PAGE, ASPACE_PROT_WRITE | ASPACE_PROT_USER),
          "overlapping private mapping in B");
    CHECK(aspace_private_pages(a) == 4 && aspace_private_pages(b) == 2,
          "backing pages owned by each");
    CHECK(aspace_translate(a, ISO_VA, &pa, &prot) &&
          aspace_translate(b, ISO_VA, &pb, &prot) && pa != pb,
          "same address, different physical pages");
    CHECK(!aspace_translate(b, ISO_VA + 3 * PAGE, NULL, NULL) &&
          aspace_translate(a, ISO_VA + 3 * PAGE, NULL, NULL),
          "a page mapped in A only is absent in B");

    /* private writes stay private -- checked in software ... */
    memset(buf, 'A', 8);
    CHECK(aspace_copy_to_user(a, ISO_VA, buf, 8), "write A");
    memset(buf, 'B', 8);
    CHECK(aspace_copy_to_user(b, ISO_VA, buf, 8), "write B");
    aspace_copy_from_user(a, buf, ISO_VA, 8);
    CHECK(buf[0] == 'A' && buf[7] == 'A', "A keeps its own data");
    aspace_copy_from_user(b, buf, ISO_VA, 8);
    CHECK(buf[0] == 'B' && buf[7] == 'B', "B keeps its own data");

    /* ... and by the MMU itself: load each page table root and read */
    CHECK(read_byte_under(a, ISO_VA) == 'A', "MMU: A sees A");
    CHECK(read_byte_under(b, ISO_VA) == 'B', "MMU: B sees B");
    CHECK(read_byte_under(a, ISO_VA + PAGE) == 0, "MMU: fresh pages are zero");

    /* a copy that crosses a page boundary lands in both pages */
    for (i = 0; i < sizeof(big); i++)
        big[i] = (UBYTE)(i + 1);
    CHECK(aspace_copy_to_user(a, ISO_VA + PAGE - 100, big, sizeof(big)), "cross-page write");
    memset(big, 0, sizeof(big));
    CHECK(aspace_copy_from_user(a, big, ISO_VA + PAGE - 100, sizeof(big)) &&
          big[0] == 1 && big[99] == 100 && big[100] == 101 && big[199] == 200,
          "cross-page read back");

    /* validation is against this process's mappings, not a number range */
    CHECK(aspace_user_range_ok(a, ISO_VA, 4 * PAGE, TRUE), "A's whole mapping valid");
    CHECK(!aspace_user_range_ok(a, ISO_VA + 4 * PAGE - 1, 2, FALSE), "range running off the end invalid");
    CHECK(!aspace_user_range_ok(b, ISO_VA + 2 * PAGE, 8, FALSE), "address valid in A, not in B");
    CHECK(!aspace_user_range_ok(a, 0x10000000, 8, FALSE), "unmapped low address invalid");
    CHECK(!aspace_user_range_ok(a, 0xFFFFFFFFULL, 2, FALSE), "range crossing 4 GiB invalid");
    CHECK(!aspace_user_range_ok(a, 0xFFFFFFFFFFFFFFF0ULL, 8, FALSE), "wrapping range invalid");
    CHECK(!aspace_user_range_ok(a, ISO_VA, 0, FALSE), "empty range invalid");
    CHECK(!aspace_copy_to_user(b, ISO_VA + 2 * PAGE, buf, 8), "copy to an unmapped address refused");

    /* read-only and supervisor-only mappings */
    CHECK(aspace_map_private(a, 0x20000000, PAGE, ASPACE_PROT_USER), "read-only mapping");
    CHECK(aspace_user_range_ok(a, 0x20000000, 8, FALSE) &&
          !aspace_user_range_ok(a, 0x20000000, 8, TRUE) &&
          !aspace_copy_to_user(a, 0x20000000, buf, 8), "read-only page refuses writes");
    CHECK(aspace_map_private(a, 0x20100000, PAGE, ASPACE_PROT_WRITE), "supervisor-only mapping");
    CHECK(!aspace_user_range_ok(a, 0x20100000, 8, FALSE), "supervisor-only page invalid for ring 3");

    /* refusals leave nothing behind */
    k = aspace_private_pages(a);
    CHECK(!aspace_map_private(a, ISO_VA + PAGE, PAGE, ASPACE_PROT_USER), "overlap refused");
    CHECK(!aspace_map_private(a, ISO_VA - PAGE, 2 * PAGE, ASPACE_PROT_USER), "partial overlap refused");
    CHECK(!aspace_map_private(a, USER_VA_LIMIT, PAGE, ASPACE_PROT_USER), "4 GiB refused");
    CHECK(!aspace_map_private(a, USER_VA_LIMIT - PAGE, 2 * PAGE, ASPACE_PROT_USER), "crossing 4 GiB refused");
    CHECK(!aspace_map_private(a, ISO_VA + 1, PAGE, ASPACE_PROT_USER), "unaligned refused");
    CHECK(!aspace_map_private(a, 0xFFFFFFFF80000000ULL, PAGE, ASPACE_PROT_USER), "kernel address refused");
    CHECK(aspace_private_pages(a) == k, "no backing taken by refused mappings");

    /* kernel mappings are present for the CPU's sake but supervisor-only */
    probe = x86_64_pmem_try_alloc_pages(1, 0);
    CHECK(aspace_translate(a, (UQUAD)(uintptr_t)&x86_64_memtest_run, NULL, &prot) &&
          !(prot & ASPACE_PROT_USER), "kernel code is supervisor-only");
    CHECK(probe != X86_64_PMEM_NONE &&
          aspace_translate(a, X86_64_PHYS_MAP_BASE + probe, NULL, &prot) &&
          !(prot & ASPACE_PROT_USER), "physical direct map is supervisor-only");
    CHECK(!aspace_user_range_ok(a, (UQUAD)(uintptr_t)&x86_64_memtest_run, 8, FALSE),
          "kernel address invalid as a user pointer");
    if (probe != X86_64_PMEM_NONE)
        x86_64_pmem_free_pages(probe, 1);
    /* The kernel's low data (the system-vector area, the framebuffer)
     * is there for ring 0's sake while it runs a system call under this
     * address space -- and supervisor-only.  The process window, by contrast,
     * is absent: only the process's own blocks (user) and the blocks of a
     * child it is launching (borrowed, supervisor-only) are ever mapped. */
    {
        UQUAD kstart, kend;

        x86_64_low_kernel_range(&kstart, &kend);
        CHECK(aspace_translate(a, 0x400, NULL, &prot) && !(prot & ASPACE_PROT_USER),
              "the system variables are mapped, supervisor-only");
        if (kend > kstart)
            CHECK(aspace_translate(a, kstart, NULL, &prot) && !(prot & ASPACE_PROT_USER) &&
                  !aspace_user_range_ok(a, kstart, 8, FALSE),
                  "the framebuffer is mapped, supervisor-only");
    }
    CHECK(!aspace_translate(a, X86_64_LOW_TPA_VIRT_BASE, NULL, NULL),
          "the process window is not in a fresh address space");
    /* a block lent to the address space is reachable for ring 0 only, and goes
     * back exactly once */
    {
        void *blk = procmem_alloc(PAGE, PROCMEM_ZERO);

        CHECK(blk != NULL, "a block to lend");
        if (blk) {
            UQUAD v = (UQUAD)(uintptr_t)blk;

            CHECK(aspace_borrow(a, v, PAGE) && procmem_pinned(blk) &&
                  aspace_translate(a, v, NULL, &prot) && !(prot & ASPACE_PROT_USER) &&
                  (prot & ASPACE_PROT_WRITE) && !aspace_user_range_ok(a, v, 8, FALSE),
                  "a borrowed block is mapped supervisor-only and pinned");
            aspace_unborrow(a, v, PAGE);
            CHECK(!procmem_pinned(blk) && !aspace_translate(a, v, NULL, NULL),
                  "an unborrowed block is unmapped and unpinned");
            CHECK(procmem_free(blk), "and can be freed");
        }
    }

    aspace_destroy(a);
    aspace_destroy(b);
    same(&s, "isolation");

    /* the documented minimum layout for an EmuCON-sized process fits, in two
     * address spaces at once */
    a = must_create("first layout address space");
    b = must_create("second layout address space");
    if (a && b) {
        UWORD rw = ASPACE_PROT_WRITE | ASPACE_PROT_USER;

        CHECK(aspace_map_private(a, USER_IMAGE_BASE, 64 * PAGE, rw | ASPACE_PROT_EXEC) &&
              aspace_map_private(a, USER_STACK_TOP - USER_STACK_SIZE,
                                        USER_STACK_SIZE, rw), "layout in A");
        CHECK(aspace_map_private(b, USER_IMAGE_BASE, 64 * PAGE, rw | ASPACE_PROT_EXEC) &&
              aspace_map_private(b, USER_STACK_TOP - USER_STACK_SIZE,
                                        USER_STACK_SIZE, rw), "same layout in B");
        CHECK(aspace_user_range_ok(a, USER_STACK_TOP - 16, 16, TRUE),
              "top of the stack is valid");
        CHECK(!aspace_user_range_ok(a, USER_STACK_TOP, 1, FALSE),
              "above the stack is not");
        CHECK(!aspace_user_range_ok(a, USER_STACK_TOP - USER_STACK_SIZE - PAGE, 1, FALSE),
              "the guard below the stack is not");
    }
    aspace_destroy(a);
    aspace_destroy(b);
    same(&s, "layout");

    /* lazy pages: a page is backed on request, zeroed, once; the pages that
     * were never touched are skipped when the area is given back */
    a = must_create("lazy address space");
    if (a) {
        UWORD rw = ASPACE_PROT_WRITE | ASPACE_PROT_USER, prot;
        UQUAD va = 0x20000000ULL, phys;
        ULONG before = aspace_private_pages(a);

        CHECK(!aspace_translate(a, va, NULL, NULL), "a lazy page is not there yet");
        CHECK(aspace_fault_in(a, va + 0x123, rw) &&
              aspace_translate(a, va, &phys, &prot) && (prot & ASPACE_PROT_USER) &&
              (prot & ASPACE_PROT_WRITE) && !(prot & ASPACE_PROT_EXEC) &&
              aspace_private_pages(a) == before + 1,
              "fault_in backs the page containing the address");
        CHECK(*(volatile UQUAD *)(uintptr_t)(X86_64_PHYS_MAP_BASE + phys) == 0 &&
              *(volatile UQUAD *)(uintptr_t)(X86_64_PHYS_MAP_BASE + phys + PAGE - 8) == 0,
              "and it is zeroed");
        CHECK(aspace_fault_in(a, va, rw) && aspace_private_pages(a) == before + 1,
              "a page that is there is left as it is");
        CHECK(aspace_fault_in(a, va + 100 * PAGE, rw) &&
              aspace_private_pages(a) == before + 2,
              "a second page, far away");
        CHECK(!aspace_protect_private(a, va, 101 * PAGE, ASPACE_PROT_USER) &&
              aspace_protect_lazy(a, va, 101 * PAGE, ASPACE_PROT_USER) &&
              aspace_private_pages(a) == before + 2 &&
              aspace_translate(a, va + 100 * PAGE, NULL, &prot) &&
              !(prot & ASPACE_PROT_WRITE) && !aspace_translate(a, va + PAGE, NULL, NULL),
              "lazy protection skips gaps without allocating; eager protection still refuses them");
        CHECK(!aspace_fault_in(a, USER_VA_LIMIT, rw), "not above the 32-bit limit");
        CHECK(aspace_release_lazy(a, va, 200 * PAGE) &&
              aspace_private_pages(a) == before &&
              !aspace_translate(a, va, NULL, NULL) &&
              !aspace_translate(a, va + 100 * PAGE, NULL, NULL),
              "release_lazy skips the pages never touched and frees the others");
        CHECK(aspace_release_lazy(a, va, 200 * PAGE), "and an empty range is nothing");
        /* eager mapping keeps the kernel's reserve too: a range that would take
         * all but half of it is refused, and nothing is left behind */
        {
            UQUAD free_bytes = pmem_free_bytes();
            UQUAD big = (free_bytes - ASPACE_FAULT_RESERVE / 2) & ~(PAGE - 1);

            CHECK(big < USER_VA_LIMIT - 0x30000000ULL &&
                  !aspace_map_private(a, 0x30000000ULL, big, rw) &&
                  pmem_free_bytes() == free_bytes &&
                  aspace_private_pages(a) == before,
                  "map_private leaves the reserve alone");
            CHECK(aspace_map_private(a, 0x30000000ULL, 16 * PAGE, rw) &&
                  aspace_release_lazy(a, 0x30000000ULL, 16 * PAGE),
                  "and a small range is fine");
        }
    }
    aspace_destroy(a);
    same(&s, "lazy pages");

    /* A foreign page late in a sparse range must prevent every permission
     * change, including changes to an earlier private page. */
    snap(&s);
    a = must_create("lazy protection ownership");
    probe = x86_64_pmem_try_alloc_pages(1, 0);
    if (a && probe != X86_64_PMEM_NONE) {
        UQUAD va = 0x20000000ULL;
        UWORD prot;

        CHECK(aspace_fault_in(a, va, ASPACE_PROT_USER) &&
              aspace_map_page(a, va + 2 * PAGE, probe, ASPACE_PROT_USER),
              "private and borrowed pages around an untouched gap");
        CHECK(!aspace_protect_lazy(a, va, 3 * PAGE, ASPACE_PROT_USER | ASPACE_PROT_WRITE) &&
              aspace_translate(a, va, NULL, &prot) && !(prot & ASPACE_PROT_WRITE),
              "lazy protection refuses borrowed backing before changing any page");
    } else {
        CHECK(FALSE, "lazy protection ownership allocation");
    }
    aspace_destroy(a);
    if (probe != X86_64_PMEM_NONE)
        x86_64_pmem_free_pages(probe, 1);
    same(&s, "lazy protection ownership");

    /* many create/map/destroy cycles, and failure at every allocation point */
    for (i = 0; i < 300; i++) {
        a = aspace_create();
        if (!a)
            break;
        if (!aspace_map_private(a, ISO_VA, 5 * PAGE, ASPACE_PROT_WRITE | ASPACE_PROT_USER)) {
            aspace_destroy(a);
            break;
        }
        aspace_destroy(a);
    }
    CHECK(i == 300, "300 private-mapping cycles");
    same(&s, "private cycles");
    for (k = 1; k <= 14; k++) {
        x86_64_pmem_test_fail_after(k);
        a = aspace_create();
        if (!a || !aspace_map_private(a, ISO_VA, 5 * PAGE, ASPACE_PROT_WRITE | ASPACE_PROT_USER))
            failed++;
        x86_64_pmem_test_fail_after(0);
        if (a)
            CHECK(failed == 0 || aspace_private_pages(a) == 0 ||
                  aspace_private_pages(a) == 5, "failed mapping leaves no partial pages");
        aspace_destroy(a);
        same(&s, "private failure unwind");
    }
    CHECK(failed >= 6, "private-mapping failures were exercised");

    /* a failed mapping that needed new PDPT/PD/PT pages gives those back too,
     * not just the leaves: the address space looks exactly as it did before */
    failed = 0;
    for (k = 1; k <= 12; k++) {
        ULONG tables0, private0;
        BOOL ok;

        a = aspace_create();
        if (!a || !aspace_map_private(a, ISO_VA, PAGE, ASPACE_PROT_USER)) {
            CHECK(FALSE, "setup for the table rollback test");
            aspace_destroy(a);
            break;
        }
        tables0 = aspace_table_pages(a);
        private0 = aspace_private_pages(a);
        x86_64_pmem_test_fail_after(k);
        /* a different 1 GiB region: needs a new PDPT entry, PD and PT */
        ok = aspace_map_private(a, 0x80000000ULL, 3 * PAGE, ASPACE_PROT_USER);
        x86_64_pmem_test_fail_after(0);
        if (!ok) {
            failed++;
            CHECK(aspace_table_pages(a) == tables0, "failed mapping returns its table pages");
            CHECK(aspace_private_pages(a) == private0, "failed mapping returns its backing");
            CHECK(!aspace_translate(a, 0x80000000ULL, NULL, NULL), "failed mapping left no leaf");
            /* and the region is usable afterwards */
            CHECK(aspace_map_private(a, 0x80000000ULL, 3 * PAGE, ASPACE_PROT_USER),
                  "mapping succeeds after a rolled-back failure");
        }
        aspace_destroy(a);
        same(&s, "table rollback");
    }
    CHECK(failed >= 4, "table-rollback failures were exercised");
}

/* The user-pointer checks the system calls use are made against the
 * current process's own page tables. */
static void test_process_validation(void)
{
    SNAP s;
    PD *pd, *saved = run;
    void *env;
    UBYTE b[8];

    snap(&s);
    pd = new_basepage();
    if (!pd)
        return;
    env = USERPTR_TO_PTR(pd->p_env);
    set_owner(pd, pd);
    set_owner(env, pd);
    CHECK(kproc_prepare_user(pd, run) && kproc_user_root(pd), "address space for validation");

    run = pd;
    CHECK(kproc_validate_user_range((UQUAD)(uintptr_t)env, 8), "own environment readable");
    CHECK(kproc_validate_user_write((UQUAD)(uintptr_t)pd + sizeof(PD), 64), "own TPA writable");
    CHECK(kproc_copy_from_user(b, (UQUAD)(uintptr_t)env, 2) && b[0] == ((UBYTE *)env)[0],
          "copy from own memory");
    CHECK(!kproc_validate_user_range((UQUAD)(uintptr_t)saved, 8),
          "the parent's basepage (supervisor-only) is not valid user memory");
    CHECK(!kproc_validate_user_range(X86_64_LOW_TPA_VIRT_BASE, 8) ||
          (UQUAD)(uintptr_t)env == X86_64_LOW_TPA_VIRT_BASE ||
          (UQUAD)(uintptr_t)pd == X86_64_LOW_TPA_VIRT_BASE,
          "window memory that is not the process's own is invalid");
    CHECK(!kproc_validate_user_range(0xFFFFFFFF80000000ULL, 8), "kernel address invalid");

    /* A read-only destination passes the read check but not the write check
     * the system calls make for every buffer a device operation fills (the
     * Rwabs/Floprd/Flopver/Flopfmt buffers, trap.c): it must be refused up
     * front, before the device is touched. */
    {
        ASPACE *as = kproc_user_aspace(pd);

        CHECK(as != NULL, "the process has an address space");
        if (as) {
            CHECK(aspace_map_private(as, 0x20000000, PAGE, ASPACE_PROT_USER), "read-only user page");
            CHECK(aspace_map_private(as, 0x20100000, PAGE, ASPACE_PROT_USER | ASPACE_PROT_WRITE),
                  "writable user page");
            CHECK(kproc_validate_user_range(0x20000000, 512), "read-only buffer is readable");
            CHECK(!kproc_validate_user_write(0x20000000, 512), "read-only buffer is not a valid output buffer");
            CHECK(!kproc_copy_to_user(0x20000000, b, 8), "copy into a read-only buffer refused");
            CHECK(kproc_validate_user_write(0x20100000, 512), "writable buffer is a valid output buffer");
            CHECK(!kproc_validate_user_dta(0x20000000), "a read-only DTA is refused");
        }
    }
    CHECK(!kproc_copy_to_user(0x10000000, b, 8), "copy to an unmapped address refused");
    run = saved;

    proc_free_owned(pd);
    same(&s, "process validation");
}

/* Exercise the production pending/active ownership transition and unwind. */
#include "../../../bdos/kaspace.h"

static PD *disposal_test_pd;
static KPROC *disposal_test_observer;
static unsigned disposal_test_commits;
#if CONF_WITH_NATIVE_SCHED
struct test_thread_execution {
    KTHREAD_STACK stack;
    PADDR syscall_stack;
    unsigned reclaims;
};

static void test_thread_reclaim(void *resource)
{
    struct test_thread_execution *execution = resource;
    execution->reclaims++;
    kthread_stack_release(&execution->stack);
    if (execution->syscall_stack) {
        kstack_free(execution->syscall_stack);
        execution->syscall_stack = 0;
    }
}
#endif

#if CONF_WITH_NATIVE_SCHED
static struct {
    BOOL enabled, seen;
    KPROC *owner;
    LONG handle;
    WORD pid;
} deferred_reentry;
static struct {
    BOOL enabled, seen, yielding, expect_yield;
    WORD pid;
    PROCESS_OBSERVER *observer;
} retiring_probe;
#endif
void x86_64_memtest_disposal_committed(PD *pd)
{
#if CONF_WITH_NATIVE_SCHED
    static char command[]={2,'O','n',0}, environment[]={0,0};
    PROCESS_OBSERVER *late=NULL;
    PROCESS_COMPLETION completion;
    LONG third;
    if (retiring_probe.enabled && kproc_pid(pd)==retiring_probe.pid) {
        ULONG root_passes=ksched_test_retiring_root_passes();
        retiring_probe.enabled=FALSE;
        retiring_probe.seen=TRUE;
        CHECK(ksched_wait(retiring_probe.pid,1,0)==EFILNF,
              "cleanup callback cannot observe an uncommitted or retiring public child");
        if (retiring_probe.expect_yield) {
            retiring_probe.yielding=TRUE;
            CHECK(ksched_yield()==E_OK && ksched_test_retiring_root_passes()!=root_passes,
                  "actual cleanup yields into root which visits but cannot reap the private retiring record");
            retiring_probe.yielding=FALSE;
        }
        CHECK(process_observer_snapshot(retiring_probe.observer,&completion) &&
              completion.state==PROCESS_UNSTARTED && !process_observer_ready(retiring_probe.observer),
              "canonical pending observation survives root entry before THREAD destruction");
    }
    if (deferred_reentry.enabled && kproc_pid(pd)==deferred_reentry.pid) {
        deferred_reentry.enabled=FALSE;
        deferred_reentry.seen=TRUE;
        CHECK(proc_exec_handle_commit(deferred_reentry.owner,deferred_reentry.handle)==EACCDN &&
              proc_exec_handle_close(deferred_reentry.owner,deferred_reentry.handle)==EACCDN &&
              proc_exec_retain_observer(deferred_reentry.owner,deferred_reentry.handle,TRUE,&late)==EACCDN &&
              !late && ksched_defer_cancel()==EACCDN,
              "replacement callbacks cannot close, recommit, bind late leases or cancel the in-flight transaction");
        third=proc_exec_handle_prepare(deferred_reentry.owner,"C:\\X32HELLO.TOS",command,environment,NULL,
                                       PROCESS_EXEC_DEFER_REPLACEMENT);
        CHECK(third>0 && proc_exec_handle_commit(deferred_reentry.owner,third)==EACCDN &&
              proc_exec_handle_snapshot(deferred_reentry.owner,third,&completion)==E_OK &&
              completion.state==PROCESS_UNSTARTED,
              "reentrant replacement can prepare private rights but cannot publish a competing successor");
        if (third>0) CHECK(proc_exec_handle_close(deferred_reentry.owner,third)==E_OK,
                           "rejected competing commit retains safely closable preparation");
    }
#endif
    if (pd != disposal_test_pd)
        return;
    disposal_test_commits++;
    CHECK(!kthread_create(pd), "disposing process rejects new threads");
    proc_dispose_process(disposal_test_observer);
}

/* Detached cancellation keeps filesystem and mapping resources alive until
 * the last non-runnable association is detached. */
static void test_kthread_live_disposal(void)
{
    SNAP s;
    PD *pd, *replacement, *saved = run;
    void *env;
    KTHREAD *a, *b;
    KPROC *observer;
    LONG handle, count;
    WORD pid;
    UWORD status;
    char bytes[4];
#if CONF_WITH_NATIVE_SCHED
    struct test_thread_execution execution;
    BOOL attached;
    uintptr_t syscall_top;
#endif

    snap(&s);
    pd = new_basepage();
    if (!pd) return;
    env = USERPTR_TO_PTR(pd->p_env);
    set_owner(pd, pd);
    set_owner(env, pd);
    CHECK(kproc_prepare_user(pd, run), "prepare process for deferred disposal");
    observer = kproc_retain(pd);
    disposal_test_pd = pd;
    disposal_test_observer = observer;
    disposal_test_commits = 0;
    pid = kproc_pid(pd);
    a = kthread_create(pd);
    b = kthread_create(pd);
    CHECK(a && b && observer, "retain live process for deferred disposal");
    if (!a || !b || !observer) {
        kthread_destroy(a);
        kthread_destroy(b);
        proc_dispose_retained(pd);
        disposal_test_pd = NULL;
        disposal_test_observer = NULL;
        kproc_release(observer);
        same(&s, "failed deferred-disposal setup");
        return;
    }
#if CONF_WITH_NATIVE_SCHED
    bzero(&execution, sizeof(execution));
    attached = kthread_stack_allocate(&execution.stack);
    CHECK(attached, "allocate execution stack for safe-context handoff");
    if (attached) {
        execution.syscall_stack = kstack_alloc(&syscall_top);
        CHECK(execution.syscall_stack && syscall_top,
              "allocate separate syscall stack for safe-context handoff");
        if (!execution.syscall_stack) {
            kthread_stack_release(&execution.stack);
            attached = FALSE;
        }
    }
    if (attached) {
        attached = kthread_set_execution(a, &execution, test_thread_reclaim);
        CHECK(attached, "transfer execution stack ownership to thread");
        if (!attached) {
            kthread_stack_release(&execution.stack);
            kstack_free(execution.syscall_stack);
        }
    }
#endif
    CHECK(!kthread_reap(a), "thread cannot be reclaimed before stop");
    run = pd;
    handle = DISK_CALL(xopen("C:\\X32HELLO.TOS", 0));
    run = saved;
    CHECK(handle >= NUMSTD && handle < NUMHANDLES, "open process-owned file");
    proc_dispose_retained(pd);
    proc_dispose_retained(pd); /* repeated cancellation must not double-clean */
    CHECK(kproc_ref_has_basepage(observer) && procmem_size(pd) &&
          !kthread_create(pd), "disposal retains PD and rejects new threads");
    CHECK(procmem_mfree(pd) == EACCDN,
          "retained process basepage cannot be freed and reused");
    if (handle >= NUMSTD && handle < NUMHANDLES)
        CHECK(sft[handle-NUMSTD].f_own == pd && sft[handle-NUMSTD].f_use,
              "process-owned file remains open while threads remain");
    CHECK(!kthread_start(a), "disposal-pending process cannot start a prepared execution");
    CHECK(kthread_stop(a) && !kthread_stop(a), "thread stop is once-only");
#if CONF_WITH_NATIVE_SCHED
    if (attached)
        CHECK(execution.stack.physical && execution.syscall_stack && !execution.reclaims,
              "stopping thread retains scheduler and syscall stacks");
    CHECK(!kthread_set_execution(a, &execution, test_thread_reclaim),
          "stopped thread rejects new execution resources");
#endif
    CHECK(kthread_reap(a), "reclaim stopped thread from safe context");
#if CONF_WITH_NATIVE_SCHED
    if (attached)
        CHECK(!execution.stack.physical && !execution.syscall_stack && execution.reclaims == 1,
              "safe-context reap frees both stacks once");
#endif
    if (handle >= NUMSTD && handle < NUMHANDLES) {
        CHECK(kproc_ref_has_basepage(observer) && sft[handle-NUMSTD].f_own == pd,
              "first thread release preserves process resources");
        run = pd;
        count = DISK_CALL(xread((WORD)handle, sizeof(bytes), bytes));
        run = saved;
        CHECK(count == sizeof(bytes), "retained process can still read its file");
    }
    CHECK(kthread_stop(b), "stop last associated thread");
    CHECK(kproc_ref_has_basepage(observer), "stopped thread retains live process");
    CHECK(kthread_reap(b), "last thread reap completes deferred disposal");
    CHECK(disposal_test_commits == 1, "reentrant disposal commits once");
    disposal_test_pd = NULL;
    disposal_test_observer = NULL;
    if (handle >= NUMSTD && handle < NUMHANDLES)
        CHECK(!sft[handle-NUMSTD].f_own, "last thread closes process-owned file");
    CHECK(observer && !kproc_ref_has_basepage(observer),
          "identity survives final live-resource teardown");
    CHECK(kproc_ref_pid(observer) == pid && !kproc_ref_status(observer, &status),
          "abandoned process retains PID without an invented exit status");
    proc_dispose_process(observer);
    replacement = new_basepage();
    if (replacement) {
        void *replacement_env = USERPTR_TO_PTR(replacement->p_env);
        set_owner(replacement, replacement);
        set_owner(replacement_env, replacement);
        CHECK(replacement == pd, "retired basepage address can be reused");
        proc_dispose_process(observer);
        CHECK(kproc_pid(replacement) && kproc_pid(replacement) != pid &&
              procmem_size(replacement), "old identity cannot dispose replacement process");
        proc_free_owned(replacement);
    }
    kproc_release(observer);
    same(&s, "thread-retained detached process resources");
}

struct completion_reclaim {
    KPROC *observer;
    unsigned calls;
};

static void test_completion_reclaim(void *resource)
{
    struct completion_reclaim *execution = resource;
    UWORD status;
    execution->calls++;
    CHECK(kproc_ref_has_basepage(execution->observer) &&
          !kproc_ref_status(execution->observer, &status),
          "completion remains unpublished while execution resources are reclaimed");
    proc_dispose_process(execution->observer); /* reentrant request is deferred */
}

/* Model the final execution handoff without starting a user thread. */
static void test_kthread_completion(void)
{
    SNAP s;
    PD *pd;
    void *env;
    KPROC *observer;
    KTHREAD *a, *b;
    UWORD status;
    struct completion_reclaim execution;

    snap(&s);
    pd = new_basepage();
    if (!pd) return;
    env = USERPTR_TO_PTR(pd->p_env);
    set_owner(pd, pd);
    set_owner(env, pd);
    CHECK(kproc_prepare_user(pd, run), "prepare final-completion process");
    kproc_set_parent(pd, run);
    observer = kproc_retain(pd);
    a = kthread_create(pd);
    b = kthread_create(pd);
    CHECK(observer && a && b, "construct completion associations");
    if (!observer || !a || !b) {
        kthread_destroy(a);
        kthread_destroy(b);
        proc_dispose_retained(pd);
        kproc_release(observer);
        same(&s, "failed completion setup");
        return;
    }
    CHECK(kthread_stop(a), "stop first thread before completion preflight");
    CHECK(!kthread_complete(a, 37),
          "unstarted process cannot publish an exit status");
#if CONF_WITH_PTERMGRP
    CHECK(!kproc_request_group_exit(observer, 37), "unstarted group rejects exit request");
#endif
    kproc_mark_started(pd); /* simulate a process that has executed */
#if CONF_WITH_PTERMGRP
    {
        PROC_CLONE_ARGS args;
        PROC_CLONE_TRANSACTION *tx = NULL, *saved;
        PROC_CLONE_RESULT output = { 0 };
        UWORD group_status;
        memset(&args, 0, sizeof(args));
        args.source = observer;
        args.process_policy = PROC_CLONE_SAME_PROCESS;
        args.vm_policy = PROC_CLONE_SHARE_VM;
        CHECK(proc_clone(&args, &tx) == E_OK && tx, "prepare before group exit latch");
        saved = tx;
        CHECK(kproc_request_group_exit(observer, 37) &&
              kproc_request_group_exit(observer, 99) &&
              kproc_group_status(observer, &group_status) && group_status == 37,
              "first group request status remains latched");
        CHECK(!kthread_create(pd) &&
              !kthread_construct_identity(observer, kproc_active_space(observer)),
              "group exit rejects ordinary and identity associations");
        CHECK(proc_clone_commit(&tx, &output) == EACCDN && tx == saved &&
              !output.thread && !output.owned_process && !output.pid && !output.tid,
              "pending clone commit rejected without consuming transaction or outputs");
        proc_clone_abort(&tx);
        CHECK(proc_clone(&args, &tx) == EACCDN && !tx,
              "group exit rejects fresh clone before allocation");
    }
#endif
    CHECK(!kthread_complete(a, 37),
          "live sibling prevents process completion");
    CHECK(kthread_reap(a), "reap first stopped thread");
    CHECK(!kproc_ref_status(observer, &status) && kproc_ref_has_basepage(observer),
          "first thread exit preserves live process and unpublished status");
    execution.observer = observer;
    execution.calls = 0;
    CHECK(kthread_set_execution(b, &execution, test_completion_reclaim),
          "transfer final execution resource");
    CHECK(kthread_stop(b), "stop final thread before completion request");
    CHECK(!kproc_ref_status(observer, &status) && kproc_ref_has_basepage(observer),
          "stopped thread retains resources until safe reap");
    CHECK(kthread_complete(b, 37), "complete and reap final stopped thread");
    CHECK(execution.calls == 1, "final execution resource reclaimed once");
    CHECK(kproc_ref_status(observer, &status) && status == 37 &&
          !kproc_ref_has_basepage(observer), "final reap publishes one process status");
    proc_dispose_process(observer);
    CHECK(kproc_ref_status(observer, &status) && status == 37,
          "retired identity ignores repeated final cleanup");
    kproc_release(observer);
    same(&s, "final thread completion and process resources");
}

/* Non-runnable identities exercise actual KPROC membership and active-space
 * references. They must be detached before the legacy process cleanup path. */
static void test_kthread_relationships(void)
{
    SNAP s;
    PD *pd;
    void *env;
    KTHREAD *a, *b;
    KTHREAD *other_thread;
    KPROC *observer, *other_observer;
    PD *other;
    KASPACE *shared;
    ULONG first_tid, saved_tid;
    UWORD status;

    snap(&s);
    pd = new_basepage();
    if (!pd) return;
    env = USERPTR_TO_PTR(pd->p_env);
    set_owner(pd, pd);
    set_owner(env, pd);
    CHECK(!kthread_create(pd), "thread association rejects unprepared process");
    CHECK(kproc_prepare_user(pd, run), "prepare thread relationship process");
    observer = kproc_retain(pd);
    kthread_test_fail_after_attach();
    CHECK(!kthread_create(pd), "failed thread association rolls back its references");
    a = kthread_create(pd);
    kthread_test_fail_after_attach();
    CHECK(!kthread_create(pd), "failed association between thread identities");
    b = kthread_create(pd);
    CHECK(observer && a && b, "construct two non-runnable threads");
    if (a && b) {
        CHECK(!kthread_start(a), "THREAD cannot start without an owned execution backend");
        first_tid = kthread_tid(a);
        CHECK(first_tid && kthread_tid(b) && first_tid != kthread_tid(b),
              "thread identities are distinct");
        CHECK(kthread_tid(b) == first_tid + 1,
              "failed association does not consume a thread identity");
        CHECK(kthread_process(a) == observer && kthread_process(b) == observer &&
              kthread_aspace(a) == kthread_aspace(b) &&
              kaspace_backend(kthread_aspace(b)) == kproc_user_aspace(pd),
              "two threads retain the process and its active space");
        kthread_destroy(a);
        a = NULL;
        CHECK(kthread_process(b) == observer &&
              kaspace_backend(kthread_aspace(b)) == kproc_user_aspace(pd),
              "remaining thread retains its process and space");
        other = kalloc(sizeof(*other));
        CHECK(other != NULL, "allocate second process identity");
        if (other) {
            bzero(other, sizeof(*other));
            if (kproc_create(other)) {
                CHECK(kproc_pid(other) != kproc_pid(pd), "process identities differ");
                CHECK(kproc_test_share_active(other, kthread_aspace(b)),
                      "associate second non-runnable process with shared space");
                other_thread = kthread_create(other);
                other_observer = kproc_retain(other);
                CHECK(other_thread && kthread_process(other_thread) != observer &&
                      kthread_aspace(other_thread) == kthread_aspace(b),
                      "distinct process identities retain one address space");
                kproc_complete(other, 23);
                CHECK(!kthread_create(other), "completed process refuses new thread");
                kthread_destroy(other_thread);
                kproc_destroy(other);
                CHECK(other_observer && kproc_ref_status(other_observer, &status) &&
                      status == 23 && !kproc_ref_has_basepage(other_observer),
                      "completed identity survives shared-space process cleanup");
                kproc_release(other_observer);
                CHECK(kthread_process(b) == observer &&
                      kaspace_backend(kthread_aspace(b)) == kproc_user_aspace(pd),
                      "second process completion leaves first space usable");
            } else {
                CHECK(FALSE, "construct second process identity");
            }
            kfree(other);
        }
    }
    kthread_destroy(a);
    kthread_destroy(b);
    saved_tid = kthread_test_set_last_tid(~(ULONG)0 - 1);
    a = kthread_create(pd);
    CHECK(a && kthread_tid(a) == ~(ULONG)0,
          "last available thread identity is nonzero");
    CHECK(!kthread_create(pd), "exhausted thread identity space fails closed");
    kthread_destroy(a);
    kthread_test_set_last_tid(saved_tid);
    a = kthread_create(pd);
    b = kthread_create(pd);
    CHECK(a && b, "construct threads for reverse release order");
    kthread_destroy(b);
    if (a)
        CHECK(kthread_process(a) == observer &&
              kaspace_backend(kthread_aspace(a)) == kproc_user_aspace(pd),
              "first thread survives releasing second thread");
    kthread_destroy(a);
    other = kalloc(sizeof(*other));
    other_thread = NULL;
    if (other) {
        bzero(other, sizeof(*other));
        if (kproc_create(other)) {
            shared = kproc_test_retain_active(pd);
            CHECK(shared && kproc_test_share_active(other, shared),
                  "associate second process for reverse cleanup order");
            kaspace_put(shared);
            other_thread = kthread_create(other);
            CHECK(other_thread != NULL, "construct second process thread");
        } else {
            kfree(other);
            other = NULL;
            CHECK(FALSE, "construct second process for reverse cleanup order");
        }
    } else {
        CHECK(FALSE, "allocate second process for reverse cleanup order");
    }
    proc_free_owned(pd);
    if (other_thread)
        CHECK(aspace_translate(kaspace_backend(kthread_aspace(other_thread)),
                               (VADDR)(uintptr_t)pd, NULL, NULL),
              "shared mapping survives first process cleanup");
    kthread_destroy(other_thread);
    if (other) {
        kproc_destroy(other);
        kfree(other);
    }
    CHECK(observer && !kproc_ref_has_basepage(observer),
          "observer retains identity after threads and process cleanup");
    kproc_release(observer);
    same(&s, "non-runnable thread relationships");
}

/* Real KPROC resources, heap/mmap metadata and ownership transactions. No
 * runnable clone entry point or scheduler publication is involved. */
static void test_proc_clone(void)
{
    SNAP baseline, acquired;
    PD *source_pd, *target_pd;
    KPROC *source, *target;
    PROC_CLONE_ARGS args;
    PROC_CLONE_TRANSACTION *tx;
    PROC_CLONE_RESULT first = PROC_CLONE_RESULT_INIT, second = PROC_CLONE_RESULT_INIT, unchanged;
    KTHREAD *survivor;
    KASPACE *space;
    VADDR heap;
    UWORD order, stage, policy, status;
    LONG rc;
    int before = failures;
#if CONF_WITH_MMAP
    LONG mapping;
#endif
    for (order = 0; order < 2; order++) {
        snap(&baseline);
        source_pd = new_basepage();
        target_pd = new_basepage();
        if (!source_pd || !target_pd) return;
        set_owner(source_pd, source_pd);
        set_owner(USERPTR_TO_PTR(source_pd->p_env), source_pd);
        set_owner(target_pd, target_pd);
        set_owner(USERPTR_TO_PTR(target_pd->p_env), target_pd);
        CHECK(kproc_prepare_user(source_pd, run), "prepare clone source active space");
        source = kproc_retain(source_pd);
        target = kproc_retain(target_pd);
        heap = kproc_uheap_alloc(source_pd, 2 * PAGE);
        CHECK(heap != 0, "reserve heap owned by shared clone space");
#if CONF_WITH_MMAP
        mapping = kproc_mmap(source_pd, 0, 2 * PAGE, PROT_READ | PROT_WRITE,
                             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        CHECK(mapping > 0, "reserve anonymous mapping before clone construction");
#endif
        bzero(&args, sizeof(args));
        bzero(&first, sizeof(first)); bzero(&second, sizeof(second));
        args.source = source;
        args.vm_policy = PROC_CLONE_SHARE_VM;
        for (policy = 0; policy < 2; policy++) {
            args.process_policy = policy ? PROC_CLONE_PREPARED_PROCESS : PROC_CLONE_SAME_PROCESS;
            args.prepared_process = policy ? target : NULL;
            for (stage = 1; stage <= PROC_CLONE_RESOURCE_MASK + 1; stage++) {
                args.resources = stage; tx = NULL; snap(&acquired);
                CHECK(proc_clone(&args, &tx) == EINVAL && !tx,
                      "resource policies must be zero for existing PROCESS resources");
                same(&acquired, "meaningless resource flags have no acquisition side effects");
            }
        }
        args.resources = 0;
        for (policy = 0; policy < 2; policy++) {
            args.process_policy = policy ? PROC_CLONE_PREPARED_PROCESS : PROC_CLONE_SAME_PROCESS;
            args.prepared_process = policy ? target : NULL;
            for (stage = 1; stage <= 4; stage++) {
                snap(&acquired);
                tx = NULL;
                proc_clone_test_fail_after(stage);
                rc = proc_clone(&args, &tx);
                CHECK(rc == ENSMEM && !tx,
                      "clone acquisition failure leaves transaction output unchanged");
                CHECK(!kproc_active_space(target) && kproc_ref_has_basepage(source) &&
                      kproc_ref_has_basepage(target), "failed clone preserves both resource owners");
                same(&acquired, "clone PROCESS/ASPACE/THREAD acquisition rollback");
            }
        }
        args.process_policy = PROC_CLONE_SAME_PROCESS;
        args.prepared_process = NULL;
        tx = NULL;
        CHECK(proc_clone(&args, &tx) == E_OK, "prepare first same-PROCESS clone identity");
        {
            PROC_CLONE_TRANSACTION *saved = tx;
            snap(&acquired);
            CHECK(proc_clone(&args, &tx) == EINVAL && tx == saved,
                  "nonempty clone handle cannot be overwritten");
            same(&acquired, "nonempty transaction rejection has no side effects");
        }
        CHECK(proc_clone_commit(&tx, &first) == E_OK && !tx,
              "commit first same-PROCESS non-runnable clone identity");
        bzero(&unchanged, sizeof(unchanged)); memcpy(&second, &unchanged, sizeof(second));
        CHECK(proc_clone_commit(&tx, &second) == EINVAL && !memcmp(&second, &unchanged, sizeof(second)),
              "repeated commit preserves result and cannot consume an identity twice");
        proc_clone_abort(&tx);
        CHECK(proc_clone(&args, &tx) == E_OK, "prepare second same-PROCESS clone identity");
        {
            PROC_CLONE_TRANSACTION *saved = tx;
            memcpy(&unchanged, &first, sizeof(first)); snap(&acquired);
            CHECK(proc_clone_commit(&tx, &first) == EINVAL && tx == saved &&
                  !memcmp(&first, &unchanged, sizeof(first)), "commit cannot replace a live result");
            same(&acquired, "nonempty result rejection retains both ownership handles");
        }
        CHECK(proc_clone_commit(&tx, &second) == E_OK && !tx,
              "commit second same-PROCESS non-runnable clone identity");
        CHECK(first.pid == kproc_ref_pid(source) && second.pid == first.pid &&
              first.tid != second.tid && kthread_process(first.thread) == source &&
              kthread_aspace(first.thread) == kthread_aspace(second.thread),
              "clone results preserve PID versus independent TID identities");
        CHECK(!kthread_start(first.thread), "committed clone identity cannot execute");
        kthread_destroy(order ? first.thread : second.thread);
        survivor = order ? second.thread : first.thread;
        /* survivor owns the remaining non-runnable THREAD from now on. */
        bzero(&first, sizeof(first)); bzero(&second, sizeof(second));

        args.process_policy = PROC_CLONE_PREPARED_PROCESS;
        args.prepared_process = target;
        CHECK(proc_clone(&args, &tx) == E_OK && !kproc_active_space(target),
              "prepared transaction does not publish target active VM");
        proc_clone_abort(&tx);
        proc_clone_abort(&tx);
        CHECK(!tx && !kproc_active_space(target), "aborted import is idempotent and leaves no active VM");
        CHECK(proc_clone(&args, &tx) == E_OK && proc_clone_commit(&tx, &first) == E_OK && !tx,
              "commit shared VM into independently prepared PROCESS resources");
        space = kthread_aspace(first.thread);
        CHECK(first.pid == kproc_ref_pid(target) && first.pid != kproc_ref_pid(source) &&
              first.tid != kthread_tid(survivor) && kthread_process(survivor) == source &&
              kthread_aspace(survivor) == space && kproc_active_space(target) == space && kproc_active_space(source) == space &&
              !kproc_thread_start_allowed(target), "independent PROCESS identity imports VM without enabling startup");
        if (order) {
            kthread_destroy(survivor);
            proc_dispose_process(source);
            CHECK(!kproc_ref_has_basepage(source) && kproc_ref_has_basepage(target),
                  "source disposal retires only its identity/resources");
            CHECK(CONF_WITH_DEMAND_PAGING ?
                  kproc_demand_page(target_pd, heap + PAGE, ASPACE_PROT_USER | ASPACE_PROT_WRITE) == 1 :
                  aspace_translate(kproc_user_aspace(target_pd), heap + PAGE, NULL, NULL),
                  "shared heap first-touch dispatch survives original PROCESS retirement");
#if CONF_WITH_MMAP
            CHECK(kproc_demand_page(target_pd, (VADDR)mapping + PAGE,
                                   ASPACE_PROT_USER | ASPACE_PROT_WRITE) == 1,
                  "shared anonymous fault dispatch survives original PROCESS retirement");
#endif
        }
        proc_dispose_process(target);
        CHECK(kproc_ref_has_basepage(target), "clone THREAD retains pending PROCESS resources");
        kthread_destroy(first.thread);
        CHECK(!kproc_ref_has_basepage(target) && !kproc_ref_status(target, &status),
              "last clone release disposes resources without inventing an exit status");
        if (!order) {
            CHECK(kproc_ref_has_basepage(source) && (CONF_WITH_DEMAND_PAGING ?
                  kproc_demand_page(source_pd, heap + PAGE, ASPACE_PROT_USER | ASPACE_PROT_WRITE) == 1 :
                  aspace_translate(kproc_user_aspace(source_pd), heap + PAGE, NULL, NULL)),
                  "source heap survives independent target cleanup");
            proc_dispose_process(source);
            CHECK(kproc_ref_has_basepage(source), "surviving source THREAD retains live resources");
            kthread_destroy(survivor);
        }
        kproc_release(target);
        kproc_release(source);
        same(&baseline, "shared clone ownership release in either PROCESS order");
    }

    /* Disposal between stages rejects commit; abort retains exact identity,
     * never revisiting a reusable PD through lookup. */
    snap(&baseline);
    source_pd = new_basepage();
    if (!source_pd) return;
    set_owner(source_pd, source_pd);
    set_owner(USERPTR_TO_PTR(source_pd->p_env), source_pd);
    CHECK(kproc_prepare_user(source_pd, run), "prepare clone commit rejection source");
    source = kproc_retain(source_pd);
    args.source = source; args.prepared_process = NULL;
    args.process_policy = PROC_CLONE_SAME_PROCESS;
    tx = NULL;
    CHECK(proc_clone(&args, &tx) == E_OK, "prepare clone before disposal request");
    bzero(&unchanged, sizeof(unchanged)); memcpy(&first, &unchanged, sizeof(first));
    proc_dispose_process(source);
    CHECK(proc_clone_commit(&tx, &first) == EACCDN && tx &&
          !memcmp(&first, &unchanged, sizeof(first)), "rejected commit leaves result and abort ownership intact");
    proc_clone_abort(&tx);
    CHECK(!tx && !kproc_ref_has_basepage(source), "rollback releases pending resources exactly once");
    kproc_release(source);
    same(&baseline, "clone commit rejection rollback");
    kcprintf("x86-64 trusted clone ownership: %s\n", failures == before ? "PASS" : "FAIL");
}

/* Exercise invalidation with real KPROC transitions between transaction stages.
 * Startup-state injection marks the record only; it never starts user code. */
static void test_proc_clone_invalidation(void)
{
    UWORD scenario, status;
    SNAP baseline, invalidated;
    PD *source_pd, *target_pd;
    KPROC *source, *target;
    KASPACE *space;
    ASPACE *target_space;
    KTHREAD *extra;
    PROC_CLONE_ARGS args;
    PROC_CLONE_TRANSACTION *tx, *saved;
    PROC_CLONE_RESULT result, sentinel;
    int before = failures;

    for (scenario = 0; scenario < 6; scenario++) {
        snap(&baseline);
        source_pd = new_basepage(); target_pd = new_basepage();
        if (!source_pd || !target_pd) return;
        set_owner(source_pd, source_pd); set_owner(USERPTR_TO_PTR(source_pd->p_env), source_pd);
        set_owner(target_pd, target_pd); set_owner(USERPTR_TO_PTR(target_pd->p_env), target_pd);
        CHECK(kproc_prepare_user(source_pd, run), "prepare invalidation source");
        source = kproc_retain(source_pd); target = kproc_retain(target_pd);
        space = kproc_test_retain_active(source_pd);
        bzero(&args, sizeof(args));
        args.source = source; args.prepared_process = target;
        args.process_policy = PROC_CLONE_PREPARED_PROCESS; args.vm_policy = PROC_CLONE_SHARE_VM;
        tx = NULL; extra = NULL; target_space = NULL;
        CHECK(proc_clone(&args, &tx) == E_OK && tx, "prepare target invalidation transaction");
        saved = tx;
        switch (scenario) {
        case 0: proc_dispose_process(target); break;
        case 1:
            CHECK(kproc_prepare_user(target_pd, run), "target independently acquires incompatible VM");
            target_space = kproc_user_aspace(target_pd);
            CHECK(target_space && target_space != kaspace_backend(space), "invalidation uses distinct real VM");
            break;
        case 2:
            extra = kthread_construct_identity(target, space);
            CHECK(extra != NULL, "target acquires another non-runnable member");
            break;
        case 3: kproc_mark_started(target_pd); break;
        case 4: kproc_complete(source_pd, 33); break;
        case 5: proc_dispose_process(source); break;
        }
        bzero(&sentinel, sizeof(sentinel)); memcpy(&result, &sentinel, sizeof(result));
        snap(&invalidated);
        CHECK(proc_clone_commit(&tx, &result) == EACCDN && tx == saved &&
              !memcmp(&result, &sentinel, sizeof(result)), "invalidated commit preserves handle and result");
        CHECK(proc_clone_commit(&tx, &result) == EACCDN && tx == saved,
              "repeated rejected commit cannot adopt VM or repeat cleanup");
        CHECK(kproc_ref_has_basepage(target) &&
              aspace_translate(kaspace_backend(space), (VADDR)(uintptr_t)source_pd, NULL, NULL),
              "failed commit preserves target resources and retained source mappings");
        if (target_space)
            CHECK(kproc_user_aspace(target_pd) == target_space, "commit cannot replace incompatible target VM");
        same(&invalidated, "rejected commit changes no allocation or resource ownership");
        proc_clone_abort(&tx); proc_clone_abort(&tx);
        CHECK(!tx && !memcmp(&result, &sentinel, sizeof(result)), "abort consumes only transaction ownership once");
        if (extra) {
            CHECK(kthread_process(extra) == target && kthread_aspace(extra) == space,
                  "abort does not detach the target's other member");
            kthread_destroy(extra);
        }
        if (target_space)
            CHECK(kproc_user_aspace(target_pd) == target_space, "abort preserves independently acquired target VM");
        proc_dispose_process(target); proc_dispose_process(source);
        CHECK(!kproc_ref_has_basepage(target) && !kproc_ref_has_basepage(source),
              "caller cleanup retires both exact identities");
        if (scenario == 4)
            CHECK(kproc_ref_status(source, &status) && status == 33, "existing source status is unchanged");
        kproc_release(target); kproc_release(source); kaspace_put(space);
        same(&baseline, "invalidation followed by abort releases resources exactly once");
    }
    kcprintf("x86-64 clone invalidation: %s\n", failures == before ? "PASS" : "FAIL");
}

/* Real low-memory allocations, PID identity, standard-handle/CWD inheritance
 * and generic disposal. The factory does not build an execution context. */
static void test_proc_clone_factory(void)
{
    SNAP baseline, acquired;
    PD *pd, *child_pd, *saved = run;
    KPROC *source, *child;
    PROC_CLONE_ARGS args;
    PROC_CLONE_TRANSACTION *tx;
    PROC_CLONE_RESULT result = PROC_CLONE_RESULT_INIT;
    LONG handle, rc;
    ULONG directories, child_ticks;
    UWORD stage, policy, i, status;
    int uses;
    SBYTE saved_stderr;
    char byte;
    int before = failures;

    snap(&baseline);
    pd = new_basepage();
    if (!pd) return;
    set_owner(pd, pd); set_owner(USERPTR_TO_PTR(pd->p_env), pd);
    CHECK(kproc_prepare_user(pd, run), "prepare resource-factory source");
    source = kproc_retain(pd);
    run = pd;
    handle = DISK_CALL(xopen("C:\\X32HELLO.TOS", 0));
    CHECK(handle >= NUMSTD && handle < NUMHANDLES, "open factory inheritance fixture");
    if (handle < NUMSTD || handle >= NUMHANDLES) {
        run = saved; proc_dispose_process(source); kproc_release(source); return;
    }
    CHECK(ixforce(1, handle, pd) == E_OK, "source owns stdout alias and explicit handle");
    CHECK(kproc_psetlimit(pd, 3, 4 * PAGE) >= 0, "set source resource-limit policy");
    kproc_set_prio(pd, -3);
    uses = sft[handle-NUMSTD].f_use;
    directories = dir_refs();
    bzero(&args, sizeof(args));
    args.source = source; args.process_policy = PROC_CLONE_NEW_PROCESS;
    args.vm_policy = PROC_CLONE_SHARE_VM; args.resources = PROC_CLONE_RESOURCE_MASK;
    snap(&acquired);
    child = source;
    CHECK(proc_create_clone_process(source, args.resources, &child) == EINVAL && child == source,
          "factory cannot overwrite an existing resource-owner handle");
    tx = NULL; run = saved;
    CHECK(proc_clone(&args, &tx) == EINVFN && !tx && run == saved,
          "foreign-source factory request cannot change execution identity");
    run = pd;
    same(&acquired, "factory handle/context admission has no side effects");
    for (stage = 1; stage <= 5; stage++) {
        snap(&acquired); tx = NULL;
        proc_clone_factory_test_fail_after(stage);
        rc = proc_clone(&args, &tx);
        CHECK(rc == ENSMEM && !tx, "factory acquisition failure publishes no child or transaction");
        CHECK(sft[handle-NUMSTD].f_use == uses && dir_refs() == directories,
              "factory rollback closes only its own inherited resource references");
        same(&acquired, "factory environment/PD/PID/handle/CWD rollback");
    }
    for (stage = 1; stage <= 4; stage++) {
        snap(&acquired); tx = NULL;
        proc_clone_test_fail_after(stage);
        CHECK(proc_clone(&args, &tx) == ENSMEM && !tx,
              "post-factory transaction failure disposes owned PROCESS exactly once");
        same(&acquired, "transaction rollback includes factory-owned resources");
    }
    snap(&acquired); tx = NULL;
    kproc_test_pid_search(kproc_ref_pid(source) - 1, 1);
    CHECK(proc_clone(&args, &tx) == ENSMEM && !tx, "PID exhaustion releases raw factory allocations");
    kproc_test_pid_search(kproc_ref_pid(source), 0);
    same(&acquired, "factory PID-allocation failure rollback");
    for (stage = 1; stage <= 12; stage++) {
        snap(&acquired); tx = NULL;
        x86_64_pmem_test_fail_after(stage);
        rc = proc_clone(&args, &tx);
        x86_64_pmem_test_fail_after(0);
        CHECK(rc == E_OK || rc == ENSMEM, "physical allocation failure returns safely from factory");
        proc_clone_abort(&tx);
        same(&acquired, "factory physical-allocation failure and successful abort");
    }
    /* A checked inheritance failure cannot leave references from earlier slots. */
    snap(&acquired); tx = NULL;
    saved_stderr = kproc_uft(pd)[2];
    kproc_uft(pd)[2] = NUMSTD - 1; /* stdout acquired successfully before this failure */
    CHECK(proc_clone(&args, &tx) == EIHNDL && !tx, "partial standard-handle inheritance fails before commit");
    kproc_uft(pd)[2] = saved_stderr;
    CHECK(sft[handle-NUMSTD].f_use == uses, "earlier inherited stdout reference is unwound exactly once");
    same(&acquired, "checked resource-inheritance failure cleanup");

    for (policy = 0; policy <= PROC_CLONE_RESOURCE_MASK; policy++) {
        snap(&acquired); tx = NULL;
        args.resources = policy;
        CHECK(proc_clone(&args, &tx) == E_OK && proc_clone_commit(&tx, &result) == E_OK && !tx,
              "factory commits an explicitly selected resource policy");
        child = result.owned_process;
        if (policy == PROC_CLONE_RESOURCE_MASK) {
            PROC_CLONE_RESULT held;
            PROC_CLONE_TRANSACTION *other = NULL, *held_tx;
            SNAP held_resources;
            CHECK(proc_clone(&args, &other) == E_OK, "prepare while an owned factory result remains live");
            held_tx = other; snap(&held_resources);
            memcpy(&held, &result, sizeof(held));
            CHECK(proc_clone_commit(&other, &result) == EINVAL && other == held_tx &&
                  !memcmp(&held, &result, sizeof(result)), "factory result cannot lose THREAD/PROCESS ownership on overwrite");
            same(&held_resources, "owned result overwrite changes no resources");
            proc_clone_abort(&other);
        }
        child_pd = kproc_live_pd(child);
        CHECK(child && child_pd && result.pid != kproc_ref_pid(source) &&
              kthread_process(result.thread) == child && !kthread_start(result.thread),
              "factory returns a distinct owned PROCESS/PID and non-runnable THREAD");
        if (child_pd) {
            CHECK(kproc_uft(child_pd)[1] == ((policy & PROC_CLONE_COPY_STANDARD_HANDLES) ? handle : 0),
                  "standard-handle inheritance is independent of VM/CWD policy");
            CHECK(sft[handle-NUMSTD].f_use == uses + !!(policy & PROC_CLONE_COPY_STANDARD_HANDLES),
                  "factory standard alias owns exactly one added SFT reference");
            for (i = 0; i < NUMCURDIR; i++)
                CHECK(kproc_curdir(child_pd)[i] == ((policy & PROC_CLONE_COPY_CWD) ? kproc_curdir(pd)[i] : 0),
                      "CWD inheritance is independent of VM/descriptor policy");
            CHECK(kproc_parent_pid(child_pd) == kproc_ref_pid(source) &&
                  kproc_get_prio(child_pd) == -3 && kproc_psetlimit(child_pd, 3, -1) == 4 * PAGE,
                  "trusted parent identity, priority and process limits are inherited");
        }
        child_ticks = kproc_test_child_ticks(source);
        kproc_test_age_unstarted(child, 37);
        proc_clone_release(&result); proc_clone_release(&result);
        CHECK(kproc_test_child_ticks(source) == child_ticks,
              "unstarted constructed child cannot charge parent execution usage");
        CHECK(!result.thread && !result.owned_process && !result.pid && !result.tid,
              "non-runnable owned result release is idempotent");
        same(&acquired, "committed factory result releases its PROCESS resources");
    }

    /* Child resources and mappings survive source retirement. */
    tx = NULL; args.resources = PROC_CLONE_RESOURCE_MASK;
    CHECK(proc_clone(&args, &tx) == E_OK && proc_clone_commit(&tx, &result) == E_OK,
          "construct factory child to outlive resource source");
    child = kproc_retain_live(result.owned_process);
    child_pd = kproc_live_pd(child);
    run = saved;
    proc_dispose_process(source);
    CHECK(!kproc_ref_has_basepage(source) && kproc_ref_has_basepage(child) &&
          sft[handle-NUMSTD].f_use == 1, "source exit preserves child's inherited file reference");
    CHECK(!kproc_notification_parent(child_pd) && !kproc_parent_pid(child_pd),
          "factory child cannot notify a replacement through a retired parent PD");
    run = child_pd;
    CHECK(DISK_CALL(xread(1, 1, &byte)) == 1, "inherited standard handle remains usable after source retirement");
    run = saved;
    proc_clone_release(&result); proc_clone_release(&result);
    CHECK(!kproc_ref_has_basepage(child) && !kproc_ref_status(child, &status),
          "owned child cleanup retains identity without fabricating completion");
    kproc_release(child); kproc_release(source);
    same(&baseline, "factory source-first cleanup and final owned-result release");
    kcprintf("x86-64 clone PROCESS factory: %s\n", failures == before ? "PASS" : "FAIL");
}

#if CONF_WITH_NATIVE_SCHED
/* These identity-only startup drafts never enter user code or backend stacks.
 * The bridge is exercised separately through the existing PROCESS launcher. */
static void forbidden_startup_reclaim(void *resource)
{
    UNUSED(resource);
    panic("identity-only THREAD unexpectedly accepted a backend");
}

typedef struct { UWORD acquired, released; BOOL fail; } STARTUP_OBSERVER_TEST;
static LONG startup_observer_acquire(void *context)
{
    STARTUP_OBSERVER_TEST *test = context;
    if (test->fail) return ENSMEM;
    test->acquired++;
    return E_OK;
}
static void startup_observer_release(void *context)
{
    STARTUP_OBSERVER_TEST *test = context;
    test->released++;
}

/* Exercise the immutable bridge with the EXISTING single-process launcher.
 * This is not execution of a proc_clone startup image or a runnable sibling. */
#if CONF_WITH_MMAP
typedef struct {
    KPROC *process;
    UWORD registered, released, completed, expected;
    BOOL fail;
} CLONE_RUN_OBSERVER;
static LONG clone_observer_acquire(void *context)
{
    CLONE_RUN_OBSERVER *watch = context;
    if (watch->fail) return ENSMEM;
    if (!kproc_retain_live(watch->process)) return EACCDN;
    watch->registered++;
    return E_OK;
}
static void clone_observer_release(void *context)
{
    CLONE_RUN_OBSERVER *watch = context;
    UWORD status;
    PROCESS_COMPLETION completion;
    CHECK(watch->registered == 1 && !watch->released, "registered clone observer releases once");
    if (kproc_ref_status(watch->process, &status)) {
        watch->completed++;
        CHECK(kproc_ref_terminal(watch->process) && kproc_ref_completion(watch->process,&completion) &&
              completion.state==PROCESS_COMPLETED && (completion.order_low || completion.order_high) &&
              (completion.kind==PROCESS_COMPLETION_NORMAL ? completion.status==status :
               completion.kind==PROCESS_COMPLETION_SIGNAL && completion.signal>0 && !completion.status),
              "native clone authoritative snapshot follows resource and wait publication");
        CHECK(status == watch->expected && !kproc_ref_has_basepage(watch->process),
              "observer retains correct completed PID/status after live resources retire");
    }
    watch->released++;
    kproc_release(watch->process); watch->process = NULL;
}
extern const UBYTE clone_probe_begin[], clone_probe_end[];
/* Unlike bridge-only tests, these dispatch the constructed CPU/FPU image. */
/* Kernel-only hook reached from a real ring-3 Syield. No test syscall. Each
 * child is constructed while its actual parent is executing in the group. */
static struct {
    BOOL enabled, waiting, reused;
    UWORD scenario, parent_yields, child_progress, successes, attempts;
    KPROC *parent; /* owning completion identity, never retains its old PD */
    PD *old_pd;
    VADDR usage;
    ULONG child_ticks;
    PROC_CLONE_IDENTITIES ids[KSCHED_LIMIT];
    PROC_CLONE_OBSERVER *observers[KSCHED_LIMIT];
} active_clone;
static LONG active_clone_construct(UWORD index)
{
    PROC_CLONE_TRANSACTION *tx = NULL;
    PROC_CLONE_ARGS args;
    THREAD_STARTUP_ARGS startup;
    KTHREAD *current = ksched_exit_thread(run);
    KPROC *source = kthread_process(current);
    ASPACE *as = kaspace_backend(kproc_active_space(source));
    VADDR stack, data;
    ULONG words[6] = { 0, 0, 0, 0, 0x11112222, 0x33334444 }, check[2];
    LONG error;
    UQUAD frame = 0x1122334455667788ULL, after;
    PD *controller = kproc_get_parent(run);
    LONG pins_before = procmem_pins(controller);
    UQUAD entry_flags, exit_flags;
    __asm__ volatile ("pushfq; popq %0" : "=r"(entry_flags));
    CHECK((entry_flags & 0x200) && !ksched_test_clone_masked(), "active construction starts outside IRQ exclusion");
    STARTUP_OBSERVER_TEST borrowed = { 0, 0, FALSE };
    THREAD_STARTUP_OBSERVER borrowed_ops = { startup_observer_acquire, startup_observer_release };
    stack = kproc_uheap_alloc(run, 2*PAGE);
    data = kproc_mmap(run,0,PAGE,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    CHECK(stack && data && kproc_demand_page(run,stack,ASPACE_PROT_WRITE) == 1 &&
          kproc_demand_page(run,stack+PAGE,ASPACE_PROT_WRITE) == 1 &&
          kproc_demand_page(run,data,ASPACE_PROT_WRITE) == 1, "back active-clone stack/outputs before construction");
    CHECK(aspace_copy_to_user(as,stack+2*PAGE-8,&frame,sizeof(frame)), "seed active return slot before publication");
    words[0] = active_clone.scenario == 1 ? 4 : active_clone.scenario == 2 ? 5 :
               active_clone.scenario == 3 ? 2 : active_clone.scenario == 11 ? 3 :
               active_clone.scenario == 4 && (index & 1) ? 2 : 0;
    words[2] = kproc_ref_pid(source); words[3] = stack;
    CHECK(aspace_copy_to_user(as,data,words,sizeof(words)), "seed active clone argument and rollback outputs");
    bzero(&args,sizeof(args)); args.source = source;
    args.process_policy = PROC_CLONE_NEW_PROCESS; args.vm_policy = PROC_CLONE_SHARE_VM;
    args.resources = PROC_CLONE_RESOURCE_MASK;
    startup.entry = 0x00600000; startup.argument = data; startup.stack_base = stack; startup.stack_size = 2*PAGE;
    CHECK(proc_clone(&args,&tx) == E_OK && proc_clone_prepare_startup(tx,&startup) == E_OK &&
          proc_clone_retain_startup_stack(tx) == E_OK && proc_clone_prepare_outputs(tx,data+16,data+20) == E_OK,
          "prepare real active-group PROCESS/THREAD/ASPACE and execution resources");
    if (active_clone.scenario == 0) {
        kproc_test_age_unstarted(proc_clone_process(tx), 37);
        kproc_test_controller_first(proc_clone_process(tx));
        active_clone.usage = data + 32;
    }
    if (active_clone.scenario == 10) {
        CHECK(proc_clone_observe_startup(tx,&borrowed_ops,&borrowed) == E_OK,
              "prepare borrowed observer solely to verify active admission refuses it");
    } else {
        if (active_clone.scenario == 8) proc_clone_test_fail_after(5);
        error = proc_clone_observe_async(tx,&active_clone.observers[index]);
        CHECK(error == (active_clone.scenario == 8 ? ENSMEM : E_OK), "owned observer acquisition and rollback");
        if (error) {
            CHECK(aspace_copy_from_user(as,&after,stack+2*PAGE-8,sizeof(after)) && after == frame &&
                  aspace_copy_from_user(as,check,data+16,sizeof(check)) && check[0] == words[4] && check[1] == words[5],
                  "observer allocation failure preserves frame and identity outputs");
            proc_clone_abort(&tx); return error;
        }
    }
    if (active_clone.scenario == 5) CHECK(kproc_mprot(run,data,PAGE,PROT_READ) == E_OK, "revoke active publication output");
    if (active_clone.scenario == 6) proc_clone_test_cancel_publication();
    if (active_clone.scenario == 7) kproc_test_fail_clone_controller();
    error = proc_clone_spawn(&tx,&active_clone.ids[index]);
    __asm__ volatile ("pushfq; popq %0" : "=r"(exit_flags));
    CHECK((entry_flags & 0x200) == (exit_flags & 0x200) && !ksched_test_clone_masked(),
          "success/failure/cancellation restores caller IRQ state before teardown");
    active_clone.attempts++;
    if (!error) {
        CHECK(!tx && active_clone.ids[index].pid && active_clone.ids[index].tid,
              "active publication consumes transaction and returns distinct ids without waiting");
        active_clone.successes++;
        CHECK(procmem_pins(controller) == pins_before + 1, "published child owns an independent controller allocation pin");
        CHECK(proc_clone_observer_state(active_clone.observers[index]) == PROC_CLONE_OBSERVER_PUBLISHED,
              "published child cannot run before creator yields");
        CHECK(!proc_clone_observer_result(active_clone.observers[index], &(PROC_CLONE_COMPLETION){0,0,0}),
              "no premature completion from publication");
        if (active_clone.scenario == 9) proc_clone_observer_release(&active_clone.observers[index]);
    } else {
        LONG expected = active_clone.scenario == 5 ? EIMBA : active_clone.scenario == 6 || active_clone.scenario == 4 ? EAGAIN :
                        active_clone.scenario == 7 ? ENSMEM : EINVFN;
        CHECK(error == expected && !active_clone.ids[index].pid && !active_clone.ids[index].tid,
              "failed active publication leaves result and outputs unchanged");
        CHECK(aspace_copy_from_user(as,check,data+16,sizeof(check)) && check[0] == words[4] && check[1] == words[5],
              "active output rollback before first publication store");
        CHECK(active_clone.scenario == 6 ? !tx : tx != NULL, "explicit cancellation consumes; preflight failure preserves transaction");
        proc_clone_abort(&tx);
        CHECK(procmem_pins(controller) == pins_before && aspace_copy_from_user(as,&after,stack+2*PAGE-8,sizeof(after)) && after == frame,
              "failed/cancelled publication releases its controller ownership and preserves return slot");
        if (active_clone.observers[index]) CHECK(proc_clone_observer_state(active_clone.observers[index]) == PROC_CLONE_OBSERVER_CANCELLED,
              "cancelled observer has no fabricated process completion");
        if (active_clone.scenario == 10) CHECK(borrowed.acquired == 1 && borrowed.released == 1,
              "borrowed observer is refused and unwound before its caller returns");
    }
    return error;
}
static struct {
    BOOL enabled, checked;
    UWORD scenario, yields;
    KPROC *parent;
    PROCESS_OBSERVER *observer, *second;
    LONG pid;
    LONG handle;
} ordinary_observation;
static void ordinary_deferred_commit(void)
{
    PROCESS_COMPLETION completion;
    PROCESS_OBSERVER *late=NULL, *replacement=NULL, *second=NULL;
    LONG next;
    static char command[]={2,'O','n',0}, environment[]={0,0};
    CHECK(proc_exec_retain_observer(ordinary_observation.parent,ordinary_observation.handle,TRUE,&late)==EACCDN && !late,
          "committed held successor is not an attachable prepared transaction");
    CHECK(ksched_wait((WORD)ordinary_observation.pid,1,0)==EFILNF,
          "deferred successor has no public child-wait ownership");
    if (ordinary_observation.scenario==8) {
        CHECK(ksched_defer_cancel()==E_OK && ksched_defer_cancel()==E_OK &&
              process_observer_ready(ordinary_observation.observer) &&
              process_observer_snapshot(ordinary_observation.observer,&completion) &&
              completion.state==PROCESS_ABANDONED && !completion.kind && !completion.order_low,
              "explicit generic cancellation abandons only the selected unexecuted successor");
        CHECK(proc_exec_handle_close(ordinary_observation.parent,ordinary_observation.handle)==E_OK,
              "cancelled deferred observation closes independently");
        ordinary_observation.handle=0;
        ordinary_observation.checked=TRUE;
    } else if (ordinary_observation.scenario==9) {
        CHECK(proc_exec_handle_prepare(ordinary_observation.parent,"C:\\MISSING.TOS",command,environment,NULL,
                                       PROCESS_EXEC_DEFER_REPLACEMENT)==EFILNF &&
              !process_observer_ready(ordinary_observation.observer),
              "failed replacement preparation preserves the prior selected successor");
    } else if (ordinary_observation.scenario==10) {
        CHECK(proc_exec_handle_close(ordinary_observation.parent,ordinary_observation.handle)==E_OK &&
              !process_observer_ready(ordinary_observation.observer),
              "CLOSE never cancels an already committed deferred successor");
        ordinary_observation.handle=0;
    } else if (ordinary_observation.scenario==11) {
        next=proc_exec_handle_prepare(ordinary_observation.parent,"C:\\X32HELLO.TOS",command,environment,NULL,
                                      PROCESS_EXEC_DEFER_REPLACEMENT);
        CHECK(next>ordinary_observation.handle &&
              proc_exec_retain_observer(ordinary_observation.parent,next,TRUE,&replacement)==E_OK &&
              process_observer_attach(replacement,&second)==E_OK,
              "replacement owns fresh prepared rights and canonical observers before commit");
        if (next<=0 || !replacement) return;
        retiring_probe.enabled=TRUE; retiring_probe.seen=FALSE; retiring_probe.expect_yield=TRUE;
        retiring_probe.pid=(WORD)ordinary_observation.pid;
        retiring_probe.observer=ordinary_observation.observer;
        deferred_reentry.owner=ordinary_observation.parent;
        deferred_reentry.handle=next;
        deferred_reentry.pid=(WORD)ordinary_observation.pid;
        deferred_reentry.seen=FALSE;
        deferred_reentry.enabled=TRUE;
        CHECK(proc_exec_handle_commit(ordinary_observation.parent,next)==kproc_ref_pid(replacement) &&
              deferred_reentry.seen && retiring_probe.seen && process_observer_ready(ordinary_observation.observer) &&
              process_observer_snapshot(ordinary_observation.observer,&completion) &&
              completion.state==PROCESS_ABANDONED && !completion.kind && !completion.order_low,
              "successful replacement abandons the old successor without a fabricated completion");
        deferred_reentry.enabled=FALSE;
        CHECK(proc_exec_handle_close(ordinary_observation.parent,ordinary_observation.handle)==E_OK,
              "superseded deferred rights close without touching their replacement");
        process_observer_release(&ordinary_observation.observer);
        process_observer_release(&ordinary_observation.second);
        ordinary_observation.observer=replacement;
        ordinary_observation.second=second;
        ordinary_observation.pid=kproc_ref_pid(replacement);
        ordinary_observation.handle=next;
    }
}
static void ordinary_observation_yield(void)
{
    KTHREAD *thread;
    PROCESS_COMPLETION snapshot;
    LONG waited;
    char command[] = {2,'O','n',0};
    static char environment[] = {0,0};
    if (retiring_probe.yielding || !ordinary_observation.enabled || !(thread=ksched_exit_thread(run)) ||
        kthread_process(thread)!=ordinary_observation.parent || ordinary_observation.checked) return;
    ordinary_observation.yields++;
    if (ordinary_observation.yields==1) {
        command[2]=ordinary_observation.scenario==2 ? 'f':'n';
        if (ordinary_observation.scenario >= 3) {
            LONG result=proc_exec_handle_prepare(ordinary_observation.parent,"C:\\X32HELLO.TOS",
                                                  command,environment,NULL,ordinary_observation.scenario>=7 ?
                                                  PROCESS_EXEC_DEFER_REPLACEMENT:PROCESS_EXEC_ASYNC);
            CHECK(result>0,"ordinary loader returns a PROCESS-owned prepared handle");
            if (result<=0) return;
            ordinary_observation.handle=result;
            CHECK(proc_exec_retain_observer(ordinary_observation.parent,result,TRUE,
                                            &ordinary_observation.observer)==E_OK,
                  "native owned handle authenticates a pre-publication observer lease");
            ordinary_observation.pid=kproc_ref_pid(ordinary_observation.observer);
        } else ordinary_observation.pid=proc_exec_observe("C:\\X32HELLO.TOS",command,environment,NULL,
                                                  &ordinary_observation.observer);
        CHECK(ordinary_observation.pid>0 && ordinary_observation.observer,
              "ordinary loader attaches retained observer before asynchronous publication");
        if (ordinary_observation.pid<=0) return;
        CHECK(process_observer_snapshot(ordinary_observation.observer,&snapshot) &&
              snapshot.state==PROCESS_UNSTARTED && !process_observer_ready(ordinary_observation.observer),
              "ordinary observation is published before child user execution");
        CHECK(process_observer_attach(ordinary_observation.observer,&ordinary_observation.second)==E_OK,
              "multiple observers share the actual ordinary child identity");
        return;
    }
    if (!ordinary_observation.observer) return;
    if (ordinary_observation.scenario>=3 && ordinary_observation.yields==2) {
        CHECK(process_observer_snapshot(ordinary_observation.observer,&snapshot) &&
              snapshot.state==PROCESS_UNSTARTED && !process_observer_ready(ordinary_observation.observer) &&
              ksched_wait((WORD)ordinary_observation.pid,1,0)==EFILNF,
              "prepared ordinary child remains non-runnable and not wait-visible across native yield");
        if (ordinary_observation.scenario==4) {
            retiring_probe.enabled=TRUE; retiring_probe.seen=FALSE; retiring_probe.expect_yield=TRUE;
            retiring_probe.pid=(WORD)ordinary_observation.pid;
            retiring_probe.observer=ordinary_observation.observer;
            CHECK(proc_exec_handle_close(ordinary_observation.parent,ordinary_observation.handle)==E_OK &&
                  proc_exec_handle_close(ordinary_observation.parent,ordinary_observation.handle)==EIHNDL,
                  "native CLOSE consumes prepared rights exactly once");
            CHECK(retiring_probe.seen &&
                  proc_exec_handle_commit(ordinary_observation.parent,ordinary_observation.handle)==EIHNDL,
                  "CLOSE callback yields safely and subsequent COMMIT rejects the consumed preparation");
            CHECK(process_observer_ready(ordinary_observation.observer) &&
                  process_observer_snapshot(ordinary_observation.observer,&snapshot) &&
                  snapshot.state==PROCESS_ABANDONED && !snapshot.kind && !snapshot.status &&
                  !snapshot.order_low && !snapshot.order_high &&
                  ksched_wait((WORD)ordinary_observation.pid,1,0)==EFILNF,
                  "cancelled prepared image abandons canonical identity without public completion");
            ordinary_observation.checked=TRUE;
        } else if (ordinary_observation.scenario==5) {
            retiring_probe.enabled=TRUE; retiring_probe.seen=FALSE; retiring_probe.expect_yield=FALSE;
            retiring_probe.pid=(WORD)ordinary_observation.pid;
            retiring_probe.observer=ordinary_observation.observer;
            ordinary_observation.checked=TRUE; /* Owner exit must abandon its preparation. */
        } else {
            CHECK(proc_exec_handle_commit(ordinary_observation.parent,ordinary_observation.handle)==ordinary_observation.pid &&
                  proc_exec_handle_commit(ordinary_observation.parent,ordinary_observation.handle)==EACCDN,
                  "commit consumes prepared admission exactly once before user execution");
            if (ordinary_observation.scenario==6) {
                CHECK(proc_exec_handle_close(ordinary_observation.parent,ordinary_observation.handle)==E_OK,
                      "closing committed rights before child execution does not cancel or reap it");
                ordinary_observation.handle=0;
            } else if (ordinary_observation.scenario>=7) ordinary_deferred_commit();
        }
        return;
    }
    if (ordinary_observation.scenario>=7) {
        CHECK(!process_observer_ready(ordinary_observation.observer) &&
              process_observer_snapshot(ordinary_observation.observer,&snapshot) &&
              snapshot.state==PROCESS_UNSTARTED && ksched_wait((WORD)ordinary_observation.pid,1,0)==EFILNF,
              "selected successor remains unexecuted across the live owner's later native yields");
        ordinary_observation.checked=TRUE;
        return;
    }
    if (ordinary_observation.scenario==1)
        CHECK(ksched_wait_ready(process_observer_ready,ordinary_observation.observer)==KSCHED_WAIT_READY &&
              process_observer_snapshot(ordinary_observation.observer,&snapshot) && snapshot.status==0xffff,
              "observer-first ordinary completion is independent of Pwait");
    waited=ksched_wait((WORD)ordinary_observation.pid,ordinary_observation.scenario==1 ? 1:0,0);
    CHECK((WORD)((ULONG)waited>>16)==ordinary_observation.pid &&
          (UWORD)waited==(ordinary_observation.scenario==2 ? 0x0400:0x00ff),
          "ordinary observer does not consume the public PROCESS wait result");
    CHECK(process_observer_ready(ordinary_observation.observer) &&
          process_observer_snapshot(ordinary_observation.observer,&snapshot) &&
          snapshot.state==PROCESS_COMPLETED && (snapshot.order_low || snapshot.order_high) &&
          (ordinary_observation.scenario==2 ? snapshot.kind==PROCESS_COMPLETION_SIGNAL &&
             snapshot.signal==4 && !snapshot.status : snapshot.kind==PROCESS_COMPLETION_NORMAL &&
             snapshot.status==0xffff && !snapshot.signal),
          "actual ordinary Pexec snapshot distinguishes full normal ffff from SIGILL after Pwait");
    CHECK(process_observer_snapshot(ordinary_observation.second,&snapshot) &&
          ksched_wait((WORD)ordinary_observation.pid,1,0)==EFILNF,
          "independent observer survives exactly-once public reaping");
    if (ordinary_observation.handle) {
        CHECK(proc_exec_handle_snapshot(ordinary_observation.parent,ordinary_observation.handle,&snapshot)==E_OK &&
              snapshot.status==0xffff && snapshot.kind==PROCESS_COMPLETION_NORMAL,
              "owned snapshot preserves full completion after public reaping");
        CHECK(proc_exec_handle_close(ordinary_observation.parent,ordinary_observation.handle)==E_OK &&
              process_observer_ready(ordinary_observation.observer),
              "closing committed rights preserves independent native observer leases");
    }
    ordinary_observation.checked=TRUE;
}
static void test_discarded_observation(void)
{
    UWORD mode;
    static PD allocation_owner;
    for (mode=0;mode<2;mode++) {
        SNAP baseline;
        PD *pd;
        void *environment;
        PROCESS_OBSERVER *observer=NULL;
        KPROC *target;
        PROCESS_COMPLETION completion;
        snap(&baseline);
        pd=new_basepage();
        CHECK(pd!=NULL,"allocate unstarted observed basepage");
        if (!pd) return;
        environment=USERPTR_TO_PTR(pd->p_env);
        target=kproc_retain(pd);
        CHECK(process_observer_attach(target,&observer)==E_OK,
              "retain actual unstarted PROCESS identity before discard");
        kproc_release(target);
        if (!mode) CHECK(Mfree(pd)==E_OK,"free unstarted observed basepage");
        else {
            procmem_set_owner(pd,&allocation_owner);
            procmem_pin((VADDR)(uintptr_t)pd,PAGE,1);
            proc_free_owned(&allocation_owner);
            CHECK(!process_observer_ready(observer) && procmem_size(pd) &&
                  process_observer_snapshot(observer,&completion) && completion.state==PROCESS_UNSTARTED,
                  "retired observed basepage remains pending through actual deferred pin reclamation");
            procmem_pin((VADDR)(uintptr_t)pd,PAGE,-1);
        }
        CHECK(!procmem_size(pd) && process_observer_ready(observer) &&
              process_observer_snapshot(observer,&completion) && completion.state==PROCESS_ABANDONED &&
              !completion.kind && !completion.status && !completion.order_low && !completion.order_high,
              "actual final allocation release publishes abandonment without fabricated completion");
        CHECK(Mfree(environment)==E_OK,"release separately owned unused environment");
        process_observer_release(&observer);
        same(&baseline,"discarded observation reclaims allocation, mappings, handles and retained identity");
    }
}
static void test_ordinary_observation(void)
{
    UWORD mode;
    int before=failures;
    static const char command[]={2,'O','p',0}, environment[]={0,0};
    test_discarded_observation();
    for (mode=0;mode<12;mode++) {
        SNAP baseline;
        LONG loaded, status;
        PD *program;
        snap(&baseline);
        loaded=Pexec(PE_LOAD,"C:\\X32HELLO.TOS",command,environment);
        CHECK(loaded>0,"load real ordinary observer parent");
        if (loaded<=0) break;
        program=(PD *)(uintptr_t)loaded;
        CHECK(proc_prepare_scheduled(program)==E_OK,"prepare real ordinary observer parent");
        bzero(&ordinary_observation,sizeof(ordinary_observation));
        ordinary_observation.scenario=mode;
        ordinary_observation.parent=kproc_retain(program);
        ordinary_observation.enabled=TRUE;
        CHECK(ksched_run(&program,1,&status)==E_OK && status==0x1357 && ordinary_observation.checked,
              "ordinary observed child and parent execute actual native continuations");
        ordinary_observation.enabled=FALSE;
        if (mode>=7 && mode!=8) {
            PROCESS_COMPLETION parent_done, child_done;
            CHECK(process_observer_ready(ordinary_observation.observer) &&
                  process_observer_snapshot(ordinary_observation.observer,&child_done) &&
                  child_done.state==PROCESS_COMPLETED && child_done.kind==PROCESS_COMPLETION_NORMAL &&
                  child_done.status==0xffff && !child_done.signal &&
                  kproc_ref_completion(ordinary_observation.parent,&parent_done) &&
                  (child_done.order_high>parent_done.order_high ||
                   (child_done.order_high==parent_done.order_high && child_done.order_low>parent_done.order_low)),
                  "generic successor executes only after canonical predecessor retirement without GUI registration");
        }
        if (mode==5) {
            PROCESS_COMPLETION abandoned;
            CHECK(retiring_probe.seen && process_observer_ready(ordinary_observation.observer) &&
                  process_observer_snapshot(ordinary_observation.observer,&abandoned) &&
                  abandoned.state==PROCESS_ABANDONED && !abandoned.kind && !abandoned.order_low,
                  "final PROCESS exit abandons owned uncommitted launch while retained lease survives");
        }
        process_observer_release(&ordinary_observation.observer);
        process_observer_release(&ordinary_observation.second);
        kproc_release(ordinary_observation.parent);
        same(&baseline,"ordinary observation/Pwait releases all identities, mappings and stacks");
    }
    kcprintf("x86-64 ordinary PROCESS observation: %s\n",failures==before ? "PASS":"FAIL");
}
void x86_64_memtest_clone_yield(void)
{
    UWORD i;
    KTHREAD *thread;
    ordinary_observation_yield();
    if (!active_clone.enabled || !(thread = ksched_exit_thread(run))) return;
    if (kthread_process(thread) == active_clone.parent) {
        active_clone.parent_yields++;
        if (active_clone.parent_yields == 1) {
            UWORD count = active_clone.scenario == 4 ? KSCHED_LIMIT : 1;
            active_clone.child_ticks = kproc_test_child_ticks(active_clone.parent);
            for (i = 0; i < count; i++) active_clone_construct(i);
            if (active_clone.scenario == 4) CHECK(active_clone.successes == KSCHED_LIMIT-1,
                  "actual active group exhausts its final slot without publishing an eighth child");
        } else if (active_clone.parent_yields == 2 && active_clone.successes && active_clone.scenario != 2 &&
                   active_clone.scenario != 4 && active_clone.scenario != 9) {
            LONG waited;
            PROC_CLONE_COMPLETION result = { 0, 0, 0 };
            if (active_clone.scenario == 1) CHECK(proc_clone_observer_state(active_clone.observers[0]) == PROC_CLONE_OBSERVER_REAPED,
                  "fast child completed and root-reaped before the parent's first wait");
            active_clone.waiting = TRUE;
            waited = ksched_wait(active_clone.ids[0].pid, active_clone.scenario == 1 ? 1 : 0, active_clone.usage);
            active_clone.waiting = FALSE;
            CHECK((WORD)((ULONG)waited >> 16) == active_clone.ids[0].pid &&
                  proc_clone_observer_result(active_clone.observers[0],&result) && result.pid == active_clone.ids[0].pid &&
                  result.tid == active_clone.ids[0].tid && result.status == (active_clone.scenario == 3 ? 0xffff : active_clone.scenario == 11 ? 0x468a : 0x2468),
                  "existing Pwait consumer and independently owned observer agree on completion");
            if (active_clone.scenario == 0) {
                ULONG usage[2];
                CHECK(kproc_copy_from_user(usage,active_clone.usage,sizeof(usage)) && usage[0] >= 37 * (1000UL / CLOCKS_PER_SEC) &&
                      kproc_test_child_ticks(active_clone.parent) - active_clone.child_ticks == usage[0] / (1000UL / CLOCKS_PER_SEC),
                      "logical parent child usage is credited once by Pwait, not also during clone retirement");
            }
        }
    } else {
        if (active_clone.waiting) active_clone.child_progress++;
        if (active_clone.scenario == 2 && !active_clone.reused && !kproc_ref_has_basepage(active_clone.parent)) {
            PD *replacement = new_basepage();
            KPROC *identity;
            CHECK(replacement != NULL, "allocate replacement after actual creator retirement");
            if (!replacement) return;
            set_owner(replacement,replacement); set_owner(USERPTR_TO_PTR(replacement->p_env),replacement);
            identity = kproc_retain(replacement);
            CHECK(replacement == active_clone.old_pd && kproc_ref_pid(identity) != kproc_ref_pid(active_clone.parent),
                  "retired parent PD really is reused under a distinct PROCESS identity");
            CHECK(!kproc_notification_parent(run), "orphan completion cannot notify replacement at reused creator PD");
            proc_dispose_process(identity); kproc_release(identity);
            active_clone.reused = TRUE;
        }
    }
}
static void test_clone_execution(void)
{
    UWORD mode;
    int before = failures;
    for (mode = 0; mode < 24; mode++) {
        SNAP baseline;
        PD *pd, *original = run;
        KPROC *source;
        KTHREAD *parent_execution;
        KASPACE *space;
        ASPACE *as;
        ASPACE_ROOT previous;
        VADDR stack, data, collision = 0;
        ULONG input[4] = { 0, 0, 0, 0 }, outputs[2] = { 0x11112222, 0x33334444 }, readback[2];
        PROC_CLONE_ARGS args;
        THREAD_STARTUP_ARGS startup;
        PROC_CLONE_TRANSACTION *tx = NULL;
        PROC_CLONE_COMPLETION done = { 0, 0, 0 };
        CLONE_RUN_OBSERVER watched = { NULL, 0, 0, 0, 0, FALSE };
        THREAD_STARTUP_OBSERVER observer = { clone_observer_acquire, clone_observer_release };
        LONG result;
        UQUAD resident_generation, frame_before = 0x1122334455667788ULL, frame_after;
        BOOL executed = mode < 3 || mode == 10 || mode >= 12;
        snap(&baseline);
        pd = new_basepage();
        if (!pd) return;
        set_owner(pd,pd); set_owner(USERPTR_TO_PTR(pd->p_env),pd);
        CHECK(kproc_prepare_user(pd,run), "prepare live parent for real clone execution");
        parent_execution = kthread_create(pd);
        CHECK(parent_execution != NULL, "caller execution association retains parent live resources");
        source = kproc_retain(pd); space = kproc_active_space(source); as = kaspace_backend(space);
        CHECK(aspace_load_private(as, 0x00600000, PAGE, ASPACE_PROT_USER | ASPACE_PROT_EXEC,
              clone_probe_begin, clone_probe_end-clone_probe_begin), "install internal CPU/FPU execution probe");
        stack = kproc_uheap_alloc(pd,2*PAGE);
        if (mode == 0) {
            data = 0xfedcb000ULL; /* execute with a real high-bit x32 pointer */
            CHECK(aspace_map_private(as,data,PAGE,ASPACE_PROT_USER | ASPACE_PROT_WRITE),
                  "map high-bit opaque argument and publication destinations");
        } else data = kproc_mmap(pd,0,PAGE,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
        CHECK(stack && data && kproc_demand_page(pd,stack,ASPACE_PROT_WRITE) == 1 &&
              kproc_demand_page(pd,stack+PAGE,ASPACE_PROT_WRITE) == 1 &&
              (mode == 0 ? aspace_translate(as,data,NULL,NULL) : kproc_demand_page(pd,data,ASPACE_PROT_WRITE) == 1), "back shared code argument/outputs and private thread stack");
        CHECK(aspace_copy_to_user(as,stack+2*PAGE-8,&frame_before,sizeof(frame_before)),
              "seed return slot before any publication preparation");
        input[0] = mode == 10 ? 3 : mode < 3 ? mode : 0; input[2] = kproc_ref_pid(source); input[3] = stack;
        CHECK(aspace_copy_to_user(as,data,input,sizeof(input)) &&
              aspace_copy_to_user(as,data+16,outputs,sizeof(outputs)), "seed private clone argument and unchanged output sentinels");
        /* This fixture establishes the same current PROCESS/ASPACE contract
         * as a kernel service entered on behalf of a live user parent. */
        previous = aspace_enter(as); run = pd;
        bzero(&args,sizeof(args)); args.source = source;
        args.process_policy = PROC_CLONE_NEW_PROCESS; args.vm_policy = PROC_CLONE_SHARE_VM;
        args.resources = PROC_CLONE_COPY_STANDARD_HANDLES | PROC_CLONE_COPY_CWD;
        startup.entry = 0x00600000; startup.argument = data; startup.stack_base = stack; startup.stack_size = 2*PAGE;
        resident_generation = procmem_next_gen();
        CHECK(proc_clone(&args,&tx) == E_OK && proc_clone_prepare_startup(tx,&startup) == E_OK &&
              proc_clone_retain_startup_stack(tx) == E_OK &&
              proc_clone_prepare_outputs(tx,data+16,data+20) == E_OK, "construct/process/VM/thread/context/leases before publication");
        watched.process = proc_clone_process(tx);
        watched.expected = mode == 0 || mode >= 12 ? 0x2468 : mode == 1 ? 0x3579 : mode == 10 ? 0x468a : 0xffff;
        watched.fail = mode == 8;
        CHECK(proc_clone_observe_startup(tx,&observer,&watched) == (mode == 8 ? ENSMEM : E_OK),
              "register retained PROCESS lifecycle observer or fail before publication");
        if (mode == 9) {
            collision = (VADDR)(uintptr_t)kproc_live_pd(watched.process);
            CHECK(aspace_load_private(as, collision, PAGE, ASPACE_PROT_USER,
                  "owned", 6), "place an unrelated owned mapping in the compatibility range");
        }
        if (mode == 3) CHECK(kproc_mprot(pd,data,PAGE,PROT_READ) == E_OK, "invalidate publication output before final preflight");
        if (mode == 4) proc_clone_test_cancel_publication();
        if (mode == 5) ksched_test_fail_clone_slot();
        if (mode == 6 || mode == 7) kproc_test_fail_clone_compat(mode-5);
        if (mode == 11) proc_dispose_process(source); /* execution association retains resources */
        bzero(&active_clone,sizeof(active_clone));
        if (mode >= 12) {
            active_clone.enabled = TRUE; active_clone.scenario = mode - 12;
            active_clone.parent = kproc_retain_live(watched.process);
            active_clone.old_pd = kproc_live_pd(watched.process);
        }
        bzero(exit_handoff_stages,sizeof(exit_handoff_stages)); exit_handoff_watch = TRUE;
        result = proc_clone_exec(&tx,&done);
        active_clone.enabled = FALSE;
        exit_handoff_watch = FALSE;
        CHECK(run == pd && aspace_is_current(as) && (mode == 11 ? !kproc_active_space(source) : kproc_active_space(source) == space) && kproc_ref_has_basepage(source),
              "child exit/cancellation preserves parent PROCESS and shared ASPACE");
        CHECK(aspace_copy_from_user(as,readback,data+16,sizeof(readback)), "parent can still access shared output memory");
        if (executed) {
            CHECK(result == E_OK && !tx && done.pid != kproc_ref_pid(source) && done.tid && done.tid != kthread_tid(parent_execution) &&
                  done.status == watched.expected,
                  "actual constructed clone RET/Pterm/fault returns its independent identity and expected status");
            CHECK(readback[0] == (ULONG)done.pid && readback[1] == done.tid,
                  "publication commits both identities before dispatch");
            CHECK(aspace_copy_from_user(as,readback,data,sizeof(readback)) && readback[1] == 0x600dcafe,
                  "real initial CPU/FPU state and three cooperative yield patterns pass");
            CHECK(exit_handoff_stages[0] == 1 + active_clone.successes && exit_handoff_stages[1] == 1 + active_clone.successes &&
                  exit_handoff_stages[2] == 1 + active_clone.successes && exit_handoff_stages[3] == 1 + active_clone.successes,
                  "one THREAD/PROCESS handoff and syscall reclamation for constructed clone");
        } else {
            CHECK(result == (mode == 3 ? EIMBA : mode == 4 ? EAGAIN : mode == 8 ? EINVAL : mode == 11 ? EINVFN : ENSMEM) && !done.pid && !done.tid &&
                  readback[0] == outputs[0] && readback[1] == outputs[1] && !exit_handoff_stages[0],
                  "preflight failure or final prepublication cancellation exposes no output or child execution");
            CHECK(aspace_copy_from_user(as,&frame_after,stack+2*PAGE-8,sizeof(frame_after)) && frame_after == frame_before,
                  "failed preflight and prepublication cancellation leave even the return slot unchanged");
            CHECK(mode == 4 ? tx == NULL : tx != NULL, "cancellation consumes handle; failed preflight preserves ownership until abort");
            if (collision) {
                char value[6];
                CHECK(aspace_copy_from_user(as,value,collision,sizeof(value)) && !memcmp(value,"owned",6),
                      "failed compatibility preparation cannot replace an unrelated mapping");
                CHECK(aspace_unmap_private(as,collision,PAGE), "release caller-owned collision fixture");
            }
            proc_clone_abort(&tx);
        }
        CHECK(watched.registered == (mode == 8 ? 0 : 1) && watched.released == watched.registered &&
              watched.completed == (executed ? 1 : 0),
              "one completion observation for executed PROCESS, none for rejected/cancelled construction");
        if (mode == 10) CHECK(procmem_free_ownerless_since(resident_generation) == 2,
              "Ptermres keeps only its PD/environment blocks, with no live process or execution resources");
        if (mode >= 12) {
            UWORD j;
            CHECK(active_clone.parent_yields == 3 && !kproc_ref_has_basepage(active_clone.parent),
                  "creator continued independently and completed before returning to group root");
            if (active_clone.scenario == 0 || active_clone.scenario == 3 || active_clone.scenario == 11)
                CHECK(active_clone.child_progress, "blocked creator permits independent child execution");
            if (active_clone.scenario == 2) CHECK(active_clone.reused, "orphan survived actual parent PD reuse");
            for (j = 0; j < KSCHED_LIMIT; j++) if (active_clone.observers[j]) {
                PROC_CLONE_COMPLETION observed = { 0, 0, 0 };
                if (active_clone.ids[j].pid) CHECK(proc_clone_observer_state(active_clone.observers[j]) == PROC_CLONE_OBSERVER_REAPED &&
                    proc_clone_observer_result(active_clone.observers[j],&observed) && observed.pid == active_clone.ids[j].pid &&
                    observed.tid == active_clone.ids[j].tid && observed.status == (active_clone.scenario == 3 || (active_clone.scenario == 4 && (j & 1)) ? 0xffff : active_clone.scenario == 11 ? 0x468a : 0x2468),
                    "each independent async observer survives wait/orphan/root reclamation with its own status");
                proc_clone_observer_release(&active_clone.observers[j]);
                proc_clone_observer_release(&active_clone.observers[j]);
            }
            kproc_release(active_clone.parent);
            if (active_clone.scenario == 11) CHECK(procmem_free_ownerless_since(resident_generation) == 2,
                  "active Ptermres retains only its two resident compatibility allocations");
        }
        CHECK(kproc_uheap_free(pd,stack) == E_OK, "scheduler root reap released final durable user-stack hold");
        run = original; aspace_leave(previous);
        kthread_destroy(parent_execution);
        proc_dispose_process(source); kproc_release(source);
        same(&baseline,"constructed clone normal/fault execution and complete root reclamation");
    }
    kcprintf("x86-64 active-group clone: %s\n", failures == before ? "PASS" : "FAIL");
    kcprintf("x86-64 executable clone: %s\n", failures == before ? "PASS" : "FAIL");
}

#endif
static void test_return_bridge(void)
{
    UWORD mode;
    int before = failures;
    for (mode = 0; mode < 2; mode++) {
        SNAP baseline;
        PD *pd, *peer_pd;
        KPROC *source, *peer;
        KASPACE *space;
        ASPACE *as;
        X32_RETURN_BRIDGE *bridge = NULL, *second = NULL;
        PROC_CLONE_TRANSACTION *tx = NULL;
        PROC_CLONE_RESULT result = PROC_CLONE_RESULT_INIT;
        PROC_CLONE_ARGS args;
        KASPACE_LEASE *stack_hold = NULL;
        KTHREAD *a, *b;
        VADDR rsp;
        UQUAD return_pc;
        UWORD status;
        LONG rc;
        UBYTE code[] = { 0xb8, 0x68, 0x24, 0, 0, 0xc3 }; /* EAX=0x2468; ret */
        snap(&baseline);
        pd = new_basepage(); peer_pd = new_basepage();
        if (!pd || !peer_pd) return;
        set_owner(peer_pd, peer_pd); set_owner(USERPTR_TO_PTR(peer_pd->p_env), peer_pd);
        pd->p_tbase = PTR_TO_USERPTR(pd + 1); pd->p_tlen = sizeof(code);
        if (mode) { code[0] = 0x0f; code[1] = 0x0b; } /* UD2 entry fault */
        memcpy(pd + 1, code, sizeof(code));
        CHECK(kproc_prepare_user(pd, run), "prepare existing launcher for bridge execution");
        source = kproc_retain(pd); peer = kproc_retain(peer_pd);
        space = kproc_active_space(source); as = kaspace_backend(space);
        CHECK(x32_return_bridge_acquire(space, &bridge) == E_OK &&
              x32_return_bridge_acquire(space, &second) == E_OK && second == bridge,
              "kernel bridge has one shared immutable mapping identity");
        return_pc = x32_return_bridge_address(bridge);
        CHECK(return_pc && x32_return_bridge_valid(bridge) &&
              !aspace_unmap_private(as, return_pc, PAGE), "bridge is protected beyond publication sealing");
        a = kthread_construct_identity(source, space); b = kthread_construct_identity(source, space);
        {
            VADDR custom_stack = kproc_uheap_alloc(pd, PAGE);
            KASPACE_LEASE *one = NULL, *two = NULL;
            CHECK(custom_stack && kproc_demand_page(pd, custom_stack, ASPACE_PROT_WRITE) == 1,
                  "back ordinary user-stack reservation before lifetime acquisition");
            CHECK(kaspace_stack_hold_acquire(space, custom_stack, PAGE, &one) == E_OK &&
                  kaspace_stack_hold_acquire(space, custom_stack, PAGE, &two) == E_OK &&
                  kthread_take_stack_hold(a, &one) && kthread_take_stack_hold(b, &two) && !one && !two,
                  "two THREADs independently own one durable user-stack reservation");
            CHECK(kproc_uheap_free(pd, custom_stack) != E_OK, "first live stack owner prevents Mfree");
            kthread_destroy(b); b = NULL;
            CHECK(kproc_uheap_free(pd, custom_stack) != E_OK && kproc_ref_has_basepage(source),
                  "last sibling retains usable PROCESS and stack after first detach");
            CHECK(x32_return_bridge_process_eligible(source, a), "sole THREAD bridge policy after sibling detaches");
            kthread_destroy(a); a = NULL;
            CHECK(kproc_uheap_free(pd, custom_stack) == E_OK, "last THREAD releases hold without double-freeing caller reservation");
        }
        a = kthread_construct_identity(source, space); b = kthread_construct_identity(source, space);
        CHECK(a && b && !x32_return_bridge_process_eligible(source, a),
              "single-process bridge refuses live sibling membership");
        kthread_destroy(b);
        CHECK(x32_return_bridge_process_eligible(source, a), "bridge policy recognizes a sole THREAD identity");
        kthread_destroy(a); /* ordinary launcher still uses its legacy implicit context */
        bzero(&args, sizeof(args)); args.source = source; args.prepared_process = peer;
        args.process_policy = PROC_CLONE_PREPARED_PROCESS; args.vm_policy = PROC_CLONE_SHARE_VM;
        CHECK(proc_clone(&args, &tx) == E_OK && proc_clone_commit(&tx, &result) == E_OK,
              "independent PROCESS retains VM while original process executes");
        rsp = kproc_user_stack(pd);
        CHECK(kproc_demand_page(pd, rsp, ASPACE_PROT_WRITE) == 1 &&
              aspace_copy_to_user(as, rsp, &return_pc, sizeof(return_pc)),
              "kernel installs selected normal-return PC into existing launch stack");
        CHECK(kaspace_lease_acquire(space, rsp - 128, 136, ASPACE_PROT_USER | ASPACE_PROT_WRITE,
              &stack_hold) == E_OK && kaspace_lease_hold_private(stack_hold),
              "retain private launch stack independently through completion");
        bzero(exit_handoff_stages, sizeof(exit_handoff_stages)); exit_handoff_watch = TRUE;
        rc = proc_start_scheduled(pd, TRUE);
        exit_handoff_watch = FALSE;
        CHECK(rc == (mode ? 0xffff : 0x2468), "real RET/entry-fault status reaches the original launcher");
        CHECK(exit_handoff_stages[0] == 1 && exit_handoff_stages[1] == 1 &&
              exit_handoff_stages[2] == 1 && exit_handoff_stages[3] == 1,
              "bridge reuses once-only safe launcher finalization off the child syscall stack");
        CHECK(kproc_ref_status(source, &status) && status == (mode ? 0xffff : 0x2468) &&
              !kproc_ref_has_basepage(source) && kproc_ref_has_basepage(peer),
              "completion retires only the executing PROCESS identity");
        CHECK(kproc_active_space(peer) == space && x32_return_bridge_valid(second) &&
              aspace_translate(as, rsp, NULL, NULL) && !kproc_ref_status(peer, &status),
              "peer PROCESS and retained stack/mappings survive original completion");
        kaspace_lease_release(&stack_hold);
        x32_return_bridge_release(&bridge); x32_return_bridge_release(&bridge);
        CHECK(x32_return_bridge_valid(second), "first bridge release preserves another owner");
        x32_return_bridge_release(&second);
        CHECK(!aspace_translate(as, return_pc, NULL, NULL), "last bridge owner releases code page once");
        proc_dispose_process(peer); proc_clone_release(&result); proc_clone_release(&result);
        kproc_release(peer); kproc_release(source);
        same(&baseline, "bridge normal/fault execution, independent VM owner and final release");
    }
    kcprintf("x86-64 trusted return bridge: %s\n", failures == before ? "PASS" : "FAIL");
}

static void test_proc_clone_startup(void)
{
    SNAP baseline, prepared;
    PD *pd;
    KPROC *source;
    KASPACE *space;
    ASPACE *backend;
    KTHREAD *identity;
    PROC_CLONE_ARGS args;
    THREAD_STARTUP_ARGS startup, bad;
    PROC_CLONE_TRANSACTION *tx = NULL, *held;
    PROC_CLONE_RESULT result = PROC_CLONE_RESULT_INIT;
    const THREAD_STARTUP_DRAFT *draft;
    const X32_THREAD_STARTUP_DRAFT *image;
    UQUAD rsp, old_slot = 0xa59b2e4c13579bdfULL, observed, regs[15];
    VADDR stack;
    UWORD stage, i;
    int before = failures;

    snap(&baseline);
    pd = new_basepage();
    if (!pd) return;
    set_owner(pd, pd); set_owner(USERPTR_TO_PTR(pd->p_env), pd);
    CHECK(kproc_prepare_user(pd, run), "prepare startup source mappings");
    source = kproc_retain(pd); space = kproc_active_space(source); backend = kaspace_backend(space);
#if CONF_WITH_MMAP
    stack = kproc_mmap(pd, 0, 2 * PAGE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#else
    stack = kproc_uheap_alloc(pd, 2 * PAGE);
#endif
    CHECK(stack && stack < USER_VA_LIMIT, "reserve internal startup test stack");
    if (!stack || stack >= USER_VA_LIMIT) {
        proc_dispose_process(source); kproc_release(source); return;
    }
    for (i = 0; i < 2; i++) {
        CHECK(CONF_WITH_DEMAND_PAGING ? kproc_demand_page(pd, stack + i * PAGE,
              ASPACE_PROT_USER | ASPACE_PROT_WRITE) == 1 : aspace_translate(backend, stack + i * PAGE, NULL, NULL),
              "caller supplies resident writable startup stack mappings");
    }
    startup.entry = (VADDR)(uintptr_t)pd;
    startup.argument = 0xfedcba98UL; startup.stack_base = stack; startup.stack_size = 2 * PAGE;
    rsp = ((stack + 2 * PAGE) & ~15ULL) - 8;
    CHECK(aspace_copy_to_user(backend, rsp, &old_slot, sizeof(old_slot)), "seed prospective return slot");
    bzero(&args, sizeof(args)); args.source = source;
    args.process_policy = PROC_CLONE_SAME_PROCESS; args.vm_policy = PROC_CLONE_SHARE_VM;
    CHECK(proc_clone(&args, &tx) == E_OK, "construct non-runnable identity before startup planning");
    {
        X32_RETURN_BRIDGE *bridge = NULL;
        CHECK(x32_return_bridge_acquire(space, &bridge) == E_OK && x32_return_bridge_valid(bridge),
              "provision trusted return bridge without arbitrary executable input");
        CHECK(!aspace_protect_private(backend, x32_return_bridge_address(bridge), PAGE,
              ASPACE_PROT_USER | ASPACE_PROT_WRITE), "immutable bridge identity rejects permission replacement");
        x32_return_bridge_release(&bridge);
        /* Like ordinary unmap, released bridge leaf pages leave reusable table
         * capacity until final ASPACE destruction. Prime that capacity before
         * per-attempt assertions; the outer full-lifetime baseline is exact. */
    }
    snap(&prepared);
    bad = startup; bad.stack_size = 136;
    CHECK(proc_clone_prepare_startup(tx, &bad) == EINVAL && !proc_clone_startup(tx),
          "startup frame rejects insufficient red zone/alignment space");
    same(&prepared, "invalid startup frame acquires no context");
    for (stage = 1; stage <= 3; stage++) {
        thread_startup_x32_test_fail_after(stage);
        CHECK(proc_clone_prepare_startup(tx, &startup) == ENSMEM && !proc_clone_startup(tx),
              "partial startup context/stack construction rolls back");
        same(&prepared, "startup metadata and both independent stack owners roll back");
    }
    for (stage = 1; stage <= 4; stage++) {
        kaspace_test_fail_lease_after(stage);
        CHECK(proc_clone_prepare_startup(tx, &startup) == ENSMEM && !proc_clone_startup(tx),
              "each startup mapping lease failure rolls back both stacks");
        same(&prepared, "failed lease acquisition preserves PROCESS and transaction baseline");
    }
    kthread_stack_test_fail_after(1);
    CHECK(proc_clone_prepare_startup(tx, &startup) == ENSMEM && !proc_clone_startup(tx),
          "existing scheduler-stack allocation failure is recoverable");
    kthread_stack_test_fail_after(0);
    x86_64_pmem_test_fail_after(1);
    CHECK(proc_clone_prepare_startup(tx, &startup) == ENSMEM && !proc_clone_startup(tx),
          "physical allocation failure cannot publish a startup image");
    x86_64_pmem_test_fail_after(0);
    same(&prepared, "startup physical failure preserves the construction transaction");
    CHECK(proc_clone_prepare_startup(tx, &startup) == E_OK, "build internal x32 CPU/FPU startup draft");
    draft = proc_clone_startup(tx); image = thread_startup_image(draft);
    CHECK(draft && image && thread_startup_validate(draft) == E_OK, "inspect immutable unpublished startup image");
    if (image) {
        CHECK(image->user.rip == startup.entry && image->user.rdi == 0xfedcba98ULL &&
              image->user.rsp == rsp && (rsp & 15) == 8 && rsp - stack >= 128 &&
              image->return_pc == USER_VA_LIMIT - PAGE && image->user.cr3 == aspace_root(backend),
              "x32 function-entry frame and high-bit argument are normalized");
        CHECK(!((uintptr_t)image->user.fpu & 15), "FXSAVE startup image is 16-byte aligned");
        CHECK(image->user.rflags == 0x202 && image->user.cs == X86_64_USER_CODE_SEL &&
              image->user.ss == X86_64_USER_DATA_SEL && !image->user.fs_base && !image->user.gs_base,
              "initial privilege, interrupt and TLS state contains no kernel context");
        memcpy(regs, &image->user, sizeof(regs));
        for (i = 0; i < 15; i++)
            CHECK(regs[i] == (i == 5 ? 0xfedcba98ULL : 0), "all other user GPRs start zero");
        for (i = 0; i < 512; i++) {
            UBYTE expected = i == 0 ? 0x7f : i == 1 ? 3 : i == 24 ? 0x80 : i == 25 ? 0x1f : i == 28 ? 0xbf : i == 29 ? 0xff : 0;
            CHECK(image->user.fpu[i] == expected, "deterministic architectural FXSAVE initial image");
        }
        __asm__ volatile ("movq %%rsp,%0" : "=r"(observed));
        CHECK(image->syscall_physical && image->scheduler_stack.physical &&
              image->syscall_physical != image->scheduler_stack.physical &&
              !x86_64_memtest_kstack_contains(image->syscall_physical, observed) &&
              !x86_64_memtest_kstack_contains(image->scheduler_stack.physical, observed),
              "draft owns two separate never-executed stacks");
    }
    CHECK(aspace_copy_from_user(backend, &observed, rsp, sizeof(observed)) && observed == old_slot,
          "startup planning never writes a prospective user return frame");
    held = tx; snap(&prepared);
    CHECK(proc_clone_prepare_startup(tx, &startup) == EINVAL && proc_clone_startup(tx) == draft,
          "startup draft cannot be overwritten");
    CHECK(proc_clone_commit(&tx, &result) == EINVFN && tx == held && !result.thread && !result.owned_process,
          "a draft is never a runnable publication token or identity commit");
    same(&prepared, "unready publication changes no ownership or output");
    {
        PROC_CLONE_TRANSACTION *factory_tx = NULL;
        PROC_CLONE_ARGS factory_args = args;
        PD *saved_run = run;
        SNAP factory_baseline;
        snap(&factory_baseline);
        factory_args.process_policy = PROC_CLONE_NEW_PROCESS;
        run = pd; /* production factory still requires the current source */
        CHECK(proc_clone(&factory_args, &factory_tx) == E_OK, "construct owned PROCESS for startup cancellation");
        run = saved_run;
        CHECK(proc_clone_prepare_startup(factory_tx, &startup) == E_OK,
              "owned PROCESS transaction can retain an unpublished backend draft");
        CHECK(proc_clone_commit(&factory_tx, &result) == EINVFN && !result.thread,
              "factory startup also requires the missing publication gates");
        proc_clone_abort(&factory_tx); proc_clone_abort(&factory_tx);
        same(&factory_baseline, "startup abort also disposes factory-owned PROCESS resources");
    }
    {
        PROC_CLONE_TRANSACTION *sealed_tx = NULL;
        STARTUP_OBSERVER_TEST observer_test = { 0, 0, TRUE };
        THREAD_STARTUP_OBSERVER observer = { startup_observer_acquire, startup_observer_release };
        VADDR output = kproc_uheap_alloc(pd, PAGE);
        ULONG sentinels[2] = { 0x12345678, 0x9abcdef0 }, outputs[2] = { 0, 0 };
        CHECK(output && (CONF_WITH_DEMAND_PAGING ? kproc_demand_page(pd, output,
              ASPACE_PROT_USER | ASPACE_PROT_WRITE) == 1 : TRUE), "back output destinations before preparation");
        CHECK(aspace_copy_to_user(backend, output, sentinels, sizeof(sentinels)), "seed publication sentinels");
        CHECK(proc_clone(&args, &sealed_tx) == E_OK &&
              proc_clone_prepare_startup(sealed_tx, &startup) == E_OK, "prepare lease-seal transaction");
        snap(&prepared);
        for (stage = 1; stage <= 2; stage++) {
            kaspace_test_fail_lease_after(stage);
            CHECK(proc_clone_prepare_outputs(sealed_tx, output, output + 4) == ENSMEM,
                  "each output lease failure preserves empty output ownership");
            same(&prepared, "partial output lease rollback");
        }
        CHECK(proc_clone_prepare_outputs(sealed_tx, output, USER_VA_LIMIT - 2 * PAGE) == EIMBA,
              "unmapped final output rejects all destinations");
        same(&prepared, "invalid output releases earlier acquired destination");
        CHECK(proc_clone_prepare_outputs(sealed_tx, output, output + 4) == E_OK, "prepare both writable identity destinations");
        snap(&prepared);
        CHECK(proc_clone_observe_startup(sealed_tx, &observer, &observer_test) == ENSMEM && !observer_test.acquired,
              "observer failure leaves draft prepared and unregistered");
        same(&prepared, "observer failure does not leak publication resources");
        observer_test.fail = FALSE;
        CHECK(proc_clone_observe_startup(sealed_tx, &observer, &observer_test) == E_OK &&
              proc_clone_seal_startup(sealed_tx) == E_OK, "register observer then atomically seal all startup/output ranges");
        CHECK(proc_clone_seal_startup(sealed_tx) == EINVAL &&
              proc_clone_prepare_outputs(sealed_tx, output, output + 4) == EINVAL,
              "sealed startup rejects duplicate seal or output replacement");
        CHECK(!aspace_protect_private(backend, stack, 2 * PAGE, ASPACE_PROT_USER) &&
              kproc_uheap_free(pd, output) != E_OK, "sealed frame and outputs reject protection/free without mutation");
#if CONF_WITH_MMAP
        CHECK(kproc_munmap(pd, stack, 2 * PAGE) == EACCDN, "sealed startup rejects whole Munmap before metadata changes");
#endif
        CHECK(thread_startup_validate(proc_clone_startup(sealed_tx)) == E_OK &&
              proc_clone_commit(&sealed_tx, &result) == EINVFN && !result.thread,
              "even sealed observed startup lacks a trusted return bridge and stays unpublished");
        CHECK(aspace_copy_from_user(backend, outputs, output, sizeof(outputs)) &&
              !memcmp(outputs, sentinels, sizeof(outputs)), "failed publication leaves every identity output unchanged");
        proc_clone_abort(&sealed_tx); proc_clone_abort(&sealed_tx);
        CHECK(observer_test.acquired == 1 && observer_test.released == 1 &&
              kproc_uheap_free(pd, output) == E_OK, "cancellation unseals before exactly-once observer/resource cleanup");
    }
    {
        PROC_CLONE_TRANSACTION *held_stack_tx = NULL;
        CHECK(proc_clone(&args, &held_stack_tx) == E_OK &&
              proc_clone_prepare_startup(held_stack_tx, &startup) == E_OK &&
              proc_clone_retain_startup_stack(held_stack_tx) == E_OK,
              "transfer durable execution-stack hold into non-runnable KTHREAD");
        CHECK(proc_clone_retain_startup_stack(held_stack_tx) == EACCDN,
              "duplicate stack transfer preserves the original owner");
#if CONF_WITH_MMAP
        CHECK(kproc_munmap(pd, stack, 2 * PAGE) == EACCDN &&
              kproc_mprot(pd, stack, 2 * PAGE, PROT_READ) == EACCDN,
              "execution hold rejects unmap/protection outside a short sealed window");
#else
        CHECK(kproc_uheap_shrink(pd, stack, PAGE) != E_OK && kproc_uheap_free(pd, stack) != E_OK,
              "execution hold rejects free/shrink while a THREAD retains its stack");
#endif
        CHECK(thread_startup_validate(proc_clone_startup(held_stack_tx)) == E_OK,
              "blocked stack mutations preserve preparation identity");
        proc_clone_abort(&held_stack_tx); proc_clone_abort(&held_stack_tx);
        CHECK(thread_startup_validate(draft) == E_OK,
              "stack-owner cancellation preserves independent preparations");
    }
    {
        X32_RETURN_BRIDGE *bridge = NULL;
        void *raw_image = NULL;
        X32_THREAD_STARTUP_DRAFT *raw;
        X32_THREAD_STACK_OWNERS owners = { { 0, 0 }, { 0, 0 } };
        CHECK(x32_return_bridge_acquire(space, &bridge) == E_OK &&
              thread_startup_arch_prepare(backend, &startup, bridge, &raw_image) == E_OK,
              "prepare an owned backend image for explicit stack transfer");
        raw = raw_image;
        if (raw) {
            UQUAD sp;
            __asm__ volatile ("movq %%rsp,%0" : "=r"(sp));
            CHECK(!x86_64_memtest_kstack_contains(raw->syscall_physical, sp) &&
                  !x86_64_memtest_kstack_contains(raw->scheduler_stack.physical, sp),
                  "stack transfer and cancellation run outside both owned execution stacks");
            CHECK(thread_startup_x32_take_stacks(raw, &owners) &&
                  !raw->syscall_physical && !raw->scheduler_stack.physical &&
                  !thread_startup_x32_take_stacks(raw, &owners), "two stack owners transfer once without publication");
            thread_startup_arch_abort(raw_image);
            CHECK(owners.syscall.physical && owners.scheduler.physical,
                  "backend cancellation cannot free transferred stacks");
            thread_startup_x32_release_syscall(&owners); thread_startup_x32_release_syscall(&owners);
            CHECK(owners.scheduler.physical && !owners.syscall.physical,
                  "safe syscall reclamation preserves scheduler-stack owner");
            thread_startup_x32_release_scheduler(&owners); thread_startup_x32_release_scheduler(&owners);
        }
        x32_return_bridge_release(&bridge);
    }
    identity = kthread_construct_identity(source, space);
    CHECK(identity && !kthread_set_execution(identity, &startup, forbidden_startup_reclaim) && !kthread_start(identity),
          "controlled startup planning does not remove identity-only execution guards");
    kthread_destroy(identity);
#if CONF_WITH_MMAP
    CHECK(kproc_mprot(pd, stack, 2 * PAGE, PROT_READ) == E_OK, "ordinary Mprot revokes preparation leases");
    CHECK(thread_startup_validate(draft) == EIMBA, "draft recheck detects protection change");
    CHECK(kproc_mprot(pd, stack, 2 * PAGE, PROT_READ | PROT_WRITE) == E_OK &&
          thread_startup_validate(draft) == EIMBA, "restored permissions cannot revive a revoked lease");
    CHECK(kproc_munmap(pd, stack, 2 * PAGE) == E_OK, "ordinary Munmap can discard draft stack mapping");
    CHECK(kproc_mmap(pd, stack, 2 * PAGE, PROT_READ | PROT_WRITE,
          MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0) == (LONG)stack,
          "remap same address with identical logical permissions");
    for (i = 0; i < 2; i++)
        CHECK(kproc_demand_page(pd, stack + i * PAGE, ASPACE_PROT_WRITE) == 1,
              "back replacement mapping before stale lease revalidation");
    CHECK(thread_startup_validate(draft) == EIMBA &&
          proc_clone_commit(&tx, &result) == EIMBA && !result.thread,
          "same-address resident replacement cannot authorize stale publication");
    CHECK(kproc_munmap(pd, stack, 2 * PAGE) == E_OK, "release replacement mapping");
#else
    CHECK(aspace_protect_private(backend, stack, 2 * PAGE, ASPACE_PROT_USER), "change draft mapping protection");
    CHECK(thread_startup_validate(draft) == EIMBA, "eager draft detects protection change");
    CHECK(kproc_uheap_free(pd, stack) == E_OK, "release unleased eager draft stack mapping");
    CHECK(kproc_uheap_alloc(pd, 2 * PAGE) == stack && thread_startup_validate(draft) == EIMBA,
          "eager heap reuse cannot revive an old startup lease");
    CHECK(kproc_uheap_free(pd, stack) == E_OK, "release replacement eager heap");
#endif
    CHECK(thread_startup_validate(draft) == EIMBA, "removed mapping invalidates startup draft without authorizing execution");
    proc_clone_abort(&tx); proc_clone_abort(&tx);
    CHECK(!tx && kproc_ref_has_basepage(source), "safe cancellation reclaims both stacks without completing source");
    proc_dispose_process(source); kproc_release(source);
    same(&baseline, "startup planning, invalidation and complete rollback");
    kcprintf("x86-64 startup draft: %s\n", failures == before ? "PASS" : "FAIL");
}
#endif

static void test_kproc_pending_ownership(void)
{
    SNAP s;
    PD *pd;
    void *env;
    ASPACE *active;
    KASPACE *retained;
    KASPACE *second;
    LONG saved_value;
    VADDR heap_va;

    snap(&s);
    pd = new_basepage();
    if (!pd) return;
    env = USERPTR_TO_PTR(pd->p_env);
    set_owner(pd, pd);
    set_owner(env, pd);
    pd->p_dreg[0] = 0x12345678L;
    CHECK(kproc_prepare_user(pd, run), "prepare active space for independent owner");
    heap_va = kproc_uheap_alloc(pd, PAGE);
    CHECK(heap_va, "allocate ordinary heap in active space");
#if CONF_WITH_MMAP
    CHECK(kproc_mmap(pd, 0, PAGE, PROT_READ | PROT_WRITE,
          MAP_PRIVATE | MAP_ANONYMOUS, 0, 0) == PTOS_MMAP_BASE,
          "reserve mapping in retained active space");
#endif
    retained = kproc_test_retain_active(pd);
    second = kaspace_share(retained);
    CHECK(retained && second, "retain active space twice");
    proc_free_owned(pd);
    CHECK(procmem_size(pd) && procmem_size(env) && procmem_pinned(pd) &&
          retained && aspace_translate(kaspace_backend(retained), (VADDR)(uintptr_t)pd, NULL, NULL),
          "active mappings keep process blocks after logical cleanup");
    CHECK(retained && aspace_copy_from_user(kaspace_backend(retained), &saved_value,
          (VADDR)(uintptr_t)pd + offsetof(PD, p_dreg), sizeof(saved_value)) &&
          saved_value == 0x12345678L, "retained mapping preserves process data");
    CHECK(retained && kaspace_heap_contains(retained, heap_va),
          "ordinary heap reservation survives KPROC destruction");
#if CONF_WITH_DEMAND_PAGING
    saved_value = -1;
    CHECK(retained && kaspace_heap_demand_page(retained, heap_va, ASPACE_PROT_EXEC) == 0 &&
          !aspace_translate(kaspace_backend(retained), heap_va, NULL, NULL),
          "retained ordinary heap rejects execution before first touch");
    CHECK(retained && kaspace_heap_demand_page(retained, heap_va, 0) == 1 &&
          aspace_copy_from_user(kaspace_backend(retained), &saved_value,
                                heap_va, sizeof(saved_value)) && !saved_value,
          "retained ordinary heap faults a zero page after process destruction");
#endif
#if CONF_WITH_MMAP
    CHECK(retained && kaspace_mmap(retained)->bytes == PAGE &&
          kaspace_mmap(retained)->regions &&
          kaspace_mmap(retained)->regions->address == PTOS_MMAP_BASE,
          "retained active space preserves its mapping metadata");
    CHECK(retained && kaspace_mmap_demand_page(retained, PTOS_MMAP_BASE, ASPACE_PROT_EXEC) == 0 &&
          kaspace_mmap_demand_page(retained, PTOS_MMAP_BASE + PAGE, 0) == -1 &&
          !aspace_translate(kaspace_backend(retained), PTOS_MMAP_BASE, NULL, NULL),
          "retained reservations reject disallowed and unreserved faults");
    saved_value = -1;
    CHECK(retained && kaspace_mmap_demand_page(retained, PTOS_MMAP_BASE, 0) == 1 &&
          aspace_copy_from_user(kaspace_backend(retained), &saved_value,
                                PTOS_MMAP_BASE, sizeof(saved_value)) && !saved_value &&
          kaspace_mmap_demand_page(retained, PTOS_MMAP_BASE, 0) == 0,
          "retained anonymous reservation faults after process destruction");
#endif
    kaspace_put(retained);
    CHECK(procmem_size(pd) && procmem_size(env), "first independent release keeps mappings");
    kaspace_put(second);
    CHECK(!procmem_size(pd) && !procmem_size(env), "last release reclaims deferred blocks");
    same(&s, "independently retained active space and deferred blocks");

    snap(&s);
    pd = new_basepage();
    if (!pd) return;
    env = USERPTR_TO_PTR(pd->p_env);
    set_owner(pd, pd);
    set_owner(env, pd);
    CHECK(kproc_load_begin(pd, ISO_VA, PAGE), "pending image for independent owner");
    retained = kproc_test_retain_pending(pd);
    CHECK(retained != NULL, "retain pending image space independently");
    proc_free_owned(pd);
    CHECK(retained && aspace_translate(kaspace_backend(retained), ISO_VA, NULL, NULL),
          "private image remains mapped after owning process is destroyed");
    kaspace_put(retained);
    same(&s, "independently retained pending image ownership");

    snap(&s);
    pd = new_basepage();
    if (!pd) return;
    env = USERPTR_TO_PTR(pd->p_env);
    set_owner(pd, pd);
    set_owner(env, pd);
    kaspace_test_fail_next();
    CHECK(!kproc_load_begin(pd, ISO_VA, PAGE), "pending adoption allocation failure is reported");
    CHECK(!kproc_user_aspace(pd), "failed pending adoption publishes no active space");
    CHECK(!kproc_load_begin(pd, ISO_VA + 1, PAGE), "failed pending image mapping rolls back");
    CHECK(kproc_load_begin(pd, ISO_VA, PAGE), "pending image is created after failed mapping");
    kproc_load_enter(pd);
    kproc_load_leave();
    x86_64_pmem_test_fail_after(1);
    CHECK(!kproc_prepare_user(pd, run), "syscall stack allocation failure is reported");
    x86_64_pmem_test_fail_after(0);
    CHECK(!kproc_load_begin(pd, ISO_VA, PAGE), "stack failure preserves the pending image");
    CHECK(kproc_prepare_user(pd, run), "pending image transfers to active ownership");
    active = kproc_user_aspace(pd);
    CHECK(active && aspace_translate(active, ISO_VA, NULL, NULL), "transferred image remains mapped");
    proc_free_owned(pd);
    proc_free_owned(pd);
    same(&s, "pending address-space ownership");

    snap(&s);
    pd = new_basepage();
    if (!pd) return;
    env = USERPTR_TO_PTR(pd->p_env);
    set_owner(pd, pd);
    set_owner(env, pd);
    CHECK(kproc_load_begin(pd, ISO_VA, PAGE), "pending image for preparation rollback");
    x86_64_pmem_test_fail_after(2);
    CHECK(!kproc_prepare_user(pd, run), "preparation fails after stack allocation");
    x86_64_pmem_test_fail_after(0);
    CHECK(!kproc_load_begin(pd, ISO_VA, PAGE), "failed preparation marks the adopted image lost");
    CHECK(!kproc_user_aspace(pd), "failed preparation publishes no active space");
    proc_free_owned(pd);
    proc_free_owned(pd);
    same(&s, "pending address-space preparation rollback");

    snap(&s);
    pd = new_basepage();
    if (!pd) return;
    env = USERPTR_TO_PTR(pd->p_env);
    set_owner(pd, pd);
    set_owner(env, pd);
    kaspace_test_fail_next();
    CHECK(!kproc_prepare_user(pd, run), "fresh address-space adoption failure is reported");
    CHECK(!kproc_user_aspace(pd), "failed fresh adoption publishes no active space");
    CHECK(kproc_prepare_user(pd, run), "fresh preparation succeeds after failed adoption");
    proc_free_owned(pd);
    proc_free_owned(pd);
    same(&s, "fresh address-space adoption rollback");
}

/* ---- KPROC records and the real process lifecycle ------------------ */

#define FAKE_PDS 1000

static void test_parent_identity_reuse(void)
{
    SNAP s;
    PD *parent, *child, *probe;
    WORD old_pid;

    snap(&s);
    parent = kalloc(sizeof(*parent));
    child = kalloc(sizeof(*child));
    probe = kalloc(sizeof(*probe));
    CHECK(parent && child && probe, "allocate parent identity test basepages");
    if (!parent || !child || !probe) {
        kfree(probe);
        kfree(parent);
        kfree(child);
        return;
    }
    bzero(parent, sizeof(*parent));
    bzero(child, sizeof(*child));
    bzero(probe, sizeof(*probe));
    if (!kproc_create(parent)) {
        CHECK(FALSE, "create parent record");
        kfree(probe); kfree(child); kfree(parent);
        return;
    }
    if (!kproc_create(child)) {
        CHECK(FALSE, "create child record");
        kproc_destroy(parent);
        kfree(probe); kfree(child); kfree(parent);
        return;
    }
    old_pid = kproc_pid(parent);
    kproc_set_parent(child, parent);
    kproc_set_parent(child, parent); /* replacing the same identity must not leak */
    CHECK(kproc_notification_parent(child) == parent && kproc_parent_pid(child) == old_pid,
          "child resolves live parent identity");
    kproc_destroy(parent);
    CHECK(!kproc_notification_parent(child) && !kproc_parent_pid(child),
          "retired parent is not a notification target");
    kproc_test_pid_search(old_pid - 1, 1);
    CHECK(!kproc_create(probe), "child identity reference retains the parent's PID");
    kproc_test_pid_search(0, 0);
    CHECK(kproc_create(parent) && kproc_pid(parent) != old_pid,
          "replacement process reuses PD with a distinct PID");
    CHECK(!kproc_notification_parent(child) && !kproc_parent_pid(child),
          "PD reuse cannot redirect an orphan notification");
    kproc_destroy(child);
    kproc_test_pid_search(old_pid - 1, 1);
    CHECK(kproc_create(probe) && kproc_pid(probe) == old_pid,
          "last child release permits retired parent PID reuse");
    kproc_test_pid_search(0, 0);
    kproc_destroy(probe);
    kproc_destroy(parent);
    kfree(probe);
    kfree(child);
    kfree(parent);
    same(&s, "parent identity across PD reuse");
}

static void test_kproc(void)
{
    SNAP s;
    PD **pds;
    PD *candidate;
    KPROC *retained, *second;
    WORD retained_pid;
    UWORD status;
    char name[33];
    UQUAD number, previous, allocated;
    PROCESS_IDENTITY identity;
    ULONG i;

    snap(&s);
    previous = 0xffffffffULL;
    CHECK(kproc_test_number_next(&previous, &allocated) && allocated == 0x100000000ULL &&
          previous == allocated, "PROCESS number carries through its high word");
    previous = ~(UQUAD)0 - 1;
    CHECK(kproc_test_number_next(&previous, &allocated) && allocated == ~(UQUAD)0,
          "last PROCESS number can be allocated");
    allocated = 0x55;
    CHECK(!kproc_test_number_next(&previous, &allocated) && previous == ~(UQUAD)0 && allocated == 0x55,
          "PROCESS number exhaustion neither wraps nor modifies output");
    pds = kalloc(FAKE_PDS * sizeof(PD *));
    CHECK(pds != NULL, "pd vector");
    if (!pds)
        return;
    for (i = 0; i < FAKE_PDS; i++) {
        pds[i] = kalloc(sizeof(PD));
        if (!pds[i] || !kproc_create(pds[i]))
            break;
    }
    CHECK(i == FAKE_PDS, "KPROC records past the fixed pool");
    CHECK(kproc_count() == s.kprocs + FAKE_PDS, "KPROC count");
    retained = kproc_retain(pds[0]);
    second = kproc_retain(pds[0]);
    CHECK(retained && second && retained == second, "retain live process twice");
    retained_pid = kproc_pid(pds[0]);
    number = kproc_ref_number(retained);
    memset(&identity, 0, sizeof(identity));
    CHECK(number && kproc_identity_info(pds[1], &identity) == E_OK &&
          ((UQUAD)identity.number_low | ((UQUAD)identity.number_high << 32)) != number,
          "PROCESS identities are independent of caller and basepage address");
    kproc_set_executable_name(pds[0], "C:\\APPS/Original.Prg");
    memset(name, 0x5a, sizeof(name));
    CHECK(kproc_ref_name(retained, name, 12) == ERANGE && name[0] == 0x5a,
          "PROCESS name short buffer is untouched");
    CHECK(kproc_ref_name(retained, name, sizeof(name)) == 12 &&
          !strcmp(name, "Original.Prg"), "PROCESS basename snapshot");
    kproc_set_executable_name(pds[1], "D:/Other.ACC");
    CHECK(kproc_ref_name(retained, name, sizeof(name)) == 12 &&
          !strcmp(name, "Original.Prg"), "PROCESS metadata identity is independent");
    candidate = kalloc(sizeof(PD));
    CHECK(candidate != NULL, "PID exhaustion test basepage");
    if (candidate) {
        bzero(candidate, sizeof *candidate);
        candidate->p_uft[0] = 7;
        kproc_test_pid_search(retained_pid - 1, 1);
        CHECK(!kproc_create(candidate) && candidate->p_uft[0] == 7,
              "PID exhaustion rejects live identity without modifying basepage");
        kproc_test_pid_search(0, 0);
    }
    CHECK(kproc_ref_has_basepage(retained) && kproc_ref_pid(retained) == retained_pid,
          "retained process has live identity");
    CHECK(!kproc_ref_status(retained, &status), "live process has no exit status");
    kproc_complete(pds[0], 42);
    CHECK(kproc_ref_has_basepage(retained) && kproc_ref_status(retained, &status),
          "completion may precede basepage release");
    CHECK(kproc_begin_exit(pds[0]) && !kproc_begin_exit(pds[0]),
          "normal exit commits process cleanup once");
    kproc_destroy(pds[0]);
    CHECK(kproc_create(pds[0]), "reuse a retired PROCESS basepage address");
    memset(&identity, 0, sizeof(identity));
    CHECK(kproc_identity_info(pds[0], &identity) == E_OK &&
          ((UQUAD)identity.number_low | ((UQUAD)identity.number_high << 32)) != number &&
          identity.pid != retained_pid, "PD reuse cannot redirect PROCESS number or PID");
    kproc_destroy(pds[0]);
    kfree(pds[0]);
    pds[0] = NULL;
    CHECK(!kproc_ref_has_basepage(retained) && kproc_ref_pid(retained) == retained_pid &&
          kproc_ref_status(retained, &status) && status == 42,
          "retained completion survives record removal and basepage release");
    CHECK(kproc_ref_name(retained, name, sizeof(name)) == 12 &&
          !strcmp(name, "Original.Prg"), "PROCESS name survives PD reclamation");
    memset(&identity, 0, sizeof(identity));
    identity.number_low = (ULONG)number; identity.number_high = (ULONG)(number >> 32);
    identity.selector = PROCESS_IDENTITY_NUMBER;
    CHECK(kproc_ref_number(retained) == number &&
          kproc_identity_info(NULL, &identity) == E_OK && identity.pid == retained_pid,
          "retained generic PROCESS identity survives PD reuse and reclamation");
    if (candidate) {
        kproc_test_pid_search(retained_pid - 1, 1);
        CHECK(!kproc_create(candidate), "retired identity reserves its PID");
        kproc_test_pid_search(0, 0);
        kfree(candidate);
    }
    kproc_release(retained);
    CHECK(kproc_ref_status(second, &status) && status == 42,
          "second reference retains completed process");
    kproc_release(second);
    {
        PD *replacement=kalloc(sizeof(PD));
        CHECK(replacement!=NULL,"PID reuse basepage");
        if (replacement) {
            PROCESS_IDENTITY fresh;
            UQUAD saved;
            ULONG count=kproc_count();
            bzero(replacement,sizeof(*replacement));
            saved=kproc_test_number_cursor(~(UQUAD)0);
            replacement->p_uft[0]=7;
            CHECK(!kproc_create(replacement) && replacement->p_uft[0]==7 && kproc_count()==count,
                  "full number exhaustion rejects actual construction without side effects");
            CHECK(kproc_test_number_cursor(saved)==~(UQUAD)0,"restore unissued injected exhaustion cursor");
            bzero(replacement,sizeof(*replacement));
            if (saved<0xffffffffULL) (void)kproc_test_number_cursor(0xffffffffULL);
            kproc_test_pid_search(retained_pid-1,1);
            CHECK(kproc_create(replacement),"PID reuse after final identity reference");
            memset(&fresh,0,sizeof(fresh));
            CHECK(kproc_identity_info(replacement,&fresh)==E_OK && fresh.pid==retained_pid &&
                  fresh.number_high && ((UQUAD)fresh.number_low|((UQUAD)fresh.number_high<<32))>number,
                  "real construction crosses low word and recycled PID gets a new PROCESS number");
            kproc_test_pid_search(0,0);
            kproc_destroy(replacement); kfree(replacement);
            CHECK(kproc_count()==count,"number exhaustion and PID reuse restore resource baseline");
        }
    }
    identity.pid = 0; identity.selector = PROCESS_IDENTITY_NUMBER;
    CHECK(kproc_identity_info(NULL, &identity) == ESRCH,
          "last reference release retires generic PROCESS lookup without an alias");
    for (i = 1; i < FAKE_PDS && pds[i]; i++) {
        kproc_destroy(pds[i]);
        kproc_destroy(pds[i]);                  /* second destroy is a no-op */
        kfree(pds[i]);
    }
    kfree(pds);
    same(&s, "kproc");
}

#define LIFECYCLES 150

/* The environment and TPA of a process need not be adjacent: with the
 * window fragmented so they are far apart, only the two blocks themselves
 * validate as user memory, not the other process's page between them. */
static void test_kproc_ranges(void)
{
    SNAP s;
    PROCMEM_STATS st;
    void **v;
    ULONG i, n = 0;
    PD *pd, *saved = run;
    UQUAD env, tpa, gap;

    snap(&s);
    procmem_stats(&st);
    v = kalloc(st.total_pages * sizeof(void *));
    CHECK(v != NULL, "vector for fragmentation");
    if (!v)
        return;
    while (n < st.total_pages && (v[n] = procmem_alloc(PAGE, 0)) != NULL)
        n++;
    CHECK(n >= 6, "enough window pages to fragment");
    if (n >= 6) {
        procmem_free(v[n - 4]);          /* hole for the environment */
        procmem_free(v[n - 1]);          /* hole for the basepage */
        pd = new_basepage();
        CHECK(pd != NULL, "basepage in the fragmented window");
        if (pd) {
            env = (UQUAD)pd->p_env;
            tpa = (UQUAD)(uintptr_t)pd;
            gap = (UQUAD)(uintptr_t)v[n - 3];
            CHECK(env == (UQUAD)(uintptr_t)v[n - 4] && tpa == (UQUAD)(uintptr_t)v[n - 1],
                  "fragmented placement as arranged");
            run = pd;
            CHECK(kproc_validate_user_range(env, 8), "environment validates");
            CHECK(kproc_validate_user_range(tpa, sizeof(PD)), "basepage validates");
            CHECK(!kproc_validate_user_range(gap, 8), "page between them does not");
            CHECK(!kproc_validate_user_range(env, tpa - env + 8), "range spanning the gap does not");
            run = saved;
            Mfree(pd);                          /* releases what it inherited */
            Mfree((void *)(uintptr_t)env);
        }
    }
    for (i = 0; i < n; i++)
        if (i + 4 != n && i + 1 != n)           /* the two freed above */
            procmem_free(v[i]);
    kfree(v);
    run = saved;
    same(&s, "fragmented kproc ranges");
}

static void test_lifecycle(void)
{
    SNAP s;
    ULONG i, k, failed = 0;
    PD *pd;
    void *env;

    snap(&s);

    /* create, launch-prepare and exit: the Pterm() path frees every block
     * the process owns, its KPROC record and its whole address space */
    for (i = 0; i < LIFECYCLES; i++) {
        pd = new_basepage();
        if (!pd)
            break;
        env = USERPTR_TO_PTR(pd->p_env);
        set_owner(pd, pd);
        set_owner(env, pd);
        if (!kproc_prepare_user(pd, run) || !kproc_user_root(pd)) {
            CHECK(FALSE, "launch preparation");
            proc_free_owned(pd);
            break;
        }
        proc_free_owned(pd);                  /* what ixterm() does */
        proc_free_owned(pd);                  /* exactly once: now a no-op */
    }
    CHECK(i == LIFECYCLES, "process create/exit cycles");
    same(&s, "process lifecycle");

    /* a basepage whose public fields were rewritten before launch (it is
     * writable until then) cannot map anything beyond what was recorded */
    pd = new_basepage();
    if (pd) {
        UQUAD hi = pd->p_hitpa;
        UQUAD envv = pd->p_env;
        void *other = procmem_alloc(PAGE, PROCMEM_ZERO);

        env = USERPTR_TO_PTR(pd->p_env);
        pd->p_hitpa = hi + 4 * PAGE;            /* reach into a neighbour */
        CHECK(!kproc_prepare_user(pd, run), "extended p_hitpa refused");
        pd->p_hitpa = hi;
        pd->p_env = (ULONG)(uintptr_t)other;    /* point at someone else's block */
        CHECK(!kproc_prepare_user(pd, run), "redirected p_env refused");
        pd->p_env = envv;
        CHECK(!kproc_user_root(pd), "refused launches left no address space");
        CHECK(kproc_prepare_user(pd, run), "unmodified basepage still launches");
        set_owner(pd, pd);
        set_owner(env, pd);
        proc_free_owned(pd);                  /* maps, references, blocks */
        procmem_free(other);
    }

    /* the environment freed and its address reused between creating the
     * basepage and launching it: the recorded range now names someone
     * else's block, which must not be mapped into the process */
    pd = new_basepage();
    if (pd) {
        void *e = USERPTR_TO_PTR(pd->p_env);
        void *stranger;

        CHECK(Mfree(e) == 0, "free environment before launch");
        stranger = procmem_alloc(PAGE, PROCMEM_ZERO);
        CHECK(stranger == e, "address reused as arranged");
        CHECK(!kproc_prepare_user(pd, run), "reused environment address refused");
        CHECK(!procmem_pinned(stranger), "stranger's block was not mapped");
        procmem_free(stranger);
        Mfree(pd);
    }

    /* Pterm() trusts the recorded launcher, not the writable p_parent field */
    pd = new_basepage();
    if (pd) {
        /* forge p_parent as the child itself: a value that can never be the
         * real launcher, so code that still trusts p_parent is caught */
        PD *forged = pd;

        kproc_set_parent(pd, run);
        pd->p_parent = (ULONG)(uintptr_t)forged;
        CHECK(forged != run, "forged parent differs from the real one");
        CHECK(kproc_get_parent(pd) == run, "recorded parent survives a forged p_parent");
        CHECK(kproc_get_parent(pd) != forged, "forged p_parent is not trusted");
        env = USERPTR_TO_PTR(pd->p_env);
        Mfree(pd);
        Mfree(env);
    }

    /* Mfree() of a live process's own block is refused, not applied */
    pd = new_basepage();
    if (pd) {
        env = USERPTR_TO_PTR(pd->p_env);
        set_owner(pd, pd);
        set_owner(env, pd);
        CHECK(kproc_prepare_user(pd, run) && kproc_user_root(pd), "prepare for Mfree test");
        CHECK(Mfree(env) == EACCDN, "Mfree of a mapped environment refused");
        CHECK(Mfree(pd) == EACCDN, "Mfree of the live basepage refused");
        CHECK(kproc_user_root(pd) != 0, "address space survives the refused Mfree");
        proc_free_owned(pd);
    }

    /* failed setup: the basepage is abandoned, never launched */
    for (i = 0; i < LIFECYCLES; i++) {
        pd = new_basepage();
        if (!pd)
            break;
        env = USERPTR_TO_PTR(pd->p_env);
        CHECK(Mfree(pd) == 0, "Mfree basepage");
        CHECK(Mfree(env) == 0, "Mfree environment");
        CHECK(Mfree(pd) != 0, "second Mfree refused");
    }
    CHECK(i == LIFECYCLES, "abandoned basepage cycles");
    same(&s, "abandoned basepages");

    /* an abandoned basepage gives back the directory references it inherited
     * (init_pd_files() took them), exactly once */
    {
        int d;
        WORD saved_use;

        for (d = 0; d < NUMCURDIR && !PD_CURDIR(run)[d]; d++)
            ;
        if (d < NUMCURDIR) {
            saved_use = dirtbl[PD_CURDIR(run)[d]].use;
            pd = new_basepage();
            CHECK(pd && dirtbl[PD_CURDIR(run)[d]].use == saved_use + 1, "basepage inherits a directory ref");
            if (pd) {
                env = USERPTR_TO_PTR(pd->p_env);
                CHECK(Mfree(pd) == 0, "Mfree abandoned basepage");
                Mfree(env);
                CHECK(dirtbl[PD_CURDIR(run)[d]].use == saved_use, "inherited directory ref released once");
            }
        }
    }

    /* Ptermres with an unlaunched child basepage: its record goes, the
     * allocations stay (kept resident), nothing is left to leak later */
    {
        PD *parent = new_basepage();
        PD *child = new_basepage();
        ULONG kp = kproc_count();

        if (parent && child) {
            void *cenv = USERPTR_TO_PTR(child->p_env);
            void *penv = USERPTR_TO_PTR(parent->p_env);

            set_owner(parent, parent);
            set_owner(penv, parent);
            set_owner(child, parent);
            set_owner(cenv, parent);
            proc_make_resident(parent, PAGE);
            CHECK(kproc_count() == kp - 1, "child KPROC dropped at Ptermres");
            CHECK(procmem_size(child) && procmem_size(cenv), "child blocks stay resident");
            /* the terminating parent's own record and references go in
             * xterm()/ixterm(); an unlaunched one is released by Mfree() */
            Mfree(parent);
            Mfree(child);                       /* its references went at Ptermres */
            Mfree(cenv);
            Mfree(penv);
        }
    }

    /* an allocation failure while preparing the launch is reported and
     * leaves nothing behind -- tried at every allocation point */
    for (k = 1; k <= 8; k++) {
        pd = new_basepage();
        if (!pd)
            break;
        env = USERPTR_TO_PTR(pd->p_env);
        set_owner(pd, pd);
        set_owner(env, pd);
        x86_64_pmem_test_fail_after(k);
        if (!kproc_prepare_user(pd, run)) {
            failed++;
            CHECK(!kproc_user_root(pd), "failed preparation leaves no address space");
        }
        x86_64_pmem_test_fail_after(0);
        proc_free_owned(pd);
    }
    CHECK(failed >= 3, "launch failures were exercised");
    same(&s, "failed launch");
}

/* ---- ring-3 processes and the x32 image loader (#398) -------------- */

/*
 * Runs the probe program (tests/x32_probe/) as a real ring-3 process, with
 * `mode` as the first character of its command tail, and returns the exit
 * code Pexec() gives back: 0..0xffff, 0xffff for a process that faulted.  A
 * negative value is a launch that was refused (ENSMEM); the basepage is then
 * still ours and is released here.  -1000 is a failure of the setup itself,
 * already reported.
 */
static BOOL probe_may_fail_early;   /* under memory pressure the basepage itself may be refused */

#if CONF_WITH_USER_CALLBACKS
enum { CALLBACK_OOM_NONE, CALLBACK_OOM_ENTRY, CALLBACK_OOM_SERVICE };
static BOOL callback_test_watching, callback_test_entered;
static PD *callback_test_owner;
static UWORD callback_test_child_exits, callback_test_child_rc;
static UWORD callback_test_oom_phase, callback_test_injected, callback_test_ooms;

#if CONF_WITH_X32_VDI
static BOOL callback_motion_watching;
static BOOL callback_irq_watching, callback_irq_armed;
static UWORD callback_irq_entries, callback_irq_exits;
static UWORD callback_motion_phase, callback_motion_cursors;
static void (*callback_motion_native_cursor)(WORD, WORD);
extern void mouse_int(UBYTE *buf);

static void callback_motion_cursor(WORD x, WORD y)
{
    static const WORD expected_x[3] = { 107, 110, 120 };
    static const WORD expected_y[3] = { 109, 113, 133 };
    UQUAD flags;

    __asm__ volatile ("pushfq; popq %0" : "=r"(flags));
    CHECK(!(flags & 0x200), "motion coordinates and native cursor notification exclude IRQs");
    CHECK(callback_motion_cursors < 3, "stale motion results do not notify the cursor");
    if (callback_motion_cursors < 3)
        CHECK(x == expected_x[callback_motion_cursors] && y == expected_y[callback_motion_cursors],
              "native cursor sees current coordinates only");
    callback_motion_cursors++;
    callback_motion_native_cursor(x, y);
}

static void callback_motion_packet(UBYTE *packet)
{
    UQUAD flags;

    __asm__ volatile ("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
    mouse_int(packet);
    if (flags & 0x200)
        __asm__ volatile ("sti" ::: "memory");
}

static void callback_motion_syscall(UQUAD call, UQUAD second)
{
    UBYTE packet[3] = { 0xf8, 7, 9 };

    if (call == ((UQUAD)VDI_SERVICE_CLASS << 32 | VDI_SERVICE_VECTOR) && second == 126) {
        if (!callback_motion_phase) {
            /* Workstation setup has finished. Start the first delivery at
             * this exchange's return boundary, without a test syscall. */
            callback_motion_native_cursor = linea_vars.user_cur;
            linea_vars.user_cur = callback_motion_cursor;
            linea_vars.GCURX = linea_vars.GCURY = 100;
            vdi_service_callback_event(VDI_CALLBACK_MOTION, 100, 100);
            callback_motion_phase = 1;
        } else if (callback_motion_phase == 4) {
            CHECK(linea_vars.GCURX == 120 && linea_vars.GCURY == 133,
                  "the newest motion handler result is applied");
            callback_motion_phase = 5;
        }
    } else if (call == ((UQUAD)1 << 32 | 0x10b)) {
        if (callback_motion_phase == 1) {
            /* Same producer as the mouse IRQ, during a live user handler. */
            callback_motion_packet(packet);
            callback_motion_phase = 2;
        } else if (callback_motion_phase == 2) {
            CHECK(linea_vars.GCURX == 107 && linea_vars.GCURY == 109,
                  "a stale handler result cannot rewind newer IRQ coordinates");
            packet[1] = 3;
            packet[2] = 4;
            callback_motion_packet(packet);
            callback_motion_phase = 3;
        } else if (callback_motion_phase == 3) {
            CHECK(linea_vars.GCURX == 110 && linea_vars.GCURY == 113,
                  "following mouse packets accumulate from the latest position");
            callback_motion_phase = 4;
        }
    }
}
#endif

void x86_64_memtest_callback_exit(PD *pd, UWORD rc)
{
#if CONF_WITH_X32_VDI
    if (callback_irq_watching && pd == callback_test_owner) {
        UQUAD flags;

        __asm__ volatile ("pushfq; popq %0" : "=r"(flags));
        CHECK(flags & 0x200, "callback termination enables IRQs before file cleanup");
        callback_irq_exits++;
    }
#endif
    if (callback_test_owner && pd != callback_test_owner &&
        kproc_get_parent(pd) == callback_test_owner) {
        callback_test_child_exits++;
        callback_test_child_rc = rc;
    }
}

static void callback_test_inject(void)
{
    x86_64_pmem_test_fail_after(1);
    callback_test_oom_phase = CALLBACK_OOM_NONE;
    callback_test_injected++;
}

void x86_64_memtest_callback_entry(UQUAD flags)
{
#if CONF_WITH_X32_VDI
    if (callback_irq_watching && run == callback_test_owner) {
        CHECK(!(flags & 0x200), "the failing callback was delivered with IRQs masked");
        callback_irq_entries++;
    }
#else
    (void)flags;
#endif
    if (run == callback_test_owner) {
        callback_test_entered = TRUE;
        if (callback_test_oom_phase == CALLBACK_OOM_ENTRY)
            callback_test_inject();
    }
}

void x86_64_memtest_callback_syscall(UQUAD call, UQUAD output)
{
#if CONF_WITH_X32_VDI
    if (callback_motion_watching && run == callback_test_owner)
        callback_motion_syscall(call, output);
    if (callback_irq_watching && callback_irq_armed && run == callback_test_owner &&
        call == ((UQUAD)VDI_SERVICE_CLASS << 32 | VDI_SERVICE_VECTOR) && output == 118) {
        /* With no timer handler installed yet, drain old events and keep
         * IRQs masked until sysret restores the user's IF. The next delivery
         * then has to come from a real user-mode PIT interruption. */
        __asm__ volatile ("cli" ::: "memory");
        vdi_service_callback_work();
        callback_irq_armed = FALSE;
    }
#endif
    if (callback_test_entered && run == callback_test_owner &&
        callback_test_oom_phase == CALLBACK_OOM_SERVICE && output &&
        call == ((UQUAD)USER_CALLBACK_CLASS << 32 | USER_CALLBACK_ALTSTACK))
        callback_test_inject();
}

void x86_64_memtest_callback_lazy_oom(void)
{
    if (callback_test_injected && run == callback_test_owner)
        callback_test_ooms++;
}
#endif

static BOOL observe_probe_fault;
static long run_probe(char mode)
{
    char tail[2];
    PD *pd;
    KPROC *fault_observer = NULL;
    long rc;

    tail[0] = mode;
    tail[1] = '\0';
    rc = Pexec(PE_BASEPAGEFLAGS, (char *)PF_STANDARD, tail, NULL);
    if (rc <= 0 && probe_may_fail_early)
        return -1000;
    CHECK(rc > 0, "probe basepage");
    if (rc <= 0)
        return -1000;
    pd = (PD *)(uintptr_t)rc;
#if CONF_WITH_USER_CALLBACKS
    if (callback_test_watching)
        callback_test_owner = pd;
#endif
    if (!kproc_set_image(pd, testprobe_image())) {
        CHECK(FALSE, "probe image attaches");
        set_owner(pd, pd);
        set_owner(USERPTR_TO_PTR(pd->p_env), pd);
        proc_free_owned(pd);
        return -1000;
    }
    if (observe_probe_fault) {
        fault_observer = kproc_retain(pd);
        CHECK(fault_observer != NULL, "fault probe observer attaches before execution");
    }
#if CONF_WITH_NATIVE_SCHED && CONF_WITH_PCLONE
    if (mode == '=' || mode == '<' || mode == '(' || mode == ')' || mode == '"' ||
        mode == '.' || mode == '>' || mode == ',' || mode == '_' || mode == ';' || mode == '/' || mode == '-' || mode == '*') {
        PD *programs[1]; LONG results[1];
        programs[0] = pd;
        rc = proc_prepare_scheduled(pd);
        if (!rc) rc = ksched_run(programs, 1, results);
        if (!rc) return results[0];
    } else
#endif
    rc = Pexec(PE_GOTHENFREE, "", (char *)pd, NULL);
    if (rc < 0) {
        set_owner(pd, pd);
        set_owner(USERPTR_TO_PTR(pd->p_env), pd);
        proc_free_owned(pd);
    }
    if (fault_observer) {
        PROCESS_COMPLETION completion;
        CHECK(kproc_ref_terminal(fault_observer) && !kproc_ref_has_basepage(fault_observer),
              "fault probe observer ready after stack and PROCESS retirement");
        CHECK(kproc_ref_completion(fault_observer,&completion) &&
              completion.kind == PROCESS_COMPLETION_SIGNAL && completion.signal == 4 &&
              !completion.status, "native fault producer preserves SIGILL with scheduler enabled or disabled");
        kproc_release(fault_observer);
    }
    return rc;
}

#define MSR_GS_BASE        0xC0000101UL
#define MSR_KERNEL_GS_BASE 0xC0000102UL

/* The state a return from ring 3 must leave exactly as it found it. */
typedef struct {
    UQUAD cr3, gs, kernel_gs;
    UQUAD syscall_rsp, user_rsp, irq_rsp;
    UQUAD flags;
} CPUSTATE;

static void cpustate(CPUSTATE *c)
{
    c->cr3 = x86_64_read_cr3();
    c->gs = x86_64_rdmsr(MSR_GS_BASE);
    c->kernel_gs = x86_64_rdmsr(MSR_KERNEL_GS_BASE);
    c->syscall_rsp = x86_64_get_kernel_stack();
    c->user_rsp = x86_64_get_saved_user_rsp();
    c->irq_rsp = x86_64_get_irq_stack();
    __asm__ volatile ("pushfq; popq %0" : "=r"(c->flags));
}

/* One ring-3 run that must exit with `want`, return to this very context
 * with the same CPU state and leak nothing. */
static void probe_expect(char mode, long want, const char *what)
{
    SNAP s;
    CPUSTATE before, after;
    PD *me = run;
    long rc;

    snap(&s);
    cpustate(&before);
    rc = run_probe(mode);
    if (rc != -1000 && rc != want)
        kcprintf("memtest: %s: exit code 0x%lx, wanted 0x%lx\n", what, rc, want);
    CHECK(rc == want, what);
    cpustate(&after);
    CHECK(run == me, "the launcher is the current process again");
    CHECK(after.cr3 == before.cr3 && after.cr3 == x86_64_kernel_pml4_phys(),
          "back on the kernel page tables");
    CHECK(after.gs == before.gs && after.kernel_gs == before.kernel_gs,
          "the GS base is back in its ring-0 state");
    CHECK(after.syscall_rsp == before.syscall_rsp && after.user_rsp == before.user_rsp &&
          after.irq_rsp == before.irq_rsp, "syscall/user/TSS stack pointers are restored");
    CHECK(!((after.flags ^ before.flags) & 0x200), "the launcher's interrupt-enable flag is restored");
    same(&s, what);
}

#if CONF_WITH_MMAP
static void free_mapping_process(PD *pd)
{
    if (pd) {
        set_owner(pd, pd);
        set_owner(USERPTR_TO_PTR(pd->p_env), pd);
        proc_free_owned(pd);
    }
}

static void test_mmap(void)
{
    SNAP s;
    PD *a, *b, *parent = run;
    ASPACE *as, *bs;
    UQUAD pa, pb;
    UQUAD cross_page = 0x12345678ULL;
    ULONG value, private_a, private_b;
    UWORD prot;
    LONG rc;
    int k, refused = 0, mapped = 0;

    snap(&s);
    a = new_basepage();
    b = new_basepage();
    if (a && b && kproc_prepare_user(a, run) && kproc_prepare_user(b, run)) {
        as = kproc_user_aspace(a);
        bs = kproc_user_aspace(b);
        private_a = aspace_private_pages(as);
        private_b = aspace_private_pages(bs);
        CHECK(kproc_mmap(a, 0, 3 * PAGE, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) == PTOS_MMAP_BASE,
              "first process anonymous map");
        CHECK(kproc_mmap(b, 0, 3 * PAGE, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) == PTOS_MMAP_BASE,
              "second process uses same virtual address");
        CHECK(!aspace_translate(as, PTOS_MMAP_BASE, NULL, NULL) &&
              !aspace_translate(bs, PTOS_MMAP_BASE, NULL, NULL) &&
              aspace_private_pages(as) == private_a && aspace_private_pages(bs) == private_b,
              "anonymous reservations have no physical backing yet");
        CHECK(kproc_demand_page(a, PTOS_MMAP_BASE, ASPACE_PROT_WRITE) == 1 &&
              kproc_demand_page(b, PTOS_MMAP_BASE, 0) == 1,
              "first legal touches back private pages");
        CHECK(aspace_private_pages(as) == private_a + 1 &&
              aspace_private_pages(bs) == private_b + 1,
              "one physical page per touched mapping page");
        pa = pb = 0;
        CHECK(aspace_translate(as, PTOS_MMAP_BASE, &pa, NULL) &&
              aspace_translate(bs, PTOS_MMAP_BASE, &pb, NULL) && pa != pb,
              "anonymous mappings have distinct physical pages");
        value = 0x660;
        CHECK(aspace_copy_to_user(as, PTOS_MMAP_BASE, &value, sizeof(value)),
              "write first process mapping");
        value = 1;
        CHECK(aspace_copy_from_user(bs, &value, PTOS_MMAP_BASE, sizeof(value)) && !value,
              "second process private data stays zero");
        CHECK(kproc_mprot(b, PTOS_MMAP_BASE + 2 * PAGE, PAGE, PROT_READ) == 0,
              "prepare read-only lazy mapping for kernel copies");
        run = b;
        value = 7;
        CHECK(!kproc_copy_from_user_resident(&value, PTOS_MMAP_BASE + 2 * PAGE, sizeof(value)) &&
              !kproc_copy_to_user_resident(PTOS_MMAP_BASE + PAGE, &value, sizeof(value)) &&
              !aspace_translate(bs, PTOS_MMAP_BASE + PAGE, NULL, NULL) &&
              !aspace_translate(bs, PTOS_MMAP_BASE + 2 * PAGE, NULL, NULL),
              "resident-only copies never back lazy pages or terminate their caller");
        CHECK(!kproc_copy_to_user(PTOS_MMAP_BASE + 2 * PAGE - 4,
                                  &cross_page, sizeof(cross_page)) &&
              !aspace_translate(bs, PTOS_MMAP_BASE + PAGE, NULL, NULL) &&
              !aspace_translate(bs, PTOS_MMAP_BASE + 2 * PAGE, NULL, NULL),
              "cross-page write rejects read-only tail before backing either page");
        CHECK(!kproc_copy_to_user(PTOS_MMAP_BASE + 2 * PAGE, &value, sizeof(value)) &&
              !aspace_translate(bs, PTOS_MMAP_BASE + 2 * PAGE, NULL, NULL),
              "kernel write rejects untouched read-only reservation without faulting");
        CHECK(kproc_copy_from_user(&value, PTOS_MMAP_BASE + 2 * PAGE, sizeof(value)) && !value &&
              aspace_translate(bs, PTOS_MMAP_BASE + 2 * PAGE, NULL, &prot) &&
              (prot & ASPACE_PROT_USER) && !(prot & ASPACE_PROT_WRITE),
              "kernel read faults the same reservation with its logical protection");
        CHECK(kproc_copy_from_user_resident(&value, PTOS_MMAP_BASE + 2 * PAGE, sizeof(value)) &&
              !kproc_copy_to_user_resident(PTOS_MMAP_BASE + 2 * PAGE, &value, sizeof(value)),
              "resident copies honor preflight residency and current write protection");
        run = parent;
        CHECK(kproc_psetlimit(a, PSETLIMIT_DATA, PAGE) == 0,
              "set anonymous allocation limit");
        CHECK(kproc_mmap(a, 0, PAGE, PROT_READ,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) == ENSMEM &&
              !kproc_uheap_alloc(a, PAGE), "mapping bytes count against allocation limits");
        CHECK(kproc_psetlimit(a, PSETLIMIT_DATA, 0) == PAGE, "restore limit");
        CHECK(kproc_mprot(a, PTOS_MMAP_BASE, PAGE, PROT_READ | PROT_EXEC) == 0 &&
              aspace_translate(as, PTOS_MMAP_BASE, NULL, &prot) &&
              (prot & ASPACE_PROT_EXEC) && !(prot & ASPACE_PROT_WRITE),
              "Mprot applies RX permissions");
        CHECK(kproc_mprot(a, PTOS_MMAP_BASE, PAGE, PROT_WRITE | PROT_EXEC) == EACCDN,
              "Mprot refuses WX");
        CHECK(kproc_mprot(a, PTOS_MMAP_BASE + PAGE, PAGE, PROT_READ) == 0 &&
              !aspace_translate(as, PTOS_MMAP_BASE + PAGE, NULL, NULL) &&
              kproc_demand_page(a, PTOS_MMAP_BASE + PAGE, ASPACE_PROT_WRITE) == 0 &&
              !aspace_translate(as, PTOS_MMAP_BASE + PAGE, NULL, NULL) &&
              kproc_demand_page(a, PTOS_MMAP_BASE + PAGE, 0) == 1 &&
              aspace_translate(as, PTOS_MMAP_BASE + PAGE, NULL, &prot) &&
              (prot & ASPACE_PROT_USER) && !(prot & (ASPACE_PROT_WRITE | ASPACE_PROT_EXEC)),
              "untouched read-only page is backed only for a legal read");
        CHECK(kproc_mprot(a, PTOS_MMAP_BASE + 2 * PAGE, PAGE, PROT_NONE) == 0 &&
              kproc_demand_page(a, PTOS_MMAP_BASE + 2 * PAGE, 0) == 0 &&
              kproc_demand_page(a, PTOS_MMAP_BASE + 2 * PAGE, ASPACE_PROT_EXEC) == 0 &&
              !aspace_translate(as, PTOS_MMAP_BASE + 2 * PAGE, NULL, NULL),
              "PROT_NONE never allocates a page");
        CHECK(kproc_mprot(a, PTOS_MMAP_BASE + 2 * PAGE, PAGE, PROT_READ | PROT_EXEC) == 0 &&
              !aspace_translate(as, PTOS_MMAP_BASE + 2 * PAGE, NULL, NULL) &&
              kproc_demand_page(a, PTOS_MMAP_BASE + 2 * PAGE, ASPACE_PROT_EXEC) == 1 &&
              aspace_translate(as, PTOS_MMAP_BASE + 2 * PAGE, NULL, &prot) &&
              (prot & ASPACE_PROT_EXEC) && !(prot & ASPACE_PROT_WRITE),
              "first instruction fetch honors permissions changed before first touch");
        CHECK(kproc_munmap(a, PTOS_MMAP_BASE + PAGE, PAGE) == 0 &&
              !aspace_translate(as, PTOS_MMAP_BASE + PAGE, NULL, NULL),
              "middle unmap removes the page");
        CHECK(kproc_mprot(a, PTOS_MMAP_BASE, 3 * PAGE, PROT_READ) == EIMBA &&
              aspace_translate(as, PTOS_MMAP_BASE, NULL, &prot) &&
              (prot & ASPACE_PROT_EXEC), "Mprot across a hole is atomic");
        CHECK(kproc_munmap(a, PTOS_MMAP_BASE, 3 * PAGE) == 0,
              "unmap spans two remaining regions and a hole");
        CHECK(aspace_private_pages(as) == private_a,
              "unmap frees only pages that were backed");
        CHECK(kproc_mmap(a, 0, PTOS_MMAP_LIMIT - PTOS_MMAP_BASE, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) == PTOS_MMAP_BASE &&
              aspace_private_pages(as) == private_a,
              "full mapping window is reserved without physical pages");
        CHECK(kproc_demand_page(a, PTOS_MMAP_LIMIT - PAGE, ASPACE_PROT_WRITE) == 1 &&
              aspace_private_pages(as) == private_a + 1,
              "sparse reservation backs only its last page on touch");
        CHECK(kproc_munmap(a, PTOS_MMAP_BASE, PTOS_MMAP_LIMIT - PTOS_MMAP_BASE) == 0 &&
              aspace_private_pages(as) == private_a,
              "sparse unmap skips untouched pages");
    } else {
        CHECK(FALSE, "mapping test process preparation");
    }
    free_mapping_process(a);
    free_mapping_process(b);             /* unmap on destruction, too */
    same(&s, "anonymous mapping isolation/cleanup");

    for (k = 1; k <= 16; k++) {
        snap(&s);
        a = new_basepage();
        if (a && kproc_prepare_user(a, run)) {
            CHECK(kproc_mmap(a, 0, 3 * PAGE, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) == PTOS_MMAP_BASE,
                  "reserve mapping before first-touch failure injection");
            x86_64_pmem_test_fail_after(k);
            rc = kproc_demand_page(a, PTOS_MMAP_BASE, ASPACE_PROT_WRITE);
            x86_64_pmem_test_fail_after(0);
            CHECK(rc == 1 || rc == 2, "first-touch allocation outcome");
            if (rc == 2) {
                refused++;
                CHECK(!aspace_translate(kproc_user_aspace(a), PTOS_MMAP_BASE, NULL, NULL),
                      "failed first touch leaves no backing page");
                CHECK(kproc_mmap(a, PTOS_MMAP_BASE, PAGE, PROT_READ,
                                MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0)
                       == EACCDN, "failed first touch retains its reservation");
                CHECK(kproc_demand_page(a, PTOS_MMAP_BASE, ASPACE_PROT_WRITE) == 1,
                      "first touch can be retried after allocation failure");
            } else {
                mapped++;
            }
        } else {
            CHECK(FALSE, "failure-injection process preparation");
        }
        free_mapping_process(a);
        same(&s, "anonymous first-touch failure cleanup");
    }
    CHECK(refused && mapped, "first-touch allocation failures and successes exercised");
}

static void test_file_mmap(void)
{
    static char path[] = "C:\\MMAP.TMP";
    char block[64], value, readback;
    PD *a = NULL, *b = NULL, *c = NULL, *parent = run;
    ASPACE *as, *bs, *cs;
    KASPACE *retained = NULL, *lease_space = NULL;
    KASPACE_LEASE *cow_lease = NULL;
    SNAP snapshot;
    UQUAD pa, pb, pc;
    DMD *media = NULL;
    CLNO head = 0;
    long fd = -1, fresh = -1;
    int i, before = failures;

    snap(&snapshot);
    fd = DISK_CALL(xcreat(path, 0));
    CHECK(fd >= NUMSTD, "create file mapping fixture");
    if (fd < NUMSTD) goto done;
    for (i = 0; i < 257; i++) {
        memset(block, 'A' + i / 64, sizeof(block));
        if (DISK_CALL(xwrite(fd, sizeof(block), block)) != sizeof(block)) break;
    }
    CHECK(i == 257, "write patterned file mapping fixture");
    media = getofd(fd)->o_dmd; head = getofd(fd)->o_dfd->o_strtcl;
    a = new_basepage(); b = new_basepage(); c = new_basepage();
    if (!a || !b || !c || !kproc_prepare_user(a, run) ||
        !kproc_prepare_user(b, run) || !kproc_prepare_user(c, run)) {
        CHECK(FALSE, "prepare file mapping address spaces"); goto done;
    }
    as = kproc_user_aspace(a); bs = kproc_user_aspace(b); cs = kproc_user_aspace(c);
    CHECK(kproc_mmap(a, 0, 5 * PAGE, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0) == PTOS_MMAP_BASE &&
          kproc_mmap(b, 0, 5 * PAGE, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0) == PTOS_MMAP_BASE,
          "reserve lazy private file mappings in two processes");
    CHECK(kproc_mmap(a, 0, PAGE, PROT_READ | PROT_WRITE,
          MAP_PRIVATE | MAP_ANONYMOUS, 0, 0) == PTOS_MMAP_BASE + 5 * PAGE,
          "reserve anonymous mapping alongside retained file mapping");
    CHECK(aspace_file_pages() == 0 && kproc_demand_page(a, PTOS_MMAP_BASE, 0) == 1 &&
          kproc_demand_page(b, PTOS_MMAP_BASE, 0) == 1, "file mappings fault lazily");
    CHECK(aspace_translate(as, PTOS_MMAP_BASE, &pa, NULL) &&
          aspace_translate(bs, PTOS_MMAP_BASE, &pb, NULL) && pa == pb &&
          aspace_file_pages() == 1 && aspace_file_references() == 2,
          "clean pages share physical backing by identity and offset");
    CHECK(DISK_CALL(xlseek(0, fd, 1)) == 257 * 64 &&
          kproc_user_access(b, PTOS_MMAP_BASE, 1, TRUE), "fault preserves file position and logical write access");
    getofd(fd)->o_dfd->o_flag |= O_MEDIA_LOST;
    CHECK(kproc_demand_page(b, PTOS_MMAP_BASE + PAGE, 0) == 3 &&
          !aspace_translate(bs, PTOS_MMAP_BASE + PAGE, NULL, NULL) &&
          aspace_file_pages() == 1 && aspace_file_references() == 2,
          "backing I/O error leaves no partial mapping, page or cache reference");
    getofd(fd)->o_dfd->o_flag &= ~O_MEDIA_LOST;
    lease_space = kproc_test_retain_active(a);
    CHECK(kaspace_lease_acquire(lease_space, PTOS_MMAP_BASE, PAGE,
          ASPACE_PROT_USER, &cow_lease) == E_OK && kaspace_lease_seal(&cow_lease, 1),
          "lease clean file mapping before COW");
    CHECK(kproc_demand_page(a, PTOS_MMAP_BASE, ASPACE_PROT_WRITE) == 2 &&
          kaspace_lease_valid(cow_lease) && aspace_translate(as, PTOS_MMAP_BASE, &pc, NULL) && pc == pa,
          "sealed COW rejects replacement without changing shared backing");
    kaspace_lease_release(&cow_lease);
    CHECK(kaspace_lease_acquire(lease_space, PTOS_MMAP_BASE, PAGE,
          ASPACE_PROT_USER, &cow_lease) == E_OK, "acquire revocable file-mapping lease");
    x86_64_pmem_test_fail_after(1);
    CHECK(kproc_demand_page(a, PTOS_MMAP_BASE, ASPACE_PROT_WRITE) == 2, "injected COW allocation failure");
    x86_64_pmem_test_fail_after(0);
    CHECK(!kaspace_lease_valid(cow_lease), "attempted file COW conservatively revokes preparation lease");
    kaspace_lease_release(&cow_lease);
    CHECK(aspace_translate(as, PTOS_MMAP_BASE, &pc, NULL) && pc == pa &&
          aspace_file_references() == 2, "failed COW preserves original mapping and references");
    CHECK(kproc_demand_page(a, PTOS_MMAP_BASE, ASPACE_PROT_WRITE) == 1 &&
          aspace_translate(as, PTOS_MMAP_BASE, &pa, NULL) && pa != pb, "write fault makes a private page");
    value = 'Z';
    CHECK(aspace_copy_to_user(as, PTOS_MMAP_BASE, &value, 1) &&
          aspace_copy_from_user(bs, &readback, PTOS_MMAP_BASE, 1) && readback == 'A', "COW preserves peer contents");
    CHECK(kproc_mprot(b, PTOS_MMAP_BASE, PAGE, PROT_READ) == 0 &&
          kproc_demand_page(b, PTOS_MMAP_BASE, ASPACE_PROT_WRITE) == 0 &&
          kproc_mprot(b, PTOS_MMAP_BASE, PAGE, PROT_READ | PROT_WRITE) == 0 &&
          aspace_file_cow(bs, PTOS_MMAP_BASE), "Mprot preserves COW and enforces logical permissions");
    value = 'X';
    CHECK(DISK_CALL(xlseek(0, fd, 0)) == 0 && DISK_CALL(xwrite(fd, 1, &value)) == 1 &&
          kproc_mmap(c, 0, 5 * PAGE, PROT_READ, MAP_PRIVATE, fd, 0) == PTOS_MMAP_BASE &&
          kproc_demand_page(c, PTOS_MMAP_BASE, 0) == 1 &&
          aspace_copy_from_user(cs, &readback, PTOS_MMAP_BASE, 1) && readback == 'X',
          "new mappings do not reuse a stale cache generation after Fwrite");
    CHECK(aspace_translate(cs, PTOS_MMAP_BASE, &pc, NULL) && pc != pb, "new generation has separate backing");
    run = b; value = 'K';
    CHECK(kproc_copy_to_user(PTOS_MMAP_BASE, &value, 1), "kernel buffer write triggers COW");
    run = parent;
    CHECK(aspace_copy_from_user(cs, &readback, PTOS_MMAP_BASE, 1) && readback == 'X', "kernel COW preserves peer");
    CHECK(DISK_CALL(xunlink(path)) == 0 && kdos_fclose(fd) == 0, "unlink and close while mapped"); fd = -1;
    CHECK(DISK_CALL(getrealcl(head, media)) != FREECLUSTER &&
          kproc_demand_page(b, PTOS_MMAP_BASE + 4 * PAGE, 0) == 1 &&
          aspace_copy_from_user(bs, &readback, PTOS_MMAP_BASE + 4 * PAGE, 1) && readback == 'E' &&
          aspace_copy_from_user(bs, &readback, PTOS_MMAP_BASE + 4 * PAGE + 64, 1) && !readback,
          "retained backing faults after unlink/close and zero fills final file page");
    CHECK(kproc_munmap(b, PTOS_MMAP_BASE + 2 * PAGE, PAGE) == 0 &&
          kproc_demand_page(b, PTOS_MMAP_BASE + 3 * PAGE, 0) == 1 &&
          aspace_copy_from_user(bs, &readback, PTOS_MMAP_BASE + 3 * PAGE, 1) && readback == 'D',
          "middle unmap preserves backing offsets and references");
    fresh = DISK_CALL(xcreat(path, 0));
    CHECK(fresh >= NUMSTD && DISK_CALL(xwrite(fresh, 1, &value)) == 1, "recreate mapped pathname");
    retained = kproc_test_retain_active(a);
    CHECK(retained != NULL, "retain file mapping space independently");
    free_mapping_process(a);
    a = NULL;
    CHECK(retained && kaspace_mmap(retained)->regions &&
          kaspace_mmap(retained)->regions->backing &&
          aspace_copy_from_user(kaspace_backend(retained), &readback, PTOS_MMAP_BASE, 1) && readback == 'Z',
          "file mapping metadata and private page survive process destruction");
    CHECK(retained && kaspace_mmap_demand_page(retained, PTOS_MMAP_BASE + PAGE, 0) == 1 &&
          aspace_copy_from_user(kaspace_backend(retained), &readback, PTOS_MMAP_BASE + PAGE, 1) &&
          readback == 'B', "retained unlinked file reservation faults after process destruction");
    {
        UQUAD previous = aspace_enter(bs);
        kproc_shutdown_file_mappings();
        aspace_leave(previous);
        CHECK(retained && kaspace_mmap(retained)->regions &&
              kaspace_mmap(retained)->regions->address == PTOS_MMAP_BASE + 5 * PAGE &&
              !kaspace_mmap(retained)->regions->backing &&
              kaspace_mmap(retained)->bytes == PAGE &&
              !aspace_translate(kaspace_backend(retained), PTOS_MMAP_BASE, NULL, NULL) &&
              !aspace_translate(bs, PTOS_MMAP_BASE, NULL, NULL) && !aspace_file_pages() &&
              DISK_CALL(getrealcl(head, media)) == FREECLUSTER,
              "shutdown releases file views in both retained and process-owned spaces");
    }
done:
    run = parent;
    kaspace_lease_release(&cow_lease);
    kaspace_put(lease_space);
    kaspace_put(retained);
    if (fd >= NUMSTD) kdos_fclose(fd);
    free_mapping_process(a); free_mapping_process(b); free_mapping_process(c);
    CHECK(aspace_file_pages() == 0 && aspace_file_references() == 0, "last mapping frees cached file pages");
    if (head) CHECK(DISK_CALL(getrealcl(head, media)) == FREECLUSTER, "last mapping reference releases unlinked cluster chain");
    if (fresh >= NUMSTD) kdos_fclose(fresh);
    DISK_CALL(xunlink(path));
    same(&snapshot, "file-backed private mappings");
    kcprintf("x86-64 file mappings/COW: %s\n", failures == before ? "PASS" : "FAIL");
}

static void test_mapping_ring3(void)
{
    int before = failures;

    test_mmap();
    test_file_mmap();
    probe_expect('0', 0, "file-backed ring-3 execution, COW, Fread, offset, unlink and split teardown");
    probe_expect('5', 0, "ELF mapping syscall: bounds, ownership, partial unmap, zeroing");
    probe_expect('[', 0, "ELF private code executes after RW to RX transition");
    probe_expect(']', 0xffff, "ELF RX mapping refuses writes");
    probe_expect('{', 0xffff, "ELF NX mapping refuses execution");
    probe_expect('}', 0xffff, "ELF PROT_NONE mapping refuses reads");
    probe_expect('|', 0, "ELF mappings reclaimed on exit");
    probe_expect('6', 0, "lazy anonymous first reads and kernel writes, untouched NONE, mixed Mprot");
    probe_expect('7', 0xffff, "first touch cannot write read-only anonymous memory");
    probe_expect('8', 0xffff, "first touch cannot execute NX anonymous memory");
    probe_expect('9', 0xffff, "first touch cannot read PROT_NONE anonymous memory");
    kcprintf("x86-64 mmap syscalls: %s\n", failures == before ? "PASS" : "FAIL");

}

#endif

#if CONF_WITH_ELF_SHARED
static void discard_interpreter(long address)
{
    PD *pd = (PD *)(uintptr_t)address;
    void *env = USERPTR_TO_PTR(pd->p_env);
    CHECK(Mfree(pd) == 0 && Mfree(env) == 0, "discard staged interpreter and environment");
}

static void test_interpreter_failures(void)
{
    static const char tail[] = { 0, 0 };
    static const char env[] = "CHECK=660\0SECOND=value\0\0";
    SNAP s;
    long rc;
    int k, failed = 0, succeeded = 0;

    /* Prime filesystem caches before taking allocator snapshots. */
    rc = Pexec(PE_LOAD, "C:\\DYNTEST.TOS", tail, env);
    CHECK(rc > 0, "interpreter failure-test fixture is available");
    if (rc <= 0)
        return;
    discard_interpreter(rc);
    for (k = 1; k <= 64; k++) {
        snap(&s);
        x86_64_pmem_test_fail_after(k);
        rc = Pexec(PE_LOAD, "C:\\DYNTEST.TOS", tail, env);
        x86_64_pmem_test_fail_after(0);
        if (rc > 0) {
            succeeded++;
            discard_interpreter(rc);
        } else {
            failed++;
            CHECK(rc == ENSMEM, "failed interpreter allocation reports ENSMEM");
        }
        same(&s, "interpreter load allocation failure/discard");
    }
    CHECK(failed && succeeded, "interpreter allocation failures and successes exercised");
}
#endif

static void test_backing_media_change(void)
{
    DMD media;
    DND directory;
    OFD *pin;
    int slots[2], i, n = 0;

    for (i = 0; i < OPNFILES && n < 2; i++)
        if (!sft[i].f_own) slots[n++] = i;
    CHECK(n == 2, "free slots for media-change reference test");
    if (n != 2) return;
    memset(&media, 0, sizeof(media)); memset(&directory, 0, sizeof(directory));
    media.m_dtl = &directory;
    pin = MGET(OFD);
    pin->o_dfd = &pin->o_disk; pin->o_disk.o_usecnt = 1;
    pin->o_disk.o_flag = O_UNLINKED; pin->o_disk.o_strtcl = 0x4321;
    pin->o_refs = 1; pin->o_dmd = &media; pin->o_dnode = &directory;
    directory.d_files = pin;
    CHECK(ofd_hold(pin), "pin before simulated media removal");
    for (i = 0; i < 2; i++) {
        sft[slots[i]].f_ofd = pin; sft[slots[i]].f_own = run; sft[slots[i]].f_use = 1;
    }
    ofd_invalidate_drive(&media);
    CHECK(!directory.d_files && !sft[slots[0]].f_ofd && !sft[slots[1]].f_ofd &&
          pin->o_refs == 1 && !pin->o_dmd && !pin->o_dnode &&
          (pin->o_dfd->o_flag & O_MEDIA_LOST), "media loss drops aliased handles once and retains pin");
    CHECK(!ofd_hold(pin), "invalidated backing cannot acquire fresh references");
    CHECK(ofd_put(pin) == E_OK, "media-lost pin releases without touching the replacement FAT");
}

/* The pin is the reference a file-backed mapping will hold after Fclose. */
static void test_unlinked_backing(void)
{
    static char path[] = "C:\\UNLINK.TMP";
    static char original[64];
    static char replacement[] = "new";
    OFD *pin = NULL;
    DMD *dm = NULL;
    CLNO head = 0;
    long first = -1, second = -1, fresh = -1, rc;
    int i, before = failures;
    char readback[8];

    memset(original, 'A', sizeof(original));
    first = DISK_CALL(xcreat(path, 0));
    CHECK(first >= NUMSTD, "create unlink fixture");
    if (first < NUMSTD) goto done;
    for (i = 0; i < 129; i++)
        if (DISK_CALL(xwrite(first, sizeof(original), original)) != sizeof(original)) break;
    CHECK(i == 129, "write multi-cluster unlink fixture");
    second = DISK_CALL(xopen(path, 0));
    CHECK(second >= NUMSTD, "independent open before unlink");
    if (second < NUMSTD) goto done;
    if (!ofd_hold(getofd(first))) {
        CHECK(FALSE, "backing reference retains open object"); goto done;
    }
    pin = getofd(first);
    head = pin->o_dfd->o_strtcl; dm = pin->o_dmd;
    CHECK(getofd(second)->o_dfd == pin->o_dfd, "opens share one backing identity");
    CHECK(DISK_CALL(xunlink(path)) == E_OK, "unlink while handles and backing pin remain");
    CHECK(DISK_CALL(xopen(path, 0)) == EFILNF, "unlinked name disappears immediately");
    fresh = DISK_CALL(xcreat(path, 0));
    CHECK(fresh >= NUMSTD, "reuse deleted name while old object lives");
    if (fresh < NUMSTD) goto done;
    CHECK(getofd(fresh)->o_dfd != pin->o_dfd, "reused slot creates a new file identity");
    CHECK(DISK_CALL(xwrite(fresh, 3, replacement)) == 3, "write replacement contents");
    CHECK(DISK_CALL(xlseek(0, second, 0)) == 0 &&
          DISK_CALL(xread(second, sizeof(readback), readback)) == sizeof(readback) &&
          readback[0] == 'A' && readback[7] == 'A', "old handle still reads old contents");
    CHECK(kdos_fclose(first) == E_OK, "close first handle while backing pinned"); first = -1;
    CHECK(kdos_fclose(second) == E_OK, "close final handle while backing pinned"); second = -1;
    CHECK(pin->o_refs == 1 && DISK_CALL(getrealcl(head, dm)) != FREECLUSTER,
          "cluster chain survives final handle close");
    CHECK(DISK_CALL(ixlseek(pin, pin->o_dfd->o_fileln)) >= 0 &&
          DISK_CALL(ixwrite(pin, 1, replacement)) == 1, "unlinked backing may grow without touching new entry");
    CHECK(DISK_CALL(ixlseek(pin, 0)) == 0 && DISK_CALL(ixread(pin, 8, readback)) == 8 &&
          readback[0] == 'A', "pin can read after all handles close");
    pin->o_dfd->o_flag |= O_CLOSING;
    CHECK(DISK_CALL(ixread(pin, 1, readback)) == EACCDN &&
          DISK_CALL(ixwrite(pin, 1, replacement)) == EACCDN &&
          DISK_CALL(ixlseek(pin, 0)) == EACCDN && !ofd_hold(pin),
          "closing backing refuses ordinary I/O and fresh mapping pins");
    pin->o_dfd->o_flag &= ~O_CLOSING;
    rc = DISK_CALL(ofd_put(pin));
    CHECK(rc == E_OK, "release final in-memory reference");
    if (rc == E_OK) pin = NULL;
    CHECK(DISK_CALL(getrealcl(head, dm)) == FREECLUSTER, "last reference reclaims old cluster chain");
    CHECK(kdos_fclose(fresh) == E_OK, "close replacement"); fresh = -1;
    fresh = DISK_CALL(xopen(path, 0));
    CHECK(fresh >= NUMSTD && DISK_CALL(xlseek(0, fresh, 2)) == 3 &&
          DISK_CALL(xlseek(0, fresh, 0)) == 0 &&
          DISK_CALL(xread(fresh, 3, readback)) == 3 && !memcmp(readback, replacement, 3),
          "old close never overwrites replacement directory metadata");
done:
    if (first >= NUMSTD) kdos_fclose(first);
    if (second >= NUMSTD) kdos_fclose(second);
    if (fresh >= NUMSTD) kdos_fclose(fresh);
    if (pin) DISK_CALL(ofd_put(pin));
    DISK_CALL(xunlink(path));
    kcprintf("x86-64 unlinked file backing: %s\n", failures == before ? "PASS" : "FAIL");
}

#if CONF_WITH_USER_CALLBACKS && CONF_WITH_X32_VDI
static void test_callback_irq_failure(char mode)
{
    UQUAD flags;
    ULONG ticks, spins;

    __asm__ volatile ("pushfq; popq %0" : "=r"(flags));
    callback_test_watching = callback_irq_watching = callback_irq_armed = TRUE;
    callback_irq_entries = callback_irq_exits = 0;
    probe_expect(mode, 0xffff, "an IRQ-delivered callback failure restores the launcher");
    CHECK(!callback_irq_armed && callback_irq_entries == 1 && callback_irq_exits == 1,
          "the callback failed after one actual IRQ delivery and reached termination");
    ticks = (ULONG)hz_200;
    for (spins = 0; spins < 10000000UL && (ULONG)hz_200 == ticks; spins++)
        __asm__ volatile ("pause");
    CHECK((ULONG)hz_200 != ticks, "timer IRQs progress after callback failure");
    /* Recover after a failed assertion so later tests cannot hang with IF=0. */
    if (flags & 0x200)
        __asm__ volatile ("sti" ::: "memory");
    else
        __asm__ volatile ("cli" ::: "memory");
    callback_test_watching = callback_irq_watching = FALSE;
    callback_test_owner = NULL;
}

static void test_callback_motion(void)
{
    WORD x = linea_vars.GCURX, y = linea_vars.GCURY;

    callback_test_watching = callback_motion_watching = TRUE;
    callback_motion_phase = callback_motion_cursors = 0;
    callback_motion_native_cursor = NULL;
    probe_expect('R', 0, "queued mouse motion survives overlapping ring-3 handlers");
    CHECK(callback_motion_phase == 5 && callback_motion_cursors == 3,
          "overlapping motion packets and the final result were all observed");
    if (callback_motion_native_cursor)
        linea_vars.user_cur = callback_motion_native_cursor;
    linea_vars.GCURX = x;
    linea_vars.GCURY = y;
    callback_test_watching = callback_motion_watching = FALSE;
    callback_test_owner = NULL;
}

static void test_callback_foreign_failure(char mode, UWORD phase)
{
    callback_test_watching = TRUE;
    callback_test_entered = FALSE;
    callback_test_child_exits = callback_test_injected = callback_test_ooms = 0;
    callback_test_child_rc = 0xffff;
    callback_test_oom_phase = phase;
    probe_expect(mode, 0xffff, "a foreign callback failure unwinds only its owner");
    x86_64_pmem_test_fail_after(0);
    CHECK(callback_test_child_exits == 1 && callback_test_child_rc == 0,
          "the invoking child independently completes and exits successfully");
    CHECK(callback_test_injected == (phase != CALLBACK_OOM_NONE) &&
          callback_test_ooms == (phase != CALLBACK_OOM_NONE),
          "the intended callback lazy-page OOM path was exercised");
    callback_test_watching = FALSE;
    callback_test_owner = NULL;
}
#endif

static void test_vdi_imports(void)
{
    SNAP s;
    int before = failures;
    long rc;

    snap(&s);
    rc = Pexec(PE_LOADGO, "VDIIMP.TOS", "", NULL);
    if (rc == EFILNF) {
        same(&s, "absent VDI imported client fixture");
        kcprintf("x86-64 VDI imports: SKIP (no VDIIMP.TOS)\n");
        return;
    }
#if CONF_WITH_X32_VDI
    CHECK(rc == 0, "loaded application imports the x32 VDI client");
    rc = Pexec(PE_LOADGO, "VDIIMP.TOS", "w", NULL);
    CHECK(rc == 0xffff, "imported VDI code is read-only in user mode");
    rc = Pexec(PE_LOADGO, "VDIIMP.TOS", "f", NULL);
    CHECK(rc == 0xffff, "a fault in an imported client reclaims workstations");
    rc = Pexec(PE_LOADGO, "VDIIMP.TOS", "", NULL);
    CHECK(rc == 0, "imported VDI remains usable after a fault");
#else
    CHECK(rc == EPLFMT, "the baseline refuses optional VDI imports");
#endif
    same(&s, "VDI imported client processes");
    probe_expect('I', 0, "VDI imports: nested Pexec, PE_LOAD/GO, relocation and invalid metadata");
    if (failures == before)
        kcprintf("x86-64 VDI imports: PASS\n");
}

#if CONF_WITH_X32_VDI
static void test_vdi_coordinate_bounds(void)
{
    struct _lineavars *saved;
    const UWORD heights[] = { 32768U, 32769U, 65535U };
    SNAP s;
    ULONG i;
    int before = failures;

    snap(&s);
    saved = kalloc(sizeof(*saved));
    CHECK(saved != NULL, "snapshot line-A state for VDI coordinate test");
    if (!saved)
        return;
    *saved = linea_vars;
    /* GOP can advertise more rows than a signed renderer WORD can
     * address. The probe sends the high coordinate expecting rejection;
     * the valid signed boundary uses an invalid output buffer so neither
     * case needs to read beyond the real framebuffer. */
    for (i = 0; i < ARRAY_SIZE(heights); i++) {
#if CONF_WITH_CUIS
        CHECK(vdi_service_test_reset(), "kernel boundary resets idle physical VDI fixture");
#endif
        linea_vars.V_REZ_VT = heights[i];
        probe_expect('P', 0, "large-screen VDI coordinate bounds and visible clipped lines");
    }
#if CONF_WITH_CUIS
    CHECK(vdi_service_test_reset(), "release physical VDI coordinate fixture");
#endif
    linea_vars = *saved;
    kfree(saved);
    same(&s, "coordinate bounds VDI test");
    if (failures == before)
        kcprintf("x86-64 VDI coordinate bounds: PASS\n");
}

static void test_vdi_padded_pitch(void)
{
    SCREEN_MODE_DESC mode;
    struct _lineavars *saved;
    SNAP s;
    ULONG x, y, stride, visible;
    volatile ULONG *row;
    BOOL pitch_ok, padding_ok = TRUE;
    int before = failures;

    screen_get_current_mode_desc(&mode);
    CHECK(mode.layout == SCREEN_LAYOUT_PACKED && mode.bits_per_pixel == 32 &&
          mode.width >= 64, "padded-pitch test has a packed GOP screen");
    if (mode.layout != SCREEN_LAYOUT_PACKED || mode.bits_per_pixel != 32 || mode.width < 64)
        return;
    snap(&s);
    saved = kalloc(sizeof(*saved));
    CHECK(saved != NULL, "snapshot line-A state for padded-pitch test");
    if (!saved)
        return;
    *saved = linea_vars;
#if CONF_WITH_CUIS
    CHECK(vdi_service_test_reset(), "kernel boundary resets physical VDI pitch fixture");
#endif

    /* Crop the visible viewport while retaining the hardware scanline
     * stride. This creates real row padding even on unpadded QEMU GOP.
     * Seed it before opening: esc_init() clears the console inside the
     * physical open, before the service could repair an incorrect pitch. */
    stride = mode.pitch / sizeof(ULONG);
    visible = (mode.width - 32) & ~7UL;
    linea_vars.V_REZ_HZ = visible;
    font_set_default();
    for (y = 0; y < mode.height; y++) {
        row = (volatile ULONG *)v_bas_ad + y * stride;
        for (x = visible; x < stride; x++)
            row[x] = 0x5a7193c5UL;
    }
    probe_expect('W', 0, "ring-3 VDI open, drawing and console clear preserve padded rows");
    pitch_ok = linea_vars.v_lin_wr == mode.pitch && linea_vars.BYTES_LIN == mode.pitch;
    for (y = 0; y < mode.height; y++) {
        row = (volatile ULONG *)v_bas_ad + y * stride;
        for (x = visible; x < stride; x++)
            if (row[x] != 0x5a7193c5UL)
                padding_ok = FALSE;
    }
#if CONF_WITH_CUIS
    CHECK(vdi_service_test_reset(), "release physical VDI pitch fixture");
#endif
    linea_vars = *saved;
    kfree(saved);
    CHECK(pitch_ok, "physical VDI open preserves the hardware pitch");
    CHECK(padding_ok, "VDI and console output leave scanline padding untouched");
    same(&s, "padded-pitch VDI test");
    if (failures == before)
        kcprintf("x86-64 VDI padded pitch: PASS\n");
}
#endif

#if CONF_WITH_CUIS
static void test_async_processes(void)
{
    PD *program;
    LONG rc, result;
    void *environment_block;
    static const char environment[] = { 0, 0 };
    char tail[] = { 1, 'p', 0 };
    UWORD i, j;
    static const char roles[] = "pozvf";
    SNAP s;
    int before = failures;
    ULONG sessions = cuis_session_count();
    for (i = 0; i < 5; i++) {
        snap(&s);
        tail[1] = roles[i];
        if (!i) {
            result = Pexec(PE_LOADGO, "C:\\ASYNC.TOS", tail, environment);
        } else {
            rc = Pexec(PE_LOAD, "C:\\ASYNC.TOS", tail, environment);
            CHECK(rc > 0, "load ordinary async process fixture");
            if (rc <= 0) break;
            program = (PD *)(uintptr_t)rc;
            environment_block = USERPTR_TO_PTR(program->p_env);
            result = Pexec(i == 2 ? PE_GO : PE_GOTHENFREE, NULL, (char *)program, NULL);
            if (i == 2) {
                CHECK(procmem_owner(program) == run && procmem_owner(environment_block) == run,
                      "PE_GO keeps caller ownership after managed descendants exit");
                CHECK(Mfree(environment_block) == E_OK && Mfree(program) == E_OK,
                      "PE_GO retained environment and basepage remain freeable");
            }
        }
        if (result != (i == 3 ? 77 : i == 4 ? 0xffff : 0))
            kcprintf("async fixture %u returned %ld\n", (unsigned)i, (long)result);
        CHECK(result == (i == 3 ? 77 : i == 4 ? 0xffff : 0),
              "ordinary Pexec bootstraps async launch, waits and orphan cleanup");
        CHECK(cuis_session_count() == sessions, "async clients' sessions are reclaimed");
        same(&s, "ordinary launch, child records, stacks, zombies and directory references reclaimed");
    }
    for (i = 0; i < 2; i++) {
        for (j = 1; j <= 2; j++) {
            snap(&s);
            rc = Pexec(PE_LOAD, "C:\\X32HELLO.TOS", "", environment);
            CHECK(rc > 0, "load group allocation failure fixture");
            if (rc <= 0) break;
            program = (PD *)(uintptr_t)rc;
            environment_block = USERPTR_TO_PTR(program->p_env);
            x86_64_pmem_test_fail_after(j);
            rc = Pexec(i ? PE_GOTHENFREE : PE_GO, NULL, (char *)program, NULL);
            x86_64_pmem_test_fail_after(0);
            CHECK(rc == ENSMEM && !ksched_active() && kproc_unlaunched(program),
                  "failed group allocation leaves an unlaunched caller-owned program");
            CHECK(procmem_owner(program) == run && procmem_owner(environment_block) == run,
                  "Pexec does not transfer ownership before group allocation succeeds");
            CHECK(Mfree(environment_block) == E_OK && Mfree(program) == E_OK,
                  "failed ordinary launch leaves both retained blocks freeable");
            same(&s, "ordinary launch group allocation failure unwinds completely");
        }
    }
    snap(&s);
    rc = Pexec(PE_LOAD, "C:\\X32HELLO.TOS", "", environment);
    CHECK(rc > 0, "load consuming launch THREAD-construction failure fixture");
    if (rc > 0) {
        program = (PD *)(uintptr_t)rc;
        environment_block = USERPTR_TO_PTR(program->p_env);
        kthread_test_fail_after_attach();
        rc = Pexec(PE_GOTHENFREE, NULL, (char *)program, NULL);
        CHECK(rc == ENSMEM && !ksched_active() && kproc_unlaunched(program),
              "THREAD construction failure leaves consuming launch unpublished");
        CHECK(procmem_owner(program) == run && procmem_owner(environment_block) == run,
              "failed THREAD construction preserves caller allocation ownership");
        CHECK(Mfree(environment_block) == E_OK && Mfree(program) == E_OK,
              "failed consuming launch allocations remain reclaimable");
    }
    same(&s, "consuming launch THREAD construction rollback");
    kcprintf("x86-64 async process calls: %s\n", failures == before ? "PASS" : "FAIL");
}

static void test_cuis_scheduler(void)
{
    PD *programs[2], *root = run;
    LONG results[2], handles[2], rc;
    static const char tail_a[] = { 1, 'a', 0 };
    static const char environment[] = { 0, 0 };
    char tail_b[] = { 2, 'b', '0', 0 };
    UWORD mode, i;
    SNAP s;
    int before = failures;
    ULONG sessions = cuis_session_count(), windows = cuis_window_count();
    BOOL focused = cuis_focus_active(), pointing = cuis_pointer_attached();

    for (mode = 0; mode < 3; mode++) {
        snap(&s);
        programs[0] = programs[1] = NULL;
        tail_b[2] = '0' + mode;
        for (i = 0; i < 2; i++) {
            rc = Pexec(PE_LOAD, "C:\\CUISCHED.TOS", i ? tail_b : tail_a, environment);
            if (rc < 0) kcprintf("scheduler load error %ld\n", (long)rc);
            CHECK(rc > 0, "load isolated scheduler client");
            if (rc <= 0) break;
            programs[i] = (PD *)(uintptr_t)rc;
            rc = proc_prepare_scheduled(programs[i]);
            CHECK(rc == E_OK, "prepare scheduler client's address space");
            if (rc < 0) break;
            run = programs[i];
            handles[i] = cuis_service(CUIS_SESSION_START, 0);
            run = root;
            CHECK(handles[i] >= 0, "register scheduler test peer");
            if (handles[i] < 0) break;
        }
        if (i == 2) {
            /* Clients discover fixture labels and exchange ordinary stream
             * packets themselves; no kernel-only message bootstrap exists. */
            run = root;
            CHECK(ksched_run(programs, 0, results) == EINVAL, "scheduler rejects empty groups");
            CHECK(ksched_run(programs, KSCHED_LIMIT + 1, results) == EINVAL, "scheduler enforces its task bound");
            {
                PD *duplicate[2] = { programs[0], programs[0] };
                CHECK(ksched_run(duplicate, 2, results) == EINVAL, "scheduler rejects duplicate programs");
            }
            {
                UWORD attempt;
                for (attempt = 1; attempt <= 2; attempt++) {
                    x86_64_pmem_test_fail_after(attempt);
                    rc = ksched_run(programs, 2, results);
                    x86_64_pmem_test_fail_after(0);
                    CHECK(rc == ENSMEM && kproc_unlaunched(programs[0]) && kproc_unlaunched(programs[1]),
                          "scheduler allocation failure preserves both unlaunched programs");
                }
            }
            kthread_stack_test_fail_after(2);
            rc = ksched_run(programs, 2, results);
            kthread_stack_test_fail_after(0);
            CHECK(rc == ENSMEM && kproc_unlaunched(programs[0]) && kproc_unlaunched(programs[1]),
                  "second scheduler stack failure rolls back the first task stack");
            {
                UWORD attempt;
                for (attempt = 1; attempt <= 2; attempt++) {
                    kthread_test_fail_attach_after(attempt);
                    rc = ksched_run(programs, 2, results);
                    kthread_test_fail_attach_after(0);
                    CHECK(rc == ENSMEM && kproc_unlaunched(programs[0]) && kproc_unlaunched(programs[1]),
                          "partial task THREAD association rolls back without starting either process");
                }
            }
            {
                ULONG started = task_thread_stages[2], stopped = task_thread_stages[3];
                ULONG reaped = task_thread_stages[4], released = task_thread_stages[7];
                rc = ksched_run(programs, 2, results);
                CHECK(rc == E_OK, "scheduler completes all managed clients");
                CHECK(task_thread_stages[2] == started + 2 && task_thread_stages[3] == stopped + 2 &&
                      task_thread_stages[4] == reaped + 2 && task_thread_stages[7] == released + 2,
                      "both runnable THREADs stop on their launcher then release each scheduler stack once on root");
            }
            if (rc == E_OK) {
                programs[0] = programs[1] = NULL;
                if (results[0] != 0 || results[1] != (mode == 0 ? 0 : mode == 1 ? 0xffff : 77))
                    kcprintf("scheduler mode %u results %ld %ld\n", (unsigned)mode, (long)results[0], (long)results[1]);
                CHECK(results[0] == 0 && results[1] == (mode == 0 ? 0 : mode == 1 ? 0xffff : 77),
                      "message handoffs, private state, FPU, nested wait, timeouts and exit status");
            }
        }
        for (i = 0; i < 2; i++) if (programs[i]) proc_discard_accessory(programs[i]);
        CHECK(run == root, "scheduler restores its launcher's process context");
        CHECK(cuis_session_count() == sessions && cuis_window_count() == windows &&
              cuis_focus_active() == focused && cuis_pointer_attached() == pointing,
              "scheduler exits retire sessions, windows, focus and pointer targets");
        same(&s, "CUIS scheduler task stacks, sessions and process resources reclaimed");
    }
    kcprintf("x86-64 CUIS scheduler: %s\n", failures == before ? "PASS" : "FAIL");
}
#endif

static void test_ring3(void)
{
    SNAP s;
    long rc;
    int k, refused = 0, ran = 0;
#if CONF_WITH_NATIVE_SCHED && CONF_WITH_PCLONE
    int public_before = failures;
    ULONG live_reaps_before = live_process_thread_reaps;
#endif

#if CONF_WITH_CUIS
    {
        int before = failures;
        CHECK(cuis_button_history_test(), "bounded CUIS history cursor, FIFO and explicit gap/policy status");
        kcprintf("x86-64 CUIS input history: %s\n", failures == before ? "PASS" : "FAIL");
    }
#endif

#if CONF_WITH_NATIVE_SCHED && CONF_WITH_PCLONE
    probe_expect(',', 0, "public SAME_PROCESS dispatch, private stacks/registers, Pterm/fault isolation, TSR refusal and exhaustion");
    probe_expect('_',0x66,"initial Pterm retains PROCESS files until final sibling exit");
    probe_expect(';',0x66,"initial fault retains PROCESS resources and final sibling status");
    {
        char bytes[2];
        LONG file=Fopen("C:\\THRLEAD.TMP",0);
        CHECK(file>=0 && Fread(file,2,bytes)==2 && bytes[0]=='O' && bytes[1]=='K',"surviving sibling uses explicit shared descriptor after leader exit");
        if(file>=0) Fclose(file);
        Fdelete("C:\\THRLEAD.TMP");
    }
    probe_expect('.',0,"PROCESS wait remains pending after leader return and consumes only final sibling exit");
    probe_expect('>',0x55,"orphan threaded PROCESS drains independently of original parent");
    {
        char bytes[2];
        LONG file=Fopen("C:\\THRGROUP.TMP",0);
        CHECK(file>=0 && Fread(file,2,bytes)==2 && bytes[0]=='O' && bytes[1]=='K',"orphan sibling preserves shared VM, file and controller ownership");
        if(file>=0) Fclose(file);
        Fdelete("C:\\THRGROUP.TMP");
    }
    {
        UQUAD generation=procmem_next_gen();
        snap(&s);
        CHECK(run_probe('/')==0x68,"final sibling Ptermres preserves legacy resident status");
        CHECK(procmem_free_ownerless_since(generation)==2,"final sibling TSR retains only basepage and environment");
        same(&s,"final sibling TSR releases transient resources");
        Fdelete("C:\\THRLEAD.TMP");
    }
    {
        UWORD stage;
        for(stage=1;stage<=5;stage++) {
            proc_clone_test_fail_after(stage);
            probe_expect('-',0,"SAME_PROCESS acquisition rollback preserves source and outputs");
        }
        proc_clone_test_cancel_publication();
        probe_expect('*',0,"SAME_PROCESS cancellation preserves source resources");
    }
    CHECK(live_process_thread_reaps > live_reaps_before, "root safely reaps stopped THREADs while PROCESS resources stay live");
    kcprintf("x86-64 SAME_PROCESS threads: %s\n", failures == public_before ? "PASS" : "FAIL");
    probe_expect('=', 0, "public Pclone executes through the real ring-3 trap");
    probe_expect('<', 77, "public clone resources survive parent retirement");
    {
        char bytes[2];
        LONG file=Fopen("C:\\PCLORPH.TMP",0);
        CHECK(file>=0 && Fread(file,2,bytes)==2 && bytes[0]=='O' && bytes[1]=='K',
              "orphan public clone executes after parent exit with inherited file");
        if(file>=0) Fclose(file);
        Fdelete("C:\\PCLORPH.TMP");
    }
    {
        UWORD stage;
        for(stage=1;stage<=5;stage++) {
            proc_clone_test_fail_after(stage);
            probe_expect('(',0,"public clone acquisition failure leaves outputs and frame unchanged");
            proc_clone_factory_test_fail_after(stage);
            probe_expect('(',0,"public clone factory rollback leaves no child or output");
        }
        proc_clone_test_cancel_publication();
        probe_expect(')',0,"public clone final cancellation leaves outputs and frame unchanged");
    }
    {
        UQUAD generation=procmem_next_gen();
        snap(&s);
        CHECK(run_probe('"')==0,"public clone Ptermres preserves explicit exit status");
        CHECK(procmem_free_ownerless_since(generation)==2,"public clone TSR retains only basepage and environment");
        same(&s,"public clone TSR transient execution resources reclaimed");
    }
    kcprintf("x86-64 public Pclone: %s\n", failures == public_before ? "PASS" : "FAIL");
#endif

    /* entry through the process-entry contract, a GEMDOS call across the
     * syscall boundary, and the exit code coming back */
    probe_expect('e', 0, "ring-3 entry state: basepage, type, stack, CPL 3, GEMDOS call");
    probe_expect('x', 0x1234, "ring-3 exit code reaches the launcher");
    {
        UQUAD flags;

        __asm__ volatile ("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
        probe_expect('x', 0x1234, "a launcher with masked IRQs retains its interrupt state");
        if (flags & 0x200)
            __asm__ volatile ("sti" ::: "memory");
    }
    {
        int before = failures;

        probe_expect('C', 0, "typed callbacks, nested foreign OLD, revocation, alternate/normal stacks and GP/SSE/red zone");
#if CONF_WITH_USER_CALLBACKS && CONF_WITH_X32_VDI
        test_callback_irq_failure('Y');
        test_callback_irq_failure('~');
        probe_expect('K', 0, "failed OLD capability allocation preserves registrations and quota");
        test_callback_motion();
        probe_expect('X', 0xffff, "a callback's privileged instruction ends only its owner");
        probe_expect('S', 0xffff, "callback stack exhaustion is contained");
        probe_expect('U', 0xffff, "an unmapped interrupted user stack cannot corrupt callback entry");
        probe_expect('Z', 0xffff, "a corrupted callback return record is contained");
        probe_expect('T', 0xffff, "a non-returning callback times out");
        test_callback_foreign_failure('%', CALLBACK_OOM_NONE);
        test_callback_foreign_failure('Q', CALLBACK_OOM_ENTRY);
        test_callback_foreign_failure('O', CALLBACK_OOM_SERVICE);
        probe_expect('C', 0, "callback state is usable after owner faults and timeouts");
#endif
        if (failures == before)
            kcprintf("x86-64 user callbacks: PASS\n");
    }
    {
        int before = failures;

        probe_expect('#', 0, "retired typed AES and generic AES trap are rejected before pointer access");
        if (failures == before)
            kcprintf("x86-64 retired AES boundary: PASS\n");
    }
    test_vdi_imports();
    probe_expect('V', 0, "x32 VDI control layout, opcode tags and bitmap bounds");
#if CONF_WITH_X32_VDI
    {
        int before = failures;

        probe_expect('W', 0, "ring-3 VDI workstation, coordinate, raster and rejected-vector calls");
        test_vdi_padded_pitch();
        test_vdi_coordinate_bounds();
        probe_expect('!', 0xffff, "a user fault reclaims its open VDI workstations");
        probe_expect('W', 0, "VDI remains usable after a client's fault");
        if (failures == before)
            kcprintf("x86-64 VDI client: PASS\n");
    }
#else
    probe_expect('W', 0, "the optional VDI services are explicitly refused in the baseline");
#endif

#if CONF_WITH_MMAP
    test_mapping_ring3();
#else
    probe_expect('4', 0, "mapping calls are unavailable when disabled");
    kcprintf("x86-64 mmap syscalls: disabled\n");
#endif

#if CONF_WITH_ELF_SHARED
    {
        int before = failures;
        ULONG shared_before;
        probe_expect('3', 0, "ring-3 shared objects: relocations, GOT/PLT, constructor, private data");
        probe_expect('2', 0, "PT_INTERP initial stack/auxv, direct and split Pexec, repeated cleanup");
        test_interpreter_failures();
        shared_before = aspace_file_cross_shares();
        probe_expect('1', 0, "filesystem LD.SO: binding, preinit/init/fini order, repeated cleanup");
        CHECK(aspace_file_cross_shares() != shared_before,
              "nested dynamic programs reuse physical DSO pages across address spaces");
        CHECK(!aspace_file_pages() && !aspace_file_references(), "runtime file page cache fully reclaimed");
        kcprintf("x86-64 LD.SO runtime: %s\n", failures == before ? "PASS" : "FAIL");
        kcprintf("x86-64 ldso relocations: %s\n", failures == before ? "PASS" : "FAIL");
        kcprintf("x86-64 PT_INTERP startup: %s\n", failures == before ? "PASS" : "FAIL");
    }
#endif

#if CONF_WITH_LIBAES
    {
        int before = failures;
        ULONG shares = aspace_file_cross_shares();
        ULONG sessions = cuis_session_count(), windows = cuis_window_count();
        ULONG visible = cuis_visible_window_count();
        BOOL updating = cuis_update_active(), focused = cuis_focus_active();
        BOOL pointing = cuis_pointer_attached();
#if CONF_WITH_X32_VDI
        SCREEN_MODE_DESC mode;
        volatile ULONG *marker = NULL;
        ULONG ticks, spins;
        CHECK(vdi_service_test_reset(), "first native open starts with an idle physical display");
        screen_get_current_mode_desc(&mode);
        if (mode.layout == SCREEN_LAYOUT_PACKED && mode.bits_per_pixel == 32 &&
            mode.width >= 256 && mode.height >= 64) {
            marker = (volatile ULONG *)((UBYTE *)v_bas_ad + 8 * mode.pitch) + mode.width - 8;
        }
        vdi_service_test_watch(marker);
#endif
        cuis_input_watching = TRUE;
        cuis_input_legacy_clean = TRUE;
        cuis_input_armed = FALSE;
        cuis_input_injections = cuis_pointer_injections = 0;
        cuis_pointer_armed = FALSE;
#if CONF_WITH_X32_VDI
        cuis_pointer_callbacks_clean = cuis_pointer_cursor_updated = FALSE;
#endif
        probe_expect(':', 0, "libaes/CUIS: nested owners, private state, geometry and fault cleanup");
        cuis_input_watching = FALSE;
        CHECK(cuis_pointer_injections == 1 && !cuis_pointer_armed,
              "CUIS pointer wait woke from a real IRQ packet and retained button edges");
        CHECK(cuis_input_legacy_clean, "focused keyboard input did not leak to the BIOS queue");
        CHECK(cuis_input_injections == 1 && !cuis_input_armed,
              "CUIS keyboard wait woke from a translated key in a real timer IRQ");
#if CONF_WITH_X32_VDI
        CHECK(cuis_pointer_guard_clean, "CUIS guarded IRQ preserves logical coordinates and defers cursor drawing");
#if CONF_WITH_USER_CALLBACKS
        CHECK(cuis_pointer_stale_clean, "CUIS input rejects superseded native VDI motion completion");
#endif
        CHECK(cuis_pointer_callbacks_clean && cuis_pointer_cursor_updated,
              "CUIS pointer routing preserves the cursor and suppresses legacy input callbacks");
        CHECK(vdi_service_test_reopen(), "kernel physical reopen preserves the timer chain");
        ticks = (ULONG)hz_200;
        for (spins = 0; spins < 10000000UL && (ULONG)hz_200 == ticks; spins++)
            __asm__ volatile ("pause");
        CHECK((ULONG)hz_200 != ticks, "timer IRQs progress after native-to-kernel display handoff");
        vdi_service_prepare_physical_open(); /* release the kernel reopen fixture */
        CHECK(marker && vdi_service_test_preserved(),
              "native first-open, last-close and fault cleanup preserve display pixels");
        vdi_service_test_watch(NULL);
        CHECK(vdi_service_test_reset(), "kernel releases idle CUIS display fixture");
#endif
        CHECK(cuis_pointer_attached() == pointing, "CUIS pointer targets reclaimed after exits");
        CHECK(cuis_focus_active() == focused, "CUIS focus reclaimed after exits");
        CHECK(cuis_update_active() == updating, "CUIS update section reclaimed after exits");
        CHECK(cuis_visible_window_count() == visible, "CUIS visible stacking repaired after exits");
        CHECK(cuis_window_count() == windows, "CUIS windows reclaimed after normal, raw and fault exits");
        CHECK(cuis_session_count() == sessions, "CUIS sessions reclaimed after normal, raw and fault exits");
        CHECK(aspace_file_cross_shares() != shares, "nested CUIS clients reuse DSO pages");
        kcprintf("x86-64 CUIS sessions: %s\n", failures == before ? "PASS" : "FAIL");
    }
    test_cuis_scheduler();
    test_async_processes();
#if CONF_WITH_MMAP
    test_ordinary_observation();
#endif
    {
        SNAP s;
        LONG result;
        ULONG sessions = cuis_session_count();
        snap(&s);
        result = Pexec(PE_LOADGO, "C:\\GEM.TOS", "", NULL);
        CHECK(result == 0, "standard GEM lifecycle, private globals and windowless timers");
        CHECK(cuis_session_count() == sessions, "implicit GEM sessions reclaimed on exit and _Exit");
        CHECK(cuis_context_test_reset(), "shared shell context persists after the final GUI client exits");
        same(&s, "GEM API processes and their async children reclaimed");
        snap(&s);
        result = Pexec(PE_LOADGO, "C:\\GEMMSG.TOS", "", NULL);
        if (result) kcprintf("GEM messages fixture returned %ld\n", (long)result);
        CHECK(result == 0, "standard GEM discovery, byte pipes, backpressure and receiver exit");
        CHECK(cuis_session_count() == sessions, "GEM discovery and message sessions reclaimed");
        same(&s, "GEM message queues, blocked snapshots and peer processes reclaimed");
        snap(&s);
        result = Pexec(PE_LOADGO, "C:\\GEMEVNT.TOS", "", NULL);
        if (result) kcprintf("GEM events fixture returned %ld\n", (long)result);
        CHECK(result == 0, "standard GEM combined waits, zero polls, snapshots and copyout errors");
        CHECK(cuis_session_count() == sessions, "GEM event sessions reclaimed after normal/fault exits");
        same(&s, "GEM event waiters, pipe bytes and child processes reclaimed");
        snap(&s);
        {
            ULONG windows = cuis_window_count();
            result = Pexec(PE_LOADGO, "C:\\GEMWIN.TOS", "", NULL);
            if (result) kcprintf("GEM windows fixture returned %ld\n", (long)result);
            CHECK(result == 0, "standard GEM window lifecycle, geometry and isolated identifiers");
            CHECK(cuis_window_count() == windows, "GEM windows reclaimed after session close and raw exit");
        }
        CHECK(cuis_session_count() == sessions, "GEM window sessions reclaimed");
        same(&s, "GEM window clients and metadata reclaimed");
        snap(&s);
        {
            ULONG windows = cuis_window_count();
            result = Pexec(PE_LOADGO, "C:\\GEMUPD.TOS", "", NULL);
            if (result) kcprintf("GEM update fixture returned %ld\n", (long)result);
            CHECK(result == 0, "standard GEM nested update locks, contention and visible regions");
            CHECK(cuis_window_count() == windows && !cuis_update_active(), "GEM update owners and windows reclaimed");
        }
        CHECK(cuis_session_count() == sessions, "GEM update sessions reclaimed");
        same(&s, "GEM update clients and temporary regions reclaimed");
        snap(&s);
        {
            ULONG windows = cuis_window_count();
            result = Pexec(PE_LOADGO, "C:\\GEMFOC.TOS", "", NULL);
            if (result) kcprintf("GEM focus fixture returned %ld\n", (long)result);
            CHECK(result == 0, "standard GEM redraw messages, stacking and isolated owner queries");
            CHECK(cuis_window_count() == windows && !cuis_focus_active(), "GEM message windows and focus reclaimed");
        }
        CHECK(cuis_session_count() == sessions, "GEM window message sessions reclaimed");
        same(&s, "GEM notices, application pipes and focus clients reclaimed");
        snap(&s);
        result = Pexec(PE_LOADGO, "C:\\GEMINP.TOS", "", NULL);
        if (result) kcprintf("GEM input fixture returned %ld\n", (long)result);
        CHECK(result == 0, "standard GEM button, rectangle and combined input events");
        CHECK(cuis_session_count() == sessions && !cuis_focus_active(), "GEM input session and focus reclaimed");
        same(&s, "GEM input predicates, windows and client reclaimed");
        snap(&s);
        result = Pexec(PE_LOADGO, "C:\\GEMMCTL.TOS", "", NULL);
        if (result) kcprintf("GEM mouse control fixture returned %ld\n", (long)result);
        CHECK(result == 0, "standard GEM mouse-control nesting, contention and cleanup");
        CHECK(cuis_session_count() == sessions && !cuis_mouse_active(), "GEM mouse owners and sessions reclaimed");
        same(&s, "GEM mouse grab clients, windows and process resources reclaimed");
        snap(&s);
        result = Pexec(PE_LOADGO, "C:\\GEMDRAW.TOS", "", NULL);
        if (result) kcprintf("GEM object drawing fixture returned %ld\n", (long)result);
        CHECK(result == 0, "userspace GEM object drawing, state changes and callbacks");
        CHECK(cuis_session_count() == sessions && !cuis_update_active(), "GEM renderer sessions and updates reclaimed");
        same(&s, "GEM renderer workstations and callback process resources reclaimed");
#if CONF_WITH_X32_VDI
        snap(&s);
        test_renderer_cursor();
        CHECK(cuis_session_count() == sessions && !cuis_update_active(), "cursor repaint sessions and updates reclaimed");
        same(&s, "cursor repaint workstations and process resources reclaimed");
#endif
    }
#else
    probe_expect(':', 0, "CUIS services are unavailable when disabled");
#endif

    /* each process gets fresh private pages: what the first leaves in its
     * data, the second does not see */
    probe_expect('i', 0, "first run starts with zeroed data");
    probe_expect('i', 0, "second run starts with zeroed data too");

    /* bad pointers as system call arguments, and unused argument registers */
    probe_expect('b', 0, "bad arguments refused by the system calls");
    probe_expect('r', 0, "arguments a call does not use are not looked at");
    probe_expect('o', 0, "Rwabs() from ring 3 fills the caller's buffer in place");
    probe_expect('s', 0, "ring 3 cannot install kernel callbacks or write the kernel variables");

    /* basepages a ring-3 process can use, and launches it cannot forge (#416) */
    probe_expect('g', 0, "ring 3 builds, launches and frees basepages; forged launches are refused");

    /* the ancestors' basepages: one (this launcher's), read-only */
    probe_expect('w', 0x101, "a ring-3 process sees its launcher's basepage as a scrubbed copy");
    probe_expect('u', 0xffff, "the ancestors page is read-only");

    /* a fault in ring 3 ends that process and nothing else */
    probe_expect('f', 0xffff, "a ring-3 write to page 0 is contained");
    probe_expect('k', 0xffff, "ring-3 access to the system variables is contained");
    probe_expect('p', 0xffff, "a privileged instruction in ring 3 is contained");
    probe_expect('e', 0, "a process runs normally after the faults");
    snap(&s);
    x86_64_memtest_fail_next_exit_handoff();
#if CONF_WITH_NATIVE_SCHED
    CHECK(run_probe('n') == 0, "nested failed exit handoff preserves legacy termination");
#else
    CHECK(run_probe('e') == 0, "failed exit handoff preserves legacy termination");
#endif
    same(&s, "failed exit handoff rollback");
    observe_probe_fault = TRUE;
#if !CONF_WITH_NATIVE_SCHED
    /* A managed THREAD must never fall back while still attached. The
     * nested probe above covers its untracked child's refusal path. */
    x86_64_memtest_fail_next_exit_handoff();
#endif
    CHECK(run_probe('\001') == 0xffff, "fault exit preserves full result and independent observation");
    observe_probe_fault = FALSE;
    same(&s, "failed fault handoff observer rollback");
#if CONF_WITH_NATIVE_SCHED
    CHECK(fallback_observer_stages == 2, "nested refused exit publishes observer after stack reclamation");
#else
    CHECK(fallback_observer_stages == 4, "refused normal/fault exits publish observers after stack reclamation");
#endif

    /*
     * A ring-3 process runs another from inside its own system call
     * (Pexec of C:\X32HELLO.TOS, an x32 ELF on the boot drive -- CI puts one
     * there): the child makes calls of its own on its own kernel stack, exits,
     * and the parent resumes on its own stack, page tables and user RSP.
     * Without the file this cannot be tried; that is reported, and CI, which
     * supplies it, requires the PASS line.
     */
    {
        CPUSTATE before, after;
        PD *me = run;

        snap(&s);
        cpustate(&before);
        bzero(exit_handoff_stages, sizeof(exit_handoff_stages));
        exit_handoff_watch = TRUE;
        rc = run_probe('n');
        exit_handoff_watch = FALSE;
        cpustate(&after);
        if (rc != 0x100) {
            CHECK(exit_handoff_stages[0] >= 2 &&
                  exit_handoff_stages[0] == exit_handoff_stages[1] &&
                  exit_handoff_stages[1] == exit_handoff_stages[2] &&
                  exit_handoff_stages[2] == exit_handoff_stages[3],
                  "nested launches transfer and finalize each syscall stack once");
        }
        CHECK(run == me, "the launcher is the current process again after a nested launch");
        CHECK(after.cr3 == before.cr3 && after.gs == before.gs && after.kernel_gs == before.kernel_gs,
              "CR3 and the GS bases are back after a nested launch");
        if (rc == 0x100) {
            kcprintf("x86-64 nested pexec: SKIP (no C:\\X32HELLO.TOS)\n");
        } else {
            CHECK(rc == 0, "a ring-3 process runs a child and resumes");
            if (rc == 0)
                kcprintf("x86-64 nested pexec: PASS\n");
            else
                kcprintf("x86-64 nested pexec: FAIL (0x%lx)\n", rc);
        }
        same(&s, "nested Pexec from ring 3");
    }

#if CONF_WITH_PCLONE
    probe_expect('^', 0, "Pclone validates ring-3 arguments before rejecting the unavailable backend");
#endif
    /* a Pexec() that runs out of loan slots half way gives back what it took */
    probe_expect('z', 0, "a failed Pexec(PE_BASEPAGE) from ring 3 releases its blocks and loans");

    /* BIOS and XBIOS pointer arguments are checked per call (#352) */
    probe_expect('A', 0, "bad BIOS/XBIOS pointers are refused, calls that would give the kernel a user address are refused, valid ones let through");
    {
        /* the probe's Setpalette() was given a valid array and changed it at
         * once: what the kernel kept is its own copy of the original */
        const UWORD *kept = x86_64_ring3_palette();
        int k, same_pal = 1;

        for (k = 0; k < 16; k++)
            if (kept[k] != (UWORD)(0x0100 + 0x11 * k))
                same_pal = 0;
        CHECK(same_pal, "Setpalette() from ring 3 keeps a copy of the palette");
        CHECK(colorptr == NULL || colorptr == kept,
              "the palette the kernel will load is its own, not the process's");
    }
    {
        long rc = run_probe('E');

        if (rc == 0x100)
            kcprintf("x86-64 DMAread/DMAwrite checks: SKIP (no disk)\n");
        else
            CHECK(rc == 0, "DMAread/DMAwrite from ring 3 check buffers, counts and devices");
    }

    /* GEMDOS pointer arguments are checked per call (#437) */
    probe_expect('c', 0, "bad GEMDOS pointers and lengths are refused, valid ones let through");

    /* the search state of a DTA is the kernel's (#437) */
    probe_expect('d', 0, "a tampered DTA cannot make Fsnext() misbehave");

    /* the basepage's p_uft/p_curdir are not the kernel's tables (#418) */
    probe_expect('t', 0, "overwriting the basepage's file and directory tables harms nothing");

    /* PE_LOAD then PE_GOTHENFREE, and a child freeing its parent's block */
    {
        snap(&s);
        rc = run_probe('l');
        if (rc == 0x100) {
            kcprintf("x86-64 pexec load: SKIP (no C:\\X32HELLO.TOS)\n");
        } else {
            CHECK(rc == 0, "ring 3 loads a program with PE_LOAD and launches it with PE_GOTHENFREE");
            kcprintf(rc == 0 ? "x86-64 pexec load: PASS\n" : "x86-64 pexec load: FAIL (0x%lx)\n", rc);
        }
        same(&s, "PE_LOAD and PE_GOTHENFREE from ring 3");
        snap(&s);
        rc = run_probe('y');
        if (rc != 0x100) {
            if (rc != 0)
                kcprintf("memtest: child Mfree probe: exit code 0x%lx\n", rc);
            CHECK(rc == 0, "a child cannot free its parent's basepage");
        }
        same(&s, "a child's Mfree of its parent's block");
    }

    /* the chain of ancestor basepages grows by one level per launch */
    {
        snap(&s);
        rc = run_probe('v');
        if (rc == 0x100) {
            kcprintf("x86-64 ancestors: SKIP (no C:\\X32HELLO.TOS)\n");
        } else {
            CHECK(rc == 0, "a grandchild sees its launcher's and the launcher's launcher's basepage");
            kcprintf(rc == 0 ? "x86-64 ancestors: PASS\n" : "x86-64 ancestors: FAIL (0x%lx)\n", rc);
        }
        same(&s, "ancestor chain of a nested launch");
    }

    /* a program whose code has an unaligned absolute relocation is loaded
     * and runs correctly (#433) */
    {
        snap(&s);
        rc = run_probe('a');
        if (rc == 0x100) {
            kcprintf("x86-64 unaligned relocation: SKIP (no C:\\X32RELOC.TOS)\n");
        } else {
            CHECK(rc == 0, "a program moved into the image window is relocated, unaligned slot included");
            kcprintf(rc == 0 ? "x86-64 unaligned relocation: PASS\n" : "x86-64 unaligned relocation: FAIL (0x%lx)\n", rc);
        }
        same(&s, "a program with an unaligned relocation");
    }

    /* a new process is mapped at its link address, relocated into it if need be (#434) */
    {
        snap(&s);
        rc = run_probe('j');
        if (rc == 0x100) {
            kcprintf("x86-64 private image: SKIP (no C:\\X32HELLO.TOS or X32RELOC.TOS)\n");
        } else {
            CHECK(rc == 0, "a new process has its image in the image window, the text read-only");
            kcprintf(rc == 0 ? "x86-64 private image: PASS\n" : "x86-64 private image: FAIL (0x%lx)\n", rc);
        }
        same(&s, "a program mapped into the image window");
    }

    /* Mfree() of an ordinary block is given the pointer as an x32 value (#434) */
    probe_expect('F', 0, "Mfree of a basepage through a pointer with upper bits set");

    /* PE_LOAD puts the program in the caller's heap, and freeing the basepage
     * gives it back (#434) */
    {
        snap(&s);
        rc = run_probe('L');
        if (rc == 0x100) {
            kcprintf("x86-64 loaded program in the caller: SKIP (no C:\\X32HELLO.TOS)\n");
        } else {
            CHECK(rc == 0, "the program PE_LOAD loaded is in the caller's heap, and goes back with its basepage");
            kcprintf(rc == 0 ? "x86-64 loaded program in the caller: PASS\n" : "x86-64 loaded program in the caller: FAIL (0x%lx)\n", rc);
        }
        same(&s, "a program loaded into the caller");
    }

    /* PE_LOAD then rejected PE_GO: the staged image remains reclaimable. */
    {
        snap(&s);
        rc = run_probe('G');
        if (rc == 0x100) {
            kcprintf("x86-64 pexec go rejected: SKIP (no C:\\X32HELLO.TOS)\n");
        } else {
            CHECK(rc == 0, "PE_GO rejects without altering the staged program");
            kcprintf(rc == 0 ? "x86-64 pexec go rejected: PASS\n" : "x86-64 pexec go rejected: FAIL (0x%lx)\n", rc);
        }
        same(&s, "PE_LOAD and rejected PE_GO from ring 3");
    }

    /* a program bigger than the image window, loaded by PE_LOAD (#434) */
    {
        snap(&s);
        rc = run_probe('B');
        if (rc == 0x100) {
            kcprintf("x86-64 pexec load big: SKIP (no C:\\X32BIG.TOS)\n");
        } else {
            CHECK(rc == 0, "PE_LOAD takes a program bigger than the image window");
            kcprintf(rc == 0 ? "x86-64 pexec load big: PASS\n" : "x86-64 pexec load big: FAIL (0x%lx)\n", rc);
        }
        same(&s, "PE_LOAD of a big program");
    }

    /* a program whose file is cut short: the loading fails with the new
     * process's address space in use (#434) */
    {
        snap(&s);
        rc = run_probe('D');
        if (rc != 0x100) {
            CHECK(rc == 0, "a failed load puts the loader's address space back");
            kcprintf(rc == 0 ? "x86-64 pexec truncated: PASS\n" : "x86-64 pexec truncated: FAIL (0x%lx)\n", rc);
        }
        same(&s, "a failed load");
    }

    /* A heap gap and rejected PE_GO followed by consuming mode 6. */
    {
        snap(&s);
        rc = run_probe('H');
        if (rc != 0x100) {
            CHECK(rc == 0, "PE_GO rejection preserves a staged image for mode 6");
            kcprintf(rc == 0 ? "x86-64 pexec go rejected with a gap: PASS\n" : "x86-64 pexec go rejected with a gap: FAIL (0x%lx)\n", rc);
        }
        same(&s, "rejected PE_GO and PE_GOTHENFREE with a gap");
    }

    /* PE_LOAD, PE_GO(THENFREE) and mode 0 with a physical allocation failing at every
     * point: whatever fails, nothing is leaked and the caller carries on (#434) */
    snap(&s);
    for (k = 1; k <= 120; k += (k < 24) ? 1 : 4) {
        x86_64_pmem_test_fail_after(k);
        probe_may_fail_early = TRUE;
        (void)run_probe('l');
        (void)run_probe('G');
        (void)run_probe('H');
        (void)run_probe('j');
        probe_may_fail_early = FALSE;
        x86_64_pmem_test_fail_after(0);
    }
    same(&s, "failed PE_LOAD launches");

#if CONF_WITH_DEMAND_PAGING
    /* Psetlimit(): the limits, the stack size, inheritance (#450) */
    {
        snap(&s);
        rc = run_probe('N');
        if (rc == 0x100) {
            kcprintf("x86-64 psetlimit: SKIP (no C:\\X32HELLO.TOS)\n");
        } else {
            CHECK(rc == 0, "Psetlimit sets and reports the limits, the stack grows to its limit, children inherit");
            kcprintf(rc == 0 ? "x86-64 psetlimit: PASS\n" : "x86-64 psetlimit: FAIL (0x%lx)\n", rc);
        }
        same(&s, "a program that sets limits");
    }

    /* a program's memory is backed when it is touched (#450) */
    {
        snap(&s);
        rc = run_probe('M');
        if (rc == 0x100) {
            kcprintf("x86-64 demand paging: SKIP (no C:\\X32HELLO.TOS)\n");
        } else {
            CHECK(rc == 0, "address space beyond the machine's memory is lazy, and running out ends only the process");
            kcprintf(rc == 0 ? "x86-64 demand paging: PASS\n" : "x86-64 demand paging: FAIL (0x%lx)\n", rc);
        }
        same(&s, "a program that runs out of memory");
    }
#else
    /* without demand paging (#562) the Psetlimit() and overcommit probes above
     * do not apply: they expect the 16 MiB default stack and Malloc() of more
     * than the machine has.  What does: memory backed when handed out. */
    kcprintf("x86-64 psetlimit: SKIP (no demand paging)\n");
    kcprintf("x86-64 demand paging: SKIP (no demand paging)\n");
    {
        snap(&s);
        rc = run_probe('?');
        if (rc == 0x100) {
            kcprintf("x86-64 no demand paging: SKIP (no C:\\X32HELLO.TOS)\n");
        } else {
            CHECK(rc == 0, "without demand paging, memory is backed when it is handed out and Malloc(-1) is no more than that");
            kcprintf(rc == 0 ? "x86-64 no demand paging: PASS\n" : "x86-64 no demand paging: FAIL (0x%lx)\n", rc);
        }
        same(&s, "a program without demand paging");
    }
#endif

    /* a program loaded from a file gets Malloc() memory of its own (#434) */
    {
        snap(&s);
        rc = run_probe('h');
        if (rc == 0x100) {
            kcprintf("x86-64 private heap: SKIP (no C:\\X32HELLO.TOS)\n");
        } else {
            CHECK(rc == 0, "a loaded program can Malloc, Mshrink and Mfree private memory");
            kcprintf(rc == 0 ? "x86-64 private heap: PASS\n" : "x86-64 private heap: FAIL (0x%lx)\n", rc);
        }
        same(&s, "a program that allocates memory");
    }

    /*
     * The same with a child that FAULTS: the other way back into the
     * launcher's context (no syscall, so no swapgs to undo; the exception
     * stack, not the child's syscall stack), which must put the ring-3
     * parent's stack, page tables, saved RSP and GS state back too.
     */
    {
        CPUSTATE before, after;
        PD *me = run;

        snap(&s);
        cpustate(&before);
        rc = run_probe('m');
        cpustate(&after);
        CHECK(run == me, "the launcher is the current process again after a nested fault");
        CHECK(after.cr3 == before.cr3 && after.gs == before.gs && after.kernel_gs == before.kernel_gs,
              "CR3 and the GS bases are back after a nested fault");
        if (rc == 0x100) {
            kcprintf("x86-64 nested fault: SKIP (no C:\\X32HELLO.TOS)\n");
        } else {
            CHECK(rc == 0, "a ring-3 process survives its child's fault and resumes");
            if (rc == 0)
                kcprintf("x86-64 nested fault: PASS\n");
            else
                kcprintf("x86-64 nested fault: FAIL (0x%lx)\n", rc);
        }
        same(&s, "nested fault from ring 3");
    }

    /*
     * Children that terminate and stay resident (Ptermres) from a ring-3
     * parent: the blocks lent to the parent for each must come back, or its
     * launches start failing once the borrow slots are used up (each child
     * takes two; there are eight).  The resident memory itself is permanent
     * by design: once the behaviour is verified, the test gives it back, so
     * a boot with the self-test enabled does not keep it for good.
     */
    {
        SNAP before, after;
        UQUAD first_gen = procmem_next_gen();

        snap(&s);
        snap(&before);
        rc = run_probe('q');
        snap(&after);
        if (rc == 0x100) {
            kcprintf("x86-64 nested ptermres: SKIP (no C:\\X32HELLO.TOS)\n");
        } else {
            CHECK(rc == 0, "a ring-3 process launches children that stay resident, again and again");
            if (rc == 0)
                kcprintf("x86-64 nested ptermres: PASS\n");
            else
                kcprintf("x86-64 nested ptermres: FAIL (0x%lx)\n", rc);
            /* each live window allocation has one tracking record (a heap
             * block), so the resident blocks account for exactly the heap
             * growth */
            {
                long grown = (long)after.pm.live_allocs - (long)before.pm.live_allocs;

                if (after.kprocs != before.kprocs || after.dir_refs != before.dir_refs ||
                    (long)after.heap.live_blocks - (long)before.heap.live_blocks != grown)
                    kcprintf("memtest: ptermres: kprocs %ld->%ld heap blocks %ld->%ld "
                             "window allocs %ld->%ld dir refs %ld->%ld\n",
                             (long)before.kprocs, (long)after.kprocs,
                             (long)before.heap.live_blocks, (long)after.heap.live_blocks,
                             (long)before.pm.live_allocs, (long)after.pm.live_allocs,
                             (long)before.dir_refs, (long)after.dir_refs);
                CHECK(after.kprocs == before.kprocs && after.dir_refs == before.dir_refs &&
                      (long)after.heap.live_blocks - (long)before.heap.live_blocks == grown,
                      "resident children leave no process records, references or stray heap blocks behind");
                CHECK(grown > 0 && grown <= 20, "only the resident blocks themselves stay allocated");
                CHECK((long)procmem_free_ownerless_since(first_gen) == grown,
                      "the resident blocks are all given back");
            }
        }
    }

    same(&s, "resident children, once given back");

    /* a process that has been launched cannot be prepared (and launched) a
     * second time: its kernel stack went to gouser() */
    snap(&s);
    {
        PD *pd = new_basepage();

        if (pd) {
            UQUAD top, stack;

            set_owner(pd, pd);
            set_owner(USERPTR_TO_PTR(pd->p_env), pd);
            CHECK(kproc_prepare_user(pd, run), "a basepage prepares");
            stack = kproc_take_kernel_stack(pd, &top);
            CHECK(stack != 0, "its kernel stack can be taken");
            CHECK(!kproc_prepare_user(pd, run), "a prepared process whose stack is gone is refused");
            kstack_free(stack);
            proc_free_owned(pd);
        }
    }
    same(&s, "relaunch refusal");

    /* an allocation failure while loading the image is an ordinary refused
     * launch, tried at every allocation point */
    snap(&s);
    /* (the 256 KiB stack alone is 64 pages: step wider once past the first few) */
    for (k = 1; k <= 240; k += (k < 24) ? 1 : 5) {
        x86_64_pmem_test_fail_after(k);
        probe_may_fail_early = TRUE;
        rc = run_probe('x');
        probe_may_fail_early = FALSE;
        x86_64_pmem_test_fail_after(0);
        if (rc == 0x1234)
            ran++;
        else if (rc == ENSMEM)
            refused++;
        else if (rc == 0xffff)
            ;                   /* (the stack is lazy: it ran, and was ended for want of a page) */
        else if (rc != -1000)
            CHECK(FALSE, "a launch under memory pressure either runs, is refused or loses a page");
    }
    if (refused < 5 || ran < 1)
        kcprintf("memtest: %d launches refused, %d ran\n", refused, ran);
    CHECK(refused >= 5 && ran >= 1, "image launch failures were exercised");
    same(&s, "failed ring-3 launches");
}

/* Private pages move from one address space to another (#434). */
static void test_move_private(void)
{
    ASPACE *a, *b;
    UBYTE pattern[64], back[64];
    UWORD prot;
    ULONG i;
    SNAP s;

    snap(&s);
    a = must_create("move source");
    b = must_create("move destination");
    if (a && b) {
        for (i = 0; i < sizeof(pattern); i++)
            pattern[i] = (UBYTE)(i + 1);
        CHECK(aspace_map_private(a, 0x10000000, 3 * PAGE, ASPACE_PROT_WRITE | ASPACE_PROT_USER),
              "block mapped in the source");
        CHECK(aspace_copy_to_user(a, 0x10001000, pattern, sizeof(pattern)), "block filled");
        CHECK(aspace_private_pages(a) == 3 && aspace_private_pages(b) == 0, "owned pages before");

        /* a range that is not all private to the source is refused, and nothing moves */
        CHECK(!aspace_move_private(a, b, 0x10000000, 4 * PAGE, ASPACE_PROT_USER),
              "a range that is not all private is refused");
        CHECK(!aspace_translate(b, 0x10000000, NULL, NULL), "nothing moved by the refused call");

        CHECK(aspace_move_private(a, b, 0x10000000, 3 * PAGE, ASPACE_PROT_WRITE | ASPACE_PROT_USER),
              "block moved");
        CHECK(!aspace_translate(a, 0x10000000, NULL, NULL) &&
              !aspace_translate(a, 0x10002000, NULL, NULL), "gone from the source");
        CHECK(aspace_private_pages(a) == 0 && aspace_private_pages(b) == 3, "owned pages after");
        CHECK(aspace_copy_from_user(b, back, 0x10001000, sizeof(back)) &&
              memcmp(back, pattern, sizeof(back)) == 0, "contents kept");

        /* the destination changes the permissions of a page: read + execute */
        CHECK(aspace_protect_private(b, 0x10001000, PAGE, ASPACE_PROT_EXEC | ASPACE_PROT_USER),
              "permissions changed");
        CHECK(aspace_translate(b, 0x10001000, NULL, &prot) &&
              (prot & ASPACE_PROT_EXEC) && !(prot & ASPACE_PROT_WRITE), "execute, not write");
        CHECK(!aspace_protect_private(b, 0x20000000, PAGE, ASPACE_PROT_USER),
              "an unmapped page cannot be protected");

        /* the source can map the addresses again; the destination frees what it got */
        CHECK(aspace_map_private(a, 0x10000000, PAGE, ASPACE_PROT_USER), "source reuses the addresses");
        CHECK(!aspace_unmap_private(b, 0x10000000, 4 * PAGE), "unmap refuses a range with a foreign page");
        CHECK(aspace_unmap_private(b, 0x10000000, 3 * PAGE), "destination unmaps what it was given");
    }
    aspace_destroy(a);
    aspace_destroy(b);
    same(&s, "moving private pages");
}

/* A copy of the probe image with one field changed must be refused. */
static void mutated(const PROG_IMAGE *good, ULONG offset, ULONG size, ULONG value, const char *what)
{
    UBYTE *copy = kalloc(good->size);
    PROG_IMAGE bad;
    ULONG i;

    CHECK(copy != NULL, "image copy");
    if (!copy)
        return;
    memcpy(copy, good->data, good->size);
    for (i = 0; i < size; i++)
        copy[offset + i] = (UBYTE)(value >> (8 * i));
    bad.data = copy;
    bad.size = good->size;
    CHECK(!image_check(&bad, NULL), what);
    kfree(copy);
}

static void test_x32image(void)
{
    const PROG_IMAGE *good = testprobe_image();
    PROG_IMAGE cut;
    UQUAD entry = 0;
    ASPACE *as;
    SNAP s;
    PROG_LAYOUT layout;
    ULONG segment;

    snap(&s);
    CHECK(image_check(good, &entry) && entry >= USER_IMAGE_BASE &&
          entry < USER_IMAGE_BASE + USER_IMAGE_SIZE, "probe image is valid");
#if CONF_WITH_CLI && !CONF_EXTERNAL_CLI
    CHECK(image_check(emucon_image(), &entry), "EmuCON image is valid");
#endif

    cut = *good;
    cut.size = 40;
    CHECK(!image_check(&cut, NULL), "truncated header refused");
    cut.size = 52 + 32;                 /* header and one program header only */
    CHECK(!image_check(&cut, NULL), "program headers beyond the file refused");

    mutated(good, 4, 1, 2, "ELFCLASS64 refused");
    mutated(good, 5, 1, 2, "big-endian refused");
    mutated(good, 16, 2, 3, "shared object refused");
    mutated(good, 18, 2, 3, "other machine refused");
    mutated(good, 44, 2, 0, "no program headers refused");
    mutated(good, 24, 4, 0x500000, "entry point outside the image refused");
    /* The probe grows as new ABI checks are linked in. Select a real data
     * segment instead of assuming that text always ends before 0x405000. */
    if (image_layout(good, &layout)) {
        for (segment = 0; segment < layout.nseg; segment++)
            if (!(layout.seg[segment].flags & 1))
                break;                  /* ELF PF_X */
        CHECK(segment < layout.nseg, "probe has a non-executable segment");
        if (segment < layout.nseg)
            mutated(good, 24, 4, (ULONG)layout.seg[segment].vaddr,
                    "entry point in a non-executable segment refused");
    }
    mutated(good, 52 + 8, 4, 0x100000, "segment below the image window refused");
    mutated(good, 52 + 8, 4, 0x7ff000, "segment running past the image window refused");
    mutated(good, 52 + 24, 4, 7, "a writable and executable segment refused");
    mutated(good, 52 + 16, 4, 0x7fffffff, "file size beyond the file refused");
    mutated(good, 52 + 20, 4, 0, "empty segment refused");
    mutated(good, 52 + 32 + 8, 4, 0x400000, "overlapping segments refused");

    /* loading fills the pages and refuses what check() refuses */
    as = must_create("image address space");
    if (as) {
        CHECK(image_load(as, good, &entry), "image loads");
        CHECK(aspace_user_range_ok(as, entry, 16, FALSE) &&
              !aspace_user_range_ok(as, entry, 16, TRUE), "text is read-only");
        aspace_destroy(as);
    }
    same(&s, "image loader");

    /* a program loaded from a file needs room after its image for the startup
     * area, all of it, and is refused without (#434) */
    snap(&s);
    {
        PD *pd = new_basepage();

        if (pd) {
            PROG_LAYOUT l;

            memset(&l, 0, sizeof l);
            l.nseg = 1;
            l.seg[0].vaddr = USER_IMAGE_BASE;
            l.seg[0].flags = 5;                 /* PF_R | PF_X */
            l.entry = USER_IMAGE_BASE;
            l.seg[0].memsz = (ULONG)(USER_IMAGE_SIZE - USER_IMAGE_SLACK) + 0x1000;
            l.end = l.seg[0].vaddr + l.seg[0].memsz;
            CHECK(kproc_load_begin(pd, USER_IMAGE_BASE, USER_IMAGE_SIZE),
                  "an address space for the program");
            CHECK(!kproc_set_loaded_image(pd, &l), "no room for the startup area: refused");
            l.seg[0].memsz = (ULONG)(USER_IMAGE_SIZE - USER_IMAGE_SLACK);
            l.end = l.seg[0].vaddr + l.seg[0].memsz;
            CHECK(kproc_set_loaded_image(pd, &l), "room for the startup area: accepted");
            set_owner(pd, pd);
            set_owner(USERPTR_TO_PTR(pd->p_env), pd);
            proc_free_owned(pd);
        }
    }
    same(&s, "startup area room");
}

void x86_64_memtest_run(void)
{
    SNAP s;

    failures = 0;

    /* Give the boot process a current-directory reference, so that every
     * basepage created below inherits (and must give back) a real one;
     * without it the reference checks would be vacuous. */
    if (Dsetpath("\\") == 0)
        CHECK(dir_refs() > 0, "boot process holds a directory reference");

    snap(&s);

    test_pmem();
    test_kheap();
    test_procmem();
    test_aspace();
    test_isolation();
    test_process_validation();
    test_kthread_relationships();
    test_proc_clone();
    test_proc_clone_invalidation();
    test_proc_clone_factory();
#if CONF_WITH_NATIVE_SCHED
    test_proc_clone_startup();
    test_return_bridge();
#if CONF_WITH_MMAP
    test_clone_execution();
#endif
#endif
    test_kthread_live_disposal();
    test_kthread_completion();
    test_kproc_pending_ownership();
    test_parent_identity_reuse();
    test_kproc();
    test_kproc_ranges();
    test_lifecycle();
    test_x32image();
    test_move_private();
    test_backing_media_change();
    test_unlinked_backing();
    test_ring3();

    same(&s, "whole memtest");
    CHECK(x86_64_pmem_bad_frees() >= s.pmem_bad, "bad free counter monotonic");

    if (failures)
        kcprintf("x86-64 memtest: FAIL (%d)\n", failures);
    else
        kcprintf("x86-64 memtest: PASS\n");
}

#endif /* CONF_WITH_X86_64_MEMTEST */
