/*
 * mmu_fault.c - regression test for the recovery from page faults (#737)
 *
 * A page that is not there yet (Mmap() memory is backed on the first touch)
 * makes the CPU fault in the middle of an instruction.  The kernel maps the
 * page and returns with RTE, and the instruction has to finish as if nothing
 * had happened: the same registers, the same memory, each write once.  What
 * the exception frames of the 68030 (formats A and B), the 68040 and the
 * 68060 hold is not the same, and an emulator or a backend that gets one wrong
 * shows up as a lost or doubled write, an address register that moved twice,
 * or a crash.  Each case here is one instruction (or a loop of one) at a page
 * boundary, on pages that are not there:
 *
 *   - auto-increment and auto-decrement stores of each width, at every
 *     offset from the boundary: the register moves once;
 *   - MOVEM of several registers across the boundary, in both directions and
 *     with both auto-modes;
 *   - an instruction with two memory operands that both fault;
 *   - read-modify-write instructions;
 *   - a long run of stores that crosses a page every 1024 steps;
 *   - an instruction fetch that crosses a page, and one that needs a page
 *     that is not there;
 *   - the kernel copying to and from such pages (Fread(), Fwrite()), and
 *     refusing a buffer that ends in memory nobody mapped.
 *
 * Only where processes have address spaces of their own and Mmap() is built
 * in, on m68k; elsewhere the file passes without testing anything.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "test.h"
#include <mint/osbind.h>
#include <mint/mintbind.h>
#include "include/mmapcall.h"

void test_mmu_fault(void);

#if defined(__mc68000__) && defined(TEST_WITH_MMAP)

#define EINVFN      (-32L)
#define EIMBA       (-40L)

#define PAGE        MM_PAGE
#define FIXTURE     "C:\\RUNTESTS.TOS"
#define CODEFILE    "C:\\MMUCODE.TMP"

/* a fresh mapping of <pages> pages, none of them there yet */
static unsigned char * __attribute__((noinline)) fresh(long pages)
{
    long m = mm_map(0L, pages, PROT_READ | PROT_WRITE, 0);

    return m > 0 ? (unsigned char *)m : (unsigned char *)0;
}

static void __attribute__((noinline)) drop(unsigned char *m, long pages)
{
    mm_unmap((long)m, pages);
}

/*
 * The instructions under test, each in a function of its own so that the
 * compiler has registers to spare for the operands.
 */
#if !defined(TEST_NO_CROSSING)
static __attribute__((noinline)) unsigned char *store_post(unsigned char *a, unsigned long v, long width)
{
    if (width == 1)
        __asm__ volatile ("move.b %1,(%0)+" : "+a" (a) : "d" (v) : "memory");
    else if (width == 2)
        __asm__ volatile ("move.w %1,(%0)+" : "+a" (a) : "d" (v) : "memory");
    else
        __asm__ volatile ("move.l %1,(%0)+" : "+a" (a) : "d" (v) : "memory");
    return a;
}

static __attribute__((noinline)) unsigned char *store_pre(unsigned char *a, unsigned long v, long width)
{
    if (width == 1)
        __asm__ volatile ("move.b %1,-(%0)" : "+a" (a) : "d" (v) : "memory");
    else if (width == 2)
        __asm__ volatile ("move.w %1,-(%0)" : "+a" (a) : "d" (v) : "memory");
    else
        __asm__ volatile ("move.l %1,-(%0)" : "+a" (a) : "d" (v) : "memory");
    return a;
}
#endif

/* MOVEM d0-d3,(a) */
static __attribute__((noinline)) void movem_store(unsigned char *a, const unsigned long *src)
{
    __asm__ volatile ("movem.l (%1),%%d0-%%d3\n\t"
                      "movem.l %%d0-%%d3,(%0)"
                      : : "a" (a), "a" (src) : "d0", "d1", "d2", "d3", "memory");
}

/* MOVEM (a)+,d0-d3, then the registers to back[] */
static __attribute__((noinline)) unsigned char *movem_load_post(unsigned char *a, unsigned long *back)
{
    __asm__ volatile ("movem.l (%0)+,%%d0-%%d3\n\t"
                      "movem.l %%d0-%%d3,(%1)"
                      : "+a" (a) : "a" (back) : "d0", "d1", "d2", "d3", "memory");
    return a;
}

