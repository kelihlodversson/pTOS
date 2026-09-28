/*
 * fsmain.c - GEMDOS file system notes and misc routines
 *
 * Copyright (C) 2001 Lineo, Inc.
 *               2002-2022 The EmuTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

/* #define ENABLE_KDEBUG */

#include "emutos.h"
#include "fs.h"
#include "mem.h"
#include "gemerror.h"
#include "biosbind.h"
#include "bdosstub.h"

/*
**
** GEMDOS file system
** ------------------
** Originally written by JSL as noted below.
**
** MODIFICATION HISTORY
**
**      13 Mar 85       SCC     Changed xgetfree() to return values into longs.
**                              (As per spec).
**
**                              Changed handling of getbpb() return to allow
**                              flagging of bad getbpb().
**
**      14 Mar 85       JSL     Modified getrec() to clear b_dirty flag.
**
**                              Modified ixcreat() to force flush
**                              (for the sake of xmkdir() ).
**
**      22 Mar 85       SCC     Modified xsetdrv() to return drive map (to
**                              bring up to spec functionality, specifically
**                              at request of Steve Schmitt (DR Logo) ).
**
**                              Extended good/bad returns based on ckdrv()
**                              results.
**
**      26 Mar 85       JSL     Modified xsfirst() to be a front end to
**                              ixsfirst().  ixsfirst() is used internally by
**                              xexec() to force a disk access prior to
**                              xpgmld() so that media change info can be
**                              updated sooner.
**
**                              Modified ixlseek() because of bug when pointer
**                              is at end of cluster prior to forward seek.
**
**                      SCC     Made above actual changes from JSL's notes.
**
**      27 Mar 85       SCC     Modified dcrack() and findit() to terminate with
**                              indication of no file found if bad return from
**                              ckdrv().
**
**      28 Mar 85       SCC     Modified xchdir() to look for NEGATIVE return
**                              from ckdrv().
**
**      29 Mar 85       JSL     Fixed xrmdir() bug of "ghost" subdirectories.
**                              Fixed problem creating files in unaccessed
**                              subdir.
**
**       4 Apr 85       SCC     Modified several functions to improve
**                              readability.
**
**                              Removed a number of old 'SCC  ?? ??? 85'
**                              modification marks, and began adding formfeeds
**                              between routines and routine headers.
**
**                              Modified dup() to return long value and distinct
**                              error codes.
**
**                              Modified xforce() and ixforce() to return long
**                              value and distinct error codes, and to range
**                              check 'h'.
**
**                              Now includes gemerror.h.
**
**                              Modified xclose() and ixclose() to return long
**                              value and distinct error codes.
**
**       5 Apr 85       SCC     Modified ixforce() to range check 'std', and
**                              moved it after xforce().
**
**       8 Apr 85       SCC     Added declaration of 'drv' parameter to log().
**
**                              Added 'long' type to declaration of flush()
**                              and error returns.
**
**                              Added 'long' type to declaration of usrio().
**
**                              Added 'int' type to declaration of getpath()
**                              and 'flg' parameter.
**
**                              Added 'long' type to declaration of getcl()
**                              and error returns.
**
**                              Added 'long' type to declaration of ckdrv()
**                              and error returns.
**
**                              Added 'long' type to declaration of opnfil().
**
**                              Added 'long' type to declaration of makopn().
**
**       9 Apr 85       SCC     Added 'int' type to declaration of log2().
**
**                              Added 'int' type to declaration of divmod().
**
**      10 Apr 85       SCC     Removed 'long' return from flush(), reversing
**                              modification of 8 Apr 85, since errors from
**                              the BIOS are handled by longjmp()ing back to
**                              top of dispatcher.
**
**                              Removed 'long' return from usrio(), reversing
**                              modification of 8 Apr 85, since errors from
**                              the BIOS are handled by longjmp()ing back to
**                              top of dispatcher.
**
**                              Changed definition of rwerr from 'int' to 'long'
**
**                              Modified ixlseek() to return ERANGE and EINTRN.
**
**                              Modified xwrite() to check validity of handle.
**
**                              Modified ixclose() to check error returns from
**                              ixlseek().
**
**                              Modified ixclose() to return EINTRN where JSL
**                              had marked 'some kind of internal error', and
**                              to return E_OK at end.
**
**                              Modified getcl() to return EINTRN.
**
**                              Modified nextcl() to return EINTRN and E_OK.
**
**                              Added rc to scan() to check error returns from
**                              ixlseek().
**
**                              Modified xsnext() to return E_OK and ENMFIL.
**
**                              Modified xsfirst() to return E_OK and ENMFIL.
**
**      11 Apr 85       SCC     Modified opnfil() to return ENHNDL.
**
**                              Modified ixcreat() to return EPTHNF, EACCDN.
**
**                              Modified xrename() to return errors.
**
**                              Added xfreset() and ixfreset().
**
**      12 Apr 85       SCC     Removed spurious ',0' from parameters passed
**                              to xclose() by xfreset().
**
**      14 Apr 85       SCC     Backed out modification of 11 Apr 85 that added
**                              xfreset() and ixfreset().  They were not the
**                              solution to the problem they were aimed at
**                              fixing.  See corresponding note in the CLI
**                              about the ^C problem.
**
**                              Modified ixdel() to not delete an open file.
**
**                              Backed out modifications to getcl() (it is now
**                              'int' and returns -1).
**
**                              Backed out modifications to nextcl() (it now
**                              returns -1).
**
**                              Backed out modifications to clfix() (it us
**                              untyped).
**
**      16 Apr 85       SCC     Modified ixdel() to close the file if it is
**                              open, and then go ahead and delete it.  This
**                              fix was in response to the fact that AS68
**                              routinely deletes its files without closing
**                              them.
**
**      29 Apr 85       SCC     Modified xrename() to check for existence of
**                              new file name before attempting to rename old
**                              file.
**
**       1 May 85       SCC     Did slight code optimization on ixsfirst().
**
**       6 May 85       SCC     Modified ixsfirst() to report EFILNF on error
**                              return from findit().
**
**                              Modified ixcreat() to return EPTHNF for null
**                              file name.
**
**       7 May 85       SCC     Modified xchdir() to return EPTHNF on failure of
**                              findit().
**
**       8 May 85       SCC     Modified xchdir() to not change path on failure
**                              of findit().
**
**       9 May 85       SCC     Modified xrename() to return EACCDN if
**                              destination filename already exists.
**
**      13 May 85       SCC     Modified xchdir() to call ucase() before path
**                              string is used.
**
**                              Modified findit() to call ucase() before name
**                              string is used.
**
**                              Modified builds() to truncate pre-'.' portion
**                              of file name to 8 characters.
**
**                              Modified xmkdir() to use ixcreat() instead of
**                              xcreat().
**
**                              Modified xcreat() to prevent external caller
**                              from creating a subdirectory.
**
**      15 May 85       SCC     Modified xgetfree() to be 0=default, 1=A:, etc.
**
**      16 May 85       SCC     Modified builds() to terminate post-'.'
**                              portion of file name upon scanning a '.' as
**                              well as the other characters it was checking.
**
**      26 Jun 85       LTG     Fixed bug in xrename to return err from open.
**
**      27 Jun 85       LTG     Added "o_mod" parm to OFD data structure.
**
**                              Modified xopen() & ixopen() to return EACCDN
**                              if try to open file with read only mode in
**                              read/write or write mode.
**
**                              Mod to xread() to return EACCDN err if try to
**                              read file opened as write only.
**
**                              Modifiedixwrite()toreturnEACCDNerriftry
**                              to write file opend as read only.
**
**                              Modified getdmd() to return NULLPTR if MGET
**                              failed.
**
**                              Modified log() to return a long indicating
**                              ENSMEM if getdmd() failed.
**
**                              Modified makofd() to return NULLPTR if MGET
**                              fails.
**
**                              Modified makdnd() to return NULLPTR if MGET
**                              fails.
**
**                              Modified makopn() to return ENSMEM if MGET
**                              fails.
**
**                              Modified scan() to return NULLPTR if makofd()
**                              or makdnd() fails.
**
**                              Modified xcreate() to return (ENSMEM) if
**                              makofd() fails.
**
**                              Modified xmkdir() to return ENSMEM if makofd()
**                              or makdnd() fails.
**
**                              Modified rmdir() to return ENSMEM if makofd()
**                              fails.
**
**      19 Jul 85       LTG     Modified scan() to make sure a file has not be
**                              deleted before creating a DND for it.  This
**                              fixes the bug that prevented some directories
**                              from being removed.
**
**      19 Jul 85       SCC     Modified scan() to prevent creation of a new
**                              DND for a subdirectory that already has one.
**
**                              Added routine uc() to upper-case a single
**                              character and removed up_string().
**
**                              Modified dcrack() to use uc().
**
**                              Modified xcmps() to use uc().
**
**                              Modified xchdir() to use uc().
**
**                              Modified findit() to not use up_string().
**
**                              Modified builds() to use uc().
**
**                              Modified match() to use uc().
**
**      22 Jul 85       LTG     Modified ixcreate() to pass mode parameter to
**                              opnfl(), 0 for RO, 2 for RW.
**
**                      SCC     Modified scan().  Modification of 19 Jul 85
**                              was not correct.
**
**                              Modified ixcreat().  Modification of 22 Jul 85
**                              did not check for R/O status correctly.
**
**      23 Jul 85       SCC     Modified scan().  Still in pursuit of
**                              corrections to mods made on 22 Jul 85.
**
**      23 Jul 85       LTG     Modified builds() to chk for SLASH when namd is
**                              8 chs long.  This fixes the bug with 8 char
**                              directory nms.
**
**      24 Jul 85       SCC     Modified scan().  (Snide comment about still not
**                              having fixed the DND problem last referred to on
**                              23 Jul 85.)
**
**      25 Jul 85       SCC     Modified xunlink().  It now reports correctly
**                              EACCDN if the file being removed is read-only.
**
**      26 Jul 85       LTG     Modified xrename(). Fixed call to getofd() to
**                              pass an int instead of a long.
**
**                      SCC     Modified ixread().  Caller could pass in long
**                              negative length, which was causing problems.
**
**      29 Jul 85       SCC     Modified ixcreat() to disallow creation of an
**                              entry beginning with '.', specifically to fix
**                              'MD .'.
**
**                              Modified xrmdir() to disallow 'RD .' or
**                              'RD ..'.
**
**      31 Jul 85       LTG     Modified ixlseek(). Now chks to see if at front of
**                              file before bumping cluster num when on cluster
**                              boundary.
**
**       6 Aug 85       LTG     Modified xchdir().  No longer removes drive
**                              specification from path name before sending it
**                              to findit().
**
**       7 Aug 85       LTG     Modified getdmd() to deallocate memory of just
**                              allocated data structures if it runs out of
**                              mem before it's done.
**
**  mod       who date          what
**  --------- --- ---------     ----
**  M00.14.01   ktb 02 Aug 85   Fix to xrw enabling proper deblocking of
**                              requests where cluster sizes are not 2.
**
**  M00.16.02   scc 11 Aug 85   Modified xmkdir() to deallocate DND it creates
**                              if it cannot allocate the OFD immediately
**                              afterwards.
**                              Modified xchmod().  An 'if (!wrt)' was followed
**                              by an 'if (wrt)'.  Changed the latter to 'else'.
**
**  M00.01.01   ktb 19 Aug 85   Modified code in ixlseek which determines if
**                              we are on cluster boundary or not
**  M00.01.01a  ktb 20 Aug 85   Try Try again
**  M00.01.01b  ktb 20 Aug 85   These mods actually revert back to the original
**                              logic in ixlseek which special cases cluster
**                              boundaries.  We aren't sure why it works, but
**                              it does.
**
**  M01.01.01   ktb 15 Oct 85   accommodate split of fs.h into fs.h and bios.h
**
** --------------------------------------------------------------------------
**      fs.c has now been broken up into separate modules.
**
**      Modification notices from this point onward are recorded in the file
**      containing the module modified.
**              - kral
** --------------------------------------------------------------------------
**
**  27 May 86 ktb M01.01.0527.04        moved xcmps to fsdir.c
**
** NOTES
**      SCC      4 Apr 85       Note about bcb management in getrec().
**
** NAMES
**
**      JSL     Jason S. Loveman
**      SCC     Steven C. Cavender
**      LTG     Louis T. Garavaglia
**      KTB     Karl T. Braun (kral)
**
** ---------------------------------------------------------------------
**
** Notes:
**      1. Cluster size must be < 32K bytes (strictly less)
**      2. Cluster size, record size must all be powers of two
**      3. With current 12-bit FAT implemented,
**              maximum media size is approx. 4000 clusters
**      4. 16-bit FAT entries not implemented yet       <<<< NO LONGER TRUE <<<<
**      5. swap routines must be defined accordingly for media
**              compatibility with 8086 (high-low) processors
**
*/


