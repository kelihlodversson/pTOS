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
#include "string.h"
#include "bdosdefs.h"
#include "bdosbind.h"
#include "gemdos.h"
#include "gemerror.h"
#include "kheap.h"
#include "procmem.h"
#include "kprint.h"
#include "pgtable.h"
#include "pmem.h"
#include "pc_x86_64_memory.h"
#include "io.h"
#include "../../../bdos/kproc.h"

extern void set_owner(void *addr, PD *p);
extern void x86_64_free_owned(PD *p);
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

/* Everything that must be exactly the same before and after a cycle. */
typedef struct {
    UQUAD pmem_free;
    UQUAD pmem_bad;
    KHEAP_STATS heap;
    PROCMEM_STATS pm;
    ULONG kprocs;
} SNAP;

static void snap(SNAP *s)
{
    s->pmem_free = x86_64_pmem_free_bytes();
    s->pmem_bad = x86_64_pmem_bad_frees();
    kheap_stats(&s->heap);
    x86_64_procmem_stats(&s->pm);
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
         a->kprocs == b.kprocs;
    if (!ok) {
        kcprintf("memtest FAIL: %s leaked: pmem %ld->%ld heap blocks %ld->%ld "
                 "pages %ld->%ld window allocs %ld->%ld free %ld->%ld kprocs %ld->%ld\n",
                 what, (long)a->pmem_free, (long)b.pmem_free,
                 (long)a->heap.live_blocks, (long)b.heap.live_blocks,
                 (long)a->heap.pages, (long)b.heap.pages,
                 (long)a->pm.live_allocs, (long)b.pm.live_allocs,
                 (long)a->pm.free_pages, (long)b.pm.free_pages,
                 (long)a->kprocs, (long)b.kprocs);
        failures++;
    }
    return ok;
}

/* ---- physical page allocator ------------------------------------- */

static void test_pmem(void)
{
    SNAP s;
    UQUAD a, b, c, limited;
    UQUAD bad, n, i;
    UQUAD *runs;

    snap(&s);

    a = x86_64_pmem_try_alloc_pages(4, 0);
    CHECK(a != X86_64_PMEM_NONE && !(a & (PAGE - 1)), "pmem alloc");
    CHECK(x86_64_pmem_free_bytes() == s.pmem_free - 4 * PAGE, "pmem accounting");

    /* free out of order: the pieces must coalesce back, not fragment */
    CHECK(x86_64_pmem_free_pages(a + 2 * PAGE, 1), "free middle");
    CHECK(x86_64_pmem_free_pages(a, 1), "free first");
    CHECK(x86_64_pmem_free_pages(a + PAGE, 1), "free second");
    CHECK(x86_64_pmem_free_pages(a + 3 * PAGE, 1), "free last");
    CHECK(x86_64_pmem_free_bytes() == s.pmem_free, "pmem restored");
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
    CHECK(x86_64_pmem_free_bytes() == s.pmem_free, "refused frees changed nothing");

    /* runtime fragmentation: free every other page of a large run, then the
     * rest -- far more separate holes than the boot-time region list could
     * name -- and everything must come back */
    n = 20000;
    runs = kalloc(n * sizeof(UQUAD));
    if (runs) {
        for (i = 0; i < n; i++) {
            runs[i] = x86_64_pmem_try_alloc_pages(1, 0);
            if (runs[i] == X86_64_PMEM_NONE)
                break;
        }
        CHECK(i == n, "20000 single pages");
        for (n = i, i = 0; i < n; i += 2)
            x86_64_pmem_free_pages(runs[i], 1);
        for (i = 1; i < n; i += 2)
            x86_64_pmem_free_pages(runs[i], 1);
        kfree(runs);
    }
    CHECK(x86_64_pmem_free_bytes() == s.pmem_free, "fragmented frees all reclaimed");

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

    /* ownership: freed twice, foreign pointers and process memory refused */
    q = kalloc(64);
    kheap_stats(&before);
    kfree(q);
    kfree(q);                                       /* double free */
    kfree(&before);                                 /* not in the heap */
    p = x86_64_procmem_alloc(1, 0);
    kfree(p);                                       /* user memory */
    x86_64_procmem_free(p);
    kheap_stats(&after);
    CHECK(after.bad_frees == before.bad_frees + 3, "bad kfree()s rejected");

    kfree(v);
    same(&s, "kheap");
}

/* ---- process window ------------------------------------------------ */