/* MOVEM d0-d3,-(a) */
static __attribute__((noinline)) unsigned char *movem_store_pre(unsigned char *a, const unsigned long *src)
{
    __asm__ volatile ("movem.l (%1),%%d0-%%d3\n\t"
                      "movem.l %%d0-%%d3,-(%0)"
                      : "+a" (a) : "a" (src) : "d0", "d1", "d2", "d3", "memory");
    return a;
}

#if !defined(TEST_NO_CROSSING)
/* MOVE.W (s)+,(d)+ ; MOVE.L (s)+,(d)+ */
static __attribute__((noinline)) void move_pair(unsigned char **s, unsigned char **d)
{
    unsigned char *a0 = *s, *a1 = *d;

    __asm__ volatile ("move.w (%0)+,(%1)+\n\t"
                      "move.l (%0)+,(%1)+"
                      : "+a" (a0), "+a" (a1) : : "memory");
    *s = a0;
    *d = a1;
}
#endif

/* longword stores of v, v+1, ... with post-increment, up to <end> */
static __attribute__((noinline)) unsigned char *store_run(unsigned char *a, unsigned char *end, unsigned long v)
{
    __asm__ volatile ("1: move.l %1,(%0)+\n\t"
                      "addq.l #1,%1\n\t"
                      "cmp.l %2,%0\n\t"
                      "bne.b 1b"
                      : "+a" (a), "+d" (v) : "a" (end) : "memory", "cc");     /* (v is counted up) */
    return a;
}

/* MOVEM d0-d7,(a) with d0..d7 = 1..8, 32 bytes on, up to <end> */
static __attribute__((noinline)) void movem_run(unsigned char *a, unsigned char *end)
{
    __asm__ volatile ("moveq #1,%%d0\n\t"
                      "moveq #2,%%d1\n\t"
                      "moveq #3,%%d2\n\t"
                      "moveq #4,%%d3\n\t"
                      "moveq #5,%%d4\n\t"
                      "moveq #6,%%d5\n\t"
                      "moveq #7,%%d6\n\t"
                      "moveq #8,%%d7\n"
                      "1: movem.l %%d0-%%d7,(%0)\n\t"
                      "lea 32(%0),%0\n\t"
                      "cmp.l %1,%0\n\t"
                      "bne.b 1b"
                      : "+a" (a) : "a" (end) : "d0", "d1", "d2", "d3", "d4", "d5", "d6", "d7", "memory", "cc");
}

static __attribute__((noinline)) unsigned long call_with(unsigned long (*f)(void), unsigned long d0v)
{
    register unsigned long d0 __asm__("d0") = d0v;

    __asm__ volatile ("jsr (%1)" : "+d" (d0) : "a" (f) : "d1", "a0", "a1", "memory", "cc");
    return d0;
}

/* the bytes [lo, hi) of the mapping hold nothing but the pattern of the offsets in [a, b) and zeros elsewhere */
static int __attribute__((noinline)) bytes_are(const volatile unsigned char *m, long lo, long hi, long a, long b, const unsigned char *src)
{
    long i;

    for (i = lo; i < hi; i++)
    {
        unsigned char want = (i >= a && i < b) ? src[i - a] : 0;

        if (m[i] != want)
            return 0;
    }
    return 1;
}

#if !defined(TEST_NO_CROSSING)
/*
 * A store with an auto-increment or auto-decrement operand, the first to
 * touch both pages.  The register moves by the width once, whichever access
 * of the instruction faulted, and exactly the bytes of the store change.
 */
