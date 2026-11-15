/*
 * mmapcall.h - Mmap(), Munmap() and Mprot() for the native tests
 *
 * The ABI of include/mman.h, spelled out: a test checks it, it does not
 * share it.  The native call has the function first, then up to six long
 * arguments.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */
#ifndef TESTS_MMAPCALL_H
#define TESTS_MMAPCALL_H

#define GEMDOS_MPROT        0x17c
#define GEMDOS_MMAP         0x17e
#define GEMDOS_MUNMAP       0x17f
#define PROT_NONE           0
#define PROT_READ           1
#define PROT_WRITE          2
#define PROT_EXEC           4
#define MAP_PRIVATE         0x02
#define MAP_FIXED_NOREPLACE 0x100000   /* (MAP_FIXED, which replaces, is refused) */
#define MAP_ANONYMOUS       0x20

#define MM_PAGE             4096L

#if defined(__mc68000__) || defined(__arm__) || defined(__x86_64__)

#define MM_UNUSED __attribute__((unused))
#define MM_NOINLINE __attribute__((noinline))

static MM_NOINLINE MM_UNUSED long sys6(long fn, long a1, long a2, long a3, long a4, long a5, long a6)
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
        : "r" ((short)fn), "r" (a1), "r" (a2), "r" (a3), "r" (a4), "r" (a5), "r" (a6)
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
#else
    register unsigned long long rax __asm__("rax") = (1ULL << 32) | (unsigned long)fn;
    register unsigned long long rdi __asm__("rdi") = (unsigned long)a1;
    register unsigned long long rsi __asm__("rsi") = (unsigned long)a2;
    register unsigned long long rdx __asm__("rdx") = (unsigned long)a3;
    register unsigned long long r10 __asm__("r10") = (unsigned long)a4;
    register unsigned long long r8 __asm__("r8") = (unsigned long)a5;
    register unsigned long long r9 __asm__("r9") = (unsigned long)a6;

    __asm__ volatile ("syscall"
        : "+r" (rax)
        : "r" (rdi), "r" (rsi), "r" (rdx), "r" (r10), "r" (r8), "r" (r9)
        : "rcx", "r11", "memory");
    return (long)(int)rax;
#endif
}

/* pages of anonymous memory at <hint> (0: anywhere), at exactly that address if <fixed> */
static MM_UNUSED long mm_map(long hint, long pages, long prot, int fixed)
{
    return sys6(GEMDOS_MMAP, hint, pages * MM_PAGE, prot,
                MAP_PRIVATE | MAP_ANONYMOUS | (fixed ? MAP_FIXED_NOREPLACE : 0), -1L, 0L);
}

static MM_UNUSED long mm_unmap(long address, long pages)
{
    return sys6(GEMDOS_MUNMAP, address, pages * MM_PAGE, 0L, 0L, 0L, 0L);
}

static MM_UNUSED long mm_prot(long address, long pages, long prot)
{
    return sys6(GEMDOS_MPROT, address, pages * MM_PAGE, prot, 0L, 0L, 0L);
}

#endif /* native architecture */

#endif
