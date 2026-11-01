/*
 * biosaccess.c - checking the arguments of a BIOS or XBIOS call from ring 3
 *
 * A BIOS (trap 13) or XBIOS (trap 14) call made by a user process runs in
 * the kernel under the process's own page tables, so a pointer it is given
 * reaches whatever the process could not itself map, the supervisor-only
 * system variables and kernel data included, unless it is checked first.
 * This is the BIOS/XBIOS counterpart of bdos/uaccess.c, which does it for
 * GEMDOS (the rules, and the error codes, are the same): a function per trap
 * class that knows, for every call that has them, which arguments are
 * pointers, what they point at and how big that is, and looks at exactly
 * those.  Arguments a call does not use are scalars and are never looked at.
 *
 * A pointer is also a way of putting a user address into kernel state that is
 * used later, from an interrupt or a callback, under whichever page tables
 * are loaded then, and a function pointer would be called in ring 0.  Calls
 * that would do that are refused to a user process (EACCDN) unless the
 * kernel can take a copy; see doc/x86_64-syscall.txt for the classification
 * of every call.
 *
 * The calls that take their arguments in a struct (Rwabs(), Floprd(),
 * Flopwr(), Flopver(), Flopfmt(), Rsconf()) are decoded and checked by the
 * trap handler itself (bios/arch/x86_64/trap.c); they are not described here.
 *
 * Copyright (C) 2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "emutos.h"
#include "gemerror.h"
#include "blkdev.h"
#include "disk.h"
#include "mouse.h"
#include "../bdos/uaccess.h"
#include <aspace_types.h>
#include "biosaccess.h"

#if CONF_WITH_USER_COPY

#define SEC_PROTOBT     512         /* the boot sector Protobt() reads and writes */
#define PALETTE_BYTES   32          /* the 16 words Setpalette() takes */
#define LAST_SETEXC_VEC 0x102       /* the highest vector number a user may query */

/* -1, "no new vector": setexc() compares the whole long, so another value
 * that merely looks like it (0xffffffff) would be installed */
#define IS_MINUS1(v)    ((v) == -1L)

long bios_check_user_args(WORD fn, const long *a)
{
    switch (fn) {
    case 0:                         /* Getmpb(mpb): kernel memory descriptors */
        return EACCDN;
    case 7:                         /* Getbpb(dev): a pointer to the kernel's BPB */
        return EACCDN;
    case 5:                         /* Setexc(vecnum, vec) */
        /* a vector set by a user process would be called, or pointed at, by
         * the kernel; only a query is allowed, and only of a vector a
         * program can name (the number is scaled and read as a kernel
         * address) */
        if (!IS_MINUS1(a[1]))
            return EACCDN;
        if ((WORD)a[0] < 0 || (WORD)a[0] > LAST_SETEXC_VEC)
            return ERANGE;
        return E_OK;
    default:
        return E_OK;
    }
}

long xbios_check_user_args(WORD fn, const long *a)
{
    switch (fn) {
    case 0x00:                      /* Initmous(type, param, vec) */
        /* the vector would be called from the keyboard interrupt in ring 0 */
        if (a[2])
            return EACCDN;
        switch ((WORD)a[0]) {
        case 1:
        case 2:
        case 4:
            return a[1] ? user_buf_check(a[1], sizeof(struct param), FALSE)
                        : E_OK;
        default:
            return E_OK;
        }
    case 0x05:                      /* Setscreen(log, phys, rez, mode) */
        /* a log or phys address becomes the screen the kernel and the VDI
         * draw on: only "no change" (0 or -1) is allowed, not an address
         * of the kernel half any more than one of the process */
        if ((a[0] != 0 && !IS_MINUS1(a[0])) || (a[1] != 0 && !IS_MINUS1(a[1])))
            return EACCDN;
        return E_OK;
    case 0x06:                      /* Setpalette(pal): read at the next VBL */
        if (a[0] & 1)
            return EIMBA;
        return user_buf_check(a[0], PALETTE_BYTES, FALSE);
    case 0x0C:                      /* Midiws(cnt, ptr): cnt + 1 bytes */
    case 0x19:                      /* Ikbdws(cnt, ptr) */
        return user_buf_check(a[1], (long)(UWORD)a[0] + 1, FALSE);
    case 0x12:                      /* Protobt(buf, serial, type, exec) */
        return user_buf_check(a[0], SEC_PROTOBT, TRUE);
    case 0x0E:                      /* Iorec(dev): a pointer to a kernel buffer record */
    case 0x22:                      /* Kbdvbase(): a pointer to the kernel's vectors */
        return EACCDN;
    case 0x20:                      /* Dosound(table): kept, read by the timer */
        /* the whole slot: 0x100000000 is neither 0 nor -1 */
        if (a[0] != 0 && !IS_MINUS1(a[0]))
            return EACCDN;
        return E_OK;
    case 0x2A:                      /* DMAread(sector, count, buf, major) */
    case 0x2B: {                    /* DMAwrite */
#if CONF_WITH_USER_ASPACE
        ULONG bytes;
        long unit = NUMFLOPPIES + (long)(WORD)a[3];
        WORD count = (WORD)a[1];
        WORD rw = (fn == 0x2A ? RW_READ : RW_WRITE) | RW_NOTRANSLATE;

        /* the device first, even for a count of 0: the implementation looks
         * the unit up whatever the count is */
        if (unit < 0 || unit >= UNITSNUM
            || !blkdev_rwabs_buffer_size(rw, 1, (WORD)unit, &bytes))
            return EUNDEV;
        if (count < 0)
            return ERANGE;
        if (!count)
            return E_OK;
        if (!blkdev_rwabs_buffer_size(rw, count, (WORD)unit, &bytes))
            return ERANGE;
        return user_buf_check(a[2], bytes, fn == 0x2A);
#else
        return E_OK;
#endif
    }
    case 0x2C:                      /* Bconmap(dev): -2 is the kernel's table */
        if ((WORD)a[0] == -2)
            return EACCDN;
        return E_OK;
    default:
        return E_OK;
    }
}

/*
 * A result that names kernel memory.  A user address lies below
 * USER_VA_LIMIT; the vectors Setexc() queries and the table Dosound() was
 * given by the kernel's own code (the bell) are kernel code and data.  The
 * caller learns nothing of where, only that no user routine is installed.
 */
static long user_pointer(long r)
{
    return (unsigned long)r >= (unsigned long)USER_VA_LIMIT ? 0L : r;
}

long bios_user_result(WORD fn, long r)
{
    if (fn == 5)                    /* Setexc(): the old vector */
        return user_pointer(r);
    return r;
}

long xbios_user_result(WORD fn, long r)
{
    switch (fn) {
    case 0x20:                      /* Dosound(): the table that was playing */
        return user_pointer(r);
    case 0x02:                      /* Physbase() */
    case 0x03:                      /* Logbase() */
        return 0;                   /* the screen is not in the process */
    default:
        return r;
    }
}

#endif /* CONF_WITH_USER_COPY */
