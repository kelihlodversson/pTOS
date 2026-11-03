/*
 * kcall.c - GEMDOS for kernel code with address spaces
 *
 * Kernel code (the boot sequence, the self-tests, the VDI) calls GEMDOS by
 * name: Pexec(), Mfree(), Fsfirst(), ... (include/bdosbind.h).  On this
 * port those names are plain C calls to the implementations in this
 * directory's parent, with the arguments already of the kind the
 * implementation takes: there is no trap, no dispatcher and no argument
 * array.  osif() is only what a process's `syscall` reaches, and the only
 * place that looks at a caller's pointers (bdos/uaccess.c).
 *
 * Only the calls kernel code makes exist; one that needs more is added here
 * with the translation osif() does for it.  Fopen(), Fread(), Fseek() and
 * Fclose() are in bdos/kfile.c, as kernel code on every architecture uses
 * them. File operations retain normal handle resolution in xread()/xwrite()
 * and the filesystem layer.
 *
 * osif() also catches the longjmp() the file system makes on a disk error.
 * A call that can reach the disk does the same here, with DISK_CALL()
 * (bdos/fs.h), minus the media change retry: it returns the error code.
 *
 * Copyright (C) 2018-2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "emutos.h"
#include "string.h"
#include "setjmp.h"
#include "fs.h"
#include "mem.h"
#include "proc.h"
#include "console.h"
#include "bdosbind.h"

long kdos_crawio(long w)
{
    return xrawio((int)w);
}

long kdos_dsetdrv(long drv)
{
    return DISK_CALL(xsetdrv((int)drv));
}

long kdos_fsetdta(long dta)
{
    xsetdta((DTAINFO *)dta);
    return 0;
}

long kdos_dsetpath(long path)
{
    return DISK_CALL(xchdir((char *)path));
}

long kdos_fsfirst(long name, long attr)
{
    return DISK_CALL(xsfirst((char *)name, (int)attr));
}

long kdos_fsnext(void)
{
    return DISK_CALL(xsnext());
}

long kdos_mxalloc(long amount, long mode)
{
    return (long)xmxalloc(amount, (int)mode);
}

#if CONF_WITH_VIDEL
long kdos_srealloc(long amount)
{
    return (long)srealloc(amount);
}
#endif

long kdos_malloc(long amount)
{
    return (long)xmalloc(amount);
}

long kdos_mfree(long block)
{
    return xmfree((void *)block);
}

long kdos_mshrink(long block, long newsiz)
{
    return xsetblk(0, (void *)block, newsiz);
}

/*
 * xexec() reaches the disk (to find and read the program's header) before it
 * installs a handler of its own, and relies on the one osif() used to set up;
 * the exit of a process closes its files
 */
long kdos_pexec(long mode, long path, long tail, long env)
{
    return DISK_CALL(xexec((WORD)mode, (char *)path, (char *)tail, (char *)env));
}

void kdos_pterm(long rc)
{
    (void)DISK_CALL((xterm((UWORD)rc), 0L));
}

long kdos_cconws(long text)
{
    return xconws((char *)text);
}
long kdos_cconis(void)
{
    return xconstat();
}
long kdos_crawcin(void)
{
    return xrawcin();
}
long kdos_dgetdrv(void)
{
    return xgetdrv();
}
long kdos_fgetdta(void)
{
    return (long)xgetdta();
}
long kdos_fcreate(long name, long attr)
{
    return DISK_CALL(xcreat((char *)name, (UBYTE)attr));
}
long kdos_fwrite(long handle, long count, long buffer)
{
    return DISK_CALL(xwrite((WORD)handle, count, (void *)buffer));
}
long kdos_fdelete(long name)
{
    return DISK_CALL(xunlink((char *)name));
}
long kdos_frename(long zero, long oldname, long newname)
{
    return DISK_CALL(xrename((WORD)zero, (char *)oldname, (char *)newname));
}
long kdos_fattrib(long name, long flag, long attr)
{
    return DISK_CALL(xchmod((char *)name, (WORD)flag, (WORD)attr));
}
long kdos_fdatime(long buffer, long handle, long flag)
{
    return DISK_CALL(xgsdtof((DOSTIME *)buffer, (WORD)handle, (WORD)flag));
}
long kdos_dcreate(long name)
{
    return DISK_CALL(xmkdir((char *)name));
}
long kdos_ddelete(long name)
{
    return DISK_CALL(xrmdir((char *)name));
}
long kdos_dgetpath(long buffer, long drive)
{
    return DISK_CALL(xgetdir((char *)buffer, (WORD)drive));
}
long kdos_dfree(long buffer, long drive)
{
    return DISK_CALL(xgetfree((LONG *)buffer, (WORD)drive));
}