static void test_auto_modes(void)
{
    static const unsigned char data[4] = { 0x91, 0xa2, 0xb3, 0xc4 };
    long width, off;

    for (width = 1; width <= 4; width *= 2)
        for (off = -3; off <= 3; off++)
        {
            unsigned char *m;
            unsigned char *a;
            unsigned long v = 0x91a2b3c4UL >> (8 * (4 - width));
            long at = PAGE + off;

            /* post-increment: starts at <at>, the store ends at <at + width> */
            m = fresh(2);
            if (!m)
            {
                ptest_fail("Mmap(for an auto-increment store) failed");
                return;
            }
            a = m + at;
            a = store_post(a, v, width);
            ptest_assert_msg(a == m + at + width, "an auto-increment register moved more or less than once");
            ptest_assert_msg(bytes_are(m, at - 8, at + width + 8, at, at + width, data),
                             "an auto-increment store wrote the wrong bytes");
            drop(m, 2);

            /* pre-decrement: starts at <at + width>, the store is at <at> */
            m = fresh(2);
            if (!m)
            {
                ptest_fail("Mmap(for an auto-decrement store) failed");
                return;
            }
            a = m + at + width;
            a = store_pre(a, v, width);
            ptest_assert_msg(a == m + at, "an auto-decrement register moved more or less than once");
            ptest_assert_msg(bytes_are(m, at - 8, at + width + 8, at, at + width, data),
                             "an auto-decrement store wrote the wrong bytes");
            drop(m, 2);
        }
}
#endif /* !TEST_NO_CROSSING */

/*
 * MOVEM of four registers across a boundary of two pages that are not there,
 * at each longword offset (and, where the emulator allows, each byte offset).
 */
static void test_movem(void)
{
    static const unsigned long src[4] = { 0x01020304UL, 0x05060708UL, 0x090a0b0cUL, 0x0d0e0f10UL };
    unsigned char want[16];
    long step = 4;
    long off, i;

#if !defined(TEST_NO_CROSSING)
    step = 1;
#endif
    for (i = 0; i < 16; i++)
        want[i] = (unsigned char)(i + 1);

    for (off = 0; off <= 16; off += step)
    {
        unsigned char *m;
        unsigned char *a;
        unsigned long back[4];
        long at = PAGE - off;
        long j;

        /* stored upwards */
        m = fresh(2);
        if (!m)
        {
            ptest_fail("Mmap(for a MOVEM store) failed");
            return;
        }
        a = m + at;
        movem_store(a, src);
        ptest_assert_msg(bytes_are(m, at - 8, at + 24, at, at + 16, want), "MOVEM across a page boundary wrote the wrong bytes");

        /* (the bytes are in, and read back: the pages are there) */
        a = m + at;
        a = movem_load_post(a, back);
        ptest_assert_msg(a == m + at + 16, "MOVEM (An)+ of pages that are there moved the register wrongly");
        for (j = 0; j < 4; j++)
            if (back[j] != src[j])
            {
                ptest_fail("MOVEM across a page boundary read the wrong registers");
                break;
            }
        drop(m, 2);

        /* ... and the load itself, from two pages that nothing has touched yet,
         * upwards (post-increment): the pages arrive in the middle of the
         * instruction, the registers get zeros and the register moves 16 bytes once */
        m = fresh(2);
        if (!m)
        {
            ptest_fail("Mmap(for a MOVEM load) failed");
            return;
        }
        for (j = 0; j < 4; j++)
            back[j] = 0xa5a5a5a5UL;
        a = m + at;
        a = movem_load_post(a, back);
        ptest_assert_msg(a == m + at + 16, "MOVEM (An)+ from pages that are not there moved the register wrongly");
        for (j = 0; j < 4; j++)
            if (back[j] != 0UL)
            {
                ptest_fail("MOVEM across a page boundary of pages that are not there did not read zeros");
                break;
            }
        drop(m, 2);

        /* stored downwards (pre-decrement; the registers go in the opposite order, d3 first) */
        m = fresh(2);
        if (!m)
        {
            ptest_fail("Mmap(for a MOVEM pre-decrement) failed");
            return;
        }
        a = m + at + 16;
        a = movem_store_pre(a, src);
        ptest_assert_msg(a == m + at, "MOVEM -(An) moved the register wrongly");
        ptest_assert_msg(bytes_are(m, at - 8, at + 24, at, at + 16, want),
                         "MOVEM -(An) across a page boundary wrote the wrong bytes");
        drop(m, 2);
    }
}

#if !defined(TEST_NO_CROSSING)
/*
 * An instruction that reads one operand and writes another, both on pages that
 * are not there (the source is mapped but untouched: reads zeros, so the
 * first page is filled with a marker first, the second page holds zeros).
 */
