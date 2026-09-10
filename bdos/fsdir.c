/*
 * fsdir.c - directory routines for the file system
 *
 * Copyright (C) 2001 Lineo, Inc.
 *               2002-2017 The EmuTOS development team
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
**  06 May 1986 ktb M01.01.SCC.FS.03    logical drive select fix
**  06 May 1986 ktb M01.01.SCC.FS.04    fix to xmkdir for time/date stamp swp
**  06 May 1986 ktb M01.01.SCC.FS.06    fix to xmkdir for time/date stamp swp
**  06 May 1986 ktb M01.01.SCC.FS.07    replaced some routines per change log.
**  06 May 1986 ktb M01.01.SCC.FS.08    fix to match()
**  06 May 1986 ktb M01.01.SCC.FS.09    fix to xrmdir re: rmovg . & ..
**  11 May 1986 ktb M01.01.KTB.SCC.01   fix to SCC DND alloc scheme [1]
**  11 May 1986 ktb M01.01.0512.01      changed the complex if statement in
**                                      scan to something a little more readable
**
**  12 May 1986 ktb M01.01.KTB.SCC.02   makdnd: dir is in use if files are
**                                      open in it.
**
**  27 May 1986 ktb M01.01.0527.01      adding definitions of a structure for
**                                      the info kept in the dta between
**                                      search-first and search-next calls.
**
**  27 May 1986 ktb M01.01.0527.02      moved makbuf from fsdir to here.
**
**  27 May 1986 ktb M01.01.0527.03      changed match's return type
**
**  27 May 1986 ktb M01.01.0527.04      moved xcmps here from fsmain.c
**
**  27 May 1986 ktb M01.01.0527.06      new subroutine for searching for DND's
**
**  27 May 1986 ktb M01.01.0529.01      findit(), scan(): removed all ref's
**                                      to O_COMPLETE flag, as we follow
**                                      different algorithms now.
**
**  08 Jul 1986 ktb M01.01a.0708.01     removed all references to d_scan
**                                      field in DND.
**
**  08 Jul 1986 ktb M01.01a.0708.02     moved def of dirscan() here from fs.h
**
**  08 Jul 1986 ktb M01.01a.0708.01     removed all references to d_scan
**
**  14 Jul 1986 ktb M01.01a.0714.01     clean up some code
**
**  21 Jul 1986 ktb M01.01.0721.02      paranoia code
**
**  31 Jul 1986 ktb M01.01.0731.01      bug in xgsdtof, writes to the file,
**                                      but only needed to update OFD.
**
**  18 Sep 1986 scc M01.01.0918.01      Completion of M01.01.0731.01:  The OFD
**                                      needed to be marked as O_DIRTY so that
**                                      the directory entry would be rewritten.
**                                      Also, the user buffer was left byte
**                                      swapped after a 'set' operation.
**
**  24 Oct 1986 scc M01.01.1024.02      Addition of buffer length check to xgetdir()
**                                      and dopath().
**
**  31 Oct 1986 scc M01.01.1031.01      Changed reference to ValidDrv() in xgetdir()
**                                      to call bios 'drive map' directly.
**
**                                      Added freednd() routine to completely remove
**                                      partially installed DNDs.  It is used in
**                                      xmkdir().
**
**   3 Nov 1986 scc M01.01.1103.01      Added code to delete written directory entry
**                                      for partially installed new directory in
**                                      xmkdir() when it cannot be fully created.
**                                      Also, zero out parent DND's d_left if we've
**                                      gotten that far.  Also made a number of changes
**                                      from NULL to NULLPTR where we really wanted a
**                                      long zero.
**
**   7 Nov 1986 scc M01.01.1107.01      Added code to xmkdir() to check for and disallow
**                                      the creation of a directory which would make
**                                      the path length longer than 63 characters.  Also
**                                      added the routine namlen() which returns the
**                                      length of 1 subdirectory name.
**
**   9 Dec 1986 scc M01.01.1209.01      Modified xsfirst() and xsnext() to flag and to
**                                      check for an initialized DTA, so that doing
**                                      a Search_Next after an unsuccessful Search_First
**                                      will fail correctly.
**
**  12 Dec 1986 scc M01.01.1212.01      Modified dcrack() to return a negative error
**                                      code from when it calls ckdrv().  Modified
**                                      findit() to return a negative error code from
**                                      when it calls dcrack().  Modified xrmdir(),
**                                      xchmod(), xrename(), xchdir(), and ixsfirst()
**                                      to check for negative error code from calls to
**                                      findit().
**
**  14 Dec 1986 scc M01.01.1214.01      Further modification to M01.01.1212.01 so that
**                                      both the negative error code and a 0 (for BDOS
**                                      level error) are checked for.
**
**                  M01.01.1214.02      Added declaration of ckdrv() as long.
**
** [1]  the scheme had a small hole, where not all searches for entries
**      started at the start of the dir (d_scan !always= 0 on entry to
**      scan)..
*/

/* #define ENABLE_KDEBUG */

#include "config.h"
#include "portab.h"
#include "endian.h"
#include "fs.h"
#include "time.h"
#include "mem.h"
#include "gemerror.h"
#include "biosbind.h"
#include "string.h"
#include "kprint.h"
#include "fatfs.h"
#include "bdosstub.h"
#if CONF_WITH_PLUGGABLE_FS
#include "pfs.h"
#endif