static ULONG pre_free_calls;
static void count_pre_free(void *base)
{
    (void)base;
    pre_free_calls++;
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
        p = x86_64_procmem_alloc(5000, PROCMEM_ZERO);
        if (!p)
            break;
        CHECK(x86_64_procmem_contains(p), "inside window");
        CHECK((UQUAD)(uintptr_t)p + 2 * PAGE <= X86_64_USER_VA_LIMIT, "below 4 GiB");
        CHECK(!((UQUAD)(uintptr_t)p & (PAGE - 1)), "page aligned");
        memset(p, 0xAA, 5000);
        CHECK(x86_64_procmem_free(p), "free");
    }
    CHECK(i == 1000, "1000 alloc/free cycles");
    same(&s, "procmem cycles");

    /* zeroing is the caller's explicit choice */
    p = x86_64_procmem_alloc(PAGE, 0);
    memset(p, 0xAA, PAGE);
    x86_64_procmem_free(p);
    q = x86_64_procmem_alloc(PAGE, 0);
    CHECK(q == p && q[100] == 0xAA, "no PROCMEM_ZERO leaves old contents");
    x86_64_procmem_free(q);
    q = x86_64_procmem_alloc(PAGE, PROCMEM_ZERO);
    CHECK(q == p && q[100] == 0 && q[PAGE - 1] == 0, "PROCMEM_ZERO zeroes");
    CHECK(x86_64_procmem_free(q), "free zeroed");

    /* double free, interior and foreign pointers are refused */
    x86_64_procmem_stats(&st);
    CHECK(!x86_64_procmem_free(q), "double free refused");
    p = x86_64_procmem_alloc(2 * PAGE, 0);
    CHECK(!x86_64_procmem_free(p + PAGE), "interior pointer refused");
    CHECK(!x86_64_procmem_free(&st), "foreign pointer refused");
    CHECK(x86_64_procmem_free(p), "free");
    x86_64_procmem_stats(&st2);
    CHECK(st2.bad_frees == st.bad_frees + 3, "refused releases counted");

    /* exhaustion: allocation fails cleanly and everything comes back */
    v = kalloc(st.total_pages * sizeof(void *));
    for (i = 0; v && i < st.total_pages + 1; i++) {
        v[i] = x86_64_procmem_alloc(PAGE, 0);
        if (!v[i])
            break;
        n++;
    }
    CHECK(v && n == st.free_pages && i == st.free_pages, "window fills exactly");
    CHECK(x86_64_procmem_alloc(PAGE, 0) == NULL, "exhausted window returns NULL");
    CHECK(!x86_64_procmem_range_live(X86_64_LOW_TPA_VIRT_BASE - PAGE, PAGE) &&
          !x86_64_procmem_range_live(X86_64_LOW_TPA_VIRT_BASE + X86_64_LOW_TPA_BYTES, PAGE),
          "range check rejects outside the window");
    while (n--)
        x86_64_procmem_free(v[n]);
    kfree(v);
    p = x86_64_procmem_alloc(PAGE, 0);
    CHECK(p != NULL, "recovers after exhaustion");
    x86_64_procmem_free(p);

    /* ownership: owned blocks die with their owner, others survive */
    {
        void *a1 = x86_64_procmem_alloc(PAGE, 0), *a2 = x86_64_procmem_alloc(PAGE, 0);
        void *b1 = x86_64_procmem_alloc(PAGE, 0), *perm = x86_64_procmem_alloc(PAGE, 0);

        x86_64_procmem_set_owner(a1, &owner_a);
        x86_64_procmem_set_owner(a2, &owner_a);
        x86_64_procmem_set_owner(b1, &owner_b);
        pre_free_calls = 0;
        x86_64_procmem_free_owned(&owner_a, count_pre_free);
        CHECK(pre_free_calls == 2, "pre_free per owned block");
        CHECK(!x86_64_procmem_size(a1) && !x86_64_procmem_size(a2), "owned blocks freed");
        CHECK(x86_64_procmem_size(b1) && x86_64_procmem_size(perm), "others survive");
        x86_64_procmem_free_owned(&owner_a, NULL);      /* again: nothing */
        x86_64_procmem_free_owned(NULL, NULL);          /* NULL is not "all" */
        CHECK(x86_64_procmem_size(perm), "permanent blocks never owned-freed");
        x86_64_procmem_disown(&owner_b);
        x86_64_procmem_free_owned(&owner_b, NULL);
        CHECK(x86_64_procmem_size(b1), "disowned block is resident");
        x86_64_procmem_free(b1);
        x86_64_procmem_free(perm);
    }

    same(&s, "procmem");
}

/* ---- address spaces ------------------------------------------------ */

