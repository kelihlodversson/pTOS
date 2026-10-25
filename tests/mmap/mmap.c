/*
 * mmap.c - regression test for Mmap(), Munmap() and Mprot() (doc/mmap.txt)
 *
 * Private anonymous and file-backed mappings, their permissions, and the
 * memory a process gets as it touches it (stack and Malloc()), where processes
 * have address spaces of their own and the feature is built in.  Elsewhere the
 * suite passes without testing anything.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "test.h"
#include <mint/osbind.h>
#include <mint/mintbind.h>

/* the ABI of include/mman.h, spelled out: a test checks it, it does not share it */
#define GEMDOS_MPROT        0x17c
#define GEMDOS_MMAP         0x17e
#define GEMDOS_MUNMAP       0x17f
#define PROT_NONE           0
#define PROT_READ           1
#define PROT_WRITE          2
#define PROT_EXEC           4
#define MAP_SHARED          0x01
#define MAP_PRIVATE         0x02
#define MAP_FIXED           0x10
#define MAP_ANONYMOUS       0x20
#define MAP_FIXED_NOREPLACE 0x100000
#define PTOS_MMAP_BASE      0x01000000UL
#define PTOS_MMAP_LIMIT     0x0f000000UL

#define EINVFN      (-32L)
#define EACCDN      (-36L)
#define EIMBA       (-40L)
#define EINVAL      (-25L)
#define EOPNOTSUPP  (-307L)

#define PAGE        4096L
#define FIXTURE     "C:\\RUNTESTS.TOS"

/* the native call: the function, then up to six long arguments */
static long sys6(long fn, long a1, long a2, long a3, long a4, long a5, long a6)
{
#if defined(__mc68000__)
    register long d0 __asm__("d0");

    __asm__ volatile (
        "move.l %7,-(%%sp)\n\t"
        "move.l %6,-(%%sp)\n\t"
        "move.l %5,-(%%sp)\n\t"
        "move.l %4,-(%%sp)\n\t"
        "move.l %3,-(%%sp)\n\t"
        "move.l %2,-(%%sp)\n\t"
        "move.w %1,-(%%sp)\n\t"
        "trap #1\n\t"
        "lea 26(%%sp),%%sp"
        : "=d" (d0)
        : "g" ((short)fn), "r" (a1), "r" (a2), "r" (a3), "r" (a4), "r" (a5), "r" (a6)
        : "d1", "d2", "a0", "a1", "a2", "memory", "cc");
    return d0;
#elif defined(__arm__)
    register long r0 __asm__("r0") = fn;
    register long r1 __asm__("r1") = a1;
    register long r2 __asm__("r2") = a2;
    register long r3 __asm__("r3") = a3;
    register long r4 __asm__("r4") = a4;
    register long r5 __asm__("r5") = a5;
    register long r6 __asm__("r6") = a6;

    __asm__ volatile (
        "svc 1"
        : "+r" (r0)
        : "r" (r1), "r" (r2), "r" (r3), "r" (r4), "r" (r5), "r" (r6)
        : "r12", "lr", "memory", "cc");
    return r0;
#elif defined(__x86_64__)
    /* the x32 transport (bios/arch/x86_64/trap.h): RAX = (class 1 << 32) | function, the
     * arguments in RDI, RSI, RDX, R10, R8 and R9, each an unsigned 32-bit value */
    register unsigned long long rax __asm__("rax") = (1ULL << 32) | (unsigned long long)(unsigned long)fn;
    register unsigned long long rdi __asm__("rdi") = (unsigned long)a1;
    register unsigned long long rsi __asm__("rsi") = (unsigned long)a2;
    register unsigned long long rdx __asm__("rdx") = (unsigned long)a3;
    register unsigned long long r10 __asm__("r10") = (unsigned long)a4;
    register unsigned long long r8 __asm__("r8") = (unsigned long)a5;
    register unsigned long long r9 __asm__("r9") = (unsigned long)a6;

    __asm__ volatile (
        "syscall"
        : "+r" (rax)
        : "r" (rdi), "r" (rsi), "r" (rdx), "r" (r10), "r" (r8), "r" (r9)
        : "rcx", "r11", "memory");
    return (long)(int)rax;
#else
    return EINVFN;
#endif
}

static long mmap_anon(long hint, long length, long prot)
{
    return sys6(GEMDOS_MMAP, hint, length, prot, MAP_PRIVATE | MAP_ANONYMOUS, -1L, 0L);
}

static long munmap(long address, long length)
{
    return sys6(GEMDOS_MUNMAP, address, length, 0L, 0L, 0L, 0L);
}

static long mprot(long address, long length, long prot)
{
    return sys6(GEMDOS_MPROT, address, length, prot, 0L, 0L, 0L);
}