#ifdef __x86_64__
/*
 * Shadows the most recently xsetdta()'d real pointer, for
 * x86_64_widen_dta() (fs.h) below to hand back verbatim to any reader of
 * run->p_xdta -- see xsetdta()'s own comment on why PTR_TO_USERPTR_UNCHECKED()'s
 * round-trip through that 32-bit field alone cannot reconstruct a
 * genuine higher-half kernel address. Kept per-process (not a single
 * global) once kernel-code-process nesting is used: proc_go() switches
 * `run` to the child and xterm() restores the parent, while child
 * AES/desktop code can call dos_sdta() and overwrite a single global
 * shadow, corrupting the parent's widened DTA once it resumes.
 *
 * This table -- not the PD itself -- is where that per-process shadow
 * lives, and that is load-bearing, not just tidiness: a PD is a real
 * process's own basepage, mapped read/write into that process's own
 * address space (bdos/arch/x86_64/rwa.c's gouser()). An earlier version
 * of this fix stored the shadow across two of PD's own reserved
 * p_3fill words -- Copilot's review of #376 caught that this lets any
 * ordinary ring-3 process forge an arbitrary 64-bit "real" pointer by
 * writing p_3fill and a matching p_xdta directly (no trap needed, it's
 * just memory the process already owns), which x86_64_widen_dta() would
 * then hand straight to Fsfirst()/Fsnext() to write through -- an
 * arbitrary-kernel-write primitive. A small fixed-size table in the
 * kernel's own BSS, indexed by the owning PD's own pointer identity,
 * cannot be reached or forged from ring 3 at all.
 *
 * Sized for this arch's actual current concurrency (a handful of nested
 * kernel-code-process launches, or at most one real process at a time --
 * #334's own still-open scope for more), not a hard limit: a full table
 * just evicts the oldest entry (slot 0), which only degrades a future
 * x86_64_widen_dta() call for whichever process owned it back to the
 * always-correct-for-ILP32-processes zero-extending fallback, never a
 * memory-safety issue.
 */