static void test_two_operands(void)
{
    unsigned char *s, *d, *a0, *a1;
    long k;

    for (k = 0; k < 4; k += 2)
    {
        long i;

        s = fresh(2);
        d = fresh(2);
        if (!s || !d)
        {
            ptest_fail("Mmap(for a two-operand instruction) failed");
            return;
        }
        for (i = 0; i < 4; i++)
            s[PAGE - 4 + i] = (unsigned char)(0xf1 + i);        /* the second page of s is not there */
        a0 = s + PAGE - 2 - k;          /* two bytes (or more) from the first page, then zeros */
        a1 = d + PAGE - 2;              /* the destination also crosses a page */
        move_pair(&a0, &a1);
        ptest_assert_msg(a0 == s + PAGE - 2 - k + 6 && a1 == d + PAGE - 2 + 6,
                         "a move with two memory operands left a register wrong");
        {
            unsigned char want[6];

            for (i = 0; i < 6; i++)
            {
                long pos = PAGE - 2 - k + i;

                want[i] = (pos >= PAGE - 4 && pos < PAGE) ? (unsigned char)(0xf1 + (pos - (PAGE - 4))) : 0;
            }
            ptest_assert_msg(bytes_are(d, PAGE - 6, PAGE + 12, PAGE - 2, PAGE + 4, want),
                             "a move with two memory operands that fault wrote the wrong bytes");
        }
        drop(s, 2);
        drop(d, 2);
    }
}
#endif /* !TEST_NO_CROSSING */

/*
 * read-modify-write: the read and the write each need the page.  (Not TAS and CAS: a
 * fault in their locked cycle is not served, it ends the process; doc/mmu-address-space.txt.)
 */
static void test_read_modify_write(void)
{
    unsigned char *m;
    unsigned long v = 5;

    m = fresh(3);
    if (!m)
    {
        ptest_fail("Mmap(for read-modify-write) failed");
        return;
    }
    __asm__ volatile ("addq.l #1,(%0)" : : "a" (m) : "memory", "cc");
    ptest_assert_msg(*(volatile unsigned long *)m == 1UL, "ADDQ to a page that was not there is wrong");
    __asm__ volatile ("add.l %1,(%0)" : : "a" (m + PAGE), "d" (v) : "memory", "cc");
    ptest_assert_msg(*(volatile unsigned long *)(m + PAGE) == 5UL, "ADD to a page that was not there is wrong");
    __asm__ volatile ("bset #3,(%0)" : : "a" (m + 2 * PAGE + 7) : "memory", "cc");
    ptest_assert_msg(m[2 * PAGE + 7] == 8 && m[2 * PAGE + 6] == 0 && m[2 * PAGE + 8] == 0,
                     "BSET on a page that was not there is wrong");
    drop(m, 3);
}

/*
 * A run of 3072 longword stores with post-increment through three pages that
 * are not there: nothing lost, nothing written twice, the register where it
 * should be.  Then the same with MOVEM of eight registers at a time.
 */
static void test_runs(void)
{
    unsigned char *m;
    unsigned long *a;
    unsigned long v = 1;
    unsigned long i;
    int bad = 0;

    m = fresh(3);
    if (!m)
    {
        ptest_fail("Mmap(for a run of stores) failed");
        return;
    }
    a = (unsigned long *)m;
    a = (unsigned long *)store_run((unsigned char *)a, m + 3 * PAGE, v);
    ptest_assert_msg(a == (unsigned long *)(m + 3 * PAGE),
                     "a run of auto-increment stores ended with the wrong address register");
    for (i = 0; i < 3 * PAGE / 4; i++)
        if (((volatile unsigned long *)m)[i] != i + 1)
            bad++;
    ptest_assert_msg(!bad, "a run of auto-increment stores lost or doubled a write");
    drop(m, 3);

    m = fresh(3);
    if (!m)
    {
        ptest_fail("Mmap(for a run of MOVEM) failed");
        return;
    }
    a = (unsigned long *)m;
    movem_run((unsigned char *)a, m + 3 * PAGE);
    bad = 0;
    for (i = 0; i < 3 * PAGE / 4; i++)
        if (((volatile unsigned long *)m)[i] != (i & 7) + 1)
            bad++;
    ptest_assert_msg(!bad, "a run of MOVEM stores lost or doubled a write");
    drop(m, 3);
}

