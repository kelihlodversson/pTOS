/*
 * kfile.c - GEMDOS file calls for kernel code
 *
 * Copyright (C) 2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

/*
 * Kernel code that runs in supervisor mode / ring 0 already (the VDI
 * reading font files for the scalable fonts) calls the GEMDOS file
 * functions directly, not through a trap and osif().  The handles and the
 * files are still those of the current process, as with a trap.
 *
 * The functions need no translation of their arguments: osif() only
 * redirects the standard handles 0 to 5, and a file the kernel opens gets
 * a handle above them.  What osif() does that they must do too is catch
 * the longjmp() the file system makes on a disk error (DISK_CALL(), in
 * fs.h); they return the error code instead.
 *
 * On x86-64 they are also what Fopen() & co. are in kernel code
 * (include/bdosbind.h, CONF_WITH_KCALL).
 */

#include "emutos.h"
#include "string.h"
#include "fs.h"
#include "bdosstub.h"

long kdos_fopen(long name, long mode)
{
    return DISK_CALL(xopen((char *)name, (WORD)mode));
}

/*
 * A disk error while xclose() flushes the buffers (of any drive, not only
 * the file's) longjmp()s out before the handle is released.  By then the
 * file is off its directory's list, and the failing drive's buffers have
 * been dropped, so closing again finds no more to do than release the
 * handle (or is interrupted by another drive's buffers, and is tried
 * again).  Only an interrupted close is repeated: one that returned, even
 * with an error, has given up its reference already.  The first result is
 * what is returned.
 */
long kdos_fclose(long h)
{
    long rc = 0, r;
    volatile BOOL returned;
    int tries;

    for (tries = 0; tries <= BLKDEVNUM; tries++)
    {
        returned = FALSE;
        r = DISK_CALL(__extension__ ({
            long closed = xclose((WORD)h);
            returned = TRUE;
            closed;
        }));
        if (!tries)
            rc = r;
        if (returned)
            break;
    }

    return rc;
}

long kdos_fread(long h, long count, long buf)
{
    return DISK_CALL(xread((WORD)h, count, (void *)buf));
}

long kdos_fseek(long offset, long h, long mode)
{
    return DISK_CALL(xlseek(offset, (WORD)h, (WORD)mode));
}

/*
 * Fsfirst()/Fsnext() into a DTA the caller names, not the process's own:
 * a search by kernel code leaves the current process's DTA alone.  A
 * search should be run until it fails (ENMFIL), so that a pluggable file
 * system's search slot is given back.
 */
long kdos_fsfirst_at(long name, long att, DTA *dta)
{
    return DISK_CALL(xfsfirst_at((char *)name, (WORD)att, (DTAINFO *)dta));
}

long kdos_fsnext_at(DTA *dta)
{
    return DISK_CALL(xfsnext_at((DTAINFO *)dta));
}