#define X86_64_DTA_SHADOW_SLOTS 8
static struct {
    PD *owner;
    DTAINFO *real;
} x86_64_dta_shadow[X86_64_DTA_SHADOW_SLOTS];

static DTAINFO *x86_64_dta_shadow_get(PD *p)
{
    int i;
    for (i = 0; i < X86_64_DTA_SHADOW_SLOTS; i++)
        if (x86_64_dta_shadow[i].owner == p)
            return x86_64_dta_shadow[i].real;
    return NULL;
}

static void x86_64_dta_shadow_set(PD *p, DTAINFO *real)
{
    int i, free_slot = -1;

    for (i = 0; i < X86_64_DTA_SHADOW_SLOTS; i++) {
        if (x86_64_dta_shadow[i].owner == p) {
            x86_64_dta_shadow[i].real = real;
            return;
        }
        if (free_slot < 0 && x86_64_dta_shadow[i].owner == NULL)
            free_slot = i;
    }
    if (free_slot < 0)
        free_slot = 0; /* table full: evict the oldest, see comment above */
    x86_64_dta_shadow[free_slot].owner = p;
    x86_64_dta_shadow[free_slot].real = real;
}

/*
 * Widens a raw run->p_xdta value back into a real, dereferenceable
 * pointer: if it matches the most recently xsetdta()'d address's own
 * low 32 bits, return that real (possibly higher-half) pointer verbatim
 * instead of zero-extending -- otherwise (an ordinary process's own
 * already-low DTA address, or no kernel-code caller has ever used
 * xsetdta() this boot) an ordinary zero-extending cast is correct, the
 * same as ILP32 arches always do. Declared in fs.h for fs/fatfs_pfs.c's/
 * fs/pfs.c's own direct run->p_xdta dereferences, which don't go through
 * xgetdta() below at all.
 */