/*
 * Instruction fetch.  The code is written into the first and last pages of
 * a mapping, which is then made executable; the pages in between are never
 * touched, so the CPU faults on the fetch itself.  A page of zeros executes as
 * ORI.B #0,D0 (two words each), which does nothing.
 */
static void test_fetch_file(void);

static void test_fetch(void)
{
    unsigned char *m;
    unsigned long (*f)(void);
    volatile unsigned short *w;
    unsigned long r;

    m = fresh(6);
    if (!m)
    {
        ptest_fail("Mmap(for instruction fetch) failed");
        return;
    }
    /* (1) MOVE.L #$0badcafe,D0 ; RTS, the opcode in the last word of page 0, the rest in page 1
     * (written first, so that the second page is in: the fetch of the opcode across the boundary;
     * the fault on the extension words is test_fetch_file()) */
    w = (volatile unsigned short *)(m + PAGE - 2);
    w[0] = 0x203c;
    w[1] = 0x0bad;
    w[2] = 0xcafe;
    w[3] = 0x4e75;
    /* (2) NOP in the last word of page 2, zeros through all of page 3 and 4, RTS at the start of page 5 */
    w = (volatile unsigned short *)(m + 3 * PAGE - 2);
    w[0] = 0x4e71;
    *(volatile unsigned short *)(m + 5 * PAGE) = 0x4e75;
    ptest_assert_msg(mm_prot((long)m, 6, PROT_READ | PROT_EXEC) == 0,
                     "Mprot(read/execute) failed");

    f = (unsigned long (*)(void))(m + PAGE - 2);
    r = f();
    ptest_assert_msg(r == 0x0badcafeUL, "an instruction that crosses a page boundary was not fetched whole");
    r = f();                            /* (again, with the pages in the ATC) */
    ptest_assert_msg(r == 0x0badcafeUL, "an instruction that crosses a page boundary failed the second time");

    f = (unsigned long (*)(void))(m + 3 * PAGE - 2);
    r = call_with(f, 0x12345678UL);
    ptest_assert_msg(r == 0x12345678UL, "code on pages that were not there did not run through");
    drop(m, 6);

    test_fetch_file();
}

/*
 * The same instruction across a page boundary, from a file: the code is written
 * to a file of two pages, and mapped, so that nothing of it is in memory and
 * the fetch of the extension words, after the opcode on the first page, is what
 * faults on the second.  (In test_fetch() the words were written first, which
 * brings the page in.)
 */
static void test_fetch_file(void)
{
    static unsigned char img[2 * PAGE];
    static const unsigned char code[] = { 0x20, 0x3c, 0x0b, 0xad, 0xca, 0xfe, 0x4e, 0x75 };
    long wh, fh, m, i;
    unsigned long (*f)(void);
    unsigned long r;

    for (i = 0; i < (long)sizeof code; i++)
        img[PAGE - 2 + i] = code[i];
    wh = Fcreate(CODEFILE, 0);
    if (wh < 0)
        return;                         /* (the disk cannot be written: nothing to show) */
    i = Fwrite((int)wh, (long)sizeof img, img);
    Fclose((int)wh);
    if (i != (long)sizeof img)
    {
        Fdelete(CODEFILE);
        return;
    }
    fh = Fopen(CODEFILE, 0);
    if (fh < 0)
    {
        Fdelete(CODEFILE);
        return;
    }
    m = sys6(GEMDOS_MMAP, 0L, 2 * PAGE, PROT_READ | PROT_EXEC, MAP_PRIVATE, fh, 0L);
    ptest_assert_msg(m > 0, "Mmap(code file) failed");
    if (m > 0)
    {
        f = (unsigned long (*)(void))(m + PAGE - 2);
        r = f();
        ptest_assert_msg(r == 0x0badcafeUL,
                         "an instruction whose extension words are on a page that is not there was not fetched whole");
        r = f();
        ptest_assert_msg(r == 0x0badcafeUL, "the same instruction failed the second time");
        mm_unmap(m, 2);
    }
    Fclose((int)fh);
    Fdelete(CODEFILE);
}

