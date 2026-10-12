/*
 * mintproc.c - the MiNT process calls pTOS can give
 *
 * Process ids, the user and group ids, the audit id, the priority, resource usage
 * and the current directory as a string.  Only a process with a kernel record
 * (CONF_WITH_KPROC) has a pid; pTOS has no users (every id is 0, the one user is
 * root), no groups and a cooperative scheduler, which the answers say.  What it
 * cannot honour is left to osif()'s EINVFN (Dlock: there is no other process to
 * lock out; Pnice, Pumask: there is no scheduler to nice and no permission bits
 * to mask).  See doc/mint-calls.txt.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "emutos.h"
#include "string.h"
#include "fs.h"
#include "gemerror.h"
#include "kproc.h"
#include "bdosstub.h"
#include "mintproc.h"

/* what Dgetpath() may write (bdos/uaccess.c UA_PATHBUF) */
#define DGETCWD_BUF     128

#define PRIO_PROCESS    0
#define PRIO_PGRP       1
#define PRIO_USER       2
#define PRIO_MIN        (-20)
#define PRIO_MAX        20

/* into the caller's memory: checked and copied where processes have address
 * spaces, else (every caller is trusted) just copied, if it is a pointer at all */
static BOOL to_user(UQUAD address, const void *src, ULONG n)
{
#if CONF_WITH_USER_ASPACE
    return kproc_copy_to_user(address, src, n);
#else
    if (!address)
        return FALSE;
    memcpy((void *)(uintptr_t)address, src, n);
    return TRUE;
#endif
}

/* the process a pid names: only the caller (0 or its own pid) can be, as there is
 * no list of others a program may look at */
static BOOL pid_is_caller(WORD pid)
{
    return pid == 0 || pid == kproc_pid(run);
}

/*
 * Pgetpriority(which, who): FreeMiNT's answer is PRIO_MAX minus the priority, 0
 * to 40, so that an error stays negative.  The process, its group (it is alone in
 * it) and its user (root, 0, the only one) can be asked about, by 0 or by their
 * own number.
 */
static long pgetpriority(WORD which, WORD who)
{
    if (who < 0)
        return EINVAL;
    switch (which)
    {
    case PRIO_PROCESS:
    case PRIO_PGRP:             /* a process leads a group of its own, numbered as it is */
        if (!pid_is_caller(who))
            return ESRCH;
        break;
    case PRIO_USER:             /* the user is root, 0 */
        if (who != 0)
            return ESRCH;
        break;
    default:
        return EINVAL;
    }
    return PRIO_MAX - kproc_get_prio(run);
}

static long psetpriority(WORD which, WORD who, WORD pri)
{
    long rc = pgetpriority(which, who);

    if (rc < 0)
        return rc;
    if (pri < PRIO_MIN)
        pri = PRIO_MIN;
    else if (pri > PRIO_MAX)
        pri = PRIO_MAX;
    kproc_set_prio(run, (SBYTE)pri);
    return E_OK;
}

/*
 * Dgetcwd(path, drv, size): the current directory of drive drv (0: the current
 * drive) as Dgetpath() gives it, into the caller's buffer of `size` bytes, NUL
 * included; ERANGE when it does not fit.
 */
static long dgetcwd(UQUAD path, WORD drv, WORD size)
{
    char buf[DGETCWD_BUF];
    long rc;
    ULONG n;

    if (size <= 0)
        return ERANGE;
    if (setjmp(errbuf))
        return errcode;                 /* a disk error while looking */
    rc = xgetdir(buf, drv);
    if (rc)
        return rc;
    n = strlen(buf) + 1;
    if (n > (ULONG)size)
        return ERANGE;
    return to_user(path, buf, n) ? E_OK : EIMBA;
}

long mintproc_call(const long *pw, BOOL *handled)
{
    ULONG usage[8];

    *handled = TRUE;
    switch (pw[0])
    {
    case GEMDOS_PGETPID:
        return kproc_pid(run);
    case GEMDOS_PGETPPID:
        return kproc_parent_pid(run);
    case GEMDOS_PGETUID:
    case GEMDOS_PGETEUID:
    case GEMDOS_PGETGID:
    case GEMDOS_PGETEGID:
    case GEMDOS_PGETAUID:
        return 0;                       /* there are no users: all of it is root */
    case GEMDOS_PSETAUID:
        /* FreeMiNT lets it be set once and never changed; there is only 0 */
        return (WORD)pw[1] == 0 ? E_OK : EINVAL;
    case GEMDOS_PGETPRIORITY:
        return pgetpriority((WORD)pw[1], (WORD)pw[2]);
    case GEMDOS_PSETPRIORITY:
        return psetpriority((WORD)pw[1], (WORD)pw[2], (WORD)pw[3]);
    case GEMDOS_PRUSAGE:
        kproc_usage(run, usage);
        return to_user((UQUAD)pw[1], usage, sizeof(usage)) ? E_OK : EIMBA;
    case GEMDOS_DGETCWD:
        return dgetcwd((UQUAD)pw[1], (WORD)pw[2], (WORD)pw[3]);
    default:
        *handled = FALSE;
        return 0;
    }
}
