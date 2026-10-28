/*
 * mmuprobe.h - the supervisor-side fixture of the MMU tests (#737)
 *
 * A program cannot read the MMU registers or test a translation, and
 * Supexec() is not for a process with an address space of its own.  The
 * regression tests ask the kernel, with Ssystem(S_MMUPROBE, ...) (bdos/ssystem.h):
 * what the MMU is and how it is set up, and what an address of the caller
 * translates to, with the instruction of the CPU that does it (PTEST), so that
 * the answer does not depend on what the program can reach.  Only in a build
 * with the regression tests (CONF_WITH_MMU_PROBE).
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */
#ifndef MMUPROBE_H
#define MMUPROBE_H

#include "portab.h"

/* flags of the access to test */
#define MMUPROBE_WRITE  0x1     /* a write (else a read) */
#define MMUPROBE_SUPER  0x2     /* a supervisor data access (else a user data access) */

/* the fields of an unknown value are 0xffffffff */
struct mmu_probe {
    ULONG va;                   /* in: the address to translate, in the address space of the caller */
    ULONG flags;                /* in: MMUPROBE_* */
    ULONG model;                /* the MMU: 30, 40 or 60 (the CPU), 0 if none */
    ULONG enabled;              /* translation is on */
    ULONG tc;                   /* TC */
    ULONG root[4];              /* 68030: CRP (high, low), SRP (high, low); 68040/60: URP, SRP, 0, 0 */
    ULONG status;               /* PSR (68030) or MMUSR (68040/60) of the PTEST */
    ULONG mapped;               /* the address translates to a page (not an invalid descriptor, not a
                                 * bus error of the walk) */
    ULONG access;               /* and the access is allowed: the page is not supervisor-only for a user
                                 * access, and not write-protected for a write */
    ULONG phys;                 /* the physical address it translates to */
    ULONG super_only;           /* the page is supervisor-only */
    ULONG write_protected;      /* the page is write-protected */
    ULONG cache_mode;           /* 68040/60: 0 write-through, 1 copyback, 2 not cached and serialized,
                                 * 3 not cached; 68030: 2 where the descriptor inhibits the cache, else 0 */
    ULONG window_ok;            /* phys is reachable through the window of the kernel */
    ULONG window_word;          /* the four bytes at phys (big endian), read through the window: the
                                 * same bytes by a second mapping */
};

#if CONF_WITH_MMU_PROBE
LONG mmu_probe(struct mmu_probe *p);    /* bios/arch/m68k/mmuprobe.c */
#endif

#endif /* MMUPROBE_H */
