/*
 * fsopnclo.c - open/close/create/delete routines for file system
 *
 * Copyright (C) 2001 Lineo, Inc.
 *               2002-2016 The EmuTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */



/*
** NOTE:
**      mods with "SCC.XX.NN" are mods which try to merge fixes to a special
**      post 1.0 / pre 1.1 version.  The notation refers to a DRI internal
**      document (see SCC), which is a change log.  SCC refers to the
**      originator of the fix.  The XX refers to the module in which the
**      fix was originally made, fs.c (FS), sup.c (SUP), etc.  The NN is
**      the fix number to that module as indicated on the change log.  For
**      the most part, these numbers are meaningless, and serve only to
**      correspond code to particular problems.
**
**  mods
**     date     who mod                 fix/change/note
**  ----------- --  ------------------  -------------------------------
**  06 May 1986 ktb M01.01.SCC.FS.02    ixcreat(): rescan.
**  21 Jul 1986 ktb M01.01.0721.01      ixcreat(): check for bad chars
**  30 Jul 1986 ktb M01.01.0730.01      deleting entries from the sft had
**                                      problems if there were dup'd entries
**                                      pointing to the same OFD
**  15 Sep 1986 scc M01.01.0915.01      ixcreat(): disallow creation of subdir
**                                      if file by same name exists
**  22 Oct 1986 scc M01.01.1022.01      xclose(): range check the handle coming in
**  23 Oct 1986 scc M01.01.1023.01      xclose(): check for closing a standard handle
**                                      that was already closed
**  23 Oct 1986 scc M01.01.1023.03      sftdel() and sftosrch() erroneously used NULL
**                                      rather than NULLPTR.
**
**  12 Dec 1986 scc M01.01.1212.01      modified ixcreat(), ixopen(), and xunlink() to
**                                      check for a negative error return from findit().
**
**  14 Dec 1986 scc M01.01.1214.01      Further modification of M01.01.1212.01 to check
**                                      for 0 return (indicating BDOS level error).
*/

/* #define ENABLE_KDEBUG */

#include "config.h"
#include "portab.h"
#include "endian.h"
#include "fs.h"
#include "fatfs.h"
#include "gemerror.h"
#include "string.h"
#include "mem.h"
#include "time.h"
#include "console.h"
#include "kprint.h"
#include "bdosstub.h"
#if CONF_WITH_PLUGGABLE_FS
#include "pfs.h"
#endif

/* the following characters are disallowed in the name when creating
 * or renaming files or folders.  this is *mostly* the same list as
 * for MS-DOS, except that Atari allows the '+' character.
 */
#define ILLEGAL_FNAME_CHARACTERS " *,:;<=>?[]|"


/*
 * forward prototypes
 */
long opnfil(FCB *f, DND *dn, int mod);
static long makopn(FCB *f, DND *dn, int h, int mod);
static FTAB *sftofdsrch(OFD *ofd);
static void sftdel(FTAB *sftp);


/*
 *  xcreat - create file with specified name, attributes
 *
 *  Function 0x3C   Fcreate
 *
 *  Error returns   EPTHNF, EACCDN, ENHNDL
 */
long xcreat(char *name, UBYTE attr)
{
    int a = attr & ~FA_SUBDIR;
#if CONF_WITH_PLUGGABLE_FS
    return pfs_do_create(name, (UWORD)a);
#else
    return fat_creat_path(name, (char)a);
#endif
}


/*
 *  xopen - open a file (path name)
 *
 *  Function 0x3D   Fopen
 *
 *  Error returns   EFILNF, opnfil()
 *
 *  +ve return      file handle
 */
long xopen(char *name, int mod)
{
    int m = mod & VALID_FOPEN_BITS;
#if CONF_WITH_PLUGGABLE_FS
    return pfs_do_open(name, m);
#else
    return fat_open_path(name, m);
#endif
}