DTAINFO *x86_64_widen_dta(ULONG stored)
{
    DTAINFO *real = x86_64_dta_shadow_get(run);
    if (real && PTR_TO_USERPTR_UNCHECKED(real) == stored)
        return real;
    return (DTAINFO *)(uintptr_t)stored;
}
#endif

/*
 *  xgetdta - Function 0x2F     f_getdta
 */
DTAINFO *xgetdta(void)          /* return address of dta */
{
#ifdef __x86_64__
    return x86_64_widen_dta(run->p_xdta);
#else
    return((DTAINFO *)run->p_xdta);
#endif
}


/*
 *  xsetdta - Function 0x1A     f_setdta
 */
void xsetdta(DTAINFO *addr)     /* set transfer address to addr */
{
    /*
     * PTR_TO_USERPTR_UNCHECKED(), not PTR_TO_USERPTR(): unlike
     * p_lowtpa/p_hitpa/p_env/p_parent (bdos/proc.c's init_pd_fields()),
     * which are always backed by the low TPA pool, this one can also be
     * set by kernel-internal callers using their own stack-local DTA
     * with an ordinary C call (not the trap dispatch) -- bios.c's
     * autoexec() is exactly this: `DTA dta; Fsetdta(&dta);` before
     * scanning the AUTO folder, an address that is genuinely
     * higher-half on x86-64. Copilot's review of #356/#364 caught that
     * the checked macro traps here on any boot that reaches a real
     * block device (#363), for a case that isn't a corruption bug the
     * way p_env's own history (#360) was: nothing widens this back and
     * dereferences it outside the same kernel call's own local scope
     * when the caller is kernel code, and a real user process's own
     * Fsetdta() call already only ever passes its own, already-32-bit
     * address.
     *
     * That reasoning covers the *storage* here being lossy, but not
     * every *reader* of it: aes/geminit.c's count_accs() (the first
     * kernel-code caller to actually exercise this at all, #375) showed
     * that xgetdta()'s own zero-extending cast (and fs/fatfs_pfs.c's/
     * fs/pfs.c's identical direct run->p_xdta casts, which don't even
     * go through xgetdta()) reconstruct the wrong address for a real
     * higher-half pointer, corrupting memory the moment Fsfirst()
     * writes through it. x86_64_widen_dta() above fixes that for every
     * reader, not just xgetdta()'s own.
     *
     * Kept per-process (in a kernel-only side table, not the PD itself --
     * see that table's own comment on why) once kernel-code-process
     * nesting is used: proc_go() switches `run` to the child and xterm()
     * restores the parent, while child AES/desktop code can call
     * dos_sdta() and overwrite a single global shadow, corrupting the
     * parent's widened DTA once it resumes.
     */
#ifdef __x86_64__
    x86_64_dta_shadow_set(run, addr);
#endif
    run->p_xdta = PTR_TO_USERPTR_UNCHECKED((DTA *)addr);
}


