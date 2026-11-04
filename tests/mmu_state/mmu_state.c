/*
 * mmu_state.c - regression test for the MMU of the m68k higher-half images (#737)
 *
 * Not "the emulator's MMU checkbox is on": what the guest has set up, read
 * from the CPU by the supervisor-side fixture of the kernel
 * (Ssystem(S_MMUPROBE), include/mmuprobe.h): translation is on with 4 KiB
 * pages, the root pointers are set, a page of the program translates to a
 * physical page that the kernel half also reaches (a second mapping of the same
 * bytes), the cache attributes are the ones the tree gives, and what the tree
 * says is not for a program is not.  Where the kernel has no such fixture (a
 * build without the MMU of a 68030, 68040 or 68060 in use) the suite passes
 * without testing anything.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "test.h"
#include <mint/osbind.h>
#include <mint/mintbind.h>

void test_mmu_state(void);

#ifdef TEST_WITH_MMU_PROBE

#define S_MMUPROBE      (-6L)           /* 0xfffa */
#define EINVFN          (-32L)

#define MMUPROBE_WRITE  0x1UL
#define MMUPROBE_SUPER  0x2UL
#define MMUPROBE_CACHE  0x4UL

/* include/mmuprobe.h */
struct mmu_probe {
    unsigned long va, flags, model, enabled, tc, root[4], status, mapped, access, phys,
                  super_only, write_protected, cache_mode, window_ok, window_word,
                  io_counter, io_rw, io_mask, cache_changes, cache_bad_ram, cache_bad_io;
};

#define KERNEL_HALF     0x80000000UL

static volatile unsigned long target = 0x12345678UL;    /* a page of the program, in its data (2-byte
                                                         * aligned at best: the fixture reads what is there) */

static long probe(struct mmu_probe *p, unsigned long va, unsigned long flags)
{
    p->va = va;
    p->flags = flags;
    return Ssystem(S_MMUPROBE, (long)p, (long)sizeof(*p));
}


/*
 * A failed check keeps, with its message, what the MMU was at the time (the
 * first failure of the suite): the model, TC, the roots, the status of the
 * PTEST and the physical address it gave.
 */
static char report[300];
static int reported;

static char *put_str(char *d, const char *s)
{
    while (*s)
        *d++ = *s++;
    return d;
}

static char *put_hex(char *d, unsigned long v)
{
    int i;

    for (i = 28; i >= 0; i -= 4)
        *d++ = "0123456789abcdef"[(v >> i) & 15];
    return d;
}

static void check(int ok, const char *msg, const struct mmu_probe *p)
{
    char *d;

    if (ok || reported)
    {
        ptest_assert_msg(ok, msg);
        return;
    }
    reported = 1;
    d = put_str(report, msg);
    d = put_str(d, " [model ");
    d = put_hex(d, p->model);
    d = put_str(d, " va ");
    d = put_hex(d, p->va);
    d = put_str(d, " tc ");
    d = put_hex(d, p->tc);
    d = put_str(d, " roots ");
    d = put_hex(d, p->root[0]);
    d = put_str(d, " ");
    d = put_hex(d, p->root[1]);
    d = put_str(d, " ");
    d = put_hex(d, p->root[2]);
    d = put_str(d, " ");
    d = put_hex(d, p->root[3]);
    d = put_str(d, " status ");
    d = put_hex(d, p->status);
    d = put_str(d, " phys ");
    d = put_hex(d, p->phys);
    d = put_str(d, "]");
    *d = 0;
    ptest_assert_msg(0, report);
}

#define CHECK(c, m) check((c), (m), &p)

