/*
 * bdosdefs.h - Public definitions for BDOS system calls
 *
 * Copyright (C) 2014-2019 The EmuTOS development team
 *
 * Authors:
 *  VRI   Vincent Rivière
 *  RFB   Roger Burrows
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef _BDOSDEFS_H
#define _BDOSDEFS_H

/* Values for Mxalloc() mode */
#define MX_STRAM 0
#define MX_TTRAM 1
#define MX_PREFSTRAM 2
#define MX_PREFTTRAM 3
#define MX_MODEMASK  0x03   /* mask for supported mode bits */

/* Values of 'mode' for Pexec() */
#define PE_LOADGO     0
#define PE_LOAD       3
#define PE_GO         4
#define PE_BASEPAGE   5
#define PE_GOTHENFREE 6
#define PE_BASEPAGEFLAGS 7
#if DETECT_NATIVE_FEATURES
#define PE_RELOCATE   50    /* required for NatFeats support only, not in Atari TOS */
#endif

/* File Attributes bits used by FAT and Fsfirst() / Fattrib() */
#define FA_RO           0x01
#define FA_HIDDEN       0x02
#define FA_SYSTEM       0x04
#define FA_VOL          0x08
#define FA_SUBDIR       0x10
#define FA_ARCHIVE      0x20

/* Values of 'wrt' for Fattrib() */
#define F_GETMOD 0x0
#define F_SETMOD 0x1

typedef struct
{
    char    d_reserved[21];     /* internal EmuTOS usage */
    char    d_attrib;           /* attributes */
    UWORD   d_time;             /* packed time */
    UWORD   d_date;             /* packed date */
    LONG    d_length;           /* size */
    char    d_fname[14];        /* name */
} DTA;

/*
 *  PD - Process Descriptor (a.k.a. BASEPAGE)
 *
 *  This is the real, in-memory GEMDOS basepage layout every ILP32 user
 *  process reads directly at these fixed byte offsets (the "0xNN"-style
 *  comments below) -- unlike bdosdefs.h's other structs (MD/MPB above),
 *  which are purely kernel-internal bookkeeping never exposed to a
 *  process at a fixed address. On the ILP32 arches (m68k, ARM) a native
 *  pointer already IS the 32-bit ABI field a process expects, so
 *  USERPTR_T(type) below is just `type *` there, unchanged from before
 *  this comment existed. On x86-64's LP64 kernel, a native pointer is 64
 *  bits -- using one here would silently produce a different (larger,
 *  differently-offset) struct layout than the ABI a real ring-3 x32
 *  process reads, exactly the mismatch cli/arch/x86_64/cmdasm.c's own
 *  comment on `sizeof(PD)` flagged as "the undecided design question
 *  #334 owns" before this was resolved: USERPTR_T(type) is ULONG there
 *  instead, matching the ABI's real 32-bit field width and giving this
 *  struct the traditional 256-byte layout on every arch. Kernel code
 *  that needs a real, dereferenceable pointer from one of these fields
 *  (or needs to store one into one) must go through USERPTR_TO_PTR()/
 *  PTR_TO_USERPTR() below rather than assuming a field is directly
 *  dereferenceable or directly assignable from a real pointer -- true
 *  today only on the ILP32 arches, where those macros are a no-op, and
 *  necessary on x86-64, where a field is a plain integer needing an
 *  explicit widen/narrow.
 */
#ifdef __x86_64__
#define USERPTR_T(type) ULONG
/* Widens a stored 32-bit ABI value back into a real, dereferenceable
 * kernel pointer -- valid to do at all only because #334's per-process
 * address-space design keeps every such value within the low canonical
 * range every process (and the kernel, via the identity-style low
 * mapping #351/#334 need) can already reach directly; this performs the
 * widening arithmetic only, not any access validation (a ring-3-supplied
 * pointer's own trustworthiness is #352's separate, still-open concern).
 * Returns void * -- callers cast to whatever pointee type the specific
 * field actually holds (DTA *, PD *, char *, ...), same as the ILP32
 * arches' own real pointer fields already require for anything other
 * than UBYTE *. */
