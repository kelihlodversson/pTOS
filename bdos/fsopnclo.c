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
static OFD *makopn(FCB *f, DND *dn, int mod, OFD *p2);
static OFD *pending_unlinks;
static long sftdel(FTAB *sftp);


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
    ofd_reap();
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
long xopen(char *name, WORD mod)
{
    int m = mod & VALID_FOPEN_BITS;
    ofd_reap();
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
static OFD *makopn(FCB *f, DND *dn, int mod, OFD *p2)
{
    OFD *p;
    DFD *dfd;
    DMD *dm;                        /*  M01.01.03   */

    dm = dn->d_drv;

    p = MGET(OFD);                  /* MGET(OFD) only returns if it succeeds */

#if CONF_WITH_FILE_PINS
    p->o_refs = 1;
#endif
    p->o_mod = mod;                 /*  set mode                    */
    p->o_dmd = dm;                  /*  link OFD to media           */
    /* MGET zeroes the initial file cursor. */
    p->o_dnode = dn;                /*  link to directory           */
    p->o_dirfil = dn->d_ofd;        /*  link to dir's ofd           */
    p->o_dirbyt = dn->d_ofd->o_bytnum - 32; /*  offset of fcb in dir*/

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

    return p;
}


/*
**  opnfil - does the real work in opening a file
**
**  Error returns   ENHNDL
**
**  NOTES:
**          make a pointer to the ith entry of sft
*/
/* Locate the live identity of a directory entry, excluding deleted names. */
OFD *ofd_find(DND *dn, long pos)
{
    OFD *fd;
    for (fd = dn->d_files; fd; fd = fd->o_link)
        if (fd->o_dirbyt == pos && !(fd->o_dfd->o_flag & O_UNLINKED))
            return fd;
    return NULL;
}

long opnfil(FCB *f, DND *dn, int mod)
{
    OFD *shared;
    int i;
    int h;

    /* find free sft handle */
    for (i = 0; i < OPNFILES; i++)
        if( !sft[i].f_own )
            break;

    if (i == OPNFILES)
        return ENHNDL;

    /* Reject reference overflow before allocating or publishing a slot. */
    shared = ofd_find(dn, dn->d_ofd->o_bytnum - 32);
    if (shared && shared->o_dfd->o_usecnt == 0x7fff) return ENSMEM;
    sft[i].f_own = run;
    sft[i].f_use = 1;
    h = i + NUMSTD;

    sft[i].f_ofd = makopn(f, dn, mod, shared);
    return h;
}


/* OFD stays in its directory list until both handles and backing-object
 * pins are gone. DFD stays in the base OFD until the last OFD is gone. */
#if CONF_WITH_FILE_PINS
BOOL ofd_hold(OFD *fd)
{
    if (!fd || !fd->o_refs || fd->o_refs == 0xffff ||
        (fd->o_dfd->o_flag & (O_MEDIA_LOST | O_CLOSING))) return FALSE;
    fd->o_refs++;
    return TRUE;
}
#endif

/* Keep the current cluster and successor until both FAT copies are durable.
 * flush() invalidates its buffer before I/O, so a retry must rewrite the
 * pending entry instead of reading a possibly already cleared FAT link. */
static void free_file_chain(DFD *file, DMD *media)
{
    BCB *b;
    while (file->o_strtcl && !endofchain(file->o_strtcl)) {
        if (!(file->o_flag & O_RECLAIM_PENDING)) {
            file->o_reclaim_next = getrealcl(file->o_strtcl, media);
            file->o_flag |= O_RECLAIM_PENDING;
        }
        clfix(file->o_strtcl, FREECLUSTER, media);
        for (b = bufl[BI_FAT]; b; b = b->b_link)
            if (b->b_bufdrv != -1 && b->b_dirty && b->b_dm == media)
                flush(b);
        file->o_strtcl = file->o_reclaim_next;
        file->o_flag &= ~O_RECLAIM_PENDING;
    }
    file->o_strtcl = 0;
}

long ofd_put(OFD *fd)
{
    DFD *dfd = fd->o_dfd;
    OFD **link;

#if CONF_WITH_FILE_PINS
    if (!fd->o_refs) return EINTRN;
    if (fd->o_refs > 1) { fd->o_refs--; return E_OK; }
#endif
    if (dfd->o_usecnt == 1 && (dfd->o_flag & O_UNLINKED) &&
        !(dfd->o_flag & O_MEDIA_LOST)) {
        dfd->o_flag |= O_CLOSING;
        free_file_chain(dfd, fd->o_dmd);
        ixclose(fd, CL_KEEP);            /* flush while reference still live */
    }
    if (fd->o_dnode) {
        for (link = &fd->o_dnode->d_files; *link && *link != fd; link = &(*link)->o_link) ;
        if (*link) *link = fd->o_link;
    }
#if CONF_WITH_FILE_PINS
    fd->o_refs = 0;
#endif
    dfd->o_usecnt--;
    if (dfd != &fd->o_disk) xmfreblk(fd);
    if (!dfd->o_usecnt)
        xmfreblk((OFD *)((char *)dfd - offsetof(OFD, o_disk)));
    return E_OK;
}

/* A handle group owns one OFD reference regardless of Fdup/Fforce aliases.
 * Keep the slot live until ofd_put's disk work completes, including longjmp. */
static long sftdel(FTAB *slot)
{
    long rc;
    int i;
    for (i = 0; i < OPNFILES; i++)
        if (&sft[i] != slot && sft[i].f_ofd == slot->f_ofd) break;
    if (i == OPNFILES) {
        rc = ofd_put(slot->f_ofd);
        if (rc < 0) return rc;
    }
    slot->f_ofd = NULL; slot->f_own = NULL; slot->f_use = 0;
#if CONF_WITH_PLUGGABLE_FS
    slot->f_pfs.fs = NULL;
#endif
    return E_OK;
}

/* Media-change invalidates file handles, but backing pins must remain safe
 * to release after the DMD/DND tree has gone. Never reclaim the new medium. */
static void invalidate_directory_files(DND *dn)
{
    OFD *fd, *next;
    int i;
    BOOL handles;
    if (dn->d_left) invalidate_directory_files(dn->d_left);
    if (dn->d_right) invalidate_directory_files(dn->d_right);
    for (fd = dn->d_files; fd; fd = next) {
        next = fd->o_link;
        fd->o_dfd->o_flag |= O_MEDIA_LOST;
        fd->o_dmd = NULL; fd->o_dnode = NULL; fd->o_dirfil = NULL;
        handles = FALSE;
        for (i = 0; i < OPNFILES; i++)
            if (sft[i].f_ofd == fd) {
                sft[i].f_ofd = NULL; sft[i].f_own = NULL; sft[i].f_use = 0;
                handles = TRUE;
            }
        if (handles) ofd_put(fd);
    }
    dn->d_files = NULL;
}

void ofd_invalidate_drive(DMD *dm)
{
    OFD **link = &pending_unlinks, *fd;
    if (dm->m_dtl) invalidate_directory_files(dm->m_dtl);
    while ((fd = *link)) {
        if (fd->o_dfd->o_flag & O_MEDIA_LOST) {
            *link = fd->o_thread;
            ofd_put(fd);                 /* memory only, never new disk I/O */
        } else link = &fd->o_thread;
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
long xclose(WORD h)
{
    int h0;
    OFD *fd;
    long rc;

    ofd_reap();
    if (h < 0)
        return E_OK;    /* always a good close on a character device */

    if (h >= NUMHANDLES)            /* M01.01.1022.01 */
        return EIHNDL;

    if ((h0 = h) < NUMSTD)
    {
        h = PD_UFT(run)[h];
        PD_UFT(run)[h0] = get_default_handle(h0);    /* revert to default */
        if (h < 0)                  /* M01.01.1023.01 */
            return E_OK;
        if (h < NUMSTD)             /* "can't happen" (bug in Fforce()?) */
            return EIHNDL;
    }
    else if (IS_BIOS_HANDLE(sft[h-NUMSTD].f_ofd))
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

    rc = ixclose(fd,CL_KEEP);

    if (sft[h-NUMSTD].f_use == 1)
        return sftdel(&sft[h-NUMSTD]);
    else
        sft[h-NUMSTD].f_use--;

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
    if ((dfd->o_flag & O_DIRTY) && !(dfd->o_flag & O_UNLINKED))
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
    ofd_reap();
#if CONF_WITH_PLUGGABLE_FS
    return pfs_do_unlink(name);
#else
    return fat_unlink_path(name);
#endif
}


/* A queue-owned OFD pins the directory, medium and shared file identity
 * before any directory write. It survives disk-error longjmps, including
 * deletion of files that had no public handles. */
void ofd_reap(void)
{
    while (pending_unlinks) {
        OFD *fd = pending_unlinks;
        OFD *next = fd->o_thread;
        DFD *file = fd->o_dfd;
        char erased = (char)ERASE_MARKER;
        if (!(file->o_flag & O_MEDIA_LOST) && (fd->o_mod & UNLINK_PENDING)) {
            ixlseek(fd->o_dirfil, fd->o_dirbyt);
            ixwrite(fd->o_dirfil, 1, &erased);
            ixclose(fd->o_dirfil, CL_DIR);
            /* Only a persisted name removal permits chain reclamation. */
            file->o_flag |= O_UNLINKED;
            fd->o_mod &= ~UNLINK_PENDING;
        }
        ofd_put(fd);
        pending_unlinks = next;
    }
}

long ixdel(DND *dn, FCB *f, long pos)
{
    FCB saved = *f;
    OFD *live, *fd;
    ofd_reap();
    live = ofd_find(dn, pos);
    if (live && live->o_dfd->o_usecnt == 0x7fff) return ENSMEM;
    fd = makopn(&saved, dn, RO_MODE, live);
    fd->o_dirbyt = pos;
    fd->o_mod |= UNLINK_PENDING;
    fd->o_thread = pending_unlinks;
    pending_unlinks = fd;
    ofd_reap();
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
