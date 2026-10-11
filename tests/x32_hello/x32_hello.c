/*
 * x32_hello.c - minimal x32 psABI userspace test program (x86-64, #334)
 *
 * A standalone executable, not a ptest_* suite -- it is not linked into
 * runtests.tos and does not use libcmini (there is no x86-64 port of it):
 * this is the "trivial C program" issue #334's own "x32 toolchain and
 * executable contract" section calls for, built by a *different* GCC
 * invocation than the kernel itself (see the ARCH_X86_64 section of the
 * top level Makefile, X32_CC/X32_CFLAGS/X32_LDFLAGS) -- ordinary -mx32
 * codegen produces an ELFCLASS32 program with real EM_X86_64 long-mode
 * instructions (Linux's "x32" psABI, not IA-32 compatibility mode), not
 * the -m64 PE32+ EFI application the kernel itself is.
 *
 * No CRT, no libc, no main(): this is freestanding, ring-3 code with
 * exactly one job. The assembly _start stub captures the entry state before
 * calling the C probe (see
 * X32_LDFLAGS' "-Wl,-z,noseparate-code"/"-Wl,-Ttext-segment=0x400000", which
 * also needs no
 * dynamic linker or startup file to satisfy). The syscall convention
 * (RAX = (trap_class << 32) | function_number, next four arguments in
 * RDI/RSI/RDX/R10) is bios/arch/x86_64/trap.h's own, reached the same way
 * a real ring-3 process's libc would: the `syscall` instruction, per
 * #333. X86_64_TRAP_GEMDOS's value (1) is inlined here rather than
 * pulled from trap.h, matching biosbind.h's own precedent for why a
 * userspace-facing file does not reach into that kernel-private header
 * for a value that only needs to match the same historic m68k trap
 * number convention (see trap.h's own "1 = GEMDOS, 13 = BIOS, 14 = XBIOS"
 * comment).
 *
 * All four argument registers are always set, even for a call that takes
 * fewer, so that the program does not depend on what the registers held at
 * entry; the dispatcher does not look at registers a call does not use
 * (#435).
 *
 * Copyright (C) 2026 The pTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

typedef unsigned long long u64;
typedef long long s64;
typedef unsigned int u32;

#define X86_64_TRAP_GEMDOS 1

static s64 gemdos0(u64 func)
{
    register u64 rax __asm__("rax") = ((u64)X86_64_TRAP_GEMDOS << 32) | func;
    register u64 rdi __asm__("rdi") = 0;
    register u64 rsi __asm__("rsi") = 0;
    register u64 rdx __asm__("rdx") = 0;
    register u64 r10 __asm__("r10") = 0;

    __asm__ volatile ("syscall"
                       : "+r" (rax)
                       : "r" (rdi), "r" (rsi), "r" (rdx), "r" (r10)
                       : "rcx", "r11", "memory");
    return (s64)rax;
}

static s64 gemdos2(u64 func, u64 a, u64 b)
{
    register u64 rax __asm__("rax") = ((u64)X86_64_TRAP_GEMDOS << 32) | func;
    register u64 rdi __asm__("rdi") = a;
    register u64 rsi __asm__("rsi") = b;
    register u64 rdx __asm__("rdx") = 0;
    register u64 r10 __asm__("r10") = 0;

    __asm__ volatile ("syscall"
                       : "+r" (rax)
                       : "r" (rdi), "r" (rsi), "r" (rdx), "r" (r10)
                       : "rcx", "r11", "memory");
    return (s64)rax;
}

static s64 gemdos3r(u64 func, u64 a, u64 b, u64 c)
{
    register u64 rax __asm__("rax") = ((u64)X86_64_TRAP_GEMDOS << 32) | func;
    register u64 rdi __asm__("rdi") = a;
    register u64 rsi __asm__("rsi") = b;
    register u64 rdx __asm__("rdx") = c;
    register u64 r10 __asm__("r10") = 0;

    __asm__ volatile ("syscall"
                       : "+r" (rax)
                       : "r" (rdi), "r" (rsi), "r" (rdx), "r" (r10)
                       : "rcx", "r11", "memory");
    return (s64)rax;
}

static s64 gemdos1r(u64 func, u64 arg)
{
    register u64 rax __asm__("rax") = ((u64)X86_64_TRAP_GEMDOS << 32) | func;
    register u64 rdi __asm__("rdi") = arg;
    register u64 rsi __asm__("rsi") = 0;
    register u64 rdx __asm__("rdx") = 0;
    register u64 r10 __asm__("r10") = 0;

    __asm__ volatile ("syscall"
                       : "+r" (rax)
                       : "r" (rdi), "r" (rsi), "r" (rdx), "r" (r10)
                       : "rcx", "r11", "memory");
    return (s64)rax;
}

static void gemdos1(u64 func, u64 arg)
{
    register u64 rax __asm__("rax") = ((u64)X86_64_TRAP_GEMDOS << 32) | func;
    register u64 rdi __asm__("rdi") = arg;
    register u64 rsi __asm__("rsi") = 0;
    register u64 rdx __asm__("rdx") = 0;
    register u64 r10 __asm__("r10") = 0;

    __asm__ volatile ("syscall"
                       : "+r" (rax)
                       : "r" (rdi), "r" (rsi), "r" (rdx), "r" (r10)
                       : "rcx", "r11", "memory");
}

/* Walks the basepage chain p_parent leads to: every link must lie on the
 * read-only ancestors page, carry this process's own environment and no file
 * or directory tables; returns 0x100 + the number of links, or 0x7f if one
 * is wrong.  (#416) */