/*
**  makopn - make an open file for sft handle h
**
*/
static long makopn(FCB *f, DND *dn, int h, int mod)
{
    OFD *p;
    OFD *p2;
    DFD *dfd;
    DMD *dm;                        /*  M01.01.03   */

    dm = dn->d_drv;

    p = MGET(OFD);                  /* MGET(OFD) only returns if it succeeds */

    p->o_mod = mod;                 /*  set mode                    */
    p->o_dmd = dm;                  /*  link OFD to media           */
    sft[h-NUMSTD].f_ofd = p;
    /* the following 2 assignments are unnecessary, since MGET zeroes the OFD */
    p->o_curcl = 0;                 /*  init file pointer info      */
    p->o_curbyt = 0;                /*  "                           */
    p->o_dnode = dn;                /*  link to directory           */
    p->o_dirfil = dn->d_ofd;        /*  link to dir's ofd           */
    p->o_dirbyt = dn->d_ofd->o_bytnum - 32; /*  offset of fcb in dir*/

    for (p2 = dn->d_files; p2; p2 = p2->o_link)
        if (p2->o_dirbyt == p->o_dirbyt)
            break;              /* same dir, same dcnt */

    p->o_link = dn->d_files;
    dn->d_files = p;
    /*
     * if this file is already open, we copy the DFD pointer; this
     * ensures that all OFDs for the same file use the same DFD.
     * otherwise, we use the DFD in the current OFD.
     */
    if (p2)
    {
        dfd = p2->o_dfd;
        dfd->o_usecnt++;                /* more than one user of DFD! */
        /* not used yet... TBA *********/
        p2->o_thread = p;
    }
    else
    {
        dfd = &p->o_disk;
        dfd->o_usecnt = 1;              /* only OFD using this DFD */
        dfd->o_td.date = f->f_td.date;  /* note: OFD time/date are  */
        dfd->o_td.time = f->f_td.time;  /*  actually little-endian! */
        dfd->o_strtcl = le2cpu16(f->f_clust);     /* 1st cluster of file */
        dfd->o_fileln = le2cpu32(f->f_fileln);    /* init length of file */
    }

    p->o_dfd = dfd;                     /* for future reference ... */

    return h;
}


/*
**  opnfil - does the real work in opening a file
**
**  Error returns   ENHNDL
**
**  NOTES:
**          make a pointer to the ith entry of sft
*/
long opnfil(FCB *f, DND *dn, int mod)
{
    int i;
    int h;

    /* find free sft handle */
    for (i = 0; i < OPNFILES; i++)
        if( !sft[i].f_own )
            break;

    if (i == OPNFILES)
        return ENHNDL;

    sft[i].f_own = run;
    sft[i].f_use = 1;
    h = i + NUMSTD;

    return makopn(f, dn, h, mod);
}


/*
**  sftofdsrch - search the sft for an entry with the specified OFD
**  returns:
**      ptr to the matching sft, or
**      NULL
*/
static FTAB *sftofdsrch(OFD *ofd)
{
    FTAB *sftp;     /* scan ptr for sft */
    int i;

    for (i = 0, sftp = sft; i < OPNFILES; i++, sftp++)
        if (sftp->f_ofd == ofd)
            return sftp;

    return NULL;
}


/*
**  sftdel - delete an entry from the sft
**      delete the entry from the sft.  If no other entries in the sft
**      have the same ofd, free up the OFD, also.
*/
static void sftdel(FTAB *sftp)
{
    FTAB *s;
    OFD *ofd;
    DFD *d;

    /*  clear out the entry  */

    ofd = (s=sftp)->f_ofd;

    s->f_ofd = 0;
    s->f_own = 0;
    s->f_use = 0;
#if CONF_WITH_PLUGGABLE_FS
    /*
     * this slot is only ever a legacy (non-pluggable) handle by the
     * time sftdel() runs on it - see the CONF_WITH_PLUGGABLE_FS branch
     * near the top of xclose() - but establish the invariant "free slot
     * implies f_pfs.fs == NULL" here regardless, so opnfil()/makopn()
     * (which know nothing about f_pfs) can never hand out a slot with a
     * stale non-NULL f_pfs.fs left over from an earlier pluggable use.
     */
    s->f_pfs.fs = 0;
#endif

    /*
     * if there are no other sft entries with same OFD, delete the OFD
     * (subject to the complication of multiple OFDs pointing to the same file)
     */
    if (sftofdsrch(ofd) == NULL)
    {
        d = ofd->o_dfd;
        if (d->o_usecnt > 0)        /* paranoia */
            d->o_usecnt--;

        if (d != &ofd->o_disk)      /* not the 'base OFD', */
            xmfreblk(ofd);          /*  so OK to delete it */

        if (d->o_usecnt == 0)       /* no more users of this file */
        {
            ofd = (OFD *)((char *)d - offsetof(OFD, o_disk));
            xmfreblk(ofd);          /* delete the 'base OFD' */
        }
    }
}