/* may the process read (mode 1) or write (mode 0) the range? */
static int accessible(long address, long length, int mode)
{
    return Maccess((void *)address, length, mode) == 0;
}

/* stack: a frame of 16 KiB that is written on each of 32 levels (512 KiB) */
static long deep(int level)
{
    volatile char frame[16384];
    long sum;
    int i;

    for (i = 0; i < (int)sizeof(frame); i += 4096)
        frame[i] = (char)(level + i);
    sum = frame[0];
    if (level > 0)
        sum += deep(level - 1);
    return sum;
}

static void test_anonymous(void)
{
    volatile unsigned char *p;
    long m, n, i;

    m = mmap_anon(0L, 3 * PAGE, PROT_READ | PROT_WRITE);
    ptest_assert_msg(m > 0 && !(m & (PAGE - 1)), "Mmap(anonymous) failed");
    if (m <= 0)
        return;
    ptest_assert_msg(m >= (long)PTOS_MMAP_BASE && m + 3 * PAGE <= (long)PTOS_MMAP_LIMIT,
                     "the mapping is outside the mapping window");
    p = (volatile unsigned char *)m;

    /* untouched memory reads as zeros, and is writable */
    for (i = 0; i < 3; i++)
        ptest_assert_msg(p[i * PAGE] == 0 && p[i * PAGE + PAGE - 1] == 0,
                         "a new mapping is not zero-filled");
    for (i = 0; i < 3 * PAGE; i++)
        p[i] = (unsigned char)(i * 7 + 1);
    for (i = 0; i < 3 * PAGE; i++)
        if (p[i] != (unsigned char)(i * 7 + 1))
        {
            ptest_fail("a written mapping does not read back");
            break;
        }

    /* the middle page read-only: it keeps its contents, and cannot be written */
    ptest_assert_msg(mprot(m + PAGE, PAGE, PROT_READ) == 0, "Mprot(read) failed");
    ptest_assert_msg(accessible(m + PAGE, PAGE, 1), "a read-only page cannot be read");
    ptest_assert_msg(!accessible(m + PAGE, PAGE, 0), "a read-only page can be written");
    ptest_assert_msg(accessible(m, PAGE, 0) && accessible(m + 2 * PAGE, PAGE, 0),
                     "the pages around it lost their write access");
    ptest_assert_msg(p[PAGE] == (unsigned char)(PAGE * 7 + 1), "Mprot changed the contents");
    ptest_assert_msg(mprot(m + PAGE, PAGE, PROT_READ | PROT_WRITE) == 0, "Mprot(read/write) failed");
    ptest_assert_msg(accessible(m + PAGE, PAGE, 0), "the page did not become writable again");

    /* no page both writes and executes */
    ptest_assert_msg(mprot(m, PAGE, PROT_WRITE | PROT_EXEC) == EACCDN, "Mprot(W|X) was not refused");
    ptest_assert_msg(mprot(m + 1, PAGE, PROT_READ) == EINVAL, "Mprot(unaligned) was not refused");
    ptest_assert_msg(mprot(m, PAGE, 8) == EINVAL, "Mprot(unknown bit) was not refused");

    /* unmapping the middle page splits the mapping */
    ptest_assert_msg(munmap(m + PAGE, PAGE) == 0, "Munmap(middle page) failed");
    ptest_assert_msg(!accessible(m + PAGE, 1, 1), "an unmapped page is still there");
    ptest_assert_msg(accessible(m, PAGE, 0) && accessible(m + 2 * PAGE, PAGE, 0),
                     "Munmap took the neighbours with it");
    ptest_assert_msg(p[0] == 1 && p[2 * PAGE] == (unsigned char)(2 * PAGE * 7 + 1),
                     "Munmap changed the neighbours");
    ptest_assert_msg(munmap(m, 3 * PAGE) == 0, "Munmap(rest) failed");
    ptest_assert_msg(!accessible(m, 1, 1) && !accessible(m + 2 * PAGE, 1, 1), "memory is left after Munmap");

    /* a new mapping is fresh zeros, even where the old one was */
    n = mmap_anon(m, 3 * PAGE, PROT_READ | PROT_WRITE);
    ptest_assert_msg(n > 0, "Mmap(again) failed");
    if (n > 0)
    {
        p = (volatile unsigned char *)n;
        for (i = 0; i < 3 * PAGE; i += 64)
            if (p[i])
            {
                ptest_fail("memory of an earlier mapping was not cleared");
                break;
            }
        munmap(n, 3 * PAGE);
    }
}

/*
 * Stores of each width into pages nobody has touched: the first store faults,
 * and the CPU may have to finish it itself (the 68040 keeps the write in the
 * frame of the fault).  Every offset in a longword, and a longword across the
 * boundary of two untouched pages.
 */