/*
 * forward prototypes
 */
char *packit(char *s, char *d);
char *dopath(DND *p, char *buf, int *len);     /* exposed via fs_internal.h for fs/fatfs_pfs.c */
DND *makdnd(DND *p, FCB *b);
static DND *dcrack(const char **np);
static int getpath(const char *p, char *d, int dirspec);
static BOOL match(char *s1, char *s2);
void makbuf(FCB *f, DTAINFO *dt);      /* exposed via fs_internal.h for fs/fatfs_pfs.c */
DND *getdnd(char *n, DND *d);
static void snipdnd(DND *dnd);
void freednd(DND *dn);
FCB *ixsnext(DTAINFO *dt);     /* exposed via fs_internal.h for fs/fatfs_pfs.c */

/*
 *  local macros
 */
#define dirscan(a,c) ((DND *) scan(a,c,FA_SUBDIR,(LONG*)&negone))


/*
 *  counter used by free_available_dnds()
 */
static LONG freed_dnds, freed_ofds; /* count of DNDs & OFDs made available */


/*
 *  xmkdir - make a directory, given path 's'
 *
 *  Function 0x39   d_create
 */
long xmkdir(char *s)
{
#if CONF_WITH_PLUGGABLE_FS
    return pfs_do_mkdir(s);
#else
    return fat_mkdir_path(s);
#endif
}


/*
 *  xrmdir - remove (delete) a directory
 *
 *  Function 0x3A   d_delete
 *
 *  Error returns:
 *                  EPTHNF
 *                  EACCDN
 *                  EINTRN
 */
long xrmdir(char *p)
{
#if CONF_WITH_PLUGGABLE_FS
    return pfs_do_rmdir(p);
#else
    return fat_rmdir_path(p);
#endif
}

/*
 *  xchmod - change/get attrib of path p
 *           if wrt = 1, set; else get
 *
 *  Function 0x43   f_attrib
 *
 *  Error returns:
 *                  EPTHNF
 *                  EFILNF
 */
long xchmod(char *p, int wrt, UBYTE mod)
{
#if CONF_WITH_PLUGGABLE_FS
    return pfs_do_chmod(p, wrt, mod);
#else
    return fat_chmod_path(p, wrt, mod);
#endif
}


/*
 *  ixsfirst - search for first dir entry that matches pattern
 *      search first for matching name, into specified address.  if
 *      address = 0L, caller wants search only, no buffer info
 *
 *  Arguments:
 *      *name - name of file to match
 *      att   - attribute of file
 *      *addr - ptr to dta info
 *
 *  returns:
 *      error code.
 */
long ixsfirst(char *name, WORD att, DTAINFO *addr)
{
    const char *s;              /*  M01.01.03                   */
    DND *dn;
    FCB *f;
    long pos;

    if (att != FA_VOL)
        att |= (FA_ARCHIVE|FA_RO);

    if ((long)(dn = findit(name,&s,0)) < 0) /* M01.01.1212.01 */
        return (long)dn;
    if (!dn)
        return EPTHNF;

    /* now scan for filename from start of directory */

    pos = 0;

    if ((f = scan(dn,s,att,&pos)) == (FCB*)NULL)
        return EFILNF;

    if (addr)
    {
        OFD *ofd = dn->d_ofd;
        memcpy(addr->dt_name, s, 12);
        if (!ofd->o_dnode)              /* i.e. root directory */
        {
            addr->dt_offset_drive = pos;
            addr->dt_cloffset = 0;
            addr->dt_clnum = 0;
        }
        else
        {
            addr->dt_offset_drive = 0L;
            addr->dt_cloffset = ofd->o_curbyt;
            addr->dt_clnum = ofd->o_curcl;
        }
        addr->dt_offset_drive |= dn->d_drv->m_drvnum & DTA_DRIVEMASK;
        addr->dt_attr = att;

        KDEBUG(("ixsfirst(%s,0x%02x,%p): DTA pvt=%08lx/%04x/%04x\n",
                name,att,addr,addr->dt_offset_drive,addr->dt_cloffset,addr->dt_clnum));

        makbuf(f, addr);
    }

    return E_OK;
}


/*
 *  xsfirst - search first for matching name, into dta
 *
 *  Function 0x4E   f_sfirst
 *
 *  Error returns:  EFILNF
 */
long xsfirst(char *name, int att)
{
#if CONF_WITH_PLUGGABLE_FS
    return pfs_do_sfirst(name, att);
#else
    return fat_sfirst_path(name, att);
#endif
}


/*
 * ixsnest
 */