static void test_aspace(void)
{
    SNAP s, s2;
    X86_64_ASPACE *as;
    UBYTE *mem;
    ULONG i, k, failed = 0;
    UQUAD flags;
    UQUAD scratch;

    snap(&s);
    mem = x86_64_procmem_alloc(3 * PAGE, PROCMEM_ZERO);
    CHECK(mem != NULL, "window block for mapping");
    if (!mem)
        return;
    snap(&s2);      /* with the block live: what each cycle must return to */

    /* create / map / destroy, far more often than any fixed pool */
    for (i = 0; i < 500; i++) {
        as = x86_64_aspace_create();
        if (!as)
            break;
        if (!x86_64_aspace_map_procmem(as, (UQUAD)(uintptr_t)mem, 3 * PAGE,
                                       ASPACE_PROT_WRITE | ASPACE_PROT_USER)) {
            x86_64_aspace_destroy(as);
            break;
        }
        if (i == 0)
            CHECK(x86_64_aspace_table_pages(as) >= 4, "PML4 + 3 table levels owned");
        x86_64_aspace_destroy(as);
    }
    CHECK(i == 500, "500 address-space cycles");
    same(&s2, "aspace cycles");

    /* a block mapped into a live address space cannot be freed or reused */
    as = x86_64_aspace_create();
    if (as) {
        CHECK(x86_64_aspace_map_procmem(as, (UQUAD)(uintptr_t)mem, 3 * PAGE,
                                        ASPACE_PROT_WRITE | ASPACE_PROT_USER), "map for pin");
        CHECK(x86_64_procmem_pinned(mem), "mapped block is pinned");
        CHECK(!x86_64_procmem_free(mem), "pinned block cannot be freed");
        x86_64_procmem_set_owner(mem, &failed);
        x86_64_procmem_free_owned(&failed, NULL);
        CHECK(x86_64_procmem_size(mem), "owned-free skips a pinned block");
        x86_64_procmem_set_owner(mem, NULL);
        x86_64_aspace_destroy(as);
        CHECK(!x86_64_procmem_pinned(mem), "destroy unpins");
    }

    /* failed setup at every allocation point unwinds completely */
    for (k = 1; k <= 8; k++) {
        x86_64_pmem_test_fail_after(k);
        as = x86_64_aspace_create();
        if (as && !x86_64_aspace_map_procmem(as, (UQUAD)(uintptr_t)mem, 3 * PAGE,
                                             ASPACE_PROT_WRITE | ASPACE_PROT_USER))
            failed++;
        else if (!as)
            failed++;
        x86_64_pmem_test_fail_after(0);
        x86_64_aspace_destroy(as);              /* NULL-safe */
        same(&s2, "aspace failure unwind");
    }
    CHECK(failed >= 4, "injected failures were exercised");

    /* virtual (32-bit ABI) and physical limits are separate constraints */
    as = x86_64_aspace_create();
    CHECK(as != NULL, "create for constraints");
    if (as) {
        scratch = x86_64_pmem_try_alloc_pages(1, 0);
        flags = ASPACE_PROT_WRITE | ASPACE_PROT_USER;
        CHECK(x86_64_aspace_map_page(as, X86_64_USER_VA_LIMIT - PAGE, scratch, flags),
              "last page below 4 GiB maps");
        CHECK(!x86_64_aspace_map_page(as, X86_64_USER_VA_LIMIT, scratch, flags),
              "va at 4 GiB refused");
        CHECK(!x86_64_aspace_map_page(as, X86_64_USER_VA_LIMIT + PAGE, scratch, flags),
              "va above 4 GiB refused");
        CHECK(!x86_64_aspace_map_page(as, 0xFFFFFFFF80000000ULL, scratch, flags),
              "kernel va refused");
        CHECK(!x86_64_aspace_map_page(as, X86_64_USER_VA_LIMIT - 2 * PAGE + 16, scratch, flags),
              "unaligned va refused");
        /* physical backing above 4 GiB is fine for a low virtual address */
        CHECK(x86_64_aspace_map_page(as, 0x40000000ULL, 0x123456000ULL, flags),
              "phys above 4 GiB at low va");
        CHECK(x86_64_aspace_map_page(as, 0x40001000ULL, 0xFFFFF000000ULL, ASPACE_PROT_USER),
              "phys far above 4 GiB at low va");
        /* only live process memory can be mapped */
        CHECK(!x86_64_aspace_map_procmem(as, 0x500000, PAGE, flags), "outside window refused");
        x86_64_pmem_free_pages(scratch, 1);
        x86_64_aspace_destroy(as);
    }

    /* tearing down the address space that is currently loaded is safe */
    as = x86_64_aspace_create();
    if (as) {
        UQUAD flagsave;

        __asm__ volatile ("pushfq; popq %0; cli" : "=r"(flagsave) :: "memory");
        x86_64_write_cr3(x86_64_aspace_pml4(as));
        x86_64_aspace_destroy(as);
        CHECK(x86_64_read_cr3() == x86_64_kernel_pml4_phys(), "CR3 back on kernel tables");
        if (flagsave & 0x200)
            x86_64_sti();
    }

    x86_64_procmem_free(mem);
    same(&s, "aspace");
}

/* ---- KPROC records and the real process lifecycle ------------------ */

#define FAKE_PDS 1000

