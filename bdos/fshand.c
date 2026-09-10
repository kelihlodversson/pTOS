/*
 * fshand.c - file handle routines for the file system
 *
 * Copyright (C) 2001 Lineo, Inc.
 *               2014-2019 The EmuTOS development team
 *
 * Authors:
 *  SCC   Steve C. Cavender
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

/* #define ENABLE_KDEBUG */

#include "emutos.h"
#include "fs.h"
#include "gemerror.h"
#include "bdosstub.h"

/*
 * xforce - 0x46, force a std handle to a non-std handle
 *
 * Arguments:
 *
 *  std - must be a standard handle
 *  h   - must NOT be a standard handle
 *
 * Error returns:
 *     EIHNDL
 */
long xforce(int std, int h)
{
    return ixforce(std,h,run);
}


/*
 * ixforce - force a std handle to a non-std handle
 *
 * Arguments:
 *
 *  std - must be a standard handle
 *  h   - must NOT be a standard handle
 */
long ixforce(int std, int h, PD *p)
{
    long fh;

    if ((std < 0) || (std >= NUMSTD))   /* validate standard handle */
        return EIHNDL;

    if (h < 0)                  /* if the non-std handle is a BIOS handle, */
        p->p_uft[std] = h;      /* just store it as-is in the PD table */
    else
    {
        if (h < NUMSTD)         /* validate the non-std handle */
            return EIHNDL;

        if (!getofd(h))         /* check that the non-std handle exists */
            return EIHNDL;

        /*
         * if the non-std handle is currently mapped to a BIOS handle,
         * store the BIOS handle in the PD table; otherwise store the
         * non-std handle & update the use count
         */
        if ((fh = (long) sft[h-NUMSTD].f_ofd) < 0L)
            p->p_uft[std] = fh;
        else
        {
            p->p_uft[std] = h;
            sft[h-NUMSTD].f_use++;
        }
    }

    return E_OK;
}


/*
 * xdup - 0x45, duplicate a file handle
 *
 * Arguments:
 *
 *  h  - must be standard handle (checked)
 *
 * Error returns:
 *     EIHNDL
 *     ENHNDL
 *
 */
long xdup(int h)
{
    int i;
    long fh;

    if ((h < 0) || (h >= NUMSTD))
        return EIHNDL;          /* only dup standard */

    fh = run->p_uft[h];

#if CONF_WITH_PLUGGABLE_FS
    /*
     * A foreign driver's cookie has no shared/refcounted lifetime today
     * - xclose() calls a driver's close()/release() once per sft[] slot,
     * gated only on that slot's own f_use (bdos/fsopnclo.c).  Copying
     * the cookie into a second, independently-tracked slot would let
     * each slot's f_use reach zero on its own and double-release the
     * same driver-side resource, as well as give the two handles
     * independent PFSCOOKIE.pos values instead of a shared file
     * position.  Refuse the dup rather than corrupt driver state; FAT
     * handles are unaffected (they're never tagged f_pfs, see
     * native_handles in fs/pfs.h), so this only blocks foreign drivers,
     * none of which are in this tree yet - lift it once pluggable
     * handles get a real shared/refcounted cookie.
     */
    if ((fh > 0) && sft[fh-NUMSTD].f_pfs.fs)
        return EIHNDL;
#endif

    for (i = 0; i < OPNFILES; i++)      /* find the first free handle */
        if (!sft[i].f_own)
            break;

    if (i == OPNFILES)
        return ENHNDL;          /* no free handles */

    sft[i].f_own = run;

    /*
     * if the standard handle is currently mapped to a non-BIOS
     * handle, copy the OFD pointer from the corresponding sft[]
     * entry to the new entry; otherwise, store the BIOS handle
     * in the OFD pointer variable
     */
    if (fh > 0)
    {
        sft[i].f_ofd = sft[fh-NUMSTD].f_ofd;
    }
    else
    {
        sft[i].f_ofd = (OFD *)fh;
    }
#if CONF_WITH_PLUGGABLE_FS
    sft[i].f_pfs.fs = 0;   /* pluggable handles are rejected above */
#endif

    sft[i].f_use = 1;

    return i+NUMSTD;            /* return the new handle */
}