void test_mmu_state(void)
{
    struct mmu_probe p;
    long rc;

    ptest_begin("mmu_state (#737)");

    if (Ssystem(-1, S_MMUPROBE, 0L) != 0)
    {
        ptest_fail("the MMU fixture of the kernel is missing");
        return;
    }
    CHECK(Ssystem(S_MMUPROBE, 0L, -1L) == (long)sizeof(p), "S_MMUPROBE size query");

    /* the MMU, as the CPU says */
    rc = probe(&p, (unsigned long)&target, 0);
    CHECK(rc == (long)sizeof(p), "S_MMUPROBE failed");
    CHECK(p.model == 30 || p.model == 40 || p.model == 60, "no MMU of a 68030, 68040 or 68060");
    CHECK(p.enabled == 1, "translation is not on");
    if (p.model == 30)
    {
        CHECK(((p.tc >> 20) & 0xf) == 12, "the pages are not 4 KiB (TC.PS)");
        CHECK(((p.tc >> 16) & 0xf) == 0, "the initial shift is not 0 (TC.IS)");
        CHECK(p.root[1] != 0, "no root pointer (CRP)");
        CHECK((p.root[0] & 3) == 3, "the root descriptors are not the 8-byte ones (CRP.DT)");
    }
    else
    {
        CHECK((p.tc & 0x4000) == 0, "the pages are not 4 KiB (TC.P)");
        CHECK(p.root[0] != 0 && p.root[1] != 0, "no root pointer (URP, SRP)");
    }

    /* a page of the program: translated, allowed, and the same bytes by the window */
    target = 0xc0ffee11UL;
    rc = probe(&p, (unsigned long)&target, 0);
    CHECK(rc > 0 && p.mapped == 1, "a page of the program is not mapped");
    CHECK(p.access == 1, "a user read of a page of the program is not allowed");
    CHECK(p.super_only == 0, "a page of the program is supervisor-only");
    CHECK(p.phys != 0xffffffffUL && (p.phys & 3) == ((unsigned long)&target & 3),
                     "no physical address for a page of the program");
    CHECK(p.window_ok == 1, "the physical page is not in the window of the kernel");
    CHECK(p.window_word == 0xc0ffee11UL,
                     "the window does not show what the program wrote: not the same bytes");

    /* a write goes to the same page: through the program, through the fixture's view of it */
    target = 0xdeadbeefUL;
    probe(&p, (unsigned long)&target, MMUPROBE_WRITE);
    CHECK(p.mapped == 1 && p.access == 1, "a user write to a page of the program is not allowed");
    CHECK(p.window_ok == 1 && p.window_word == 0xdeadbeefUL, "the window shows an old value");

    /* what the tree keeps from a program: not for a user access, mapped for the kernel */
#ifdef TEST_WITH_USER_ASPACE
    probe(&p, KERNEL_HALF, 0);
    CHECK(p.mapped == 1 && p.access == 0, "the kernel half is open to a user access");
    CHECK(p.super_only == 1, "the kernel half is not supervisor-only");
    probe(&p, KERNEL_HALF, MMUPROBE_SUPER);
    CHECK(p.mapped == 1 && p.access == 1, "the kernel half is not open to the kernel");
#endif
#if defined(TEST_WITH_USER_ASPACE) || defined(TEST_MMU_IO_ADDR)
    probe(&p, 0UL, 0);
    CHECK(p.access == 0, "the first page (the vectors) is open to a user access");
#endif

#ifdef TEST_MMU_IO_ADDR
    /* the I/O area: mapped for the kernel, supervisor-only, and not cached */
    probe(&p, TEST_MMU_IO_ADDR, MMUPROBE_SUPER);
    CHECK(p.mapped == 1, "the I/O area is not mapped");
    probe(&p, TEST_MMU_IO_ADDR, 0);
    CHECK(p.access == 0, "the I/O area is open to a user access");
    probe(&p, TEST_MMU_IO_ADDR, MMUPROBE_SUPER);
    CHECK(p.cache_mode == 2 || p.cache_mode == 3, "the I/O area is cached (or its cache mode is not known)");
#endif
#ifdef TEST_MMU_IO_CACHE
    /*
     * Cached and cache-inhibited pages in turn, with the caches as the kernel
     * has them: RAM and a register written and read back in turns, a counter
     * of a device that has to keep changing (a cached copy would not), and
     * the register has the value once the data cache is pushed out.  The
     * kernel picks the registers (bios/arch/m68k/mmuprobe.c).
     */
    rc = probe(&p, TEST_MMU_IO_ADDR, MMUPROBE_SUPER | MMUPROBE_CACHE);
    CHECK(rc == (long)sizeof(p), "S_MMUPROBE(cache) failed");
    CHECK(p.cache_changes >= 2, "a register of a device that runs by itself did not change: the I/O area is read from the cache");
    CHECK(p.cache_bad_ram == 0, "RAM did not read back what was written, between the accesses to I/O");
    CHECK(p.cache_bad_io == 0, "a register did not have what was written to it: a write to the I/O area stayed in the cache");
#endif
    /* RAM is cached (write-through or copyback, never inhibited) */
    probe(&p, (unsigned long)&target, 0);
    CHECK(p.cache_mode == 0 || p.cache_mode == 1, "RAM is not cached");

#ifdef TEST_WITH_USER_ASPACE
    /* an address nothing maps */
    probe(&p, 0x40000000UL, 0);
    CHECK(p.access == 0, "an unmapped address is open to a user access");
#endif

    ptest_pass();
}

#else /* !TEST_WITH_MMU_PROBE */

void test_mmu_state(void)
{
    ptest_begin("mmu_state (#737)");
    ptest_pass();                       /* no MMU fixture in this kernel */
}

#endif