FCB *ixsnext(DTAINFO *dt)
{
    char name[12];
    char *buf, *bufend;
    DMD *dmd;
    BCB *bcb;
    FCB *fcb;
    LONG offset, recnum;
    WORD drive, buftype, rootdirlen, found;
    CLNO cluster = 0;

    drive = dt->dt_offset_drive & DTA_DRIVEMASK;
    if (drive >= BLKDEVNUM)
        return NULL;

    builds(dt->dt_name,name);   /* build FCB-style name */
    name[11] = dt->dt_attr;

    dmd = drvtbl[drive];

    /*
     * determine starting point
     */
    if ((dt->dt_cloffset == 0) && (dt->dt_clnum == 0))
    {
        buftype = BT_ROOT;
        offset = dt->dt_offset_drive & ~DTA_DRIVEMASK;
        recnum = offset >> dmd->m_rblog;
        offset &= dmd->m_rbm;           /* within record */
        rootdirlen = dmd->m_recoff[BT_DATA] - dmd->m_recoff[BT_ROOT];
    }
    else
    {
        buftype = BT_DATA;
        offset = dt->dt_cloffset;       /* within cluster */
        cluster = dt->dt_clnum;
        recnum = cl2rec(cluster,dmd) + (offset >> dmd->m_rblog);
        offset &= dmd->m_rbm;           /* within record */
    }

    KDEBUG(("ixsnext(%p): drv=%d,buftype=%d,recnum=%ld,offset=%ld\n",
            dt,drive,buftype,recnum,offset));

    /*
     * search directory
     */
    found = 0;
    while(1)
    {
        if (buftype == BT_ROOT)
        {
            if (recnum >= rootdirlen)       /* end of root */
                break;
        }
        else
        {
            if (((recnum&dmd->m_clrm) == 0) && (offset == 0))   /* end of cluster */
            {
                cluster = getrealcl(cluster,dmd);
                if (endofchain(cluster))    /* end of directory */
                    break;
                recnum = cl2rec(cluster,dmd);
            }
        }

        bcb = getbcb(dmd,buftype,recnum);
        buf = bcb->b_bufr;
        bufend = buf + dmd->m_recsiz;
        for (fcb = (FCB *)(buf+offset); fcb < (FCB *)bufend; fcb++) {
            if (fcb->f_name[0] == 0x00)     /* never used, so must be end */
                return NULL;
            if ((found = match(name,fcb->f_name)))
                break;
        }
        if (found)
            break;
        recnum++;
        offset = 0;
    }

    if (!found)
        return NULL;

    /*
     * update the private area
     */
    offset = (char *)fcb - buf + sizeof(FCB);   /* to next FCB within buffer */
    if (buftype == BT_ROOT)
    {
        dt->dt_offset_drive = (recnum << dmd->m_rblog) + offset;
        dt->dt_offset_drive |= dmd->m_drvnum;
    }
    else
    {
        dt->dt_cloffset = ((recnum&dmd->m_clrm) << dmd->m_rblog) + offset;
        dt->dt_clnum = cluster;
    }

    return fcb;
}


/*
 *  xsnext - search next, return into dta
 *
 *  Function 0x4F   f_snext
 *
 *  Error returns:  ENMFIL
 */
long xsnext(void)
{
#if CONF_WITH_PLUGGABLE_FS
    return pfs_do_snext();
#else
    return fat_snext_path();
#endif
}


/*
 *  xgsdtof - get/set date/time of file into or from buffer
 *
 *  Function 0x57   f_datime
 */
long xgsdtof(DOSTIME *buf, int h, int wrt)
{
    OFD *f = getofd(h);
    DFD *dfd;

    if (!f)
        return EIHNDL;

    dfd = f->o_dfd;

    if (wrt)
    {
        dfd->o_td.time = cpu2le16(buf->time);
        dfd->o_td.date = cpu2le16(buf->date);
        dfd->o_flag |= O_DIRTY;           /* M01.01.0918.01 */
    }
    else
    {
        buf->time = le2cpu16(dfd->o_td.time);
        buf->date = le2cpu16(dfd->o_td.date);
    }

    return E_OK;
}


#define NEWCODE
#ifdef  NEWCODE
/*  M01.01.03  */
#define isnotdelim(x)   ((x) && (x!='*') && (x!=SLASH) && (x!='.') && (x!=' '))

/*
 *  builds - build a directory style file spec from a portion of a path name
 *
 *      the string at 's1' is expected to be a path spec in the form of
 *      (xxx/yyy/zzz).  *builds* will take the string and crack it
 *      into the form 'ffffffffeee' where 'ffffffff' is a non-terminated
 *      string of characters, padded on the right, specifying the filename
 *      portion of the file spec.  (The file spec terminates with the first
 *      occurrence of a SLASH or NULL, the filename portion of the file spec
 *      terminates with SLASH, NULL, PERIOD or WILDCARD-CHAR).  'eee' is the
 *      file extension portion of the file spec, and is terminated with
 *      any of the above.  The file extension portion is left justified into
 *      the last three characters of the destination (11 char) buffer, but is
 *      padded on the right.  The padding character depends on whether or not
 *      the filename or file extension was terminated with a separator
 *      (NULL, SLASH, PERIOD) or a WILDCARD-CHAR.
 *
 */

/* s1 source
 * s2 dest
 */
