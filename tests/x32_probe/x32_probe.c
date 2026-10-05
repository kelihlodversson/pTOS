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
 *      have Ssystem() write into the kernel's system variables, to launch its own
 *      (already running) basepage a second time with PE_GO and PE_GOTHENFREE,
 *      and to use the kernel-internal Pexec mode 50: all must be refused;
 *      exits with 0 if they were
 *   g  basepages (#416): Pexec(PE_BASEPAGE) hands this process a basepage it
 *      can read and write; machine code placed in it runs with PE_GO and
 *      exits with 0x42; a launch of a copy of it, of a basepage with a field
 *      pointing elsewhere, of one already launched or freed, and of this
 *      process's own is refused; PE_BASEPAGEFLAGS works; exits with a bit
 *      mask of what was wrong (0 = all well)
 *   n  run another program from inside this one, twice: Pexec(PE_LOADGO) of
 *      C:\X32HELLO.TOS, which must exit with 0, after which this process
 *      must still be able to make system calls; exits with 0 if so, 0x100 if
 *      the file is not on the boot drive (reported as a skip by the self-test)
 *   m  like n, but the child (C:\X32HELLO.TOS with the tail "f") faults: the
 *      Pexec must return 0xffff, and this process must still be able to make
 *      system calls; exits with 0 if so, 0x100 if the file is not there
 *   q  like n, but ten children that terminate and stay resident (Ptermres,
 *      tail "r"): each must exit with 0 and this process must keep launching
 *      (the blocks lent to it for a resident child must be returned, or its
 *      launches start to fail); exits with 0 if so, 0x100 if the file is not
 *      there
 *   l  Pexec(PE_LOAD) of C:\\X32HELLO.TOS and PE_GOTHENFREE of its basepage; exits
 *      with 0, 0x100 if the file is not there
 *   y  a child (tail F) must not be able to Mfree this process's basepage
 *   w  walk the chain of ancestor basepages (p_parent): exits with 0x100 + the
 *      number of links, or 0x7f if a link is malformed (#416)
 *   v  like n, but the child (tail "w") must see one more ancestor than this
 *      process does; exits with 0 if so, 0x100 if the file is not there
 *   u  write to the ancestors page (a page fault: it is read-only)
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
#define EINVFN (-32)
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
        /* this very (running) basepage a second time, and mode 50 */
        if (sys4(GEMDOS, 0x4b, 4, (s64)(int)(unsigned long)"", (s64)basepage, 0) != EIMBA ||
            sys4(GEMDOS, 0x4b, 6, (s64)(int)(unsigned long)"", (s64)basepage, 0) != EIMBA ||
            sys4(GEMDOS, 0x4b, 50, 0, 0, 0) != EINVFN)
            bad |= 32;
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
        int run_no;

        /* twice: the second child reuses what the first one released (its
         * kernel stack, its blocks) while this process's own frames are live
         * on its own stack, which a launcher resuming on the wrong stack
         * would not survive */
        for (run_no = 0; run_no < 2; run_no++) {
            s64 rc = sys4(GEMDOS, 0x4b, 0, (s64)(int)(unsigned long)"X32HELLO.TOS",
                          (s64)(int)(unsigned long)"", 0);

            if (rc == -33)              /* EFILNF: not on the boot drive */
                pterm(0x100);
            if (rc != 0)
                bad |= 1;               /* the child's exit code, or an error */
            if (gemdos(0x19, 0, 0) != 0 && gemdos(0x19, 0, 0) != 2)
                bad |= 2;               /* this process's calls still work */
        }
        pterm(bad);
        break;
    }
    case 'q': {
        static const char resident_tail[] = { 1, 'r', 0 };
        int run_no;

        for (run_no = 0; run_no < 10; run_no++) {
            s64 rc = sys4(GEMDOS, 0x4b, 0, (s64)(int)(unsigned long)"X32HELLO.TOS",
                          (s64)(int)(unsigned long)resident_tail, 0);

            if (rc == -33)
                pterm(0x100);
            if (rc != 0)
                bad |= 1;
        }
        if (gemdos(0x19, 0, 0) != 0 && gemdos(0x19, 0, 0) != 2)
            bad |= 2;
        pterm(bad);
        break;
    }
    case 'm': {
        static const char fault_tail[] = { 1, 'f', 0 };
        s64 rc = sys4(GEMDOS, 0x4b, 0, (s64)(int)(unsigned long)"X32HELLO.TOS",
                      (s64)(int)(unsigned long)fault_tail, 0);

        if (rc == -33)
            pterm(0x100);
        if (rc != 0xffff)
            bad |= 1;                   /* the faulting child's exit code */
        if (gemdos(0x19, 0, 0) != 0 && gemdos(0x19, 0, 0) != 2)
            bad |= 2;                   /* this process's calls still work */
        pterm(bad);
        break;
    }
    case 'g': {
        /* movabs rax, (GEMDOS << 32) | Pterm; mov edi, 0x42; xor esi,esi;
         * xor edx,edx; xor r10,r10; syscall; jmp . */
        static const unsigned char code[] = {
            0x48, 0xb8, 0x4c, 0, 0, 0, 1, 0, 0, 0,
            0xbf, 0x42, 0, 0, 0, 0x31, 0xf6, 0x31, 0xd2,
            0x4d, 0x31, 0xd2, 0x0f, 0x05, 0xeb, 0xfe
        };
        static volatile u32 forged[64];     /* a basepage-sized copy */
        const s64 none = (s64)(int)(unsigned long)"";
        volatile u32 *bp, *bp2;
        s64 rc, bp_addr, bp2_addr, env;
        u32 hitpa;
        unsigned i;

        bp_addr = sys4(GEMDOS, 0x4b, 5, 0, none, 0);
        if (bp_addr <= 0 || bp_addr > 0x7fffffffLL)
            pterm(1);
        bp = (volatile u32 *)(unsigned long)bp_addr;
        if (bp[0] != (u32)bp_addr)
            bad |= 2;                       /* p_lowtpa */
        bp[1] = bp[0] + 0x7f1;              /* (unaligned) grow the TPA within its block */
        for (i = 0; i < sizeof(code); i++)
            ((volatile unsigned char *)bp)[0x100 + i] = code[i];
        bp[2] = (u32)bp_addr + 0x100;       /* p_tbase */
        env = bp[11];                       /* p_env */
        hitpa = bp[1];                      /* p_hitpa */

        /* a copy of it is a forgery, a bad p_env or p_hitpa is not a launch */
        for (i = 0; i < 64; i++)
            forged[i] = bp[i];
        if (sys4(GEMDOS, 0x4b, 4, none, (s64)(int)(unsigned long)forged, 0) != EIMBA)
            bad |= 8;
        bp[11] = 0x1000;
        if (sys4(GEMDOS, 0x4b, 4, none, bp_addr, 0) != -66)
            bad |= 16;                      /* EPLFMT */
        bp[11] = (u32)env;
        bp[1] = 0xfffff000u;
        if (sys4(GEMDOS, 0x4b, 4, none, bp_addr, 0) != -66)
            bad |= 16;
        bp[1] = hitpa;
        /* p_tbase out of the TPA */
        bp[2] = 0x1000;
        if (sys4(GEMDOS, 0x4b, 4, none, bp_addr, 0) != -66)
            bad |= 16;
        bp[2] = (u32)bp_addr + 0x100;

        /* the real launch: the child exits with 0x42 */
        rc = sys4(GEMDOS, 0x4b, 4, none, bp_addr, 0);
        if (rc != 0x42)
            bad |= 32;
        /* once launched it is not launchable again, freed or not */
        if (sys4(GEMDOS, 0x4b, 4, none, bp_addr, 0) != EIMBA)
            bad |= 64;
        rc = gemdos(0x49, bp_addr, 0);
        if (rc != 0)
            bad |= 128;
        if (gemdos(0x49, env, 0) != 0)
            bad |= 1024;
        if (sys4(GEMDOS, 0x4b, 4, none, bp_addr, 0) != EIMBA)
            bad |= 64;

        /* PE_GOTHENFREE: runs the code, frees its own blocks, and is gone */
        bp_addr = sys4(GEMDOS, 0x4b, 5, 0, none, 0);
        if (bp_addr <= 0) {
            bad |= 2048;
        } else {
            bp = (volatile u32 *)(unsigned long)bp_addr;
            bp[1] = bp[0] + 0x7f1;
            for (i = 0; i < sizeof(code); i++)
                ((volatile unsigned char *)bp)[0x100 + i] = code[i];
            bp[2] = (u32)bp_addr + 0x100;
            env = bp[11];
            if (sys4(GEMDOS, 0x4b, 6, none, bp_addr, 0) != 0x42)
                bad |= 2048;
            if (gemdos(0x49, bp_addr, 0) == 0 || gemdos(0x49, env, 0) == 0)
                bad |= 4096;                /* already given back */
            if (sys4(GEMDOS, 0x4b, 6, none, bp_addr, 0) != EIMBA)
                bad |= 64;
        }

        /* PE_BASEPAGEFLAGS, and a launch of somebody else's block */
        bp2_addr = sys4(GEMDOS, 0x4b, 7, 0, none, 0);
        if (bp2_addr <= 0)
            bad |= 256;
        else {
            bp2 = (volatile u32 *)(unsigned long)bp2_addr;
            env = bp2[11];
            if (sys4(GEMDOS, 0x4b, 6, none, env, 0) != EIMBA)
                bad |= 512;                 /* the env block is no basepage */
            if (gemdos(0x49, bp2_addr, 0) != 0 || gemdos(0x49, env, 0) != 0)
                bad |= 128;
        }
        pterm(bad);
        break;
    }
    case 'l': {
        /* PE_LOAD of a program, then PE_GOTHENFREE of the basepage it gives */
        s64 bpa = sys4(GEMDOS, 0x4b, 3, (s64)(int)(unsigned long)"X32HELLO.TOS",
                       (s64)(int)(unsigned long)"", 0);
        volatile u32 *bpp = (volatile u32 *)(unsigned long)bpa;

        if (bpa == -33)
            pterm(0x100);
        if (bpa <= 0)
            pterm(1);
        if (bpp[0] != (u32)bpa)
            bad |= 2;                       /* p_lowtpa */
        if (sys4(GEMDOS, 0x4b, 6, (s64)(int)(unsigned long)"", bpa, 0) != 0)
            bad |= 4;                       /* the program's own exit code */
        pterm(bad);
        break;
    }
    case 'y': {
        /* a child must not free what its parent holds: the child (tail F and
         * the address in hex) tries to Mfree this process's basepage */
        static char ftail[12] = { 9, 'F' };
        s64 bpa = sys4(GEMDOS, 0x4b, 5, 0, (s64)(int)(unsigned long)"", 0);
        s64 rc, envp;
        int k;

        if (bpa <= 0)
            pterm(1);
        for (k = 0; k < 8; k++)
            ftail[2 + k] = "0123456789abcdef"[(bpa >> (28 - 4 * k)) & 15];
        ftail[10] = 0;
        rc = sys4(GEMDOS, 0x4b, 0, (s64)(int)(unsigned long)"X32HELLO.TOS",
                  (s64)(int)(unsigned long)ftail, 0);
        if (rc == -33)
            pterm(0x100);
        if (rc != 0)
            bad |= 1;                       /* the child's Mfree was refused */
        envp = *(volatile u32 *)(unsigned long)(bpa + 0x2c);
        if (gemdos(0x49, bpa, 0) != 0 || gemdos(0x49, envp, 0) != 0)
            bad |= 2;                       /* still ours to free */
        pterm(bad);
        break;
    }
    case 'w':
        pterm(walk_ancestors(basepage));
        break;
    case 'v': {
        static const char walk_tail[] = { 1, 'w', 0 };
        s64 rc = sys4(GEMDOS, 0x4b, 0, (s64)(int)(unsigned long)"X32HELLO.TOS",
                      (s64)(int)(unsigned long)walk_tail, 0);

        if (rc == -33)
            pterm(0x100);
        pterm(rc == walk_ancestors(basepage) + 1 ? 0 : 1);
        break;
    }
    case 'u':
        bad_address = (u32 *)(unsigned long)((*(const u32 *)(unsigned long)(basepage + 0x24)) + 0x24);
        *bad_address = 0;               /* the ancestors page is read-only */
        break;
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
