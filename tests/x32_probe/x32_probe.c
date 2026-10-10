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
 *   t  scribble over the basepage's p_uft and p_curdir, then use handles and
 *      directories; exits with a mask of what failed (#418)
 *   c  the pointer arguments of GEMDOS calls are checked per call: bad strings,
 *      buffers, lengths and Pexec arguments are refused with EIMBA/ERANGE and
 *      valid ones are let through; exits with a mask of what was wrong (#437)
 *   d  the search state of a DTA is the kernel's: a tampered DTA (an unmounted
 *      drive, a wild cluster) cannot crash Fsnext(); a DTA never searched with
 *      has no more files (#437)
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
        /* unmapped user address as a DTA */
        if (gemdos(0x1a, 0x30000000, 0) != EIMBA)
            bad |= 1;
        pterm(bad);
        break;
    case 'r':
        /* a call is not judged by registers it does not use: Dgetdrv()
         * takes no argument, so whatever the other four hold -- here
         * kernel addresses -- must not matter (#435) */
        if (sys4(GEMDOS, 0x19, (s64)0xFFFFFFFF80000000LL,
                 (s64)0xFFFFFFFF80001000LL, (s64)0xFFFF800000000000LL,
                 (s64)0xFFFFFFFF80002000LL) < 0)
            bad |= 1;
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

        /* PE_GO of a child that stays resident (Ptermres): the basepage and
         * environment remain this process's, readable and freeable */
        {
            static const unsigned char rescode[] = {
                0x48, 0xb8, 0x31, 0, 0, 0, 1, 0, 0, 0,      /* Ptermres */
                0xbf, 0, 1, 0, 0, 0x31, 0xf6, 0x31, 0xd2,
                0x4d, 0x31, 0xd2, 0x0f, 0x05, 0xeb, 0xfe
            };

            bp_addr = sys4(GEMDOS, 0x4b, 5, 0, none, 0);
            if (bp_addr <= 0) {
                bad |= 8192;
            } else {
                bp = (volatile u32 *)(unsigned long)bp_addr;
                bp[1] = bp[0] + 0x7f1;
                for (i = 0; i < sizeof(rescode); i++)
                    ((volatile unsigned char *)bp)[0x100 + i] = rescode[i];
                bp[2] = (u32)bp_addr + 0x100;
                env = bp[11];
                if (sys4(GEMDOS, 0x4b, 4, none, bp_addr, 0) != 0)
                    bad |= 8192;
                if (bp[0] != (u32)bp_addr)
                    bad |= 8192;            /* still readable */
                if (gemdos(0x49, bp_addr, 0) != 0 || gemdos(0x49, env, 0) != 0)
                    bad |= 16384;
            }
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
    case 'z': {
        /* Pexec() running out of loan slots half way (the environment is
         * lent, the TPA cannot be) must give back what it had got: eight
         * basepages fill the table, freeing one TPA leaves one slot */
        s64 bpa[8], envs[8], rc, renv;
        int k, n;

        for (n = 0; n < 8; n++) {
            bpa[n] = sys4(GEMDOS, 0x4b, 5, 0, (s64)(int)(unsigned long)"", 0);
            if (bpa[n] <= 0)
                break;
            envs[n] = *(volatile u32 *)(unsigned long)(bpa[n] + 0x2c);
        }
        if (n == 8) {
            if (gemdos(0x49, bpa[7], 0) != 0)
                bad |= 1;
            for (k = 0; k < 3; k++)
                if (sys4(GEMDOS, 0x4b, 5, 0, (s64)(int)(unsigned long)"", 0) != ENSMEM)
                    bad |= 2;
        } else {
            bad |= 4;                       /* the table is not what this assumes */
        }
        for (k = 0; k < n; k++) {
            if (gemdos(0x49, envs[k], 0) != 0)
                bad |= 8;
            if (k < 7 && gemdos(0x49, bpa[k], 0) != 0)
                bad |= 8;
        }
        /* all returned: a basepage is available again */
        rc = sys4(GEMDOS, 0x4b, 5, 0, (s64)(int)(unsigned long)"", 0);
        if (rc <= 0) {
            bad |= 16;
        } else {
            renv = *(volatile u32 *)(unsigned long)(rc + 0x2c);
            if (gemdos(0x49, renv, 0) != 0 || gemdos(0x49, rc, 0) != 0)
                bad |= 32;
        }
        pterm(bad);
        break;
    }
    case 't': {
        /* the file and directory tables are the kernel's, not the basepage's
         * (#418): scribble over both with wild indices, then use files,
         * directories and the console, and exit (the self-test checks that
         * nothing was released that was not held) */
        volatile unsigned char *b = (volatile unsigned char *)(unsigned long)basepage;
        static char path[128];
        s64 fh;
        int k;

        gemdos(0x0e, 2, 0);                     /* Dsetdrv(C:) */
        for (k = 0; k < 6; k++)
            b[0x30 + k] = (unsigned char)(0x70 + k);    /* p_uft: past any table */
        for (k = 0; k < 16; k++)
            b[0x40 + k] = (unsigned char)(0xf0 + k);    /* p_curdir */
        if (gemdos(0x19, 0, 0) != 2)
            bad |= 1;
        if (gemdos(0x47, (s64)(int)(unsigned long)path, 0) != 0)
            bad |= 2;                           /* Dgetpath uses the current directory */
        /* the standard-handle map: Fdup() of stdout, which the kernel resolves
         * through p_uft, must give a real handle that closes again; and a
         * console write must not fault */
        gemdos(0x09, (s64)(int)(unsigned long)"", 0);   /* Cconws("") */
        /* a zero-length Fwrite(stdout) resolves the handle through the map
         * (syshnd()) without printing anything */
        if (sys(GEMDOS, 0x40, 1, 0, (s64)(int)(unsigned long)path) != 0)
            bad |= 32;
        fh = gemdos(0x45, 1, 0);
        if (fh < 6)
            bad |= 4;
        else if (gemdos(0x3e, fh, 0) != 0)
            bad |= 4;
        fh = gemdos(0x3d, (s64)(int)(unsigned long)"X32HELLO.TOS", 0);
        if (fh == -33)
            ;                                   /* not on the boot drive: skip the file part */
        else if (fh < 6)
            bad |= 8;
        else if (gemdos(0x3e, fh, 0) != 0)
            bad |= 16;
        /* read a file that is always on the boot drive: the image itself.
         * The kernel keeps its open-file descriptors in the higher half,
         * where a pointer has its sign bit set, and Fread() once took that
         * for an invalid BIOS handle */
        fh = gemdos(0x3d, (s64)(int)(unsigned long)"\\EFI\\BOOT\\BOOTX64.EFI", 0);
        if (fh == -33 || fh == -34)
            ;                                   /* no such file here: skip */
        else if (fh < 6)
            bad |= 64;
        else {
            static char magic[4];

            if (sys(GEMDOS, 0x3f, fh, 2, (s64)(int)(unsigned long)magic) != 2 ||
                magic[0] != 'M' || magic[1] != 'Z')
                bad |= 64;
            if (gemdos(0x3e, fh, 0) != 0)
                bad |= 128;
        }
        pterm(bad);
        break;
    }
    case 'c': {
        /* the pointers of GEMDOS calls are checked per call (#437) */
#define P(x) ((s64)(int)(unsigned long)(x))
#define EIHNDL (-37)
#define ERANGE (-64)
        static char buf[256];
        static char longstr[2000];
        static char tail128[128];
        static char bigenv[40000];
        const s64 unmapped = 0x30000000, lowvec = 0x84, kern = (s64)(int)0x80000000;
        const s64 ro = 0x3ffb0000;          /* the ancestors page: read-only */
        const s64 stack_end = 0x3ffffff8;   /* 64 bytes from here cross the top */
        s64 rc;
        int k;

        for (k = 0; k < (int)sizeof(longstr); k++)
            longstr[k] = 'A';

        /* strings: unmapped, kernel, supervisor-only, null, no terminator */
        if (gemdos(0x3d, unmapped, 0) != EIMBA) bad |= 1;
        if (gemdos(0x3d, kern, 0) != EIMBA) bad |= 1;
        if (gemdos(0x3d, lowvec, 0) != EIMBA) bad |= 1;
        if (gemdos(0x3d, 0, 0) != EIMBA) bad |= 1;
        if (gemdos(0x3d, P(longstr), 0) != ERANGE) bad |= 2;
        if (gemdos(0x09, unmapped, 0) != EIMBA) bad |= 1;      /* Cconws */
        if (gemdos(0x4e, unmapped, 0) != EIMBA) bad |= 1;      /* Fsfirst */
        if (gemdos(0x41, lowvec, 0) != EIMBA) bad |= 1;        /* Fdelete */
        if (sys4(GEMDOS, 0x56, 0, P("a"), unmapped, 0) != EIMBA) bad |= 1;   /* Frename */
        /* a valid string is let through (the file need not exist) */
        rc = gemdos(0x3d, P("NOSUCH.FIL"), 0);
        if (rc == EIMBA || rc == ERANGE) bad |= 4;

        /* buffers, with an invalid handle so a call that gets past the
         * pointer check fails with EIHNDL, not by touching anything */
        if (sys4(GEMDOS, 0x3f, 6, 16, unmapped, 0) != EIMBA) bad |= 8;
        if (sys4(GEMDOS, 0x3f, 6, 16, lowvec, 0) != EIMBA) bad |= 8;
        if (sys4(GEMDOS, 0x3f, 6, 16, ro, 0) != EIMBA) bad |= 8;      /* an output */
        if (sys4(GEMDOS, 0x3f, 6, 64, stack_end, 0) != EIMBA) bad |= 8;
        if (sys4(GEMDOS, 0x3f, 6, 0x100, 0xfffffff0LL, 0) != EIMBA) bad |= 8;
        if (sys4(GEMDOS, 0x3f, 6, 0x7fffffffLL, P(buf), 0) != EIMBA) bad |= 8;
        if (sys4(GEMDOS, 0x3f, 6, -1, P(buf), 0) != ERANGE) bad |= 16;
        if (sys4(GEMDOS, 0x40, 6, 16, unmapped, 0) != EIMBA) bad |= 8;
        if (sys4(GEMDOS, 0x3f, 6, 16, P(buf), 0) != EIHNDL) bad |= 32;
        if (sys4(GEMDOS, 0x40, 6, 16, P(buf), 0) != EIHNDL) bad |= 32;
        if (sys4(GEMDOS, 0x40, 6, 16, ro, 0) != EIHNDL) bad |= 32;    /* an input */
        if (sys4(GEMDOS, 0x3f, 6, 0, unmapped, 0) != EIHNDL) bad |= 32;  /* nothing to touch */

        /* the other calls with buffers */
        if (gemdos(0x47, unmapped, 0) != EIMBA) bad |= 64;            /* Dgetpath */
        if (gemdos(0x47, P(buf), 0) != 0) bad |= 64;
        if (gemdos(0x36, unmapped, 3) != EIMBA) bad |= 64;            /* Dfree */
        if (gemdos(0x0a, unmapped, 0) != EIMBA) bad |= 64;            /* Cconrs */
        if (sys4(GEMDOS, 0x57, unmapped, 6, 0, 0) != EIMBA) bad |= 64;   /* Fdatime, get */
        if (sys4(GEMDOS, 0x57, ro, 6, 0, 0) != EIMBA) bad |= 64;         /* writes it */
        if (sys4(GEMDOS, 0x57, ro, 6, 1, 0) != EIHNDL) bad |= 64;        /* only reads it */
        if (gemdos(0x14, 0x100000, 0x1000) != EACCDN) bad |= 64;         /* Maddalt */
        /* scalars are judged as the call takes them: Fdatime()'s flag is an
         * int, so 2^32 is "get" (a write), and S_CONSOLE_DIM's size is a LONG,
         * so 2^32 - 8 + 8 is 8 bytes written whatever the upper half says */
        if (sys4(GEMDOS, 0x57, ro, 6, 0x100000000LL, 0) != EIMBA) bad |= 64;
        if (sys4(GEMDOS, 0x154, (s64)(short)0xfffe, ro, (s64)0xffffffff00000008LL, 0) != EIMBA)
            bad |= 64;
        /* function numbers that are negative once narrowed to an int */
        if (sys4(GEMDOS, 0xffffffffULL, 0, 0, 0, 0) != EINVFN)
            bad |= 64;
        if (sys4(GEMDOS, 0x80000000ULL, 0, 0, 0, 0) != EINVFN)
            bad |= 64;

        /* Pexec: the pointers depend on the mode */
        if (sys4(GEMDOS, 0x4b, 0, unmapped, P(""), 0) != EIMBA) bad |= 128;
        if (sys4(GEMDOS, 0x4b, 0, P("X32HELLO.TOS"), unmapped, 0) != EIMBA) bad |= 128;
        if (sys4(GEMDOS, 0x4b, 0, P("X32HELLO.TOS"), P(""), unmapped) != EIMBA) bad |= 128;
        if (sys4(GEMDOS, 0x4b, 5, 0, P(""), unmapped) != EIMBA) bad |= 128;
        if (sys4(GEMDOS, 0x4b, 5, 0, unmapped, 0) != EIMBA) bad |= 128;
        /* the tail must end within 128 bytes (the kernel adds a NUL after the
         * copy); an empty environment is two NULs, and both must be readable */
        for (k = 0; k < (int)sizeof(tail128); k++)
            tail128[k] = 'A';
        if (sys4(GEMDOS, 0x4b, 5, 0, P(tail128), 0) != ERANGE) bad |= 512;
        if (sys4(GEMDOS, 0x4b, 5, 0, P(""), ro + 0xfff) != EIMBA) bad |= 512;
        /* an environment too long for envsize()'s WORD count */
        for (k = 0; k < (int)sizeof(bigenv); k++)
            bigenv[k] = 'A';
        if (sys4(GEMDOS, 0x4b, 5, 0, P(""), P(bigenv)) != ERANGE) bad |= 512;
        /* the limit: 32766 bytes including the two NULs that end it */
        for (k = 0; k < (int)sizeof(bigenv); k++)
            bigenv[k] = 'A';
        bigenv[32764] = 0;
        bigenv[32765] = 0;                  /* 32766 bytes: just fits */
        rc = sys4(GEMDOS, 0x4b, 5, 0, P(""), P(bigenv));
        if (rc <= 0) {
            bad |= 512;
        } else {
            s64 e3 = *(volatile u32 *)(unsigned long)(rc + 0x2c);

            if (gemdos(0x49, e3, 0) != 0 || gemdos(0x49, rc, 0) != 0)
                bad |= 512;
        }
        bigenv[32764] = 'A';
        bigenv[32765] = 0;
        bigenv[32766] = 0;                  /* 32767 bytes: one too many */
        if (sys4(GEMDOS, 0x4b, 5, 0, P(""), P(bigenv)) != ERANGE) bad |= 512;
        /* a leading NUL is not the end of an environment: envsize() goes on
         * until two NULs in a row, so the bytes after it must be readable
         * (here "\0X" at the very end of the stack mapping) */
        {
            volatile unsigned char *top = (volatile unsigned char *)0x3ffffffeUL;

            top[0] = 0;
            top[1] = 'X';
            if (sys4(GEMDOS, 0x4b, 5, 0, P(""), 0x3ffffffeLL) != EIMBA) bad |= 512;
        }
        /* ... and a valid empty environment ("\0\0") on the last two bytes of
         * the mapping is accepted: nothing past it is read */
        {
            volatile unsigned char *top = (volatile unsigned char *)0x3ffffffeUL;

            top[1] = 0;
            rc = sys4(GEMDOS, 0x4b, 5, 0, P(""), 0x3ffffffeLL);
            if (rc <= 0) {
                bad |= 512;
            } else {
                s64 e2 = *(volatile u32 *)(unsigned long)(rc + 0x2c);

                if (gemdos(0x49, e2, 0) != 0 || gemdos(0x49, rc, 0) != 0)
                    bad |= 512;
            }
        }
        /* an odd-sized environment is copied rounded up to an even size: the
         * byte after its last NUL is read too ("A\0\0" ends the mapping) */
        {
            volatile unsigned char *top = (volatile unsigned char *)0x3ffffffdUL;

            top[0] = 'A';
            top[1] = 0;
            top[2] = 0;
            if (sys4(GEMDOS, 0x4b, 5, 0, P(""), 0x3ffffffdLL) != EIMBA) bad |= 512;
            /* the same, with the byte after it inside the mapping: accepted */
            top = (volatile unsigned char *)0x3ffffff0UL;
            top[0] = 'A';
            top[1] = 0;
            top[2] = 0;
            rc = sys4(GEMDOS, 0x4b, 5, 0, P(""), 0x3ffffff0LL);
            if (rc <= 0) {
                bad |= 512;
            } else {
                s64 e2 = *(volatile u32 *)(unsigned long)(rc + 0x2c);

                if (gemdos(0x49, e2, 0) != 0 || gemdos(0x49, rc, 0) != 0)
                    bad |= 512;
            }
        }
        /* a launch of a basepage needs one: null is refused up front */
        if (sys4(GEMDOS, 0x4b, 4, 0, 0, 0) != EIMBA) bad |= 512;
        if (sys4(GEMDOS, 0x4b, 6, 0, 0, 0) != EIMBA) bad |= 512;
        /* no environment given: the caller's own is inherited through its
         * basepage's p_env, which it can have rewritten */
        {
            volatile u32 *me = (volatile u32 *)(unsigned long)basepage;
            u32 saved = me[11];

            me[11] = 0x30000000u;
            if (sys4(GEMDOS, 0x4b, 5, 0, P(""), 0) != EIMBA) bad |= 512;
            me[11] = 0x84u;
            if (sys4(GEMDOS, 0x4b, 5, 0, P(""), 0) != EIMBA) bad |= 512;
            me[11] = saved;
        }
        /* mode 5's second argument is flags, not a pointer: kernel-looking
         * values there are fine (and a basepage comes back) */
        rc = sys4(GEMDOS, 0x4b, 5, 0, P(""), 0);
        if (rc <= 0) {
            bad |= 256;
        } else {
            s64 env = *(volatile u32 *)(unsigned long)(rc + 0x2c);

            if (gemdos(0x49, env, 0) != 0 || gemdos(0x49, rc, 0) != 0)
                bad |= 256;
        }
        pterm(bad);
        break;
    }
    case 'd': {
        /* the search state in a DTA is the kernel's, not the process's (#437) */
        static u32 dta[16];                 /* a DTA: 44 bytes, ours to set */
        static u32 fresh[16];
        s64 rc;

        gemdos(0x0e, 2, 0);                 /* Dsetdrv(C:) */
        gemdos(0x1a, P(dta), 0);            /* Fsetdta has no return value */
        if ((u32)gemdos(0x2f, 0, 0) != (u32)(unsigned long)dta)
            bad |= 1;                       /* Fgetdta: it must have taken */
        rc = sys4(GEMDOS, 0x4e, P("*.*"), 0x10, 0, 0);   /* files and directories */
        if (rc == 0) {
            /* a new search that finds nothing ends the old one on that DTA */
            if (sys4(GEMDOS, 0x4e, P("NOSUCH.QQQ"), 0x10, 0, 0) == 0)
                bad |= 16;
            if (gemdos(0x4f, 0, 0) != -49)
                bad |= 16;
            rc = sys4(GEMDOS, 0x4e, P("*.*"), 0x10, 0, 0);
        }
        if (rc == 0) {
            /* point the private part at drive 5, which is not mounted: the
             * kernel used to dereference its (null) drive table entry */
            dta[3] = 5;                     /* dt_offset_drive (after dt_name[12]) */
            dta[4] = 0x7fff7fff;            /* dt_cloffset and dt_clnum, 16 bits each */
            rc = gemdos(0x4f, 0, 0);
            if (rc != 0 && rc != -49)
                bad |= 2;                   /* a file, or no more of them */
        } else if (rc != -33 && rc != -49) {
            bad |= 4;                       /* neither a file nor none */
        }
        /* A process keeps the search state of its last eight DTAs.  Search
         * with nine, the first of them again just before the ninth: the one
         * to go is the oldest, the second, and the restarted first goes on. */
        if (rc == 0 || rc == -49 || rc == -33) {
            static u32 many[9][16];
            int m;
            s64 r0, r1, r2;

            for (m = 0; m < 8; m++) {
                gemdos(0x1a, P(many[m]), 0);
                sys4(GEMDOS, 0x4e, P("*.*"), 0x10, 0, 0);
            }
            gemdos(0x1a, P(many[0]), 0);
            sys4(GEMDOS, 0x4e, P("*.*"), 0x10, 0, 0);       /* restart the first */
            gemdos(0x1a, P(many[8]), 0);
            sys4(GEMDOS, 0x4e, P("*.*"), 0x10, 0, 0);       /* the ninth */
            gemdos(0x1a, P(many[2]), 0);
            r2 = gemdos(0x4f, 0, 0);
            gemdos(0x1a, P(many[0]), 0);
            r0 = gemdos(0x4f, 0, 0);
            gemdos(0x1a, P(many[1]), 0);
            r1 = gemdos(0x4f, 0, 0);
            if (r2 == 0 && (r0 != 0 || r1 != -49))      /* (a directory of two or more) */
                bad |= 32;
        }
        /* an exhausted search stays exhausted, even if a file turns up later */
        {
            static u32 ex[16];
            s64 h;

            gemdos(0x1a, P(ex), 0);
            h = sys4(GEMDOS, 0x3c, P("X32EX1.TMP"), 0, 0, 0);
            if (h >= 0) {
                gemdos(0x3e, h, 0);
                if (sys4(GEMDOS, 0x4e, P("X32EX?.TMP"), 0, 0, 0) != 0 ||
                    gemdos(0x4f, 0, 0) != -49)
                    bad |= 64;
                h = sys4(GEMDOS, 0x3c, P("X32EX2.TMP"), 0, 0, 0);
                if (h >= 0)
                    gemdos(0x3e, h, 0);
                if (gemdos(0x4f, 0, 0) != -49)
                    bad |= 64;
                gemdos(0x41, P("X32EX1.TMP"), 0);
                gemdos(0x41, P("X32EX2.TMP"), 0);
            }
        }
        /* a DTA nothing was searched with has no search to continue */
        gemdos(0x1a, P(fresh), 0);
        fresh[3] = 2;
        if (gemdos(0x4f, 0, 0) != -49)
            bad |= 8;
        pterm(bad);
        break;
    }
    case 'l': {
        /* PE_LOAD of a program, then PE_GOTHENFREE of the basepage it gives.
         * The program is in this process's own heap until then: it can be
         * read and patched, and the patch is what runs (#434) */
        static const char patch_tail[] = { 1, 'P', 0 };
        s64 bpa, rc;
        volatile u32 *bpp, *w;
        u32 first, last;

        bpa = sys4(GEMDOS, 0x4b, 3, (s64)(int)(unsigned long)"X32HELLO.TOS",
                   (s64)(int)(unsigned long)patch_tail, 0);
        bpp = (volatile u32 *)(unsigned long)bpa;
        if (bpa == -33)
            pterm(0x100);
        if (bpa <= 0)
            pterm(0x200 | (int)(-bpa & 0xff));
        if (bpp[0] != (u32)bpa)
            bad |= 2;                       /* p_lowtpa */
        if (bpp[2] < 0x10000000 || bpp[2] >= 0x3f000000)
            bad |= 8;                       /* its entry point is in this process's heap */
        if (bpp[1] - bpp[0] > 0x1000)
            bad |= 16;                      /* and its TPA is the basepage and little else */
        /* its initialised data, found by value, patched */
        first = (bpp[4] - bpp[3]) & ~3u;            /* p_dbase - p_tlen: where the image starts */
        last = bpp[6] + bpp[7];                     /* p_bbase + p_blen */
        for (w = (volatile u32 *)(unsigned long)first; (unsigned long)w < last; w++)
            if (*w == 0x11111111u) {
                *w = 0x22222222u;
                break;
            }
        if ((unsigned long)w >= last)
            bad |= 32;                      /* not found */
        rc = sys4(GEMDOS, 0x4b, 6, (s64)(int)(unsigned long)"", bpa, 0);
        if (rc != 0)
            bad |= 4;                       /* the program's own exit code: it saw the patch */
        pterm(bad);
        break;
    }
    case 'G': {
        /* PE_LOAD, patch, then PE_GO (not PE_GOTHENFREE): when the program has
         * ended its pages are this process's again, with what it left in them,
         * and are Malloc() memory to free */
        static const char patch_tail[] = { 1, 'P', 0 };
        s64 bpa, rc;
        volatile u32 *bpp, *w;
        u32 first, last;

        bpa = sys4(GEMDOS, 0x4b, 3, (s64)(int)(unsigned long)"X32HELLO.TOS",
                   (s64)(int)(unsigned long)patch_tail, 0);
        bpp = (volatile u32 *)(unsigned long)bpa;
        if (bpa == -33)
            pterm(0x100);
        if (bpa <= 0)
            pterm(0x200 | (int)(-bpa & 0xff));
        first = (bpp[4] - bpp[3]) & ~3u;
        last = bpp[6] + bpp[7];
        for (w = (volatile u32 *)(unsigned long)first; (unsigned long)w < last; w++)
            if (*w == 0x11111111u) {
                *w = 0x22222222u;
                break;
            }
        if ((unsigned long)w >= last)
            pterm(32);
        rc = sys4(GEMDOS, 0x4b, 4, (s64)(int)(unsigned long)"", bpa, 0);
        if (rc != 0)
            bad |= 4;                       /* the program's own exit code */
        if (*w != 0x22222222u)
            bad |= 2;                       /* readable again, as it was left */
        if (gemdos(0x49, first & ~0xfffu, 0) != 0)
            bad |= 8;                       /* a block of ours now */
        if (gemdos(0x49, first & ~0xfffu, 0) == 0)
            bad |= 16;                      /* and only once */
        pterm(bad);
        break;
    }
    case 'H': {
        /* the same with the child leaving a block of its own below the image */
        static const char patch_tail[] = { 1, 'm', 0 };
        s64 bpa, rc, gap;
        volatile u32 *bpp, *w;
        u32 first;

        /* a gap below the program: the heap's first block is freed again */
        gap = gemdos(0x48, 0x10000, 0);
        if (gap <= 0)
            pterm(64);
        bpa = sys4(GEMDOS, 0x4b, 3, (s64)(int)(unsigned long)"X32HELLO.TOS",
                   (s64)(int)(unsigned long)patch_tail, 0);
        bpp = (volatile u32 *)(unsigned long)bpa;
        if (bpa == -33)
            pterm(0x100);
        if (bpa <= 0)
            pterm(0x200 | (int)(-bpa & 0xff));
        first = (bpp[4] - bpp[3]) & ~3u;
        w = (volatile u32 *)(unsigned long)first;
        if (gemdos(0x49, gap, 0) != 0)
            bad |= 128;
        rc = sys4(GEMDOS, 0x4b, 4, (s64)(int)(unsigned long)"", bpa, 0);
        if (rc != 0)
            bad |= 4;                       /* the program's own exit code */
        (void)*w;                           /* readable again */
        if (gemdos(0x49, first & ~0xfffu, 0) != 0)
            bad |= 8;                       /* a block of ours now, though the child put its own below it */
        if (gemdos(0x49, first & ~0xfffu, 0) == 0)
            bad |= 16;                      /* and only once */
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
    case 'o': {
        /* Rwabs() reads straight into the caller's buffer: sector 0 of C:
         * is a FAT boot sector, which ends in 0x55 0xAA (#446) */
        /* Rwabs() transfers whole logical sectors of the volume, up to
         * MAX_LOGSEC_SIZE (bios/blkdev.h, 32768) bytes each, not 512 */
        static unsigned char sector[32768];
        u32 args[6];

        args[0] = 0;                    /* r_w: read */
        args[1] = (u32)(unsigned long)sector;
        args[2] = 1;                    /* numb */
        args[3] = 0;                    /* first */
        args[4] = 2;                    /* drive C: */
        args[5] = 0;                    /* lfirst */
        if (sys(BIOS, 4, (s64)(int)(unsigned long)args, 0, 0) != 0)
            bad |= 1;
        if (sector[510] != 0x55 || sector[511] != 0xAA)
            bad |= 2;
        pterm(bad);
        break;
    }
    case 'a': {
        /* a program linked for another address is moved into the image
         * window and relocated, an unaligned absolute address in an
         * instruction included (#433): once started by mode 0, once loaded
         * by PE_LOAD and then launched */
        static const char reloc_tail[] = { 1, 'a', 0 };
        s64 rc, bpa;

        rc = sys4(GEMDOS, 0x4b, 0, (s64)(int)(unsigned long)"X32RELOC.TOS",
                  (s64)(int)(unsigned long)reloc_tail, 0);
        if (rc == -33)
            pterm(0x100);
        if (rc != 0)
            pterm(1);
        bpa = sys4(GEMDOS, 0x4b, 3, (s64)(int)(unsigned long)"X32RELOC.TOS",
                   (s64)(int)(unsigned long)reloc_tail, 0);
        if (bpa <= 0)
            pterm(2);
        rc = sys4(GEMDOS, 0x4b, 6, (s64)(int)(unsigned long)"", bpa, 0);
        if (rc != 0)
            pterm(4);
        /* linked for another address without its relocations: refused, with
         * EPLFMT, instead of run with every absolute address wrong */
        rc = sys4(GEMDOS, 0x4b, 0, (s64)(int)(unsigned long)"X32NOREL.TOS",
                  (s64)(int)(unsigned long)reloc_tail, 0);
        if (rc == -33)
            pterm(0x100);
        pterm(rc == -66 ? 0 : 8);
        break;
    }
    case 'h': {
        /* a child loaded from a file gets Malloc() memory of its own (#434) */
        static const char heap_tail[] = { 1, 'h', 0 };
        s64 rc = sys4(GEMDOS, 0x4b, 0, (s64)(int)(unsigned long)"X32HELLO.TOS",
                      (s64)(int)(unsigned long)heap_tail, 0);

        if (rc == -33)
            pterm(0x100);
        pterm(rc == 0 ? 0 : 1);
        break;
    }
    case 'j': {
        /* a new process has its image in the image window, linked for it or
         * not, and its text is read-only: a child that writes to it faults
         * (0xffff) */
        static const char link_tail[] = { 1, 'i', 0 };
        static const char text_tail[] = { 1, 'T', 0 };
        static const char area_tail[] = { 1, 'S', 0 };
        s64 rc, rc2, rc3, rc4;

        rc = sys4(GEMDOS, 0x4b, 0, (s64)(int)(unsigned long)"X32HELLO.TOS",
                  (s64)(int)(unsigned long)link_tail, 0);
        if (rc == -33)
            pterm(0x100);
        rc2 = sys4(GEMDOS, 0x4b, 0, (s64)(int)(unsigned long)"X32RELOC.TOS",
                   (s64)(int)(unsigned long)link_tail, 0);
        if (rc2 == -33)
            pterm(0x100);
        rc3 = sys4(GEMDOS, 0x4b, 0, (s64)(int)(unsigned long)"X32HELLO.TOS",
                   (s64)(int)(unsigned long)text_tail, 0);
        /* and the startup area after it holds what the C startup code builds */
        rc4 = sys4(GEMDOS, 0x4b, 0, (s64)(int)(unsigned long)"X32HELLO.TOS",
                   (s64)(int)(unsigned long)area_tail, 0);
        pterm((rc == 0 ? 0 : 1) | (rc2 == 0 ? 0 : 2) | (rc3 == 0xffff ? 0 : 4) |
              (rc4 == 0 ? 0 : 8));
        break;
    }
    case 'F': {
        /* what is not Malloc() memory takes the ordinary path with the same
         * narrowed pointer: a basepage this process made (Pexec mode 5) is
         * freed through a pointer with other bits in the upper half (#434) */
        s64 bpa = sys4(GEMDOS, 0x4b, 5, 0, (s64)(int)(unsigned long)"", 0);

        if (bpa <= 0)
            pterm(1);
        pterm(sys4(GEMDOS, 0x49, bpa | 0xffff00000000LL, 0, 0, 0) == 0 ? 0 : 2);
        break;
    }
    case 'L': {
        /* the program PE_LOAD put in this process's heap is not Malloc()
         * memory: Mfree() leaves it alone; freeing the basepage unlaunched
         * gives it back (#434) */
        s64 a, bpa, b;
        volatile u32 *bpp;
        u32 image;

        a = sys4(GEMDOS, 0x48, 4096, 0, 0, 0);
        bpa = sys4(GEMDOS, 0x4b, 3, (s64)(int)(unsigned long)"X32HELLO.TOS",
                   (s64)(int)(unsigned long)"", 0);
        if (bpa == -33)
            pterm(0x100);
        if (a <= 0 || bpa <= 0)
            pterm(1);
        bpp = (volatile u32 *)(unsigned long)bpa;
        image = (bpp[4] - bpp[3]) & ~0xfffu;        /* the image's first page */
        if (image != (u32)a + 4096)
            bad |= 2;                       /* first fit: right after the first block */
        if (sys4(GEMDOS, 0x49, image, 0, 0, 0) != -40)
            bad |= 4;                       /* Mfree() leaves it alone */
        if (sys4(GEMDOS, 0x49, bpa, 0, 0, 0) != 0)
            bad |= 8;                       /* free the basepage */
        b = sys4(GEMDOS, 0x48, 4096, 0, 0, 0);
        if (b != image)
            bad |= 16;                      /* and the address space is free again */
        pterm(bad);
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
