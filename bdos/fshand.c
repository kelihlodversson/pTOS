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
#if CONF_WITH_USER_ASPACE
#include "kproc.h"
#endif
#include "bdosstub.h"
#if CONF_WITH_PROCESS_FILES
#include "process_files.h"
#endif

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
long xforce(WORD std, WORD h)
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
    FTAB *file;
    FS_HANDLE handle;

    if ((std < 0) || (std >= NUMSTD))   /* validate standard handle */
        return EIHNDL;
#if CONF_WITH_USER_ASPACE
    if (!kproc_file_references_allowed(p)) return EACCDN;
#endif
#if CONF_WITH_PROCESS_FILES
    if (process_files_managed(p)) {
        LONG result;
        if (h<0) {
            return process_files_force_bios(p,(WORD)std,(WORD)h);
        }
        if (h<NUMSTD) return EIHNDL;
        result=process_files_duplicate(run,(WORD)h,p,(WORD)std);
        return result<0?result:E_OK;
    }
#endif

#if CONF_WITH_USER_ASPACE
    if (PD_UFT(p)[std]>=NUMSTD && fs_sft_busy(PD_UFT(p)[std]-NUMSTD))
        return EACCDN;
#endif
    if (h < 0)                  /* if the non-std handle is a BIOS handle, */
        PD_UFT(p)[std] = h;      /* just store it as-is in the PD table */
    else
    {
        if (h < NUMSTD)         /* validate the non-std handle */
            return EIHNDL;

        if (fs_handle_lookup(h, &handle) != E_OK)
            return EIHNDL;
        file = handle.slot;

        /*
         * if the non-std handle is currently mapped to a BIOS handle,
         * store the BIOS handle in the PD table; otherwise store the
         * non-std handle & update the use count
         */
        if (handle.bios)
            PD_UFT(p)[std] = handle.bios;
        else
        {
            PD_UFT(p)[std] = h;
            file->f_use++;
#if !CONF_WITH_FAT_FILE_LIFETIMES
            /* Inheritance does not change the slot owner. Retain a
             * conservative marker until all opens of this identity close:
             * f_use alone cannot distinguish a parent's closed explicit
             * handle from the child's remaining inherited reference. */
            if (file->f_ofd && p != file->f_own)
                file->f_ofd->o_dfd->o_flag |= O_INHERITED;
#endif
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
long xdup(WORD h)
{
    int i;
    FS_HANDLE source;

    if ((h < 0) || (h >= NUMSTD))
        return EIHNDL;          /* only dup standard */
#if CONF_WITH_PROCESS_FILES
    if (process_files_managed(run)) return process_files_duplicate(run,h,run,-1);
#endif
#if CONF_WITH_USER_ASPACE
    if (!kproc_file_references_allowed(run)) return EACCDN;
#endif

    if (fs_handle_lookup(h, &source) != E_OK)
        return EIHNDL;

#if CONF_WITH_PLUGGABLE_FS
    /*
     * Legacy Fdup creates a second numbered slot. A PFS cookie has one
     * cursor and one final-release owner, so only a binding that shares
     * the original slot can duplicate it.
     */
    if (source.slot && source.slot->f_pfs.fs)
        return EIHNDL;
#endif
    for (i = 0; i < OPNFILES; i++)      /* find the first free handle */
        if (!sft[i].f_own)
            break;

    if (i == OPNFILES)
        return ENHNDL;          /* no free handles */

#if CONF_WITH_USER_ASPACE
    if (!fs_sft_claim(i)) return ENHNDL;
#endif
    sft[i].f_own = run;

    sft[i].f_bios = source.bios;
    sft[i].f_ofd = source.slot ? source.slot->f_ofd : NULL;
#if CONF_WITH_PLUGGABLE_FS
    sft[i].f_pfs.fs = 0;   /* pluggable handles are rejected above */
#endif

    sft[i].f_use = 1;

    return i+NUMSTD;            /* return the new handle */
}
