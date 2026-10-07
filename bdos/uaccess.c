/*
 * uaccess.c - checking the pointers a GEMDOS call is given
 *
 * A GEMDOS call from a process that is not trusted (a ring-3 process on
 * x86-64) must not make the kernel read or write memory the process could
 * not itself.  Whether an argument is a pointer, what it points at and how
 * big that is, is only known per call, so this file has a descriptor for
 * every call that takes pointers and checks exactly those arguments before the
 * call runs.  Arguments a call does not use are never looked at (an early
 * check of every raw argument, tried once, rejected valid calls whose unused
 * arguments happened to hold kernel addresses, and accepted unmapped ones).
 *
 * The kernel services a system call under the calling process's page tables,
 * so memory that passes the check is simply used through the same address:
 * there is nothing to copy or translate.  What the check establishes, for the
 * whole range [ptr, ptr + len): every page is mapped and user-accessible
 * (and writable, for an output) in the calling process, the range does not
 * wrap, and it lies within the 32-bit address space of the x32 ABI.  The
 * supervisor-only mappings the kernel keeps in every address space (the
 * system variables, kernel data) have no user bit and so fail it.
 *
 * Without per-process address spaces (CONF_WITH_USER_ASPACE off) there is no
 * mapping to check against, and the checks reduce to null pointers and bad
 * lengths: enough to run the same descriptors on every architecture.
 *
 * Copyright (C) 2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "emutos.h"
#include "string.h"
#include "gemerror.h"
#include "bdosdefs.h"
#include "kproc.h"
#include "fs.h"
#include "proc.h"
#include "bdosstub.h"
#include "ssystem.h"
#include "uaccess.h"

#if CONF_WITH_USER_COPY

#define UA_PATH_MAX     1024UL      /* longest path or file name string */
#define UA_ENV_MAX      32766UL     /* longest environment block (envsize() counts in a WORD) */
#define UA_TAIL_MAX     PDCLSIZE    /* command tail: copied up to this many */
#define UA_PATHBUF      128         /* what Dgetpath() may write */
#define UA_DFREE        16          /* a DISKINFO */
#define UA_DATIME       4           /* Fdatime()'s time and date words */

enum { UA_STR = 1, UA_BUF, UA_FIXED, UA_CONRS, UA_DTAPTR, UA_CURDTA, UA_PRIV };
enum { UD_R = 1, UD_W = 2, UD_RW = 3 };

typedef struct {
    UWORD fn;           /* GEMDOS function number */
    UBYTE arg;          /* pw[] index of the pointer (0: none) */
    UBYTE kind;
    UBYTE dir;
    UBYTE lenarg;       /* UA_BUF: pw[] index of the length */
    UWORD fixed;        /* UA_FIXED: the length */
} UARG;

static const UARG uargs[] = {
    { 0x09, 1, UA_STR,    UD_R,  0, 0 },                 /* Cconws */
    { 0x0A, 1, UA_CONRS,  UD_RW, 0, 0 },                 /* Cconrs */
#if CONF_WITH_USER_ASPACE
    { 0x14, 0, UA_PRIV,   0,     0, 0 },                 /* Maddalt: not for a user process */
#endif
    { 0x1A, 1, UA_DTAPTR, UD_W,  0, 0 },                 /* Fsetdta */
    { 0x36, 1, UA_FIXED,  UD_W,  0, UA_DFREE },          /* Dfree */
    { 0x39, 1, UA_STR,    UD_R,  0, 0 },                 /* Dcreate */
    { 0x3A, 1, UA_STR,    UD_R,  0, 0 },                 /* Ddelete */
    { 0x3B, 1, UA_STR,    UD_R,  0, 0 },                 /* Dsetpath */
    { 0x3C, 1, UA_STR,    UD_R,  0, 0 },                 /* Fcreate */
    { 0x3D, 1, UA_STR,    UD_R,  0, 0 },                 /* Fopen */
    { 0x3F, 3, UA_BUF,    UD_W,  2, 0 },                 /* Fread */
    { 0x40, 3, UA_BUF,    UD_R,  2, 0 },                 /* Fwrite */
    { 0x41, 1, UA_STR,    UD_R,  0, 0 },                 /* Fdelete */
    { 0x43, 1, UA_STR,    UD_R,  0, 0 },                 /* Fattrib */
    { 0x47, 1, UA_FIXED,  UD_W,  0, UA_PATHBUF },        /* Dgetpath */
    { 0x4E, 1, UA_STR,    UD_R,  0, 0 },                 /* Fsfirst: path ... */
    { 0x4E, 0, UA_CURDTA, UD_W,  0, 0 },                 /* ... and its DTA */
    { 0x4F, 0, UA_CURDTA, UD_W,  0, 0 },                 /* Fsnext */
    { 0x56, 2, UA_STR,    UD_R,  0, 0 },                 /* Frename: old ... */
    { 0x56, 3, UA_STR,    UD_R,  0, 0 },                 /* ... and new name */
};