void builds(const char *s1, char *s2)
{
    int i;
    char c;

    /*
     *  copy filename part of pathname to destination buffer until a
     *  delimiter is found
     */

    for (i = 0; (i < LEN_ZNODE) && isnotdelim(*s1); i++)
        *s2++ = toupper(*s1++);

    /*
     *  if we have reached the max number of characters for the filename
     *   part, skip the rest until we reach a delimiter
     */

    if (i == LEN_ZNODE)
        while (*s1 && (*s1 != '.') && (*s1 != SLASH))
            s1++;

    /*
     *  if the current character is a wildcard character, set the padding
     *  char with a "?" (wildcard), otherwise replace it with a space
     */
    c =  (*s1 == '*') ? '?' : ' ';

    if (*s1 == '*')                 /*  skip over wildcard char     */
        s1++;

    if (*s1 == '.')                 /*  skip over extension delim   */
        s1++;

    /*
     *  now that we've parsed out the filename part, pad out the
     *  destination with "?" wildcard chars
     */
    for ( ; i < LEN_ZNODE; i++)
        *s2++ = c;

    /*
     *  copy extension part of file spec up to max number of characters
     *  or until we find a delimiter
     */
    for (i = 0; i < LEN_ZEXT && isnotdelim(*s1); i++)
        *s2++ = toupper(*s1++);

    /*
     *  if the current character is a wildcard character, set the padding
     *  char with a "?" (wildcard), otherwise replace it with a space
     */
    c = (*s1 == '*') ? '?' : ' ';

    /*
     *  pad out the file extension
     */
    for ( ; i < LEN_ZEXT; i++)
        *s2++ = c;
}

#else

/*
 *  builds -
 *
 *  Last modified   LTG     23 Jul 85
 */

/* s1 is source, s2 dest */
void builds(const char *s1, char *s2)
{
    int i;
    char c;

    for (i = 0; (i < 8) && (*s1) && (*s1 != '*') && (*s1 != SLASH) &&
            (*s1 != '.') && (*s1 != ' '); i++)
        *s2++ = toupper(*s1++);

    if (i == 8)
        while (*s1 && (*s1 != '.') && (*s1 != SLASH))
            s1++;

    c = (*s1 == '*') ? '?' : ' ';

    if (*s1 == '*')
        s1++;

    if (*s1 == '.')
        s1++;

    for ( ; i < 8; i++)
        *s2++ = c;

    for (i = 0; (i < 3) && (*s1) && (*s1 != '*') && (*s1 != SLASH) &&
            (*s1 != '.') && (*s1 != ' '); i++)
        *s2++ = toupper(*s1++);

    c = (*s1 == '*') ? '?' : ' ';

    for ( ; i < 3; i++)
        *s2++ = c;
}

#endif


/*
 *  xrename - rename a file,
 *      oldpath p1, new path p2
 *
 *  Function 0x56   f_rename
 *
 *  Error returns:
 *                  EPTHNF
 *                  EACCDN
 *                  ENSAME
 */
/* rename file, n unused, old path p1, new path p2 */
/*ARGSUSED*/
long xrename(int n, char *p1, char *p2)
{
    (void)n;
#if CONF_WITH_PLUGGABLE_FS
    return pfs_do_rename(p1, p2);
#else
    return fat_rename_path(p1, p2);
#endif
}


/*
 *  xchdir - change current dir to path p
 *
 *  Function 0x3B   d_setpath
 *
 *  Error returns:
 *              EPTHNF
 *              ckdrv()
 */
long xchdir(char *p)
{
#if CONF_WITH_PLUGGABLE_FS
    return pfs_do_chdir(p);
#else
    return fat_chdir_path(p);
#endif
}


/*
 *  search dirtbl[]: if entry matches, update usage count;
 *  otherwise, create new entry & update usage count.
 *
 *  returns error if no space for new entry,
 *  otherwise returns index of entry found
 */
int incr_curdir_usage(DND *dnd)
{
    DIRTBL_ENTRY *p;
    int i;

    if (!dnd)               /* precautionary paranoia */
        return EINTRN;

    for (i = 1, p = dirtbl+1; i < NCURDIR; i++, p++)    /* look for matching DND */
        if (p->dnd == dnd)
            break;

    if (i >= NCURDIR)       /* not found, so look for free slot */
        for (i = 1, p = dirtbl+1; i < NCURDIR; i++, p++)
            if (!p->use)
                break;

    if (i >= NCURDIR)       /* no slot available */
        return EINTRN;

    p->use++;               /* update use count */
    p->dnd = dnd;           /* link to DND      */

    return i;
}


/*
 * decrements usage count of dirtbl[], ensuring it never goes negative
 */
void decr_curdir_usage(int n)
{
    DIRTBL_ENTRY *p;

    if ((n <= 0) || (n >= NCURDIR))
    {
        KDEBUG(("Decrement for invalid slot %d has been ignored\n",n));
        return;
    }

    p = &dirtbl[n];

    p->use--;
    if (p->use < 0)
    {
        p->use = 0;
        KDEBUG(("Negative usage count for slot %d has been fixed\n",n));
    }

    if (p->use == 0)        /* clean out empty slots */
        p->dnd = NULL;
}


/*
 *  xgetdir - return path spec of current dir into specified buffer
 *
 *  Function 0x47   d_getpath
 *
 *  Note that we deliberately do not check for mediachange on removable
 *  drives - this is the same behaviour as Atari TOS
 *
 *  Error returns:
 *                  EDRIVE
 */
long xgetdir(char *buf, int drv)
{
#if CONF_WITH_PLUGGABLE_FS
    return pfs_do_getdir(buf, drv);
#else
    return fat_getdir_path(buf, drv);
#endif
}


/*
 *  dirinit -
 */