/*
 *  xsetdrv - set default drive
 *      ( 0 = A, etc )
 *      Function 0x0E   d_setdrv
 *
 * Analogous to TOS 4.04 (which restricts drive selection to A:->P:), we
 * only allow selection within our set of valid drives.  Historically we
 * only allowed drives which existed in the drive map, but this caused
 * problems with BetaDOS, which expects to be able to select drive P:
 * even if it does not exist.
 */
long xsetdrv(int drv)
{
    if ((drv >= 0) && (drv < BLKDEVNUM))
        run->p_curdrv = drv;

    return Drvmap();
}


/*
 * xgetdrv - get default drive
 *
 * (0 = A, etc )
 *
 *      Function 0x19   d_getdrv
 *
 *      Last modified   SCC     1 May 85
 */
long xgetdrv(void)
{
    return run->p_curdrv;
}


/*
 * syshnd - converts handle to another value:
 *
 * if the handle is not a standard handle, returns index into sft[]; else,
 * if the standard handle is mapped to a user handle, returns index into sft[];
 * else returns the bios handle that the standard handle is mapped to.
 *
 * note: does not validate the input argument!
 *
 * Arguments:
 *
 *  h  - handle
 */
static int syshnd(int h)
{
    if (h >= NUMSTD)
        return(h-NUMSTD);

    if ((h = run->p_uft[h]) > 0 )
        return(h-NUMSTD);

    return(h);
}


/*
 *  getofd - returns ptr to OFD corresponding to input handle
 *
 *  returns NULL if invalid input handle
 */
OFD *getofd(int h)
{
    WORD n;

    if ((h < 0) || (h >= NUMHANDLES))
        return NULL;

    n = syshnd(h);
    if (n < 0)
        return NULL;

    return sft[n].f_ofd;
}


#if CONF_WITH_PLUGGABLE_FS
/*
 *  getpfsslot - returns ptr to the sft[] entry for a handle owned by a
 *  pluggable filesystem driver (see fs/pfs.c), or NULL if 'h' is not a
 *  valid handle or does not belong to one.
 *
 *  Mirrors getofd() (above), including std-handle indirection via the
 *  same syshnd(), so every caller of xread()/xwrite()/xclose() - not
 *  just osif() - reaches a pluggable handle correctly.
 */
FTAB *getpfsslot(int h)
{
    WORD n;

    if ((h < 0) || (h >= NUMHANDLES))
        return NULL;

    n = syshnd(h);
    if (n < 0)
        return NULL;

    if (!sft[n].f_pfs.fs)
        return NULL;

    return &sft[n];
}
#endif