static int walk_ancestors(u64 basepage)
{
    const volatile u32 *bp = (const volatile u32 *)(unsigned long)basepage;
    u32 env = bp[11];                   /* p_env */
    u32 at = bp[9];                     /* p_parent */
    int n = 0, i;

    while (at) {
        const volatile u32 *a = (const volatile u32 *)(unsigned long)at;

        if (at < 0x0fff0000u || at + 0x100 > 0x0fff1000u || (at & 0xff) || n >= 16)
            return 0x7f;
        if (a[11] != env || a[8] || a[9] > 0x0fff1000u)
            return 0x7f;                /* p_env, p_uft, p_parent */
        if (a[12] || (a[13] & 0xffff))
            return 0x7f;                /* p_uft */
        for (i = 0; i < 4; i++)
            if (a[16 + i])
                return 0x7f;            /* p_curdir */
        n++;
        at = a[9];
    }
    return 0x100 + n;
}

u32 x32_reloc_target;
/* initialised data a caller of Pexec(PE_LOAD) patches before launching (\001P) */
u32 x32_patch = 0x11111111;
#ifdef X32_BIG
static volatile unsigned char big_bss[6u << 20];
#endif

/* a deep recursion, 64 KiB a frame, to touch the stack down to n * 64 KiB */
static int deep_stack(int n)
{
    volatile unsigned char buf[65536];

    buf[0] = (unsigned char)n;
    buf[sizeof buf - 1] = (unsigned char)n;
    return n ? deep_stack(n - 1) + buf[0] : buf[0];
}

#define PSETLIMIT_CORE  2
#define PSETLIMIT_DATA  3
#define PSETLIMIT_STACK 0x100