/* dn: dir descr for dir */
FCB *dirinit(DND *dn)
{
    OFD *fd;            /*  ofd for this dir  */
    int num;
    RECNO i2;
    UBYTE *s1;
    DMD *dm;
    FCB *f1;

    fd = dn->d_ofd;                                 /*  OFD for dir */
    num = (dm = fd->o_dmd)->m_recsiz;               /*  bytes/rec   */

    /*
     *  for each record in the current cluster, besides the first record,
     *  get the record and zero it out
     */
    for (i2 = 1; i2 < dm->m_clsiz; i2++)
    {
        KDEBUG(("dirinit i2 = %li\n",i2));
        s1 = getrec(fd->o_currec+i2,fd,1);
        bzero(s1, num);
    }

    /*
     *  now zero out the first record and return a pointer to it
     */
    f1 = (FCB *) (s1 = getrec(fd->o_currec,fd,1));

    bzero(s1, num);
    return f1;
}


/*
 * packit - pack into user buffer
 * more especially, convert a filename of the form
 *   NAME    EXT
 * into:
 *   NAME.EXT
 */
char *packit(char *s, char *d)
{
    char *s0;
    int i;

    if (*s)
    {
        s0 = s;
        for (i = 0; (i < 8) && (*s) && (*s != ' '); i++)
            *d++ = *s++;

        if (*s0 != '.') /* not a special directory entry */
        {
            s = s0 + 8; /* ext */

            if (*s != ' ')
            {
                *d++ = '.';
                for (i = 0; (i < 3) && (*s) && (*s != ' '); i++)
                    *d++ = *s++;
            }
        }
    }

    *d = '\0';

    return d;
}


/*
 *  dopath -
 *
 *      M01.01.1024.02
 */
char *dopath(DND *p, char *buf, int *len)
{
    char temp[LEN_ZFNAME];
    char *tp;
    long tlen;

    if (p->d_parent)
        buf = dopath(p->d_parent,buf,len);

    tlen = (long)packit(p->d_name,temp) - (long)temp;
    tp = temp;
    while (*len)
    {
        (*len)--;                           /* len must never go < 0 */
        if (tlen--)
            *buf++ = *tp++;
        else
        {
            *buf++ = SLASH;
            break;
        }
    }

    return buf;
}


/*
 *  negone - for use as parameter
 */
static const long negone = { -1L };


/*
 *  findit - find a file/dir entry
 *      M01.01.SCC.FS.07        (routine replaced for this fix)
 */
/*  name: name of file/dir
 * dflag: T: name is for a directory
 */
DND *findit(char *name, const char **sp, int dflag)
{
    DND *p;
    const char *n;
    DND *pp, *newp;
    int i;
    char s[11];

    /* crack directory and drive */

    n = name;
    KDEBUG(("findit(%s)\n",n));

    if ((long)(p = dcrack(&n)) < 0)                     /* M01.01.1214.01 */
        return p;

    /*
     *  Force scan() to read from the beginning of the directory again,
     *  since we have gone to a scheme of keeping fewer DNDs in memory.
     */
    do
    {
        if (!(i = getpath(n,s,dflag)))
            break;

        if (i < 0)
        {       /*  path is '.' or '..'  */

            if (i == -2)                /*  go to parent (..)  */
                p = p->d_parent;

            i = -i;             /*  num chars is 1 or 2  */
            goto scanxt;
        }

        /*
         *  go down a level in the path...
         *     save a pointer to the current DND, which will
         *     become the parent, and get the node on the left,
         *     which is the first child.
         */
        pp = p;                 /*  save ptr to parent dnd      */

        if (!(newp = p->d_left))
        {                               /*  [1] [see below]     */
                                        /*  make sure children  */
            newp = dirscan(p,n);        /*  are logged in       */
        }

        if (!(p = newp))        /*  If no children, exit loop */
            break;

        /*
         *  check all subdirectories at this level.  if we run out
         *     of siblings in the DND list (p->d_right == NULL), then
         *     we should rescan the whole directory and make sure they
         *     are all logged in.
         */
        while(p && (strncasecmp(s,p->d_name,11) != 0))
        {
            newp = p->d_right;          /*  next sibling        */

            if (newp == NULL)           /* if no more siblings  */
            {
                p = 0;
                if (pp)
                    p = dirscan(pp,n);
            }
            else
                p = newp;
        }

    scanxt:
    if (*(n = n + i))
        n++;
    else
        break;
    } while (p && i);

    /* p = 0 ==> not found
     i = 0 ==> found at p (dnd entry)
     n = points at filename */

    *sp = n;

    return p;
}
/*
 * [1]  The first call to dirscan is if there are no children logged in.
 *      However, we need to call dirscan if children are logged in and we still
 *      didn't find the desired node, as the desired child may've been flushed.
 *      This is a terrible thing to have happen to a child.  However, we can't
 *      afford to have all these kids around here, so when new ones come in, we
 *      see which we can flush out (see makdnd()).  This is a hack -- no doubt
 *      about that; the cached DND scheme needs to be redesigned all around.
 *      Anyway, the second call to dirscan backs up to the parent (note that n
 *      has not yet been bumped, so is still pointing to the current subdir's
 *      name -- in effect, starting us at this level all over again.
 *                      -- ktb
 */


