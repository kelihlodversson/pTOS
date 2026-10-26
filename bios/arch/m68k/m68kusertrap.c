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

#if CONF_ATARI_HIGHER_HALF
/*
 * The Atari has far more XBIOS calls than the shared checks of
 * bios/biosaccess.c know of, and several of them hand an address to the
 * kernel that it would use in supervisor mode: Mfpint() and Xbtimer() install
 * an interrupt handler, VgetRGB() writes to a pointer, Keytbl() and Prtblk()
 * read tables at one.  So a program may make only the calls below, whose
 * arguments are numbers or are checked by xbios_check_user_args(); the rest
 * are refused until they are checked one by one.
 */
static BOOL atari_xbios_allowed(WORD fn)
{
    switch (fn) {
    case 0x00: case 0x02: case 0x03: case 0x04: case 0x05: case 0x06:
    case 0x07: case 0x0C: case 0x0F: case 0x11: case 0x12: case 0x15:
    case 0x16: case 0x17: case 0x18: case 0x19: case 0x1A: case 0x1B:
    case 0x1C: case 0x1D: case 0x1E: case 0x20: case 0x21: case 0x23:
    case 0x25: case 0x27: case 0x2A: case 0x2B: case 0x2C: case 0x40:
    case 0x58: case 0x59: case 0x5A: case 0x5B: case 0x5C:
        return TRUE;
    default:
        return FALSE;
    }
}
#endif

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
#if CONF_ATARI_HIGHER_HALF
    if (args->xbios && !atari_xbios_allowed(fn))
        return EACCDN;
#endif
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