static void test_kproc(void)
{
    SNAP s;
    PD **pds;
    ULONG i;

    snap(&s);
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
    for (i = 0; i < FAKE_PDS && pds[i]; i++) {
        kproc_destroy(pds[i]);
        kproc_destroy(pds[i]);                  /* second destroy is a no-op */
        kfree(pds[i]);
    }
    kfree(pds);
    same(&s, "kproc");
}

#define LIFECYCLES 150

/* A basepage as Pexec(PE_BASEPAGEFLAGS) creates it; 0 on failure. */
static PD *new_basepage(void)
{
    long rc = Pexec(PE_BASEPAGEFLAGS, (char *)PF_STANDARD, "", NULL);

    return (rc > 0) ? (PD *)(uintptr_t)rc : NULL;
}

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
    x86_64_procmem_stats(&st);
    v = kalloc(st.total_pages * sizeof(void *));
    CHECK(v != NULL, "vector for fragmentation");
    if (!v)
        return;
    while (n < st.total_pages && (v[n] = x86_64_procmem_alloc(PAGE, 0)) != NULL)
        n++;
    if (n >= 6) {
        x86_64_procmem_free(v[n - 4]);          /* hole for the environment */
        x86_64_procmem_free(v[n - 1]);          /* hole for the basepage */
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
            kproc_destroy(pd);
            x86_64_procmem_free((void *)(uintptr_t)env);
            x86_64_procmem_free(pd);
        }
    }
    for (i = 0; i < n; i++)
        if (i + 4 != n && i + 1 != n)           /* the two freed above */
            x86_64_procmem_free(v[i]);
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
        if (!kproc_prepare_user(pd, run) || !kproc_user_pml4(pd)) {
            CHECK(FALSE, "launch preparation");
            x86_64_free_owned(pd);
            break;
        }
        x86_64_free_owned(pd);                  /* what ixterm() does */
        x86_64_free_owned(pd);                  /* exactly once: now a no-op */
    }
    CHECK(i == LIFECYCLES, "process create/exit cycles");
    same(&s, "process lifecycle");

    /* a basepage whose public fields were rewritten before launch (it is
     * writable until then) cannot map anything beyond what was recorded */
    pd = new_basepage();
    if (pd) {
        UQUAD hi = pd->p_hitpa;
        UQUAD envv = pd->p_env;
        void *other = x86_64_procmem_alloc(PAGE, PROCMEM_ZERO);

        env = USERPTR_TO_PTR(pd->p_env);
        pd->p_hitpa = hi + 4 * PAGE;            /* reach into a neighbour */
        CHECK(!kproc_prepare_user(pd, run), "extended p_hitpa refused");
        pd->p_hitpa = hi;
        pd->p_env = (ULONG)(uintptr_t)other;    /* point at someone else's block */
        CHECK(!kproc_prepare_user(pd, run), "redirected p_env refused");
        pd->p_env = envv;
        CHECK(!kproc_user_pml4(pd), "refused launches left no address space");
        CHECK(kproc_prepare_user(pd, run), "unmodified basepage still launches");
        x86_64_free_owned(pd);                  /* owner not set: nothing yet */
        kproc_destroy(pd);
        x86_64_procmem_free(env);
        x86_64_procmem_free(pd);
        x86_64_procmem_free(other);
    }

    /* Mfree() of a live process's own block is refused, not applied */
    pd = new_basepage();
    if (pd) {
        env = USERPTR_TO_PTR(pd->p_env);
        set_owner(pd, pd);
        set_owner(env, pd);
        CHECK(kproc_prepare_user(pd, run) && kproc_user_pml4(pd), "prepare for Mfree test");
        CHECK(Mfree(env) == EACCDN, "Mfree of a mapped environment refused");
        CHECK(Mfree(pd) == EACCDN, "Mfree of the live basepage refused");
        CHECK(kproc_user_pml4(pd) != 0, "address space survives the refused Mfree");
        x86_64_free_owned(pd);
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
            CHECK(!kproc_user_pml4(pd), "failed preparation leaves no address space");
        }
        x86_64_pmem_test_fail_after(0);
        x86_64_free_owned(pd);
    }
    CHECK(failed >= 3, "launch failures were exercised");
    same(&s, "failed launch");
}

void x86_64_memtest_run(void)
{
    SNAP s;

    failures = 0;
    snap(&s);

    test_pmem();
    test_kheap();
    test_procmem();
    test_aspace();
    test_kproc();
    test_kproc_ranges();
    test_lifecycle();

    same(&s, "whole memtest");
    CHECK(x86_64_pmem_bad_frees() >= s.pmem_bad, "bad free counter monotonic");

    if (failures)
        kcprintf("x86-64 memtest: FAIL (%d)\n", failures);
    else
        kcprintf("x86-64 memtest: PASS\n");
}

#endif /* CONF_WITH_X86_64_MEMTEST */