#define USERPTR_TO_PTR(up) ((void *)(uintptr_t)(ULONG)(up))
/* Narrows a real kernel pointer down to the 32-bit ABI field width.
 * Traps rather than silently truncating if the address does not
 * actually fit -- the exact silent-corruption bug #351 documents for a
 * plain (LONG) cast, just caught here instead of reproduced. Every
 * caller's own pointer is expected to already be low/32-bit-representable
 * by construction (#334's per-process address-space design only ever
 * hands out such addresses for anything meant to end up in a PD field),
 * so this is a correctness safety net, not a normal-path failure mode. */
static inline ULONG ptr_to_userptr(const void *p)
{
    UQUAD addr = (UQUAD)(uintptr_t)p;

    if (addr > 0xFFFFFFFFULL)
        __builtin_trap();
    return (ULONG)addr;
}
#define PTR_TO_USERPTR(p) ptr_to_userptr(p)
#else
#define USERPTR_T(type) type *
#define USERPTR_TO_PTR(up) ((void *)(up))
#define PTR_TO_USERPTR(p) (p)
#endif

#define NUMSTD      6       /* number of standard files */
#define NUMCURDIR   BLKDEVNUM   /* number of entries in curdir array */
#define PDCLSIZE    0x80    /*  size of command line in bytes  */

typedef struct _pd PD;
struct _pd
{
/* 0x00 */
    USERPTR_T(UBYTE) p_lowtpa;      /* pointer to start of TPA */
    USERPTR_T(UBYTE) p_hitpa;       /* pointer to end of TPA+1 */
    USERPTR_T(UBYTE) p_tbase;       /* pointer to base of text segment */
    LONG    p_tlen;         /* length of text segment */
/* 0x10 */
    USERPTR_T(UBYTE) p_dbase;       /* pointer to base of data segment */
    LONG    p_dlen;         /* length of data segment */
    USERPTR_T(UBYTE) p_bbase;       /* pointer to base of bss segment */
    LONG    p_blen;         /* length of bss segment */
/* 0x20 */
    USERPTR_T(DTA) p_xdta;
    USERPTR_T(PD) p_parent;      /* parent PD */
    ULONG   p_flags;        /* see below */
    USERPTR_T(char) p_env;         /* pointer to environment string */
/* 0x30 */
    SBYTE   p_uft[NUMSTD];  /* index into sys file table for std files */
    char    p_lddrv;
    UBYTE   p_curdrv;
    LONG    p_1fill[2];
/* 0x40 */
    UBYTE   p_curdir[NUMCURDIR];    /* index into sys dir table */
    char    p_2fill[32-NUMCURDIR];
/* 0x60 */
    LONG    p_3fill[2];
    LONG    p_dreg[1];      /* dreg[0] */
    LONG    p_areg[5];      /* areg[3..7] */
/* 0x80 */
    char    p_cmdlin[PDCLSIZE];     /* command line image */
};

/* p_flags values: */
#define PF_FASTLOAD     0x0001
#define PF_TTRAMLOAD    0x0002
#define PF_TTRAMMEM     0x0004
#define PF_STANDARD     (PF_FASTLOAD | PF_TTRAMLOAD | PF_TTRAMMEM)

/*
 *  MD - Memory Descriptor
 */
typedef struct _md MD;
struct _md
{
        MD      *m_link;    /* next MD, or NULL */
        UBYTE   *m_start;   /* start address of memory block */
        LONG    m_length;   /* number of bytes in memory block*/
        PD      *m_own;     /* owner's process descriptor */
};

/*
 *  MPB - Memory Partition Block
 */
typedef struct _mpb MPB;
struct _mpb
{
        MD      *mp_mfl;    /* memory free list */
        MD      *mp_mal;    /* memory allocated list */
        MD      *mp_rover;  /* roving pointer - no longer used */
};

#define PATH_ENV "PATH="    /* PATH environment variable */

#endif /* _BDOSDEFS_H */
