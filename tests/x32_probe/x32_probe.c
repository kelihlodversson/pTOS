/*
 * x32_probe.c - ring-3 probe program for the boot self-test (x86-64, #398)
 *
 * A freestanding x32 executable the kernel's memory/process self-test
 * (bios/machine/pc-x86_64/memtest.c) embeds and launches as a real ring-3
 * process, once per behaviour it wants to see.  The behaviour is chosen by the
 * first character of the command tail (the basepage's p_cmdlin):
 *
 *   e  check the entry state: basepage in RDI, entry type 0 in RSI, RSP + 8
 *      divisible by 16, CPL 3 with the user selectors, and that a GEMDOS call
 *      crosses the syscall boundary and returns a value; exits with a
 *      bit mask of what was wrong (0 = all well)
 *   x  exit with the code 0x1234
 *   i  exit with 0 if its writable data starts zeroed, 1 if not, leaving a
 *      marker behind: a second run must still start zeroed
 *   b  pass the kernel a bad pointer (an unmapped one) and a kernel
 *      address as GEMDOS arguments; exit with 0 if both are refused
 *   s  try to install a kernel callback vector (BIOS Setexc 0x102) and to
 *      have Ssystem() write into the kernel's system variables: all must be
 *      refused; exits with 0 if they were
 *   n  run another program from inside this one: Pexec(PE_LOADGO) of
 *      C:\X32HELLO.TOS, which must exit with 0, after which this process
 *      must still be able to make system calls; exits with 0 if so, 0x100 if
 *      the file is not on the boot drive (reported as a skip by the self-test)
 *   f  write to address 0 (a page fault)
 *   k  read the kernel's system variables at 0x4ba (a page fault: that page is
 *      supervisor-only)
 *   p  execute a privileged instruction (a general protection fault)
 *
 * The syscall convention is bios/arch/x86_64/trap.h's: RAX = (class << 32) |
 * function, arguments in RDI/RSI/RDX/R10, all four always set (the kernel
 * checks all four whatever the call's own arity).
 *
 * Copyright (C) 2026 The pTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

typedef unsigned long long u64;
typedef long long s64;
typedef unsigned int u32;

#define GEMDOS 1
#define EIMBA  (-40)
#define ENSMEM (-39)
#define EACCDN (-36)
#define BIOS   13

/* the selectors gdt.h gives ring 3 */
#define USER_CODE_SEL 0x2b
#define USER_DATA_SEL 0x23

static s64 sys4(u64 class, u64 func, s64 a, s64 b, s64 c, s64 d)
{
    register u64 rax __asm__("rax") = (class << 32) | func;
    register s64 rdi __asm__("rdi") = a;
    register s64 rsi __asm__("rsi") = b;
    register s64 rdx __asm__("rdx") = c;
    register s64 r10 __asm__("r10") = d;

    __asm__ volatile ("syscall"
                      : "+r" (rax)
                      : "r" (rdi), "r" (rsi), "r" (rdx), "r" (r10)
                      : "rcx", "r11", "memory");
    return (s64)rax;
}

static s64 sys(u64 class, u64 func, s64 a, s64 b, s64 c)
{
    return sys4(class, func, a, b, c, 0);
}

static s64 gemdos(u64 func, s64 a, s64 b)
{
    return sys(GEMDOS, func, a, b, 0);
}

static void pterm(int code)
{
    gemdos(0x4c, code, 0);
    for (;;)
        ;
}

static volatile u32 marker;

/* an address the compiler cannot see through, so that dereferencing a bad
 * one stays a runtime fault rather than a compile-time diagnostic */
static u32 *volatile bad_address;

void x32_probe_main(u64 basepage, u64 entry_type, u64 entry_rsp, u64 cs, u64 ss);

void x32_probe_main(u64 basepage, u64 entry_type, u64 entry_rsp, u64 cs, u64 ss)
{
    const char *tail = (const char *)(unsigned long)(basepage + 0x80);
    int bad = 0;

    switch (tail[0]) {
    case 'e':
        if (!basepage || basepage > 0xffffffffULL ||
            *(const u32 *)(unsigned long)basepage != (u32)basepage)
            bad |= 0x01;                /* p_lowtpa is the basepage itself */
        if (entry_type != 0)
            bad |= 0x02;                /* ENTRY_PROGRAM */
        if (((entry_rsp + 8) & 15) != 0)
            bad |= 0x04;
        if (cs != USER_CODE_SEL)
            bad |= 0x08;
        if (ss != USER_DATA_SEL)
            bad |= 0x10;
        gemdos(0x0e, 2, 0);             /* Dsetdrv(2) ... */
        if (gemdos(0x19, 0, 0) != 2)    /* ... Dgetdrv() sees it */
            bad |= 0x20;
        pterm(bad);
        break;
    case 'x':
        pterm(0x1234);
        break;
    case 'i':
        bad = marker != 0;
        marker = 0xa5a5a5a5u;
        pterm(bad);
        break;
    case 'b':
        /* unmapped user address as a DTA; a kernel address (the sign
         * extension of 0x80000000) as a string argument */
        if (gemdos(0x1a, 0x30000000, 0) != EIMBA)
            bad |= 1;
        if (gemdos(0x3b, (s64)(int)0x80000000, 0) != EIMBA)
            bad |= 2;
        pterm(bad);
        break;
    case 's':
        if (sys(BIOS, 5, 0x102, 0x500000, 0) != -1)
            bad |= 1;                   /* Setexc(etv_term, user address) */
        /* Ssystem() writing through a pointer into the kernel's system
         * variables: S_GETCOOKIE (8) and S_CONSOLE_DIM (-2) */
        if (sys(GEMDOS, 0x154, 8, 0x5f435055, 0x400) != EIMBA)
            bad |= 4;
        if (sys(GEMDOS, 0x154, -2, 0x400, 16) != EIMBA)
            bad |= 8;
        /* Ssystem(S_SETLVAL, etv_term, a user address) */
        if (sys(GEMDOS, 0x154, 0x0d, 0x408, 0x500000) != EACCDN)
            bad |= 16;
        pterm(bad);
        break;
    case 'n': {
        s64 rc = sys4(GEMDOS, 0x4b, 0, (s64)(int)(unsigned long)"X32HELLO.TOS", (s64)(int)(unsigned long)"", 0);

        if (rc == -33)                  /* EFILNF: not on the boot drive */
            pterm(0x100);
        if (rc != 0)
            bad |= 1;                   /* the child's exit code, or an error */
        if (gemdos(0x19, 0, 0) != 0 && gemdos(0x19, 0, 0) != 2)
            bad |= 2;                   /* this process's calls still work */
        pterm(bad);
        break;
    }
    case 'f':
        bad_address = (u32 *)0;
        *bad_address = 1;
        break;
    case 'k':
        bad_address = (u32 *)0x4ba;
        bad = (int)*(volatile u32 *)bad_address;
        break;
    case 'p':
        __asm__ volatile ("hlt");
        break;
    default:
        break;
    }
    pterm(0x7fff);                      /* an unknown mode, or a fault that did not happen */
}
