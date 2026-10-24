/*
 * usertrap.c - what a user process may ask of the BIOS and the XBIOS (ARM)
 *
 * The BIOS and XBIOS trap entries (vectorsasm.S) call bios_user_check() before
 * a call made by a process in user mode (CONF_WITH_USER_ASPACE) and
 * bios_user_result_filter() after it.  The rules are those of bios/biosaccess.c, which
 * has them for the calls with up to four arguments in registers; what a
 * call with more, or that takes its arguments in a way only the architecture
 * knows, needs is here.  A call the kernel itself makes does not come this way.
 *
 * Copyright (C) 2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"

#if CONF_WITH_USER_ASPACE

#include "emutos.h"
#include "gemerror.h"
#include "blkdev.h"
#include "disk.h"
#include "../bdos/uaccess.h"
#include "biosaccess.h"

#define BIOS_RWABS      4
#define XBIOS_SETPALETTE 6
#define XBIOS_FLOPRD    8
#define XBIOS_FLOPWR    9
#define XBIOS_FLOPFMT   10
#define XBIOS_FLOPVER   19
#define XBIOS_DMAREAD   0x2A
#define XBIOS_DMAWRITE  0x2B

/*
 * fn is the function, a[0..3] its first four arguments (the registers r1-r4)
 * and rest[0..] the fifth and later ones, as the call itself reads them off the
 * stack.  Returns E_OK, or the error the caller gets instead of the call.
 */
long bios_user_check(WORD fn, const long *a, const long *rest, int xbios);
long bios_user_result_filter(long r, WORD fn, int xbios);

long bios_user_check(WORD fn, const long *a, const long *rest, int xbios)
{
    if (!xbios) {
        if (fn == BIOS_RWABS) {
            /* Rwabs(rwflag, buf, cnt, recno, dev, lrecno) */
            WORD rw = (WORD)a[0];
            WORD cnt = (WORD)a[2];
            WORD dev = (WORD)rest[0];
            ULONG bytes;

            if (!a[1] && dev >= 0 && dev < NUMFLOPPIES)
                return E_OK;        /* TOS: a null buffer updates the media-change state */
            if (!blkdev_rwabs_buffer_size(rw, cnt, dev, &bytes))
                return ERANGE;
            return user_buf_check(a[1], (long)bytes, (rw & RW_RW) == RW_READ);
        }
        return bios_check_user_args(fn, a);
    }

    switch (fn) {
    case XBIOS_SETPALETTE:          /* the kernel reads the palette at a later time, under other tables */
    case XBIOS_FLOPRD:              /* the floppy calls take more arguments than four... */
    case XBIOS_FLOPWR:
    case XBIOS_FLOPFMT:
    case XBIOS_FLOPVER:             /* ...and there is no floppy on these machines */
        return EACCDN;
    case XBIOS_DMAREAD:             /* these go to the disk driver with the address of the */
    case XBIOS_DMAWRITE:            /* caller, where Rwabs() has a bounce buffer for the DMA of */
        return EACCDN;              /* a virtio disk: refused until they have one */
    default:
        return xbios_check_user_args(fn, a);
    }
}

/* the result r of call fn, as the process may see it (kernel addresses are 0) */
long bios_user_result_filter(long r, WORD fn, int xbios)
{
    return xbios ? xbios_user_result(fn, r) : bios_user_result(fn, r);
}

#endif /* CONF_WITH_USER_ASPACE */