void x32_entry_probe(u64 basepage, u64 entry_type, u64 stack)
{
    int bad = 0;
    /* command tail, TOS style: a length byte, then the text.  "f" makes this
     * program fault, "r" to terminate and stay resident (the boot self-test's
     * nested cases run it that way); no tail is the ordinary entry check below. */
    const volatile char *cmdline = (const volatile char *)(unsigned long)(basepage + 0x80);
    static u32 *volatile null_pointer;

    if (cmdline[0] == 1 && cmdline[1] == 'f')
        *null_pointer = 1;              /* a page fault in ring 3 */
    if (cmdline[0] == 9 && cmdline[1] == 'F') {
        /* Mfree() of the address given in hex: must be refused */
        u64 addr = 0;
        int k;

        for (k = 0; k < 8; k++) {
            char c = cmdline[2 + k];

            addr = (addr << 4) | (u64)(c <= '9' ? c - '0' : c - 'a' + 10);
        }
        gemdos1(0x4c, gemdos1r(0x49, addr) == 0 ? 1 : 0);
    }
    if (cmdline[0] == 1 && cmdline[1] == 'w')
        gemdos1(0x4c, walk_ancestors(basepage));
    if (cmdline[0] == 1 && cmdline[1] == 'a') {
        /* an absolute address as an instruction's immediate: the
         * R_X86_64_32 slot is not 4-byte aligned, and must still have been
         * relocated, i.e. equal the same address taken PC-relative (#433) */
        u32 absolute, relative;

        __asm__ ("\t.p2align 2\n\tmovl $x32_reloc_target, %0" : "=a" (absolute));
        __asm__ ("leal x32_reloc_target(%%rip), %0" : "=r" (relative));
        gemdos1(0x4c, absolute == relative ? 0 : 1);
    }
    if (cmdline[0] == 1 && cmdline[1] == 'm') {
        /* a block left allocated at the exit: the heap table has it below the
         * image if the launcher left a gap there (#434) */
        gemdos1(0x4c, gemdos1r(0x48, 4096) > 0 ? 0 : 1);
    }
    if (cmdline[0] == 1 && cmdline[1] == 'h') {
        /* Malloc() memory is private pages in the heap range, usable, zeroed
         * and given back by Mfree() and Mshrink() (#434) */
        volatile u32 *p, *q;
        u64 a, b, c;
        int k, bad_heap = 0;

        a = (u64)gemdos1r(0x48, 100000);
        b = (u64)gemdos1r(0x48, 4096);
        if (a < 0x10000000ULL || a >= 0x7f000000ULL || (a & 0xfff) ||
            b < 0x10000000ULL || b >= 0x7f000000ULL || (b & 0xfff))
            bad_heap |= 1;
        if (a < b + 4096 && b < a + 100000)
            bad_heap |= 2;                      /* overlap */
        p = (volatile u32 *)(unsigned long)a;
        q = (volatile u32 *)(unsigned long)b;
        for (k = 0; k < 100000 / 4; k++)
            if (p[k] != 0)
                bad_heap |= 4;                  /* not zeroed */
        for (k = 0; k < 100000 / 4; k++)
            p[k] = (u32)k + 1;
        q[0] = 0x5a5a5a5a;
        for (k = 0; k < 100000 / 4; k++)
            if (p[k] != (u32)k + 1)
                bad_heap |= 8;
        if (q[0] != 0x5a5a5a5a)
            bad_heap |= 8;
        if (gemdos1r(0x48, 0xffffffffULL) <= 0)
            bad_heap |= 16;                     /* Malloc(-1): the largest block */
        if (gemdos3r(0x4a, 0, a, 8192) != 0 || p[1] != 2)
            bad_heap |= 32;                     /* Mshrink() keeps the front */
        if (gemdos3r(0x4a, 0, a, 100000) != -67)
            bad_heap |= 64;                     /* growing is EGSBF */
        if (gemdos1r(0x49, a) != 0)
            bad_heap |= 128;                    /* Mfree() */
        if (gemdos1r(0x49, a) != -40)
            bad_heap |= 256;                    /* twice is EIMBA */
        if (gemdos1r(0x49, b) != 0)
            bad_heap |= 512;
        /* the arguments are x32 values: a length is its low 32 bits and so is
         * a pointer, whatever the upper half of the register holds */
        c = (u64)gemdos1r(0x48, 16384);
        if (gemdos3r(0x4a, 0, c, 0x100002000ULL) != 0)
            bad_heap |= 1024;
        if (gemdos1r(0x49, c | 0xffff00000000ULL) != 0)
            bad_heap |= 2048;
        /* a large block is reserved and given back without a search through
         * every page the process owns (64 MiB: 16384 pages) */
        c = (u64)gemdos1r(0x48, 64 * 1024 * 1024);
        if (!c || gemdos1r(0x49, c) != 0)
            bad_heap |= 4096;
        gemdos1(0x4c, bad_heap);
    }
    if (cmdline[0] == 1 && cmdline[1] == 'z') {
        /* demand paging (#450): Malloc() hands out address space and a page
         * gets its physical page when it is touched.  1.5 GiB is more than the
         * test machine has; touched here and there it costs a few pages.  A
         * kernel call that writes to or reads from an untouched page finds it
         * backed. */
        const u64 big_size = 0x60000000ULL;
        volatile u32 *p;
        u64 big, all, small;
        int bad_lazy = 0;
        static const char pattern[] = "\\";

        all = (u64)gemdos1r(0x48, 0xffffffffULL);               /* Malloc(-1) */
        if (all < 0x6e000000ULL || all > 0x7f000000ULL - 0x10000000ULL)
            bad_lazy |= 1;
        big = (u64)gemdos1r(0x48, big_size);
        if (big < 0x10000000ULL || big + big_size > 0x7f000000ULL || (big & 0xfff)) {
            gemdos1(0x4c, 2);
        }
        p = (volatile u32 *)(unsigned long)big;
        if (p[0] || p[big_size / 8 / 4] || p[big_size / 4 - 1])
            bad_lazy |= 4;                      /* zero, wherever it is */
        p[0] = 0x11111111;
        p[big_size / 8 / 4] = 0x22222222;
        p[big_size / 4 - 1] = 0x33333333;
        if (p[0] != 0x11111111 || p[big_size / 8 / 4] != 0x22222222 ||
            p[big_size / 4 - 1] != 0x33333333)
            bad_lazy |= 8;
        /* the kernel writes to a page not touched yet (Dgetpath) ... */
        if (gemdos3r(0x47, big + big_size / 2, 0, 0) != 0 ||
            *(volatile unsigned char *)(unsigned long)(big + big_size / 2) != 0)
            bad_lazy |= 16;
        /* ... and reads from one (a file name: an empty name is no pointer error) */
        if (gemdos2(0x3d, big + big_size / 4, 0) == -40)
            bad_lazy |= 32;
        (void)pattern;
        if (gemdos1r(0x49, big) != 0)
            bad_lazy |= 64;
        /* a block shrunk or freed gives its touched pages back, the rest was never there */
        small = (u64)gemdos1r(0x48, 8 * 4096);
        ((volatile unsigned char *)(unsigned long)small)[0] = 1;
        ((volatile unsigned char *)(unsigned long)small)[5 * 4096] = 1;
        if (gemdos3r(0x4a, 0, small, 2 * 4096) != 0 ||
            ((volatile unsigned char *)(unsigned long)small)[0] != 1)
            bad_lazy |= 128;
        if (gemdos1r(0x49, small) != 0)
            bad_lazy |= 256;
        gemdos1(0x4c, bad_lazy);
    }
    if (cmdline[0] == 1 && cmdline[1] == 'Z') {
        /* a page of a block that was freed is no longer lazy: touching it
         * faults, which ends this process as Pterm(-1) (#450) */
        volatile unsigned char *p = (volatile unsigned char *)(unsigned long)gemdos1r(0x48, 8192);

        p[0] = 1;
        gemdos1r(0x49, (u64)(unsigned long)p);
        p[0] = 2;
        gemdos1(0x4c, 0);               /* not reached */
    }
    if (cmdline[0] == 1 && cmdline[1] == 'Y') {
        /* the same, but it is the kernel that touches the pages: Dgetpath()
         * writes into one untouched page after the other.  Running out of
         * physical pages ends the process, it is not a bad pointer (#450) */
        u64 all = (u64)gemdos1r(0x48, 0xffffffffULL);
        u64 p = (u64)gemdos1r(0x48, all);
        u64 k;

        for (k = 0; k < all; k += 4096)
            if (gemdos3r(0x47, p + k, 0, 0) != 0)
                gemdos1(0x4c, 0x66);    /* an error code: the process survived */
        gemdos1(0x4c, 0x55);            /* the machine has more memory than that */
    }
    if (cmdline[0] == 1 && cmdline[1] == 'X') {
        /* touch every page of all the address space there is: the machine has
         * less memory, and the process that finds none left ends as Pterm(-1)
         * without the kernel running out (#450) */
        u64 all = (u64)gemdos1r(0x48, 0xffffffffULL);
        volatile unsigned char *p = (volatile unsigned char *)(unsigned long)gemdos1r(0x48, all);
        u64 k;

        for (k = 0; k < all; k += 4096)
            p[k] = 1;
        gemdos1(0x4c, 0x55);            /* the machine has more memory than that */
    }
    if (cmdline[0] == 1 && cmdline[1] == 'l') {
        /* Psetlimit() (#450): the defaults, MiNT's modes 2 and 3 and the stack
         * (0x100), with a negative value only asking */
        s64 a, rc;
        int bad_lim = 0;

        if (gemdos2(0x11f, PSETLIMIT_STACK, (u64)-1LL) != 0x1000000 ||
            gemdos2(0x11f, PSETLIMIT_STACK, (u64)-1LL) != 0x1000000)
            bad_lim |= 1;                       /* 16 MiB, and asking leaves it */
        if (gemdos2(0x11f, 1, 0) != -32 || gemdos2(0x11f, 7, 0) != -32)
            bad_lim |= 2;                       /* CPU time is not kept; no such mode */
        if (gemdos2(0x11f, PSETLIMIT_DATA, (u64)-1LL) != 0 ||
            gemdos2(0x11f, PSETLIMIT_CORE, (u64)-1LL) != 0)
            bad_lim |= 4;                       /* no limit */
        /* the allocatable memory */
        if (gemdos2(0x11f, PSETLIMIT_DATA, 0x100000) != 0 ||
            gemdos2(0x11f, PSETLIMIT_DATA, (u64)-1LL) != 0x100000)
            bad_lim |= 8;
        if (gemdos1r(0x48, 0x200000) != 0)
            bad_lim |= 16;                      /* over the limit: no memory */
        a = gemdos1r(0x48, 0x80000);
        if (a <= 0)
            bad_lim |= 32;
        if (gemdos1r(0x48, 0xffffffffULL) != 0x80000)
            bad_lim |= 64;                      /* Malloc(-1): what is left */
        gemdos1r(0x49, (u64)a);
        if (gemdos2(0x11f, PSETLIMIT_DATA, 0) != 0x100000)
            bad_lim |= 128;                     /* no limit again; the old value */
        a = gemdos1r(0x48, 0x200000);
        if (a <= 0)
            bad_lim |= 256;
        gemdos1r(0x49, (u64)a);
        /* the total memory counts the image too: this one is already bigger */
        gemdos2(0x11f, PSETLIMIT_CORE, 4096);
        if (gemdos1r(0x48, 4096) != 0)
            bad_lim |= 512;
        gemdos2(0x11f, PSETLIMIT_CORE, 0);
        /* the stack: 64 MiB, so that the Malloc() memory ends lower */
        if (gemdos2(0x11f, PSETLIMIT_STACK, 0x4000000) != 0x1000000 ||
            gemdos2(0x11f, PSETLIMIT_STACK, (u64)-1LL) != 0x4000000)
            bad_lim |= 1024;
        if (gemdos1r(0x48, 0xffffffffULL) > 0x6c000000LL)
            bad_lim |= 2048;
        if (gemdos2(0x11f, PSETLIMIT_STACK, 100) != -64)
            bad_lim |= 4096;                    /* too small */
        if (gemdos2(0x11f, PSETLIMIT_STACK, 0) != 0x4000000 ||
            gemdos2(0x11f, PSETLIMIT_STACK, (u64)-1LL) != 0x1000000)
            bad_lim |= 8192;                    /* 0 is the default */
        /* a block in the way of a bigger stack */
        a = gemdos1r(0x48, 0x68000000);
        if (a <= 0)
            bad_lim |= 16384;
        rc = gemdos2(0x11f, PSETLIMIT_STACK, 0x10000000);
        if (rc != -39)
            bad_lim |= 32768;
        gemdos1r(0x49, (u64)a);
        gemdos1(0x4c, bad_lim);
    }
    if (cmdline[0] == 1 && cmdline[1] == 'R') {
        /* a limit that is not a whole number of pages: Malloc(-1) reports what
         * Malloc() will give, a whole number of pages (#450) */
        u64 a;

        gemdos2(0x11f, PSETLIMIT_DATA, 0x100064);
        a = (u64)gemdos1r(0x48, 0xffffffffULL);
        if (a != 0x100000)
            gemdos1(0x4c, 1);
        a = (u64)gemdos1r(0x48, a);
        gemdos1(0x4c, a > 0 ? 0 : 2);
    }
    if (cmdline[0] == 1 && cmdline[1] == 'G') {
        /* the heap ends one guard page below the stack's address space: the
         * largest block ends there, and the guard page is neither (#450) */
        u64 all = (u64)gemdos1r(0x48, 0xffffffffULL);
        u64 p = (u64)gemdos1r(0x48, all);

        if (p + all != 0x7efff000ULL)
            gemdos1(0x4c, 1);
        *(volatile unsigned char *)(unsigned long)(p + all) = 1;   /* faults */
        gemdos1(0x4c, 0);
    }
    if (cmdline[0] == 1 && cmdline[1] == 'D') {
        /* 20 MiB of stack is more than the default 16: the process faults */
        gemdos1(0x4c, deep_stack(320) & 1);
    }
    if (cmdline[0] == 1 && cmdline[1] == 'E') {
        /* ... with a stack limit of 64 MiB it is fine, and the limit cannot
         * then be put below what the stack has grown to */
        if (gemdos2(0x11f, PSETLIMIT_STACK, 0x4000000) != 0x1000000)
            gemdos1(0x4c, 1);
        deep_stack(320);
        if (gemdos2(0x11f, PSETLIMIT_STACK, 0x100000) != -64)
            gemdos1(0x4c, 2);
        gemdos1(0x4c, 0);
    }
    if (cmdline[0] == 1 && cmdline[1] == 'Q') {
        /* the limits of the launcher are this process's (what Psetlimit()
         * set before the launch) */
        gemdos1(0x4c, gemdos2(0x11f, PSETLIMIT_STACK, (u64)-1LL) == 0x2000000 &&
                      gemdos2(0x11f, PSETLIMIT_DATA, (u64)-1LL) == 0x300000 ? 0 : 1);
    }
    if (cmdline[0] == 1 && cmdline[1] == 'i') {
        /* the code runs in the image window, wherever the program was linked
         * for: at its link address if that lies there, else moved into it and
         * relocated (#434) */
        unsigned long here = (unsigned long)&x32_entry_probe;

        gemdos1(0x4c, here >= 0x400000UL && here < 0x800000UL ? 0 : 1);
    }
    if (cmdline[0] == 1 && cmdline[1] == 'S') {
        /* the startup area: the bss is advertised as running to a page
         * boundary, and 0x10800 bytes (the biggest environment a caller may
         * give is 16383 four-byte environ slots) can be written from
         * p_bbase + p_blen + 4 on (#434) */
        const u32 *bp = (const u32 *)(unsigned long)basepage;
        volatile unsigned char *at = (volatile unsigned char *)(unsigned long)(bp[6] + bp[7] + 4);
        u32 k;

        if ((bp[6] + bp[7]) & 0xfff)
            gemdos1(0x4c, 1);
        for (k = 0; k < 0x10800; k++)
            at[k] = (unsigned char)k;
        gemdos1(0x4c, 0);
    }
#ifdef X32_BIG
    if (cmdline[0] == 1 && cmdline[1] == 'B') {
        /* a program with a 6 MiB bss (x32big.elf), loaded whole into the
         * caller's heap by PE_LOAD: both ends of it are there and zero (#434) */
        if (big_bss[0] || big_bss[sizeof big_bss - 1])
            gemdos1(0x4c, 1);
        big_bss[0] = 1;
        big_bss[sizeof big_bss - 1] = 2;
        gemdos1(0x4c, big_bss[0] == 1 && big_bss[sizeof big_bss - 1] == 2 ? 0 : 2);
    }
#endif
    if (cmdline[0] == 1 && cmdline[1] == 'P')
        gemdos1(0x4c, x32_patch == 0x22222222 ? 0 : 1);
    if (cmdline[0] == 1 && cmdline[1] == 'T') {
        /* the text is read-only: writing to it faults, which ends this
         * process as Pterm(-1) (#434) */
        *(volatile unsigned char *)(unsigned long)&x32_entry_probe = 0x90;
        gemdos1(0x4c, 0);               /* not reached unless the text is writable */
    }
    if (cmdline[0] == 1 && cmdline[1] == 'r')
        gemdos2(0x31, 0x100, 0);        /* Ptermres(0x100, 0): stay resident */

    /* a GEMDOS round trip with a result the kernel must get right: select
     * drive C: and read it back, so a call that returns nothing or garbage
     * is a failure, not just a call that was made */
    gemdos1r(0x0e, 2);                  /* Dsetdrv(2) */
    if (gemdos0(0x19) != 2)             /* Dgetdrv() */
        bad = 1;
    if (!(basepage && basepage <= 0xffffffffULL &&
          *(const u32 *)(unsigned long)basepage == (u32)basepage &&
          entry_type == 0 && (stack & 15) == 8))
        bad = 1;
    gemdos1(0x4c, bad);                 /* Pterm(bad) */
}