/*
 * The kernel copies to and from pages that are not there (Fread() into one
 * mapping, Fwrite() out of another), and refuses a buffer that runs out of
 * the mapping.
 */
static void test_kernel_copy(void)
{
    unsigned char *m, *n;
    static unsigned char ref[3 * 1024];
    long fh, rc, i;
    int bad;

    fh = Fopen(FIXTURE, 0);
    if (fh < 0)
        return;                         /* (no fixture to read: the rest of the suite shows that) */
    rc = Fread((int)fh, (long)sizeof ref, ref);
    if (rc != (long)sizeof ref)
    {
        Fclose((int)fh);
        return;
    }

    m = fresh(3);
    if (!m)
    {
        Fclose((int)fh);
        ptest_fail("Mmap(for Fread) failed");
        return;
    }
    /* 3 KiB that start 1 KiB before the end of the first page and run through the second */
    Fseek(0L, (int)fh, 0);
    rc = Fread((int)fh, (long)sizeof ref, m + PAGE - 1024);
    ptest_assert_msg(rc == (long)sizeof ref, "Fread() into pages that were not there failed");
    bad = 0;
    for (i = 0; i < (long)sizeof ref; i++)
        if (m[PAGE - 1024 + i] != ref[i])
            bad++;
    ptest_assert_msg(!bad, "Fread() into pages that were not there stored the wrong bytes");
    ptest_assert_msg(bytes_are(m, 0, PAGE - 1024, 0, 0, ref) && bytes_are(m, PAGE + 2048, 3 * PAGE, 0, 0, ref),
                     "Fread() wrote outside its buffer");

    /* a buffer that runs out of the mapping is refused as a whole */
    Fseek(0L, (int)fh, 0);
    rc = Fread((int)fh, 2 * PAGE, m + 2 * PAGE - 1024);
    ptest_assert_msg(rc == EIMBA, "Fread() into a buffer that leaves the mapping was not refused");
    Fclose((int)fh);

    /* Fwrite() out of pages that were not there: zeros, to a file read back */
    n = fresh(2);
    if (n)
    {
        long wh = Fcreate("C:\\MMUFAULT.TMP", 0);

        if (wh >= 0)
        {
            rc = Fwrite((int)wh, 2 * PAGE - 512, n + 256);
            ptest_assert_msg(rc == 2 * PAGE - 512, "Fwrite() out of pages that were not there failed");
            Fclose((int)wh);
            wh = Fopen("C:\\MMUFAULT.TMP", 0);
            if (wh >= 0)
            {
                rc = Fread((int)wh, 2 * PAGE - 512, m);
                ptest_assert_msg(rc == 2 * PAGE - 512, "the file written from new pages is short");
                ptest_assert_msg(bytes_are(m, 0, 2 * PAGE - 512, 0, 0, ref),
                                 "Fwrite() out of pages that were not there wrote something else than zeros");
                Fclose((int)wh);
            }
            Fdelete("C:\\MMUFAULT.TMP");
        }
        drop(n, 2);
    }
    drop(m, 3);
}

void test_mmu_fault(void)
{
    ptest_begin("mmu_fault (#737)");

    if (Ssystem(-1, 7, 0L) == EINVFN || !(Ssystem(7, 0L, 0L) & 1))
    {
        ptest_pass();                   /* no address spaces of their own here */
        return;
    }
    if (mm_map(0L, 0L, 0, 0) == EINVFN)
    {
        ptest_pass();                   /* ... or no Mmap() */
        return;
    }

#if !defined(TEST_NO_CROSSING)
    test_auto_modes();
#endif
    test_movem();
#if !defined(TEST_NO_CROSSING)
    test_two_operands();
#endif
    test_read_modify_write();
    test_runs();
    test_fetch();
    test_kernel_copy();
    ptest_pass();
}

#else /* not m68k, or no Mmap() */

void test_mmu_fault(void)
{
    ptest_begin("mmu_fault (#737)");
    ptest_pass();
}

#endif