/*
 *  scan - scan a directory for an entry with the desired name.
 *      scans a directory indicated by a DND.  attributes figure in matching
 *      as well as the entry's name.  posp is an indicator as to where to start
 *      searching.  A posp of -1 means to use the scan pointer in the dnd, and
 *      return the pointer to the DND, not the FCB.
 */
FCB *scan(DND *dnd, const char *n, WORD att, LONG *posp)
{
    char name[12];
    FCB *fcb;
    OFD *fd;
    DND *dnd1;
    BOOL m;                 /*  T: found a matching FCB             */

    KDEBUG(("scan(%p,'%s',0x%x,%p)\n",dnd,n,att,posp));

    m = 0;                  /*  have_match = false                  */
    builds(n,name);         /*  format name into dir format         */
    name[11] = att;

    dnd1 = 0; /* dummy to avoid warning */

    /*
     *  if there is no open file descr for this directory, make one
     */

    if (!(fd = dnd->d_ofd))
        fd = makofd(dnd);   /* makofd() also updates dnd->d_ofd */

    /*
     *  seek to desired starting position.  If posp == -1, then start at
     *  the beginning.
     */
    ixlseek(fd, (*posp == -1) ? 0L : *posp);

    /*
     *  scan thru the directory file, looking for a match
     */
    while ((fcb = (FCB *) ixread(fd,32L,NULL)) && (fcb->f_name[0]))
    {
        /*
         *  Add New DND.
         *  ( iff after scan ptr && not a .
         *  or .. && subdirectory && not deleted ) M01.01.0512.01
         */
        if ((fcb->f_attrib & FA_SUBDIR)         &&
            (fcb->f_name[0] != '.')             &&
            (fcb->f_name[0] != (char)ERASE_MARKER))
        {       /*  see if we already have it  */
            dnd1 = getdnd(&fcb->f_name[0], dnd);
            if (!dnd1)
                dnd1 = makdnd(dnd,fcb);   /* always succeeds */
        }

        if ((m = match(name, fcb->f_name)))
             break;
    }

    KDEBUG(("\n   scan(pos=%ld DND=%p DNDfoundFile=%p name=%s name=%s, %d)",
            (long)fd->o_bytnum,dnd,dnd1,fcb?fcb->f_name:"(null)",name,m));

    /* restore directory scanning pointer */
    if (*posp != -1L)
        *posp = fd->o_bytnum;

    /*
     *  if there was no match, but we were looking for a deleted entry,
     *  then return a pointer to a deleted fcb.  Otherwise, if there was
     *  no match, return a null pointer
     */
    if (!m)
    {       /*  assumes that (*n != 0xe5) (if posp == -1)  */
        if (fcb && (*n == (char)ERASE_MARKER))
            return fcb;
        return (FCB *)NULL;
    }

    if (*posp == -1)
    {       /*  seek to position of found entry  */
        ixlseek(fd,fd->o_bytnum - 32);
        return (FCB *)dnd1;
    }

    return fcb;
}


/*
 *  makdnd - make a child subdirectory of directory p
 *              M01.01.SCC.FS.07
 */
DND *makdnd(DND *p, FCB *b)
{
    DIRTBL_ENTRY *dt;
    DND *p1;
    DND **prev;
    OFD *fd;
    int i;

    fd = p->d_ofd;

    /*
     *  scavenge a DND at this level if we can find one that has not
     *  d_left
     */
    for (prev = &p->d_left; (p1 = *prev); prev = &p1->d_right)
    {
        if (!p1->d_left)
        {
            /* do not scavenge if it's locked */
            if (p1->d_flag & DND_LOCKED)
                continue;

            /* check dirtbl[] to see if anyone is using this guy */
            for (i = 1, dt = dirtbl+1; i < NCURDIR; i++, dt++)
                if (dt->use && (dt->dnd == p1))
                    break;

            KDEBUG(("\n makdnd check dirtbl (%d)",i));

            if ((i >= NCURDIR) && (p1->d_files == NULL))
            {       /*  M01.01.KTB.SCC.02  */
                /* clean out this DND for reuse */

                p1->d_flag = 0;
                p1->d_scan = 0L;
                p1->d_files = (OFD *) 0;
                if (p1->d_ofd)
                    xmfreblk(p1->d_ofd);
                break;
            }
        }
    }

    /* we didn't find one that qualifies, so allocate a new one */

    if (!p1)
    {
        KDEBUG(("\n makdnd new"));

        p1 = MGET(DND); /* MGET(DND) only returns if it succeeds */

        /* do this init only on a newly allocated DND */
        p1->d_right = p->d_left;
        p->d_left = p1;
        p1->d_parent = p;
    }

    /* complete the initialization */

    p1->d_ofd = (OFD *) 0;
    p1->d_strtcl = le2cpu16(b->f_clust);
    p1->d_drv = p->d_drv;
    p1->d_dirfil = fd;
    p1->d_dirpos = fd->o_bytnum - 32;
    p1->d_td.time = b->f_td.time;   /* note: DND time/date are  */
    p1->d_td.date = b->f_td.date;   /*  actually little-endian! */
    memcpy(p1->d_name, b->f_name, 11);

    KDEBUG(("\n makdnd(%p)",p1));

    return p1;
}


