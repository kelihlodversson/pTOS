/*
 * classicvec.c - unpack a classic-ABI call of one of the default BIOS vectors
 *
 * See classicvec.h.  The entry points (util/arch/m68k/miscasm.S) pass the
 * address of the caller's arguments, still packed as TOS has them, and the
 * handler; the arguments are read out here in C, so that the call of the
 * handler is made by the compiler in whichever ABI the kernel is built with.
 *
 * Copyright (C) 2025-2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "emutos.h"
#include "classicvec.h"

#if !CONF_WITH_NATIVE_ABI

/* a LONG of the frame: two WORDs, the higher first (they need not be long aligned) */
static LONG frame_long(const WORD *p)
{
    return ((LONG)(UWORD)p[0] << 16) | (UWORD)p[1];
}

/* (WORD, WORD): Bconout() of a device */
LONG classic_ww(const WORD *frame, LONG (*fn)(WORD, WORD))
{
    return fn(frame[0], frame[1]);
}

/* (WORD): hdv_bpb, hdv_mediach */
LONG classic_w(const WORD *frame, LONG (*fn)(WORD))
{
    return fn(frame[0]);
}

/* (WORD rw, void *buf, WORD cnt, WORD recnr, WORD dev, LONG lrecnr): hdv_rw */
LONG classic_rwabs(const WORD *frame,
                   LONG (*fn)(WORD, UBYTE *, WORD, WORD, WORD, LONG))
{
    return fn(frame[0], (UBYTE *)frame_long(frame + 1), frame[3], frame[4],
              frame[5], frame_long(frame + 6));
}

#endif /* !CONF_WITH_NATIVE_ABI */