/* ------------------------------------------------------------------ */

/* The pointer argument as a user address, or FALSE if it cannot be one:
 * the x32 ABI has 32-bit addresses. */
static BOOL uaddr(long v, ULONG *a)
{
    if ((ULONG)((UQUAD)v >> 32) != 0)
        return FALSE;
    *a = (ULONG)(UQUAD)v;
    return TRUE;
}

/* [a, a + len) usable by the calling process (len > 0), for writing if asked */
static BOOL range_ok(ULONG a, ULONG len, BOOL write)
{
    if (!len)
        return TRUE;
    if (a + len < a)                    /* wraps around the 4 GiB space */
        return FALSE;
#if CONF_WITH_USER_ASPACE
    return write ? kproc_validate_user_write(a, len)
                 : kproc_validate_user_range(a, len);
#else
    return a != 0;
#endif
}

/* A NUL-terminated string of at most `max` bytes (the NUL included), all of
 * it readable; ERANGE if it does not end within `max` bytes. */
static long str_ok(long v, ULONG max)
{
    ULONG a, n = 0;

    if (!uaddr(v, &a) || !a)
        return EIMBA;
#if CONF_WITH_USER_ASPACE
    while (n < max) {
        ULONG chunk = 4096UL - (a & 4095UL);
        const char *s;
        ULONG i;

        if (chunk > max - n)
            chunk = max - n;
        if (!range_ok(a, chunk, FALSE))
            return EIMBA;
        s = (const char *)(uintptr_t)a;     /* validated: usable directly */
        for (i = 0; i < chunk; i++)
            if (!s[i])
                return E_OK;
        a += chunk;
        n += chunk;
    }
    return ERANGE;
#else
    (void)n;
    (void)max;
    return E_OK;
#endif
}

static long buf_ok(long v, long len, BOOL write)
{
    ULONG a;

    if (len < 0 || (UQUAD)len > 0xffffffffULL)
        return ERANGE;
    if (!len)
        return E_OK;
    if (!uaddr(v, &a) || !range_ok(a, (ULONG)len, write))
        return EIMBA;
    return E_OK;
}

/* ------------------------------------------------------------------ */

/* Cconrs(): byte 0 is the capacity, then the length byte, then the text */
static long conrs_ok(long v)
{
    ULONG a;

    if (!uaddr(v, &a) || !range_ok(a, 1, FALSE))
        return EIMBA;
#if CONF_WITH_USER_ASPACE
    return buf_ok(v, 2L + *(const UBYTE *)(uintptr_t)a, TRUE);
#else
    return E_OK;
#endif
}

/* the address Fsetdta() is given: the kernel writes a DTAINFO there later */
static long dtaptr_ok(long v)
{
#if CONF_WITH_USER_ASPACE
    ULONG a;

    return (uaddr(v, &a) && kproc_validate_user_dta(a)) ? E_OK : EIMBA;
#else
    return v ? E_OK : EIMBA;
#endif
}

/* the DTA the process set (or the default one in its basepage) */
static long curdta_ok(void)
{
#if CONF_WITH_USER_ASPACE
    return kproc_validate_user_dta((UQUAD)(uintptr_t)kproc_get_dta(run)) ? E_OK : EIMBA;
#else
    return E_OK;
#endif
}

/*
 * An environment: strings, each ending with a NUL, the list ending at the first
 * NUL that is itself followed by a NUL -- exactly where envsize() stops, which
 * is what matters: every byte it reads, including the second NUL, must be
 * readable, and the list must be short enough for envsize()'s WORD count.
 * (An empty list is two NULs; a lone leading NUL followed by something else is
 * not the end of anything.)
 */