/*
 *  dcrack - parse out start of 1st path element, get DND
 *      if needed, logs in the drive specified (explicitly or implicitly) in
 *      the path spec pointed to by 'np', parses out the first path element
 *      in that path spec, and adjusts 'np' to point to the first char in that
 *      path element.
 *
 *  returns
 *      ptr to DND for 1st element in path, or error
 */
static DND *dcrack(const char **np)
{
    const char *n;
    DND *p;
    int d;
    LONG l;                                             /* M01.01.1212.01 */

    KDEBUG(("\n dcrack(%p -> '%s')",np,*np));

    /*
     **  get drive spec (or default) and make sure drive is logged in
     */

    n = *np;                    /*  get ptr to name             */
    if (n[0] && (n[1] == ':'))  /*  if we start with drive spec */
    {
        d = toupper(n[0]) - 'A';/*    compute drive number      */
        n += 2;                 /*    bump past drive number    */
    }
    else                        /*  otherwise                   */
        d = run->p_curdrv;      /*    assume default            */

    /* M01.01.1212.01 */
    if ((l=ckdrv(d, TRUE)) < 0) /*  check for valid drive & log */
        return (DND *)l;        /*    in.  abort if error       */

    /*
     *  if the pathspec begins with SLASH, then the first element is
     *  the root.  Otherwise, it is the current default directory.  Get
     *  the proper DND for this element
    */

    if (*n == SLASH)
    {   /* [D:]\path */
        p = drvtbl[d]->m_dtl;   /*  get root dir for log drive  */
        n++;                    /*  skip over slash             */
    }
    else
    {
        int curdir = run->p_curdir[d];
        p = dirtbl[curdir].dnd; /*  else use curr dir   */
    }

    /* whew ! */ /*  <= thankyou, Jason, for that wonderful comment */

    *np = n;
    return (DND *)p;
}


/*
 *  getpath - get a path element
 *      The buffer pointed to by 'd' must be at least the size of the file
 *      spec buffer in a directory entry (including file type), and will
 *      be filled with the directory style format of the path element if
 *      no error has occurred.
 *
 *  returns
 *      -1 if '.'
 *      -2 if '..'
 *       0 if p => name of a file (no trailing SLASH or !dirspec)
 *      >0 (nbr of chars in path element (up to SLASH)) && buffer 'd' filled.
 *
 */

/* p: start of path element to crack
 * d: ptr to destination buffer
 * dirspec: true = no file name, just dir path
 */
static int getpath(const char *p, char *d, int dirspec)
{
    int i, i2;
    const char *p1;

    for (i = 0, p1 = p; *p1 && (*p1 != SLASH); p1++, i++)
        ;

    /*
     *  If the string we have just scanned over is a directory name, it
     *  will either be terminated by a SLASH, or 'dirspec' will be set
     *  indicating that we are dealing with a directory path only
     *  (no file name at the end).
     */

    if (*p1 != '\0' || dirspec)
    {       /*  directory name  */
        i2 = 0;
        if (p[0] == '.')            /*  dots in name        */
        {
            i2--;                   /*  -1 for dot          */
            if (p[1] == '.')
                i2--;               /*  -2 for dotdot       */
            return i2;
        }

        if (i)                      /*  if not null path el */
            builds(p,d);            /*  d => dir style fn   */

        return i;                   /*  return nbr chars    */
    }

    return 0;               /*  if string is a file name    */
}


/*
 *  match - utility routine to compare file names
 */
/* char *s1  -   name we are checking */
/* char *s2  -   name in fcb */
static BOOL match(char *s1, char *s2)
{
    int i;

    /*
     **  skip VFAT long file name entries
     */

    if (s2[11] == FA_LFN)
        return FALSE;

    /*
     *  check for deleted entry.  wild cards don't match deleted entries,
     *  only specific requests for deleted entries do.
     */

    if (*s2 == (char)ERASE_MARKER)
    {
        if (*s1 == '?')
            return FALSE;
        else if (*s1 == (char)ERASE_MARKER)
            return TRUE;
    }

    /*
     **  compare names
     */

    for (i = 0; i < 11; i++, s1++, s2++)
        if (*s1 != '?')
            if (toupper(*s1) != toupper(*s2))
                return FALSE;

    /*
     *  check attribute match   M01.01.SCC.FS.08
     *  volume labels and subdirs must be specifically asked for
     */

    if ((*s1 != FA_VOL) && (*s1 != FA_SUBDIR))
        if (!(*s2))
            return TRUE;

    return (*s1 & *s2) ? TRUE : FALSE;
}


/*                              M01.01.0527.02
 *  makbuf - copy info from FCB into DTA info area
 */
void makbuf(FCB *f, DTAINFO *dt)
{                                       /*  M01.01.03   */
    dt->dt_fattr = f->f_attrib;
    dt->dt_td.time = le2cpu16(f->f_td.time);
    dt->dt_td.date = le2cpu16(f->f_td.date);
    dt->dt_fileln = le2cpu32(f->f_fileln);

    packit(f->f_name,dt->dt_fname);
}



/*
 *  getdnd - find a dnd with matching name
 */
DND *getdnd(char *n, DND *d)
{
    DND *dnd;

    for (dnd = d->d_left; dnd; dnd = dnd->d_right)
    {
        if (strncasecmp(n,dnd->d_name,11) == 0)
            return dnd;
    }

    return (DND *)NULL;
}


/*
 *  snipdnd - snip a DND out of a chain
 */