/*
 *  xclose - Close a file.
 *
 *  Function 0x3E   Fclose
 *
 *  Error returns   EIHNDL, ixclose()
 *
 *  SCC:    I have added 'rc' to allow return of status from ixclose().  I
 *          do not yet know whether it is appropriate to perform the
 *          operations inside the 'if' statement following the invocation
 *          of ixclose(), but I am leaving the flow of control intact.
 */
long xclose(int h)
{
    int h0;
    OFD *fd;
    long rc;

    if (h < 0)
        return E_OK;    /* always a good close on a character device */

    if (h >= NUMHANDLES)            /* M01.01.1022.01 */
        return EIHNDL;

    if ((h0 = h) < NUMSTD)
    {
        h = run->p_uft[h];
        run->p_uft[h0] = get_default_handle(h0);    /* revert to default */
        if (h < 0)                  /* M01.01.1023.01 */
            return E_OK;
        if (h < NUMSTD)             /* "can't happen" (bug in Fforce()?) */
            return EIHNDL;
    }
    else if (((long) sft[h-NUMSTD].f_ofd) < 0L)
    {
        if (!(--sft[h-NUMSTD].f_use))
        {
            sft[h-NUMSTD].f_ofd = 0;
            sft[h-NUMSTD].f_own = 0;
        }

        return E_OK;
    }

#if CONF_WITH_PLUGGABLE_FS
    /*
     * 'h' is a real sft[] handle at this point (either unchanged, or the
     * handle a std-handle redirection was pointing at, above) - if it
     * belongs to a pluggable driver, close it there instead of treating
     * f_ofd as an OFD*.
     */
    if (sft[h-NUMSTD].f_pfs.fs)
    {
        struct pfs_ops *pfs = sft[h-NUMSTD].f_pfs.fs;

        /*
         * close() runs on every xclose() of this slot, same as ixclose()
         * does for the legacy OFD path below; release() only runs once
         * the last sft[] reference to this cookie is gone (mirrors
         * sftdel() being called only when f_use reaches zero), so a
         * duplicated handle (Fforce()-shared slot, or an xdup()-copied
         * one) doesn't have its driver resources torn down while a
         * sibling handle still expects them to be live.
         */
        rc = pfs_handle_close(&sft[h-NUMSTD].f_pfs);

        if (!(--sft[h-NUMSTD].f_use))
        {
            if (pfs->release)
                pfs->release(&sft[h-NUMSTD].f_pfs);
            sft[h-NUMSTD].f_pfs.fs = 0;
            sft[h-NUMSTD].f_own = 0;
        }

        return rc;
    }
#endif

    if (!(fd = getofd(h)))
        return EIHNDL;

    rc = ixclose(fd,0);

    if (!(--sft[h-NUMSTD].f_use))
        sftdel(&sft[h-NUMSTD]);

    return rc;
}


