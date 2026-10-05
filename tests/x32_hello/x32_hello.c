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
 * X32_LDFLAGS' "-Wl,-n"/"-Wl,-Ttext=0x400000", which also needs no
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
 * fewer: x86_64_trap_dispatch()'s from_ring3 argument-safety check
 * (trap.c) inspects all four unconditionally regardless of the function's
 * real arity, and rejects the call (EIMBA) if any of them still holds a
 * leftover value that looks like a kernel address -- see that function's
 * own comment.
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

        if (at < 0x3ffb0000u || at + 0x100 > 0x3ffb1000u || (at & 0xff) || n >= 16)
            return 0x7f;
        if (a[11] != env || a[8] || a[9] > 0x3ffb1000u)
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
    if (cmdline[0] == 1 && cmdline[1] == 'w')
        gemdos1(0x4c, walk_ancestors(basepage));
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