static void snipdnd(DND *dnd)
{
    DND **prev;

    for (prev = &(dnd->d_parent->d_left); *prev != dnd; prev = &((*prev)->d_right))
        ;                           /* find the pointer to this DND */
    *prev = dnd->d_right;           /* make it point to the one after us */
}


/*
 *  freednd - free an allocated and linked-in DND
 *
 */
void freednd(DND *dn)                    /* M01.01.1031.02 */
{
    if (dn->d_ofd)                  /* free associated OFD if it's linked */
        xmfreblk(dn->d_ofd);

    snipdnd(dn);                    /* cut this DND out of the chain */

    while (dn->d_left) {            /* is this step really necessary? */
        freednd(dn->d_left);
    }
    xmfreblk(dn);                   /* finally free this DND */
}


/*
 *  makofd - create an OFD for a directory
 *
 *  also: updates the DND with the pointer to the OFD
 *        returns the pointer to the OFD
 */
OFD *makofd(DND *p)
{
    OFD *f;
    DFD *dfd;

    /*
     * if we run out of memory when allocating the OFD, xmgetblk()
     * will run free_available_dnds() behind our backs.  we mustn't
     * let it free up the DND for which we're allocating an OFD!
     * so we lock the DND first.
     */
    p->d_flag |= DND_LOCKED;    /* can't let this DND be scavenged! */
    f = MGET(OFD);              /* MGET(OFD) only returns if it succeeds */
    p->d_flag &= ~DND_LOCKED;   /* ok, we're safe again */

    p->d_ofd = f;       /* update pointer in DND */

    dfd = &f->o_disk;
    f->o_dfd = dfd;
    f->o_dirfil = p->d_dirfil;
    f->o_dnode = p->d_parent;
    f->o_dirbyt = p->d_dirpos;
    f->o_dmd = p->d_drv;

    dfd->o_usecnt = 1;
    dfd->o_td.date = p->d_td.date;
    dfd->o_td.time = p->d_td.time;
    dfd->o_strtcl = p->d_strtcl;
    dfd->o_fileln = 0x7fffffffL;

    return f;
}


/*
 * function used by free_available_dnds()
 */
static void process_dnd_tree(DND *dndstart)
{
    DND *dnd, *prev;
    DIRTBL_ENTRY *dt;
    WORD i;

    /*
     * follow the sibling chain
     */
    for (dnd = dndstart, prev = NULL; dnd; dnd = dnd->d_right) {
        /*
         * if child exists, first process the tree based on that child
         */
        if (dnd->d_left)
            process_dnd_tree(dnd->d_left);

        /*
         * check again, since above we may have freed up the entire child tree
         */
        if (dnd->d_left) {
            KDEBUG(("DND at %p has children\n",dnd));
            prev = dnd;
            continue;
        }

        /*
         * no children - but are there open files?
         */
        if (dnd->d_files) {     /* open files in this directory, can't free DND */
            KDEBUG(("DND at %p has open files\n",dnd));
            prev = dnd;
            continue;
        }

        /*
         * no open files - but is this anyone's current dir?
         */
        for (i = 1, dt = dirtbl+1; i < NCURDIR; i++, dt++)
            if (dt->use && (dt->dnd == dnd))
                break;
        if (i < NCURDIR) {      /* it's someone's current directory */
            KDEBUG(("DND at %p is a current directory\n",dnd));
            prev = dnd;
            continue;
        }

        /*
         * not current - but are we at the root?
         */
        if (!dnd->d_parent) {
            KDEBUG(("DND at %p is a root directory\n",dnd));
            prev = dnd;
            continue;
        }

        /*
         * not at the root - but are we locked?
         */
        if (dnd->d_flag&DND_LOCKED) {
            KDEBUG(("DND at %p is locked\n",dnd));
            prev = dnd;
            continue;
        }

        /*
         * we've got a freeable DND
         *
         * if there was a previous sibling, link it to the next
         * else point the parent to the next & say there's no previous
         */
        if (prev) {
            prev->d_right = dnd->d_right;
        } else {
            dnd->d_parent->d_left = dnd->d_right;
            prev = NULL;
        }

        /*
         * now we can free up the DND and any associated OFD
         */
        if (dnd->d_ofd) {
            xmfreblk(dnd->d_ofd);
            freed_ofds++;
        }
        xmfreblk(dnd);
        freed_dnds++;
    }
}


/*
 * the following routine is called (by xmgetblk() in osmem.c) when we
 * cannot get memory for a DND or OFD.  it calls process_dnd_tree() to
 * free up DNDs that are not absolutely required (this is the same idea
 * as the "scavenge" procedure in makdnd() above).
 */
WORD free_available_dnds(void)
{
    DMD *dmd;
    WORD i;

    KDEBUG(("free_available_dnds() called\n"));
    freed_dnds = freed_ofds = 0L;

    /*
     * process all DMDs
     */
    for (i = 0; i < BLKDEVNUM; i++) {
        dmd = drvtbl[i];
        if (!dmd)
            continue;
        if (dmd->m_dtl)
            process_dnd_tree(dmd->m_dtl);
    }

    KDEBUG(("freed %ld DNDs, %ld OFDs\n",freed_dnds,freed_ofds));
    return freed_dnds+freed_ofds;
}
