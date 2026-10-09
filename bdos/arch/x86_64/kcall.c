/*
 * kcall.c - GEMDOS for kernel code on x86-64
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
 * with the translation osif() does for it (a handle to its open file, for
 * instance, which is why there is no Fread() or Fwrite() yet).
 *
 * osif() also catches the longjmp() the file system makes on a disk error.
 * A call that can reach the disk does the same here, minus the media change
 * retry: it returns the error code.
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

/*
 * run a call that can reach the disk; the file system reports a hard error
 * with a longjmp() to errbuf, which only osif() has set up so far
 */
#define DISK_CALL(call)                                         \
__extension__                                                   \
({                                                              \
    long _rc;                                                   \
    jmp_buf _saved;                                             \
                                                                \
    memcpy(_saved, errbuf, sizeof(errbuf));                     \
    if (setjmp(errbuf))                                         \
    {                                                           \
        _rc = errcode;                                          \
        mark_bcbs_invalid(errdrv);                              \
    }                                                           \
    else                                                        \
        _rc = (call);                                           \
    memcpy(errbuf, _saved, sizeof(errbuf));                     \
    _rc;                                                        \
})

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