static void test_first_stores(void)
{
    volatile unsigned char *b;
    long m, i;
#if defined(__mc68000__)
    long k;
#endif

    m = mmap_anon(0L, 8 * PAGE, PROT_READ | PROT_WRITE);
    ptest_assert_msg(m > 0, "Mmap(for the first stores) failed");
    if (m <= 0)
        return;
    b = (volatile unsigned char *)m;

    for (i = 0; i < 4; i++)             /* bytes: one page each, offsets 0-3 */
        b[i * PAGE + i] = (unsigned char)(0x11 * (i + 1));
    for (i = 0; i < 4; i++)
        ptest_assert_msg(b[i * PAGE + i] == (unsigned char)(0x11 * (i + 1)) && b[i * PAGE + i + 1] == 0,
                         "a byte stored into a new page is wrong");
    for (i = 0; i < 2; i++)             /* words at offsets 0 and 2 */
        *(volatile unsigned short *)(b + (4 + i) * PAGE + i * 2) = (unsigned short)(0xa1b2 + i);
    for (i = 0; i < 2; i++)
        ptest_assert_msg(*(volatile unsigned short *)(b + (4 + i) * PAGE + i * 2) == (unsigned short)(0xa1b2 + i) &&
                         b[(4 + i) * PAGE + i * 2 + 2] == 0, "a word stored into a new page is wrong");
    *(volatile unsigned long *)(b + 6 * PAGE + 4) = 0x01020304UL;
    ptest_assert_msg(*(volatile unsigned long *)(b + 6 * PAGE + 4) == 0x01020304UL && b[6 * PAGE + 3] == 0 &&
                     b[6 * PAGE + 8] == 0, "a longword stored into a new page is wrong");
#if defined(__mc68000__)
    /* the 68020 and later store a longword at any address: across two new pages */
    for (k = 1; k < 4; k++)
    {
        volatile unsigned char *edge = b + 7 * PAGE - 4 + k;
        long j;

        for (j = -8; j < 8; j++)        /* (what the last round left) */
            b[7 * PAGE + j] = 0;
        *(volatile unsigned long *)(b + 7 * PAGE - 4 + k) = 0x0a0b0c0dUL + (unsigned long)k;
        ptest_assert_msg(*(volatile unsigned long *)edge == 0x0a0b0c0dUL + (unsigned long)k &&
                         edge[-1] == 0 && edge[4] == 0, "a longword stored across two new pages is wrong");
    }
#endif
    munmap(m, 8 * PAGE);
}

static void test_reservations(void)
{
    volatile unsigned char *p;
    long m, n;

    /* PROT_NONE costs nothing and holds the address space until it is enabled */
    m = mmap_anon(0L, 4 * PAGE, PROT_NONE);
    ptest_assert_msg(m > 0, "Mmap(PROT_NONE) failed");
    if (m <= 0)
        return;
    ptest_assert_msg(!accessible(m, 1, 1), "a PROT_NONE mapping can be read");
    ptest_assert_msg(mprot(m, 4 * PAGE, PROT_READ | PROT_WRITE) == 0, "Mprot(enable) failed");
    p = (volatile unsigned char *)m;
    p[3 * PAGE + 5] = 0x5a;
    ptest_assert_msg(p[3 * PAGE + 5] == 0x5a && p[0] == 0, "a page enabled after the reservation misbehaves");

    /* holes in the range, and the exact address of another mapping */
    ptest_assert_msg(mprot(m, 5 * PAGE, PROT_READ) == EIMBA, "Mprot over a hole was not refused");
    ptest_assert_msg(accessible(m, 4 * PAGE, 0), "a refused Mprot changed permissions");
    n = sys6(GEMDOS_MMAP, m, PAGE, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1L, 0L);
    ptest_assert_msg(n == EACCDN, "MAP_FIXED_NOREPLACE replaced a mapping");

    /* the arguments that make no sense */
    ptest_assert_msg(mmap_anon(0L, 0L, PROT_READ) == EINVAL, "Mmap(length 0) was not refused");
    ptest_assert_msg(sys6(GEMDOS_MMAP, 0L, PAGE, PROT_READ, MAP_SHARED | MAP_ANONYMOUS, -1L, 0L) == EOPNOTSUPP,
                     "MAP_SHARED was not refused");
    ptest_assert_msg(sys6(GEMDOS_MMAP, 0L, PAGE, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1L, 0L)
                     == EOPNOTSUPP, "MAP_FIXED was not refused");
    ptest_assert_msg(mmap_anon(0L, PAGE, PROT_WRITE | PROT_EXEC) == EACCDN, "Mmap(W|X) was not refused");
    ptest_assert_msg(mmap_anon(0L, 0x7fffffffL, PROT_READ) < 0, "an enormous Mmap succeeded");
    ptest_assert_msg(sys6(GEMDOS_MMAP, 0L, PAGE, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, -1L, PAGE) == EINVAL,
                     "Mmap(anonymous, offset) was not refused");

    munmap(m, 4 * PAGE);
}

