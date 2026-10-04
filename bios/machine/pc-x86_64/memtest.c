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
#include "x32image.h"
#include "io.h"
#include "../../../bdos/kproc.h"
#include "../../../bdos/fs.h"

extern void set_owner(void *addr, PD *p);
extern void x86_64_free_owned(PD *p);
extern void x86_64_make_resident(PD *p, ULONG keep_bytes);
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
        if (run->p_curdir[d])
            sum += dirtbl[run->p_curdir[d]].use;
    return sum;
}

static void snap(SNAP *s)
{
    s->dir_refs = dir_refs();
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
static X86_64_ASPACE *must_create(const char *what)
{
    X86_64_ASPACE *as = x86_64_aspace_create();

    CHECK(as != NULL, what);
    return as;
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
    n = x86_64_pmem_free_bytes() / PAGE / 4;    /* a quarter of free RAM, */
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
        x86_64_procmem_keep(&owner_b, 0, NULL);
        x86_64_procmem_free_owned(&owner_b, NULL);
        CHECK(x86_64_procmem_size(b1), "kept block is resident");
        x86_64_procmem_free(b1);
        x86_64_procmem_free(perm);
    }

    /* Ptermres: the owner's block is cut back to the requested pages and
     * the tail returned; the rest of what it owns is kept, not freed */
    {
        UBYTE *blk = x86_64_procmem_alloc(4 * PAGE, 0);
        void *other = x86_64_procmem_alloc(PAGE, 0);
        PROCMEM_STATS before, after;

        x86_64_procmem_set_owner(blk, blk);
        x86_64_procmem_set_owner(other, blk);
        x86_64_procmem_stats(&before);
        pre_free_calls = 0;
        x86_64_procmem_keep(blk, PAGE + 1, count_pre_free);
        x86_64_procmem_stats(&after);
        CHECK(x86_64_procmem_size(blk) == 2 * PAGE, "Ptermres keeps whole pages of the prefix");
        CHECK(after.free_pages == before.free_pages + 2, "unused tail returned");
        CHECK(pre_free_calls == 1 && x86_64_procmem_size(other), "other owned block kept");
        x86_64_procmem_free_owned(blk, NULL);
        CHECK(x86_64_procmem_size(blk) && x86_64_procmem_size(other), "resident blocks survive owner cleanup");
        x86_64_procmem_free(blk);
        x86_64_procmem_free(other);
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
    UBYTE buf_dummy[16];

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
    as = must_create("address space for the pin test");
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
    as = must_create("address space for the constraint tests");
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
        /* ring 3 may be given a device or bogus physical page, but the kernel
         * must refuse to copy through the direct map to it, not fault */
        CHECK(x86_64_aspace_user_range_ok(as, 0x40000000ULL, 8, TRUE), "device-style page is valid for ring 3");
        CHECK(!x86_64_aspace_copy_from_user(as, buf_dummy, 0x40000000ULL, 8) &&
              !x86_64_aspace_copy_to_user(as, 0x40000000ULL, buf_dummy, 8) &&
              !x86_64_aspace_copy_from_user(as, buf_dummy, 0x40000FFCULL, 8),
              "copy to or from a page that is not owned RAM is refused");
        /* only live process memory can be mapped */
        CHECK(!x86_64_aspace_map_procmem(as, 0x500000, PAGE, flags), "outside window refused");
        x86_64_pmem_free_pages(scratch, 1);
        x86_64_aspace_destroy(as);
    }

    /* tearing down the address space that is currently loaded is safe */
    as = must_create("address space to tear down while loaded");
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
static UBYTE read_byte_under(X86_64_ASPACE *as, UQUAD va)
{
    UQUAD flags;
    UBYTE value;

    __asm__ volatile ("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
    x86_64_write_cr3(x86_64_aspace_pml4(as));
    value = *(volatile UBYTE *)(uintptr_t)va;
    x86_64_write_cr3(x86_64_kernel_pml4_phys());
    if (flags & 0x200)
        x86_64_sti();
    return value;
}

static void test_isolation(void)
{
    SNAP s;
    X86_64_ASPACE *a, *b;
    UQUAD pa, pb, probe;
    UWORD prot;
    UBYTE buf[128], big[200];
    ULONG i, k, failed = 0;

    snap(&s);
    a = must_create("first isolation address space");
    b = must_create("second isolation address space");
    if (!a || !b) {
        x86_64_aspace_destroy(a);
        x86_64_aspace_destroy(b);
        return;
    }

    /* the same virtual addresses in both, with different backing */
    CHECK(x86_64_aspace_map_private(a, ISO_VA, 3 * PAGE + 1, ASPACE_PROT_WRITE | ASPACE_PROT_EXEC | ASPACE_PROT_USER),
          "private mapping in A");
    CHECK(x86_64_aspace_map_private(b, ISO_VA, 2 * PAGE, ASPACE_PROT_WRITE | ASPACE_PROT_USER),
          "overlapping private mapping in B");
    CHECK(x86_64_aspace_private_pages(a) == 4 && x86_64_aspace_private_pages(b) == 2,
          "backing pages owned by each");
    CHECK(x86_64_aspace_translate(a, ISO_VA, &pa, &prot) &&
          x86_64_aspace_translate(b, ISO_VA, &pb, &prot) && pa != pb,
          "same address, different physical pages");
    CHECK(!x86_64_aspace_translate(b, ISO_VA + 3 * PAGE, NULL, NULL) &&
          x86_64_aspace_translate(a, ISO_VA + 3 * PAGE, NULL, NULL),
          "a page mapped in A only is absent in B");

    /* private writes stay private -- checked in software ... */
    memset(buf, 'A', 8);
    CHECK(x86_64_aspace_copy_to_user(a, ISO_VA, buf, 8), "write A");
    memset(buf, 'B', 8);
    CHECK(x86_64_aspace_copy_to_user(b, ISO_VA, buf, 8), "write B");
    x86_64_aspace_copy_from_user(a, buf, ISO_VA, 8);
    CHECK(buf[0] == 'A' && buf[7] == 'A', "A keeps its own data");
    x86_64_aspace_copy_from_user(b, buf, ISO_VA, 8);
    CHECK(buf[0] == 'B' && buf[7] == 'B', "B keeps its own data");

    /* ... and by the MMU itself: load each page table root and read */
    CHECK(read_byte_under(a, ISO_VA) == 'A', "MMU: A sees A");
    CHECK(read_byte_under(b, ISO_VA) == 'B', "MMU: B sees B");
    CHECK(read_byte_under(a, ISO_VA + PAGE) == 0, "MMU: fresh pages are zero");

    /* a copy that crosses a page boundary lands in both pages */
    for (i = 0; i < sizeof(big); i++)
        big[i] = (UBYTE)(i + 1);
    CHECK(x86_64_aspace_copy_to_user(a, ISO_VA + PAGE - 100, big, sizeof(big)), "cross-page write");
    memset(big, 0, sizeof(big));
    CHECK(x86_64_aspace_copy_from_user(a, big, ISO_VA + PAGE - 100, sizeof(big)) &&
          big[0] == 1 && big[99] == 100 && big[100] == 101 && big[199] == 200,
          "cross-page read back");

    /* validation is against this process's mappings, not a number range */
    CHECK(x86_64_aspace_user_range_ok(a, ISO_VA, 4 * PAGE, TRUE), "A's whole mapping valid");
    CHECK(!x86_64_aspace_user_range_ok(a, ISO_VA + 4 * PAGE - 1, 2, FALSE), "range running off the end invalid");
    CHECK(!x86_64_aspace_user_range_ok(b, ISO_VA + 2 * PAGE, 8, FALSE), "address valid in A, not in B");
    CHECK(!x86_64_aspace_user_range_ok(a, 0x10000000, 8, FALSE), "unmapped low address invalid");
    CHECK(!x86_64_aspace_user_range_ok(a, 0xFFFFFFFFULL, 2, FALSE), "range crossing 4 GiB invalid");
    CHECK(!x86_64_aspace_user_range_ok(a, 0xFFFFFFFFFFFFFFF0ULL, 8, FALSE), "wrapping range invalid");
    CHECK(!x86_64_aspace_user_range_ok(a, ISO_VA, 0, FALSE), "empty range invalid");
    CHECK(!x86_64_aspace_copy_to_user(b, ISO_VA + 2 * PAGE, buf, 8), "copy to an unmapped address refused");

    /* read-only and supervisor-only mappings */
    CHECK(x86_64_aspace_map_private(a, 0x20000000, PAGE, ASPACE_PROT_USER), "read-only mapping");
    CHECK(x86_64_aspace_user_range_ok(a, 0x20000000, 8, FALSE) &&
          !x86_64_aspace_user_range_ok(a, 0x20000000, 8, TRUE) &&
          !x86_64_aspace_copy_to_user(a, 0x20000000, buf, 8), "read-only page refuses writes");
    CHECK(x86_64_aspace_map_private(a, 0x20100000, PAGE, ASPACE_PROT_WRITE), "supervisor-only mapping");
    CHECK(!x86_64_aspace_user_range_ok(a, 0x20100000, 8, FALSE), "supervisor-only page invalid for ring 3");

    /* refusals leave nothing behind */
    k = x86_64_aspace_private_pages(a);
    CHECK(!x86_64_aspace_map_private(a, ISO_VA + PAGE, PAGE, ASPACE_PROT_USER), "overlap refused");
    CHECK(!x86_64_aspace_map_private(a, ISO_VA - PAGE, 2 * PAGE, ASPACE_PROT_USER), "partial overlap refused");
    CHECK(!x86_64_aspace_map_private(a, X86_64_USER_VA_LIMIT, PAGE, ASPACE_PROT_USER), "4 GiB refused");
    CHECK(!x86_64_aspace_map_private(a, X86_64_USER_VA_LIMIT - PAGE, 2 * PAGE, ASPACE_PROT_USER), "crossing 4 GiB refused");
    CHECK(!x86_64_aspace_map_private(a, ISO_VA + 1, PAGE, ASPACE_PROT_USER), "unaligned refused");
    CHECK(!x86_64_aspace_map_private(a, 0xFFFFFFFF80000000ULL, PAGE, ASPACE_PROT_USER), "kernel address refused");
    CHECK(x86_64_aspace_private_pages(a) == k, "no backing taken by refused mappings");

    /* kernel mappings are present for the CPU's sake but supervisor-only */
    probe = x86_64_pmem_try_alloc_pages(1, 0);
    CHECK(x86_64_aspace_translate(a, (UQUAD)(uintptr_t)&x86_64_memtest_run, NULL, &prot) &&
          !(prot & ASPACE_PROT_USER), "kernel code is supervisor-only");
    CHECK(probe != X86_64_PMEM_NONE &&
          x86_64_aspace_translate(a, X86_64_PHYS_MAP_BASE + probe, NULL, &prot) &&
          !(prot & ASPACE_PROT_USER), "physical direct map is supervisor-only");
    CHECK(!x86_64_aspace_user_range_ok(a, (UQUAD)(uintptr_t)&x86_64_memtest_run, 8, FALSE),
          "kernel address invalid as a user pointer");
    if (probe != X86_64_PMEM_NONE)
        x86_64_pmem_free_pages(probe, 1);
    /* The kernel's low data (the system-vector area, the kernel-data pool)
     * is there for ring 0's sake while it runs a system call under this
     * address space -- and supervisor-only; so is the process window, which
     * a process's own blocks are then mapped over with the user bit. */
    {
        UQUAD kstart, kend;

        x86_64_low_kernel_range(&kstart, &kend);
        CHECK(x86_64_aspace_translate(a, 0x400, NULL, &prot) && !(prot & ASPACE_PROT_USER),
              "the system variables are mapped, supervisor-only");
        CHECK(x86_64_aspace_translate(a, kstart, NULL, &prot) && !(prot & ASPACE_PROT_USER) &&
              !x86_64_aspace_user_range_ok(a, kstart, 8, FALSE),
              "the kernel-data pool is mapped, supervisor-only");
    }
    CHECK(x86_64_aspace_translate(a, X86_64_LOW_TPA_VIRT_BASE, NULL, &prot) &&
          !(prot & ASPACE_PROT_USER) &&
          !x86_64_aspace_user_range_ok(a, X86_64_LOW_TPA_VIRT_BASE, 8, FALSE),
          "the process window is mapped supervisor-only in a fresh address space");

    x86_64_aspace_destroy(a);
    x86_64_aspace_destroy(b);
    same(&s, "isolation");

    /* the documented minimum layout for an EmuCON-sized process fits, in two
     * address spaces at once */
    a = must_create("first layout address space");
    b = must_create("second layout address space");
    if (a && b) {
        UWORD rw = ASPACE_PROT_WRITE | ASPACE_PROT_USER;

        CHECK(x86_64_aspace_map_private(a, X86_64_USER_IMAGE_BASE, 64 * PAGE, rw | ASPACE_PROT_EXEC) &&
              x86_64_aspace_map_private(a, X86_64_USER_STACK_TOP - X86_64_USER_STACK_SIZE,
                                        X86_64_USER_STACK_SIZE, rw), "layout in A");
        CHECK(x86_64_aspace_map_private(b, X86_64_USER_IMAGE_BASE, 64 * PAGE, rw | ASPACE_PROT_EXEC) &&
              x86_64_aspace_map_private(b, X86_64_USER_STACK_TOP - X86_64_USER_STACK_SIZE,
                                        X86_64_USER_STACK_SIZE, rw), "same layout in B");
        CHECK(x86_64_aspace_user_range_ok(a, X86_64_USER_STACK_TOP - 16, 16, TRUE),
              "top of the stack is valid");
        CHECK(!x86_64_aspace_user_range_ok(a, X86_64_USER_STACK_TOP, 1, FALSE),
              "above the stack is not");
        CHECK(!x86_64_aspace_user_range_ok(a, X86_64_USER_STACK_TOP - X86_64_USER_STACK_SIZE - PAGE, 1, FALSE),
              "the guard below the stack is not");
    }
    x86_64_aspace_destroy(a);
    x86_64_aspace_destroy(b);
    same(&s, "layout");

    /* many create/map/destroy cycles, and failure at every allocation point */
    for (i = 0; i < 300; i++) {
        a = x86_64_aspace_create();
        if (!a)
            break;
        if (!x86_64_aspace_map_private(a, ISO_VA, 5 * PAGE, ASPACE_PROT_WRITE | ASPACE_PROT_USER)) {
            x86_64_aspace_destroy(a);
            break;
        }
        x86_64_aspace_destroy(a);
    }
    CHECK(i == 300, "300 private-mapping cycles");
    same(&s, "private cycles");
    for (k = 1; k <= 14; k++) {
        x86_64_pmem_test_fail_after(k);
        a = x86_64_aspace_create();
        if (!a || !x86_64_aspace_map_private(a, ISO_VA, 5 * PAGE, ASPACE_PROT_WRITE | ASPACE_PROT_USER))
            failed++;
        x86_64_pmem_test_fail_after(0);
        if (a)
            CHECK(failed == 0 || x86_64_aspace_private_pages(a) == 0 ||
                  x86_64_aspace_private_pages(a) == 5, "failed mapping leaves no partial pages");
        x86_64_aspace_destroy(a);
        same(&s, "private failure unwind");
    }
    CHECK(failed >= 6, "private-mapping failures were exercised");

    /* a failed mapping that needed new PDPT/PD/PT pages gives those back too,
     * not just the leaves: the address space looks exactly as it did before */
    failed = 0;
    for (k = 1; k <= 12; k++) {
        ULONG tables0, private0;
        BOOL ok;

        a = x86_64_aspace_create();
        if (!a || !x86_64_aspace_map_private(a, ISO_VA, PAGE, ASPACE_PROT_USER)) {
            CHECK(FALSE, "setup for the table rollback test");
            x86_64_aspace_destroy(a);
            break;
        }
        tables0 = x86_64_aspace_table_pages(a);
        private0 = x86_64_aspace_private_pages(a);
        x86_64_pmem_test_fail_after(k);
        /* a different 1 GiB region: needs a new PDPT entry, PD and PT */
        ok = x86_64_aspace_map_private(a, 0x80000000ULL, 3 * PAGE, ASPACE_PROT_USER);
        x86_64_pmem_test_fail_after(0);
        if (!ok) {
            failed++;
            CHECK(x86_64_aspace_table_pages(a) == tables0, "failed mapping returns its table pages");
            CHECK(x86_64_aspace_private_pages(a) == private0, "failed mapping returns its backing");
            CHECK(!x86_64_aspace_translate(a, 0x80000000ULL, NULL, NULL), "failed mapping left no leaf");
            /* and the region is usable afterwards */
            CHECK(x86_64_aspace_map_private(a, 0x80000000ULL, 3 * PAGE, ASPACE_PROT_USER),
                  "mapping succeeds after a rolled-back failure");
        }
        x86_64_aspace_destroy(a);
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
    CHECK(kproc_prepare_user(pd, run) && kproc_user_pml4(pd), "address space for validation");

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
        X86_64_ASPACE *as = kproc_user_aspace(pd);

        CHECK(as != NULL, "the process has an address space");
        if (as) {
            CHECK(x86_64_aspace_map_private(as, 0x20000000, PAGE, ASPACE_PROT_USER), "read-only user page");
            CHECK(x86_64_aspace_map_private(as, 0x20100000, PAGE, ASPACE_PROT_USER | ASPACE_PROT_WRITE),
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

    x86_64_free_owned(pd);
    same(&s, "process validation");
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
    CHECK(n >= 6, "enough window pages to fragment");
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
            Mfree(pd);                          /* releases what it inherited */
            Mfree((void *)(uintptr_t)env);
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
        set_owner(pd, pd);
        set_owner(env, pd);
        x86_64_free_owned(pd);                  /* maps, references, blocks */
        x86_64_procmem_free(other);
    }

    /* the environment freed and its address reused between creating the
     * basepage and launching it: the recorded range now names someone
     * else's block, which must not be mapped into the process */
    pd = new_basepage();
    if (pd) {
        void *e = USERPTR_TO_PTR(pd->p_env);
        void *stranger;

        CHECK(Mfree(e) == 0, "free environment before launch");
        stranger = x86_64_procmem_alloc(PAGE, PROCMEM_ZERO);
        CHECK(stranger == e, "address reused as arranged");
        CHECK(!kproc_prepare_user(pd, run), "reused environment address refused");
        CHECK(!x86_64_procmem_pinned(stranger), "stranger's block was not mapped");
        x86_64_procmem_free(stranger);
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

    /* an abandoned basepage gives back the directory references it inherited
     * (init_pd_files() took them), exactly once */
    {
        int d;
        WORD saved_use;

        for (d = 0; d < NUMCURDIR && !run->p_curdir[d]; d++)
            ;
        if (d < NUMCURDIR) {
            saved_use = dirtbl[run->p_curdir[d]].use;
            pd = new_basepage();
            CHECK(pd && dirtbl[run->p_curdir[d]].use == saved_use + 1, "basepage inherits a directory ref");
            if (pd) {
                env = USERPTR_TO_PTR(pd->p_env);
                CHECK(Mfree(pd) == 0, "Mfree abandoned basepage");
                Mfree(env);
                CHECK(dirtbl[run->p_curdir[d]].use == saved_use, "inherited directory ref released once");
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
            x86_64_make_resident(parent, PAGE);
            CHECK(kproc_count() == kp - 1, "child KPROC dropped at Ptermres");
            CHECK(x86_64_procmem_size(child) && x86_64_procmem_size(cenv), "child blocks stay resident");
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
            CHECK(!kproc_user_pml4(pd), "failed preparation leaves no address space");
        }
        x86_64_pmem_test_fail_after(0);
        x86_64_free_owned(pd);
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
static long run_probe(char mode)
{
    char tail[2];
    PD *pd;
    long rc;

    tail[0] = mode;
    tail[1] = '\0';
    rc = Pexec(PE_BASEPAGEFLAGS, (char *)PF_STANDARD, tail, NULL);
    CHECK(rc > 0, "probe basepage");
    if (rc <= 0)
        return -1000;
    pd = (PD *)(uintptr_t)rc;
    if (!kproc_set_image(pd, x86_64_x32probe_image())) {
        CHECK(FALSE, "probe image attaches");
        set_owner(pd, pd);
        set_owner(USERPTR_TO_PTR(pd->p_env), pd);
        x86_64_free_owned(pd);
        return -1000;
    }
    rc = Pexec(PE_GOTHENFREE, "", (char *)pd, NULL);
    if (rc < 0) {
        set_owner(pd, pd);
        set_owner(USERPTR_TO_PTR(pd->p_env), pd);
        x86_64_free_owned(pd);
    }
    return rc;
}

#define MSR_GS_BASE        0xC0000101UL
#define MSR_KERNEL_GS_BASE 0xC0000102UL

/* The state a return from ring 3 must leave exactly as it found it. */
typedef struct {
    UQUAD cr3, gs, kernel_gs;
} CPUSTATE;

static void cpustate(CPUSTATE *c)
{
    c->cr3 = x86_64_read_cr3();
    c->gs = x86_64_rdmsr(MSR_GS_BASE);
    c->kernel_gs = x86_64_rdmsr(MSR_KERNEL_GS_BASE);
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
    same(&s, what);
}

static void test_ring3(void)
{
    SNAP s;
    long rc;
    int k, refused = 0, ran = 0;

    /* entry through the process-entry contract, a GEMDOS call across the
     * syscall boundary, and the exit code coming back */
    probe_expect('e', 0, "ring-3 entry state: basepage, type, stack, CPL 3, GEMDOS call");
    probe_expect('x', 0x1234, "ring-3 exit code reaches the launcher");

    /* each process gets fresh private pages: what the first leaves in its
     * data, the second does not see */
    probe_expect('i', 0, "first run starts with zeroed data");
    probe_expect('i', 0, "second run starts with zeroed data too");

    /* bad pointers and kernel addresses as system call arguments */
    probe_expect('b', 0, "bad arguments refused by the system calls");
    probe_expect('s', 0, "ring 3 cannot install kernel callbacks or write the kernel variables");

    /* a fault in ring 3 ends that process and nothing else */
    probe_expect('f', 0xffff, "a ring-3 write to page 0 is contained");
    probe_expect('k', 0xffff, "ring-3 access to the system variables is contained");
    probe_expect('p', 0xffff, "a privileged instruction in ring 3 is contained");
    probe_expect('e', 0, "a process runs normally after the faults");

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
        rc = run_probe('n');
        cpustate(&after);
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
            x86_64_kstack_free(stack);
            x86_64_free_owned(pd);
        }
    }
    same(&s, "relaunch refusal");

    /* an allocation failure while loading the image is an ordinary refused
     * launch, tried at every allocation point */
    snap(&s);
    /* (the 256 KiB stack alone is 64 pages: step wider once past the first few) */
    for (k = 1; k <= 240; k += (k < 24) ? 1 : 5) {
        x86_64_pmem_test_fail_after(k);
        rc = run_probe('x');
        x86_64_pmem_test_fail_after(0);
        if (rc == 0x1234)
            ran++;
        else if (rc == ENSMEM)
            refused++;
        else if (rc != -1000)
            CHECK(FALSE, "a launch under memory pressure either runs or is refused");
    }
    if (refused < 5 || ran < 1)
        kcprintf("memtest: %d launches refused, %d ran\n", refused, ran);
    CHECK(refused >= 5 && ran >= 1, "image launch failures were exercised");
    same(&s, "failed ring-3 launches");
}

/* A copy of the probe image with one field changed must be refused. */
static void mutated(const X32_IMAGE *good, ULONG offset, ULONG size, ULONG value, const char *what)
{
    UBYTE *copy = kalloc(good->size);
    X32_IMAGE bad;
    ULONG i;

    CHECK(copy != NULL, "image copy");
    if (!copy)
        return;
    memcpy(copy, good->data, good->size);
    for (i = 0; i < size; i++)
        copy[offset + i] = (UBYTE)(value >> (8 * i));
    bad.data = copy;
    bad.size = good->size;
    CHECK(!x86_64_x32image_check(&bad, NULL), what);
    kfree(copy);
}

static void test_x32image(void)
{
    const X32_IMAGE *good = x86_64_x32probe_image();
    X32_IMAGE cut;
    UQUAD entry = 0;
    X86_64_ASPACE *as;
    SNAP s;

    snap(&s);
    CHECK(x86_64_x32image_check(good, &entry) && entry >= X86_64_USER_IMAGE_BASE &&
          entry < X86_64_USER_IMAGE_BASE + X86_64_USER_IMAGE_SIZE, "probe image is valid");
#if CONF_WITH_CLI
    CHECK(x86_64_x32image_check(x86_64_emucon_image(), &entry), "EmuCON image is valid");
#endif

    cut = *good;
    cut.size = 40;
    CHECK(!x86_64_x32image_check(&cut, NULL), "truncated header refused");
    cut.size = 52 + 32;                 /* header and one program header only */
    CHECK(!x86_64_x32image_check(&cut, NULL), "program headers beyond the file refused");

    mutated(good, 4, 1, 2, "ELFCLASS64 refused");
    mutated(good, 5, 1, 2, "big-endian refused");
    mutated(good, 16, 2, 3, "shared object refused");
    mutated(good, 18, 2, 3, "other machine refused");
    mutated(good, 44, 2, 0, "no program headers refused");
    mutated(good, 24, 4, 0x500000, "entry point outside the image refused");
    mutated(good, 24, 4, 0x405000, "entry point in a non-executable segment refused");
    mutated(good, 52 + 8, 4, 0x100000, "segment below the image window refused");
    mutated(good, 52 + 8, 4, 0x7ff000, "segment running past the image window refused");
    mutated(good, 52 + 24, 4, 7, "a writable and executable segment refused");
    mutated(good, 52 + 16, 4, 0x7fffffff, "file size beyond the file refused");
    mutated(good, 52 + 20, 4, 0, "empty segment refused");
    mutated(good, 52 + 32 + 8, 4, 0x400000, "overlapping segments refused");

    /* loading fills the pages and refuses what check() refuses */
    as = must_create("image address space");
    if (as) {
        CHECK(x86_64_x32image_load(as, good, &entry), "image loads");
        CHECK(x86_64_aspace_user_range_ok(as, entry, 16, FALSE) &&
              !x86_64_aspace_user_range_ok(as, entry, 16, TRUE), "text is read-only");
        x86_64_aspace_destroy(as);
    }
    same(&s, "image loader");
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
    test_kproc();
    test_kproc_ranges();
    test_lifecycle();
    test_x32image();
    test_ring3();

    same(&s, "whole memtest");
    CHECK(x86_64_pmem_bad_frees() >= s.pmem_bad, "bad free counter monotonic");

    if (failures)
        kcprintf("x86-64 memtest: FAIL (%d)\n", failures);
    else
        kcprintf("x86-64 memtest: PASS\n");
}

#endif /* CONF_WITH_X86_64_MEMTEST */