/*
**  ixclose -
**
**  Error returns   EINTRN
**
**  Last modified   SCC     10 Apr 85
**
**  NOTE:   I'm not sure that returning immediately upon an error from
**          ixlseek() is the right thing to do.  Some data structures may
**          not be updated correctly.  Watch out for this!
**          Also, I'm not sure that the EINTRN return is ok.
*/
long ixclose(OFD *fd, int part)
{                                   /*  M01.01.03                   */
    OFD *p, **q;
    int i;                          /*  M01.01.03                   */
    BCB *b;
    DFD *dfd = fd->o_dfd;

    /*
     * if the file or folder has been modified, we need to make sure
     * that the date/time, starting cluster, and file length in the
     * directory entry are updated.  In addition, for files, we must
     * set the archive flag.
     *
     * The following code avoids multiple ixlseek()/ixlread()/ixlwrite()
     * sequences by just getting a pointer to a buffer containing the
     * FCB, and updating it directly.  We must do an ixwrite() at the
     * end so that the buffer is marked as dirty and is subsequently
     * written.
     */
    if (dfd->o_flag & O_DIRTY)
    {
        FCB *fcb;
        UBYTE attr;

        ixlseek(fd->o_dirfil,fd->o_dirbyt); /* start of dir entry */
        fcb = (FCB *)ixread(fd->o_dirfil,32L,NULL);
        attr = fcb->f_attrib;               /* get attributes */
        memcpy(&fcb->f_td,&dfd->o_td,10);   /* copy date/time, start, length */
        fcb->f_clust = le2cpu16(fcb->f_clust);  /*  & fixup byte order */
        fcb->f_fileln = le2cpu32(fcb->f_fileln);

        if (part & CL_DIR)
            fcb->f_fileln = 0L;             /* dir lengths on disk are zero */
        else
            attr |= FA_ARCHIVE;             /* set the archive flag for files */

        ixlseek(fd->o_dirfil,fd->o_dirbyt+11);  /* seek to attrib byte */
        ixwrite(fd->o_dirfil,1,&attr);          /*  & rewrite it       */
        dfd->o_flag &= ~O_DIRTY;            /* not dirty any more */
    }

    if ((!part) || (part & CL_FULL))
    {
        q = &fd->o_dnode->d_files;

        for (p = *q; p ; p = *(q = &p->o_link))
            if (p == fd)
                break;

        /* someone else has this file open **** TBA */

        if (p)
            *q = p->o_link;
        else
            return EINTRN;  /* some kind of internal error */
    }

    /*
     * flush all drives
     *
     * this could in theory be improved by flushing all sectors for one
     * drive before moving on to the next, reducing arm movement on
     * partitioned hard disks.  however this would cost code space and,
     * in practice, flushing usually takes place to one drive only.
     */
    for (i = BI_FAT; i <= BI_DATA; i++)
        for (b = bufl[i]; b; b = b->b_link)
            if ((b->b_bufdrv != -1) && b->b_dirty)
                flush(b);

    return E_OK;
}


/*
 *  xunlink - unlink (delete) a file
 *
 *  Function 0x41   Fdelete
 *
 *  returns     EFILNF, EACCDN, ixdel()
 */
long xunlink(char *name)
{
#if CONF_WITH_PLUGGABLE_FS
    return pfs_do_unlink(name);
#else
    return fat_unlink_path(name);
#endif
}


/*
**  ixdel - internal delete file.
**
**  Traverse the list of files open for this directory node.
**  If a file is found that has the same position in the directory as the one
**  we are to delete, then scan the system file table to see if this process is
**  then owner.  If so, then close it, otherwise abort.
**
**  NOTE:       both 'for' loops scan for the entire length of their
**              respective data structures, and do not drop out of the loop on
**              the first occurrence of a match.
**      Used by
**              ixcreat()
**              xunlink()
**              xrmdir()
**
*/
long ixdel(DND *dn, FCB *f, long pos)
{
    OFD *fd;
    DMD *dm;
    int n2;
    int n;
    char c;

    for (fd = dn->d_files; fd; fd = fd->o_link)
        if (fd->o_dirbyt == pos)
            for (n = 0; n < OPNFILES; n++)
                if (sft[n].f_ofd == fd)
                {
                    if (sft[n].f_own == run)
                        ixclose(fd,0);
                    else
                        return EACCDN;
                }

    /*
     * Traverse this file's chain of allocated clusters, freeing them.
     */
    dm = dn->d_drv;
    n = le2cpu16(f->f_clust);

    while (n && !endofchain(n))
    {
        n2 = getrealcl(n,dm);
        clfix(n,FREECLUSTER,dm);
        n = n2;
    }

    /*
     * Mark the directory entry as erased.
     */
    fd = dn->d_ofd;
    ixlseek(fd,pos);
    c = (char)ERASE_MARKER;
    ixwrite(fd,1L,&c);
    ixclose(fd,CL_DIR);

    /*
     * NOTE that the preceding routines that do physical disk operations
     * will 'longjmp' on failure at the BIOS level, thereby allowing us to
     * simply return with E_OK.
     */
    return E_OK;
}


/*
**  contains_illegal_characters - check for illegal filename chars in specified string
**
**  returns TRUE if found
*/
BOOL contains_illegal_characters(const char *test)
{
    const char *ref = ILLEGAL_FNAME_CHARACTERS;
    const char *t;

    while(*ref)
    {
        for (t = test; *t; t++)
            if (*t == *ref)
                return TRUE;
        ref++;
    }

    return FALSE;
}
