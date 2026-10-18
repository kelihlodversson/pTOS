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
#include "gemerror.h"
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
 * handle (or fails on another drive's buffers, and is tried again).  The
 * first error is what is returned.
 */
long kdos_fclose(long h)
{
    long rc, again;
    int tries;

    rc = again = DISK_CALL(xclose((WORD)h));
    for (tries = 0; again < 0 && again != EIHNDL && tries < BLKDEVNUM; tries++)
        again = DISK_CALL(xclose((WORD)h));

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
