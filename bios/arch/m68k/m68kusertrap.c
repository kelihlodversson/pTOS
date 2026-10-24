/*
 * m68kusertrap.c - BIOS and XBIOS calls of a process in its own address space
 *                  (68040, 68060)
 *
 * The trap #13 and #14 handler (vectors.S) hands a call made in user mode to
 * m68k_user_biosxbios(), which runs it on the kernel stack of the process,
 * unlike the classic handler, which runs the BIOS on the stack of its caller.
 * The arguments are on the user stack as the 68000 pushed them, tightly packed
 * words and longs; they are copied, decoded into the long arguments
 * bios/usertrap.c checks, and the shim of the vector table (bios.c, xbios.c)
 * is given the copy.
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
#include "xbios.h"

#include "../bdos/uaccess.h"
long bios_user_check(WORD fn, const long *a, const long *rest, int xbios);
long bios_user_result_filter(long r, WORD fn, int xbios);

extern LONG xbios_unimpl(void);

/* what vectors.S builds on the stack */
struct bxargs {
    const PFLONG *vecs;
    ULONG ent;
    ULONG usp;
    ULONG xbios;
};

#define ARG_WORDS   10                  /* the function number, and the most a call takes */
#define XB_SUPEXEC  0x26

/* the words of the calls that take pointers (or are checked for another reason), after the function number */
struct shape {
    UBYTE fn;
    const char *w;
};

static const struct shape bshape[] = {
    { 0x04, "wlwww" },                  /* Rwabs(rw, buf, cnt, recno, dev): lrecno is not looked at */
    { 0x05, "wl" },                     /* Setexc */
};

static const struct shape xshape[] = {
    { 0x00, "wll" },                    /* Initmous(type, param, vec) */
    { 0x05, "llww" },                   /* Setscreen(log, phys, rez, mode) */
    { 0x06, "l" },                      /* Setpalette */
    { 0x0C, "wl" },                     /* Midiws(cnt, ptr) */
    { 0x12, "llww" },                   /* Protobt(buf, serial, type, exec) */
    { 0x19, "wl" },                     /* Ikbdws */
    { 0x20, "l" },                      /* Dosound */
    { 0x2A, "lwlw" },                   /* DMAread(sector, count, buf, major) */
    { 0x2B, "lwlw" },                   /* DMAwrite */
    { 0x2C, "w" },                      /* Bconmap */
};

/* the arguments of the call as the longs bios/usertrap.c looks at; a[0-3] the first four, rest[0] the fifth */
static void decode(const char *w, const WORD *pw, long *a, long *rest)
{
    int n = 0, word = 0;

    for (; *w; w++, n++) {
        long v;

        if (*w == 'w') {
            v = pw[word++];
        } else {
            v = ((long)(UWORD)pw[word] << 16) | (UWORD)pw[word + 1];
            word += 2;
        }
        if (n < 4)
            a[n] = v;
        else if (n == 4)
            rest[0] = v;
    }
}

LONG m68k_user_biosxbios(struct bxargs *args);

LONG m68k_user_biosxbios(struct bxargs *args)
{
    WORD pw[ARG_WORDS];
    long a[4] = { 0, 0, 0, 0 };
    long rest[2] = { 0, 0 };
    const char *w = NULL;
    const struct shape *tab = args->xbios ? xshape : bshape;
    int count = args->xbios ? (int)ARRAY_SIZE(xshape) : (int)ARRAY_SIZE(bshape);
    WORD fn;
    long rc;
    int i;
    LONG r;

    if (!user_stack_words(args->usp, pw, ARG_WORDS))
        return ERANGE;
    fn = pw[0];
    if (fn < 0 || (ULONG)fn >= args->ent)
        return (LONG)(UWORD)fn;         /* not implemented: the number, as TOS does */

    for (i = 0; i < count; i++)
        if (tab[i].fn == fn)
            w = tab[i].w;
    if (w)
        decode(w, pw + 1, a, rest);
    if (args->xbios && fn == XB_SUPEXEC)
        return EACCDN;
    rc = bios_user_check(fn, a, rest, (int)args->xbios);
    if (rc != E_OK)
        return rc;

    if (args->vecs[fn] == (PFLONG)xbios_unimpl)
        r = xbios_do_unimpl(fn);
    else
        r = ((LONG (*)(const WORD *))args->vecs[fn])(pw + 1);
    return bios_user_result_filter(r, fn, (int)args->xbios);
}

#endif /* CONF_WITH_USER_ASPACE */