static long env_ok(long v)
{
    ULONG a, n = 0;

    if (!uaddr(v, &a))
        return EIMBA;
#if CONF_WITH_USER_ASPACE
    while (n < UA_ENV_MAX) {
        ULONG chunk = 4096UL - (a & 4095UL);
        const char *s;
        ULONG i;

        if (chunk > UA_ENV_MAX - n)
            chunk = UA_ENV_MAX - n;
        if (!range_ok(a, chunk + 1, FALSE))     /* one more: the byte after the last */
            return EIMBA;
        s = (const char *)(uintptr_t)a;         /* validated: usable directly */
        for (i = 0; i < chunk; i++)
            if (!s[i] && !s[i + 1])
                return E_OK;
        a += chunk;
        n += chunk;
    }
    return ERANGE;
#else
    (void)n;
    return E_OK;
#endif
}

/*
 * Pexec(mode, path, tail, env): which arguments are pointers depends on the
 * mode.  Modes 0 and 3 load a file (path is a string); 5 and 7 only create a
 * basepage (the path slot holds flags); 4 and 6 launch a basepage the caller
 * made, which is looked up in the kernel's own records and never dereferenced
 * here.  The tail is copied from the user and must end (with its NUL) within
 * PDCLSIZE bytes, as the kernel adds a terminator of its own after the copy;
 * the environment, if given, is a list of strings ending with a double NUL.
 */
static long pexec_ok(const long *pw)
{
    WORD mode = (WORD)pw[1];
    long rc;

    if (mode == 4 || mode == 6)
        return E_OK;
    if (mode == 0 || mode == 3) {
        rc = str_ok(pw[2], UA_PATH_MAX);
        if (rc)
            return rc;
    } else if (mode != 5 && mode != 7) {
        return E_OK;                    /* not a mode: the call says EINVFN */
    }
    rc = str_ok(pw[3], UA_TAIL_MAX);
    if (rc)
        return rc;
    if (pw[4])
        return env_ok(pw[4]);
#if CONF_WITH_USER_ASPACE
    /* no environment given: alloc_env() inherits the caller's own, through the
     * p_env field of its basepage, which the caller can have rewritten */
    return env_ok((long)run->p_env);
#else
    return E_OK;
#endif
}

/* Fdatime(buf, handle, wflag): the two words are read when setting, else written */
static long fdatime_ok(const long *pw)
{
    return buf_ok(pw[1], UA_DATIME, pw[3] == 0);
}

/* Ssystem(mode, arg1, arg2): only the modes that store through a pointer */
static long ssystem_ok(const long *pw)
{
    WORD mode = (WORD)pw[1];
    long arg1 = pw[2], arg2 = pw[3];

#if CONF_WITH_USER_ASPACE
    /* S_SETLVAL/S_SETWVAL/S_SETBVAL store a caller-chosen value into a kernel
     * system variable, among them vectors the kernel calls in ring 0: not for
     * a user process.  (Without address spaces every caller is trusted.) */
    if (mode >= 0x000d && mode <= 0x000f)
        return EACCDN;
#endif
    if (mode == 0x0008 && arg2)                         /* S_GETCOOKIE value */
        return buf_ok(arg2, 4, TRUE);
    if (mode == (WORD)0xfffe && arg2 > 0)               /* S_CONSOLE_DIM struct */
        return buf_ok(arg1, arg2 < (long)sizeof(struct console_dim) ? arg2
                            : (long)sizeof(struct console_dim), TRUE);
    return E_OK;
}

/* ------------------------------------------------------------------ */

long bdos_check_user_args(const long *pw)
{
    long fn = pw[0];
    UWORD i;
    long rc;

    if (fn == 0x4B)
        return pexec_ok(pw);
    if (fn == 0x57)
        return fdatime_ok(pw);
    if (fn == GEMDOS_SSYSTEM)
        return ssystem_ok(pw);

    for (i = 0; i < ARRAY_SIZE(uargs); i++) {
        const UARG *u = &uargs[i];

        if (u->fn != fn)
            continue;
        switch (u->kind) {
        case UA_STR:
            rc = str_ok(pw[u->arg], UA_PATH_MAX);
            break;
        case UA_BUF:
            rc = buf_ok(pw[u->arg], pw[u->lenarg], u->dir & UD_W);
            break;
        case UA_FIXED:
            rc = buf_ok(pw[u->arg], u->fixed, u->dir & UD_W);
            break;
        case UA_CONRS:
            rc = conrs_ok(pw[u->arg]);
            break;
        case UA_DTAPTR:
            rc = dtaptr_ok(pw[u->arg]);
            break;
        case UA_CURDTA:
            rc = curdta_ok();
            break;
        case UA_PRIV:
            rc = EACCDN;
            break;
        default:
            rc = E_OK;
        }
        if (rc)
            return rc;
    }
    return E_OK;
}

#endif /* CONF_WITH_USER_COPY */