static void test_file(void)
{
    long fd, m, rc, i;
    volatile unsigned char *p;
    static unsigned char page[2][PAGE];

    /* a file that is on every test disk, read with Fread() to compare with: the
     * test never writes to the disk */
    fd = Fopen(FIXTURE, 0);
    ptest_assert_msg(fd >= 0, "cannot open the fixture");
    if (fd < 0)
        return;
    Fseek(0L, (int)fd, 0);
    ptest_assert_msg(Fread((int)fd, 2 * PAGE, page) == 2 * PAGE, "cannot read the fixture");

    m = sys6(GEMDOS_MMAP, 0L, 2 * PAGE, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0L);
    ptest_assert_msg(m > 0, "Mmap(file) failed");
    if (m > 0)
    {
        p = (volatile unsigned char *)m;
        for (i = 0; i < 2 * PAGE; i++)
            if (p[i] != page[i / PAGE][i % PAGE])
            {
                ptest_fail("a file mapping does not show the file");
                break;
            }
        /* writing to it makes the page private: the file stays as it is */
        p[10] = (unsigned char)(page[0][10] ^ 0xff);
        ptest_assert_msg(p[10] == (unsigned char)(page[0][10] ^ 0xff), "a private file page cannot be written");
        ptest_assert_msg(p[11] == page[0][11] && p[PAGE + 10] == page[1][10],
                         "the copy of a file page lost its neighbours");
        Fseek(10L, (int)fd, 0);
        {
            unsigned char byte;

            ptest_assert_msg(Fread((int)fd, 1L, &byte) == 1 && byte == page[0][10],
                             "a write to a private mapping reached the file");
        }
        ptest_assert_msg(munmap(m, 2 * PAGE) == 0, "Munmap(file) failed");
    }

    /* an offset that is not a whole page, and one past the end of the file */
    rc = sys6(GEMDOS_MMAP, 0L, PAGE, PROT_READ, MAP_PRIVATE, fd, 100L);
    ptest_assert_msg(rc == EINVAL, "Mmap(file, unaligned offset) was not refused");
    rc = sys6(GEMDOS_MMAP, 0L, PAGE, PROT_READ, MAP_PRIVATE, fd, 0x10000000L);
    ptest_assert_msg(rc == EIMBA, "Mmap(file, beyond its end) was not refused");
    rc = sys6(GEMDOS_MMAP, 0L, PAGE, PROT_READ, MAP_PRIVATE, 99L, 0L);
    ptest_assert_msg(rc < 0, "Mmap(bad handle) succeeded");

    Fclose((int)fd);
}

static void test_lazy_stack(void)
{
    /* the stack grows as it is used: half a MiB, 16 KiB at a time */
    ptest_assert_msg(deep(31) != 0x7fffffffL, "the stack did not hold");
}

static void test_lazy_heap(void)
{
    volatile char *block;
    long i;

    /* Malloc() memory is there as it is touched */
    block = (volatile char *)Malloc(2L * 1024 * 1024);
    ptest_assert_msg(block != 0 && (long)block > 0, "Malloc(2 MiB) failed");
    if (block && (long)block > 0)
    {
        for (i = 0; i < 2L * 1024 * 1024; i += PAGE)
            block[i] = (char)(i >> 12);
        for (i = 0; i < 2L * 1024 * 1024; i += PAGE)
            if (block[i] != (char)(i >> 12))
            {
                ptest_fail("lazily backed Malloc() memory does not read back");
                break;
            }
        Mfree((void *)block);
    }
}

void test_mmap(void);

void test_mmap(void)
{
    long probe;

    ptest_begin("mmap");

    if (Ssystem(-1, 7, 0L) == EINVFN || !(Ssystem(7, 0L, 0L) & 1))
    {
#ifdef TEST_WITH_MMAP
        ptest_fail("the build has Mmap() but the processes have no address spaces");
#else
        ptest_pass();                   /* no address spaces of their own here */
#endif
        return;
    }
    probe = mmap_anon(0L, PAGE, PROT_READ);
    if (probe == EINVFN)
    {
#ifdef TEST_WITH_MMAP
        ptest_fail("Mmap() is built in but answers EINVFN");
#else
        ptest_pass();                   /* ... or no Mmap() */
#endif
        return;
    }
    ptest_assert_msg(probe > 0, "Mmap(one page) failed");
    if (probe > 0)
        munmap(probe, PAGE);

    test_anonymous();
    test_first_stores();
    test_reservations();
    test_file();
    test_lazy_stack();
    test_lazy_heap();
    ptest_pass();
}
