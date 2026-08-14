/*
 * fatfs_pfs.c - built-in FAT filesystem implementation
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 *
 * Holds the DND/name-shaped FAT cores and path-level wrappers used when
 * pluggable filesystem support is off.  When it is on, fat_pfs_ops exposes
 * those cores through the vtable; root, lookup and readdir support remain
 * specific to that vtable path.
 */

#include "config.h"
#include "portab.h"
#include "pfs.h"
#include "fs.h"
#include "fs_internal.h"
#include "fatfs.h"
#include "gemerror.h"
#include "biosbind.h"
#include "string.h"
#include "kprint.h"
#include "mem.h"
#include "endian.h"
#include "time.h"

#define ROOT_PSEUDO_CLUSTER 1

static void unpackit(const char *src, char *dst)
{
    const char *s;
    char *d;
    int i;

    memset(dst,' ',11);

    for (i = 0, s = src, d = dst; (i < 8) && *s && (*s != '.'); i++)
        *d++ = *s++;

    while(*s)
        if (*s++ == '.')
            break;

    for (i = 0, d = dst+8; (i < 3) && *s; i++)
        *d++ = *s++;

    *(dst+11) = '\0';
}

static BOOL is_subdir(const char *s1,DND *dn1, DND *dn2)
{
    char s2[LEN_ZFNAME];

    if (dn2->d_parent != dn1)
        return FALSE;

    packit(dn2->d_name,s2);

    return strncasecmp(s1,s2,LEN_ZFNAME) ? FALSE : TRUE;
}

static WORD update_fcb(OFD *fd,LONG posp,LONG len,BYTE *buf)
{
    if (ixlseek(fd,posp) != posp)
        return -1;
    if (ixwrite(fd,len,buf) != len)
        return -1;

    return 0;
}

/*
 * fat_countfree - fast scan of FAT16 filesystem to count free clusters
 */
static CLNO fat_countfree(DMD *dm)
{
    int recnum, offset;
    CLNO free, clnum;
    char *buf;

    for (clnum = 2, free = 0; clnum < dm->m_numcl+2; )
    {
        /*
         * get the next FAT record
         */
        recnum = (clnum * sizeof(CLNO)) >> dm->m_rblog;
        offset = (clnum * sizeof(CLNO)) & dm->m_rbm;
        buf = getrec(recnum, dm->m_fatofd, 0);

        /*
         * scan the FAT record, counting free slots
         */
        for ( ; (offset < dm->m_recsiz) && (clnum < (dm->m_numcl+2)); offset += sizeof(CLNO), clnum++)
        {
            if (*(CLNO *)(buf+offset) == 0)
                free++;
        }
    }

    return free;
}

static LONG fat_dfree(struct pfs_ops *fs, WORD drive, ULONG out[4])
{
    CLNO i, free;
    long n;
    DMD *dm;

    (void)fs;
    if ((n = ckdrv(drive, TRUE)) < 0)
        return ERR;
    dm = drvtbl[n];
    if (dm->m_16)
        free = fat_countfree(dm);
    else
    {
        free = 0;
        for (i = 0; i < dm->m_numcl; i++)
            if (!getrealcl(i + 2, dm))
                free++;
    }
    out[0] = (ULONG)free;
    out[1] = (ULONG)dm->m_numcl;
    out[2] = (ULONG)dm->m_recsiz;
    out[3] = (ULONG)dm->m_clsiz;
    return E_OK;
}

static LONG fat_open(PFSCOOKIE *dir, const char *name, WORD mode, PFSCOOKIE *out)
{
    FCB *f;
    DND *dn = (DND *)dir->index;
    long pos;
    LONG rc;

    pos = 0;
    if (!(f = scan(dn, name, FA_NORM, &pos)))
        return EFILNF;
    if ((f->f_attrib & FA_RO) && (mode != 0))
        return EACCDN;
    rc = opnfil(f, dn, mode);
    if (rc < 0)
        return rc;
    out->fs = dir->fs;
    out->index = rc;
    out->aux = 0;
    out->pos = 0;
    return E_OK;
}

static LONG fat_create(PFSCOOKIE *dir, const char *name, UWORD attr, PFSCOOKIE *out)
{
    DND *dn = (DND *)dir->index;
    OFD *fd;
    FCB *f;
    const char *s = name;
    char n[2], a[11];                       /*  M01.01.03   */
    int i, f2;                              /*  M01.01.03   */
    long pos, rc;

    n[0] = (char)ERASE_MARKER; n[1] = 0;

    if (!*s || (*s == '.'))         /*  no file name || '.' || '..' */
        return EPTHNF;

    /*  M01.01.0721.01  */
    if (contains_illegal_characters(s))
        return EACCDN;

    /*
     * if the volume label attribute is set, no others are allowed
     */
    if ((attr&FA_VOL) && (attr != FA_VOL))
        return EACCDN;

    /*
     * volume labels may only be created in the root
     */
    if ((attr == FA_VOL) && dn->d_parent)
        return EACCDN;

    if (!(fd = dn->d_ofd))
        fd = makofd(dn);            /* makofd() also updates dn->d_ofd */

    /*
     * if a matching file already exists, we delete it first.  note
     * that the definition of matching, and the action taken, differs
     * depending on whether the file is a volume label or not:
     *  . for a volume label, *any* existing volume label matches
     *    and will be deleted (reference: Rainbow TOS Release Notes)
     *  . for other files, the name must match, and the existing
     *    file will be deleted unless (a) it's read-only or a folder
     *    or (b) the file being created is a folder.
     */
    pos = 0;
    if (attr == FA_VOL)
        f = scan(dn,"*.*",FA_VOL,&pos);
    else f = scan(dn,s,FA_NORM|FA_SUBDIR,&pos);

    if (f)                  /* found matching file / label */
    {
        if (attr != FA_VOL) /* for normal files, need to check more stuff */
        {
                                        /* M01.01.0730.01   */
            if ((f->f_attrib & (FA_SUBDIR | FA_RO)) || (attr == FA_SUBDIR))
                return EACCDN;          /*  subdir or read only  */
        }
        pos -= 32;
        if (ixdel(dn,f,pos) < 0)    /* file currently open by another process? */
            return EACCDN;
    }
    else
        pos = 0;

    /* now scan for empty space */

    /*  M01.01.SCC.FS.02  */
    while( !( f = scan(dn,n,0xff,&pos) ) )
    {
        /*  not in current dir, need to grow  */
        if (!fd->o_dnode)           /*  but can't grow root  */
            return EACCDN;

        if ( nextcl(fd,1) )
            return EACCDN;

        f = dirinit(dn);
        pos = 0;
    }

    builds(s,a);
    pos -= 32;
    f->f_attrib = (UBYTE)attr;
    for (i = 0; i < 10; i++)
        f->f_fill[i] = 0;
    f->f_td.time = le2cpu16(current_time);
    f->f_td.date = le2cpu16(current_date);
    f->f_clust = 0;
    f->f_fileln = 0;
    ixlseek(fd,pos);
    ixwrite(fd,11L,a);              /* write name, set dirty flag */
    ixclose(fd,CL_DIR);             /* partial close to flush */
    ixlseek(fd,pos);
    s = (char*) ixread(fd,32L,NULL);
    f2 = rc = opnfil((FCB*)s,dn,(f->f_attrib&FA_RO)?RO_MODE:RW_MODE);

    if (rc < 0)
        return rc;

    getofd(f2)->o_dfd->o_flag |= O_DIRTY;

    out->fs = dir->fs;
    out->index = f2;
    out->aux = 0;
    out->pos = 0;
    return E_OK;
}

/*
 *  dots - the "." and ".." directory entries written by fat_mkdir()
 */
static const char dots[22] = ".          ";

/*
 *  namlen - parameter points to a character string of 11 bytes max
 */
static int namlen(char *s11)                            /* M01.01.1107.01 */
{
    int i, len;

    for (i = len = 1; i <= 11; i++, s11++)
    {
        if (*s11 && (*s11 != ' '))
        {
            len++;
            if (i == 9)
                len++;
        }
    }

    return len;
}

/*
 *  fat_mkdir - make a directory, given a containing-directory cookie
 *  'dir' and the new directory's final component 'name'.  Core of the
 *  GEMDOS 0x39 (Dcreate) opcode; the in-gate vtable entry and the
 *  off fat_mkdir_path() shim both reach this code.
 *
 *  Body is the verbatim xmkdir() from bdos/fsdir.c (M01.01.1107.01 etc.)
 *  with its original create open replaced by fat_create().
 */
static LONG fat_mkdir(PFSCOOKIE *dir, const char *name)
{
    OFD *f;
    FCB *f2;
    OFD *fd,*f0;
    DFD *dfd;
    FCB *b;
    DND *dn;
    int h,cl,plen;
    long rc;
    PFSCOOKIE newfc;

    rc = fat_create(dir, name, FA_SUBDIR, &newfc);
    if (rc < 0)
        return rc;
    h = (int)newfc.index;

    f = getofd(h);

    /* build a DND in the tree */
    fd = f->o_dirfil;

    ixlseek(fd,f->o_dirbyt);
    b = (FCB *) ixread(fd,32L,NULL);

    /* is the total path length too long? */    /* M01.01.1107.01 */
    plen = namlen( b->f_name );
    for (dn = f->o_dnode; dn; dn = dn->d_parent)
        plen += namlen(dn->d_name);
    if (plen >= (LEN_ZPATH-3))
    {
        ixdel(f->o_dnode, b, f->o_dirbyt);
        return EACCDN;
    }

    /* note: makdnd() and makofd() only return if they succeed */
    dn = makdnd(f->o_dnode,b);
    f0 = makofd(dn);            /* makofd() also updates dn->d_ofd */

    /* initialize dir cluster */
    if (nextcl(f0,1))
    {
        ixdel(f->o_dnode, b, f->o_dirbyt);      /* M01.01.1103.01 */
        f->o_dnode->d_left = NULL;              /* M01.01.1103.01 */
        freednd(dn);                            /* M01.01.1031.02 */
        return EACCDN;
    }

    f2 = dirinit(dn);                   /* pointer to dirty dir block */

    /* write identifier */
    memcpy(f2, dots, 22);
    f2->f_attrib = FA_SUBDIR;
    dfd = f0->o_dfd;
    f2->f_td = dfd->o_td;            /* time/date are little-endian */
    cl = le2cpu16(dfd->o_strtcl);
    f2->f_clust = cl;
    f2->f_fileln = 0;
    f2++;

    /* write parent entry .. */
    memcpy(f2, dots, 22);
    f2->f_name[1] = '.';           /* This is .. */
    f2->f_attrib = FA_SUBDIR;
    /* if creating a folder in the root, the parent entry needs special handling */
    if (!fd->o_dnode)
    {
        f2->f_td.time = 0;          /* time/date of parent must be zero */
        f2->f_td.date = 0;
        f2->f_clust = 0;            /* cluster number is zero too */
    }
    else
    {
        dfd = f->o_dirfil->o_dfd;
        f2->f_td = dfd->o_td;   /* time/date are little-endian */
        f2->f_clust = le2cpu16(dfd->o_strtcl);
    }
    f2->f_fileln = 0;
    memcpy(f, f0, sizeof(OFD));
    /* the memcpy also copied f->o_dfd, which now points at f0's embedded
     * DFD; nextcl() already marked that DFD O_DIRTY, so ixclose() below
     * sees the flag and writes the parent directory entry. */
    ixclose(f,CL_DIR | CL_FULL);    /* force flush and write */
    xmfreblk(f);
    sft[h-NUMSTD].f_own = 0;
    sft[h-NUMSTD].f_ofd = 0;
    return E_OK;
}

static LONG fat_remove(PFSCOOKIE *dir, const char *name)
{
    DND *dn = (DND *)dir->index;
    FCB *f;
    long pos;

    pos = 0;
    if (!(f = scan(dn, name, FA_NORM, &pos)))
        return EFILNF;
    if (f->f_attrib & FA_RO)
        return EACCDN;
    pos -= 32;
    return ixdel(dn, f, pos);
}

/* Remove an already-resolved directory DND. */
static LONG fat_rmdir_dnd(DND *d)
{
    DND *d1, **q;
    FCB *f;
    OFD *fd, *f2;
    long pos;

    if (!d->d_parent)
        return EACCDN;

    if (!(fd = d->d_ofd))
        fd = makofd(d);

    ixlseek(fd, 0x40L);
    do
    {
        if (!(f = (FCB *)ixread(fd, 32L, NULL)))
            break;
    } while ((f->f_name[0] == (char)ERASE_MARKER) || (f->f_attrib == FA_LFN));

    if ((f != (FCB *)NULL) && (f->f_name[0] != 0x00))
        return EACCDN;

    for (d1 = *(q = &d->d_parent->d_left); d1 != d; d1 = *(q = &d1->d_right))
        ;

    if (d->d_files)
        return EINTRN;
    if (d->d_left)
        return EINTRN;

    *q = d->d_right;

    if (d->d_ofd)
        xmfreblk(d->d_ofd);

    d1 = d->d_parent;
    xmfreblk(d);

    ixlseek((f2 = fd->o_dirfil), (pos = fd->o_dirbyt));
    f = (FCB *)ixread(f2, 32L, NULL);

    return ixdel(d1, f, pos);
}

static LONG __attribute__((unused)) fat_rmdir(PFSCOOKIE *dir, const char *name)
{
    DND *parent = (DND *)dir->index;
    DND *d;
    FCB *f;
    long pos;

    /* pfs_resolve_dir() resolves a trailing slash to the target directory. */
    if (!*name)
        return fat_rmdir_dnd(parent);

    if (*name == '.')
        return EPTHNF;

    pos = 0;
    f = scan(parent, name, FA_SUBDIR, &pos);
    if (!f)
        return EPTHNF;
    d = getdnd(&f->f_name[0], parent);
    if (!d)
        return EPTHNF;

    return fat_rmdir_dnd(d);
}

static LONG fat_chattr(PFSCOOKIE *dir, const char *name, BOOL set, UWORD *dos_attr)
{
    DND *dn = (DND *)dir->index;
    OFD *fd;
    char mod = (char)*dos_attr;
    long pos;

    pos = 0;
    if (!scan(dn, name, FA_NORM, &pos))
        return EFILNF;
    if (set && (mod & ~FA_NORM))
        return EACCDN;
    pos -= 21;
    fd = dn->d_ofd;
    ixlseek(fd, pos);
    if (!set)
        ixread(fd, 1L, &mod);
    else
    {
        ixwrite(fd, 1L, &mod);
        ixclose(fd, CL_DIR);
    }
    *dos_attr = (UWORD)(UBYTE)mod;
    return E_OK;
}

static LONG fat_rename(PFSCOOKIE *olddir, const char *oldname,
                       PFSCOOKIE *newdir, const char *newname)
{
    OFD *fd;
    DFD *dfd;
    FCB *f;
    DND *dn1 = (DND *)olddir->index;
    DND *dn2 = (DND *)newdir->index;
    DMD *dmd1, *dmd2;
    CLNO strtcl1, strtcl2, temp;
    const char *s1 = oldname, *s2 = newname;
    char buf[11], att;
    int hnew;
    long posp;
    UWORD filetime, filedate;
    CLNO clust;
    LONG fileln, rc;
    PFSCOOKIE newfc;
    BOOL was_locked;

    posp = 0L;
    was_locked = dn1->d_flag & DND_LOCKED;
    dn1->d_flag |= DND_LOCKED;
    f = scan(dn2,s2,FA_SUBDIR|FA_ARCHIVE|FA_RO,&posp);
    if (!was_locked)
        dn1->d_flag &= ~DND_LOCKED;
    if (f)
        return EACCDN;

    dmd1 = dn1->d_drv;
    strtcl1 = dn1->d_parent ? dn1->d_strtcl : ROOT_PSEUDO_CLUSTER;

    posp = 0L;
    f = scan(dn1,s1,FA_NORM|FA_SUBDIR,&posp);
    if (!f)
        return EFILNF;

    if (f->f_attrib & FA_RO)
        return EACCDN;

    fd = dn1->d_ofd;
    posp -= 32;

    att = f->f_attrib;
    filetime = le2cpu16(f->f_td.time);
    filedate = le2cpu16(f->f_td.date);
    clust = le2cpu16(f->f_clust);
    fileln = le2cpu32(f->f_fileln);

    dmd2 = dn2->d_drv;
    strtcl2 = dn2->d_parent ? dn2->d_strtcl : ROOT_PSEUDO_CLUSTER;

    if (contains_illegal_characters(s2))
        return EACCDN;

    if (dmd1 != dmd2)
        return ENSAME;

    if (strtcl1 != strtcl2)
    {
        OFD *fd2, *fdparent;
        BOOL dn1_was_locked, dn2_was_locked;

        if (is_subdir(s1,dn1,dn2))
            return EACCDN;

        dn1_was_locked = dn1->d_flag & DND_LOCKED;
        dn2_was_locked = dn2->d_flag & DND_LOCKED;
        dn1->d_flag |= DND_LOCKED;
        dn2->d_flag |= DND_LOCKED;
        rc = fat_create(newdir, newname, (UWORD)(UBYTE)att, &newfc);
        if (!dn1_was_locked)
            dn1->d_flag &= ~DND_LOCKED;
        if (!dn2_was_locked)
            dn2->d_flag &= ~DND_LOCKED;
        if (rc < 0)
            return EPTHNF;
        hnew = (int)newfc.index;
        fd2 = getofd(hnew);

        buf[0] = (char)ERASE_MARKER;
        if (update_fcb(fd,posp,1L,buf) < 0)
        {
            KDEBUG(("xrename(): can't erase old entry\n"));
            return EACCDN;
        }

        dfd = fd2->o_dfd;
        dfd->o_td.time = cpu2le16(filetime);
        dfd->o_td.date = cpu2le16(filedate);
        dfd->o_strtcl = clust;
        dfd->o_fileln = fileln;

        fdparent = fd2->o_dirfil;
        if (att&FA_SUBDIR) {
            dfd->o_fileln = 0x7fffffffL;

            if (!fd2->o_dnode->d_name[0])
                temp = 0;
            else temp = fdparent->o_dfd->o_strtcl;
            temp = cpu2le16(temp);
            if (update_fcb(fd2,32+26,2L,(BYTE *)&temp) < 0)
            {
                KDEBUG(("xrename(): can't update .. entry\n"));
                return EINTRN;
            }

            if (update_fcb(fdparent,fd2->o_dirbyt+11,1L,&att) < 0)
            {
                KDEBUG(("xrename(): can't update parent's attr byte\n"));
                return EINTRN;
            }
        }
        dfd->o_flag |= O_DIRTY;
        if (att&FA_SUBDIR) {
            ixclose(fd2,CL_DIR|CL_FULL);
            xmfreblk(fd2);
            sft[hnew-NUMSTD].f_own = 0;
            sft[hnew-NUMSTD].f_ofd = 0;
        } else xclose(hnew);
        ixclose(fdparent,CL_DIR);
    }
    else
    {
        builds(s2,buf);
        if (update_fcb(fd,posp,11L,buf) < 0)
        {
            KDEBUG(("xrename(): can't update FCB with new name\n"));
            return EACCDN;
        }
    }

    if (att&FA_SUBDIR) {
        DND *dnd;
        char s[LEN_ZFNAME];
        unpackit(s1,s);
        dnd = getdnd(s,dn1);
        if (dnd) {
            KDEBUG(("xrename(): delete existing DND @ %p\n",dnd));
            freednd(dnd);
        }
    }

    return ixclose(fd,CL_DIR);
}

#if CONF_WITH_PLUGGABLE_FS

#define FAT_ALL_ATTR (FA_RO | FA_HIDDEN | FA_SYSTEM | FA_VOL | FA_SUBDIR | FA_ARCHIVE)

/* Build an absolute path "X:\...\name" into 'buf' (size 'buflen'), where
 * the "\...\" portion comes from walking 'dir's DND up to the root via
 * dopath(), and the drive letter from that DND's own DMD - never from
 * ambient process state.
 */
static LONG fat_abspath(PFSCOOKIE *dir, const char *tail, char *buf, int buflen)
{
    DND *dn = (DND *)dir->index;
    char *p = buf;
    char *end;
    int len;

    if (buflen < 4)
        return ERANGE;

    *p++ = (char)('A' + dn->d_drv->m_drvnum);
    *p++ = ':';

    len = buflen - 3;      /* -2 for "X:" already written, -1 to always
                             * leave room for strlcpy()'s trailing null */
    end = dopath(dn, p, &len);
    /*
     * dopath() silently stops writing when it runs out of room (its own
     * comment: "len must never go < 0") rather than reporting truncation
     * - it always ends a component it *did* finish with SLASH, so 'len'
     * hitting exactly 0 without the last byte written being SLASH means
     * it stopped mid-component.  Treat that - and 'tail' not fitting in
     * whatever room is left - as ERANGE rather than silently operating
     * on the wrong (truncated) path.
     */
    if ((len == 0) && ((end == p) || (end[-1] != SLASH)))
        return ERANGE;

    if (strlen(tail) > (size_t)len)
        return ERANGE;

    strlcpy(end, tail, (size_t)len + 1);

    return E_OK;
}

static LONG fat_root(struct pfs_ops *fs, WORD drive, PFSCOOKIE *out)
{
    char path[4];
    const char *sp;
    DND *dn;

    (void)fs;

    path[0] = (char)('A' + drive);
    path[1] = ':';
    path[2] = SLASH;
    path[3] = 0;

    dn = findit(path, &sp, 1);
    if ((LONG)dn < 0)
        return (LONG)dn;
    if (!dn)
        return EPTHNF;

    out->fs = &fat_pfs_ops;
    out->index = (LONG)dn;
    out->aux = 0;
    out->pos = 0;

    return E_OK;
}

static LONG fat_lookup(PFSCOOKIE *dir, const char *path, PFSCOOKIE *out)
{
    char abspath[LEN_ZPATH];
    const char *sp;
    DND *dn;
    LONG rc;

    rc = fat_abspath(dir, path, abspath, sizeof(abspath));
    if (rc < 0)
        return rc;

    dn = findit(abspath, &sp, 1);
    if ((LONG)dn < 0)
        return (LONG)dn;
    if (!dn)
        return EPTHNF;

    out->fs = &fat_pfs_ops;
    out->index = (LONG)dn;
    out->aux = 0;
    out->pos = 0;

    return E_OK;
}

static LONG fat_close(PFSCOOKIE *fc)
{
    return xclose((int)fc->index);
}

static LONG fat_read(PFSCOOKIE *fc, LONG pos, LONG len, UBYTE *buf)
{
    OFD *ofd = getofd((int)fc->index);

    if (!ofd)
        return EIHNDL;
    if (ixlseek(ofd, pos) != pos)
        return EREADF;

    return ixread(ofd, len, buf);
}

static LONG fat_write(PFSCOOKIE *fc, LONG pos, LONG len, const UBYTE *buf)
{
    OFD *ofd = getofd((int)fc->index);

    if (!ofd)
        return EIHNDL;
    if (ixlseek(ofd, pos) != pos)
        return EWRITF;

    return ixwrite(ofd, len, (void *)buf);
}

/* Private pool of DTAINFO search cursors, one per concurrent
 * fs/pfs.c-driven directory listing on a FAT drive - independent of the
 * caller's own DTA, since readdir()'s cursor contract is a single LONG,
 * not a whole DTA buffer.  '*cursor' is (pool index + 1); 0 means
 * "start a new listing".
 */
typedef struct {
    BOOL used;
    DTAINFO dta;
} FAT_READDIR_SLOT;

static FAT_READDIR_SLOT fat_readdir_pool[CONF_PFS_MAX_SEARCHES];

static void fat_readdir_result(char *name, int namelen, PFSATTR *outattr, const DTAINFO *dt)
{
    strlcpy(name, dt->dt_fname, namelen);
    outattr->size = (ULONG)dt->dt_fileln;
    outattr->dos_attr = (UWORD)(UBYTE)dt->dt_fattr;
    outattr->date = dt->dt_td.date;
    outattr->time = dt->dt_td.time;
}

static LONG fat_readdir(PFSCOOKIE *dir, LONG *cursor, char *name, int namelen, PFSATTR *outattr)
{
    WORD slot;

    if (*cursor == 0)
    {
        char path[LEN_ZPATH];
        WORD i;
        LONG rc;

        for (i = 0; i < CONF_PFS_MAX_SEARCHES; i++)
            if (!fat_readdir_pool[i].used)
                break;
        if (i == CONF_PFS_MAX_SEARCHES)
            return ENHNDL;

        rc = fat_abspath(dir, "*.*", path, sizeof(path));
        if (rc < 0)
            return rc;

        fat_readdir_pool[i].dta.dt_offset_drive = -1;
        rc = ixsfirst(path, FAT_ALL_ATTR, &fat_readdir_pool[i].dta);
        if (rc < 0)
            return rc;

        fat_readdir_pool[i].used = TRUE;
        *cursor = i + 1;
        /* remember which pool slot this cookie's search owns, so
         * fat_release() can free it if the search is abandoned before
         * running to end-of-directory (a new Fsfirst() on the same DTA,
         * or the owning process exiting - see fs/pfs.c's
         * pfs_search_free()). */
        dir->aux = i + 1;

        fat_readdir_result(name, namelen, outattr, &fat_readdir_pool[i].dta);
        return E_OK;
    }

    slot = (WORD)(*cursor - 1);
    if ((slot < 0) || (slot >= CONF_PFS_MAX_SEARCHES) || !fat_readdir_pool[slot].used)
        return ENMFIL;

    {
        FCB *f = ixsnext(&fat_readdir_pool[slot].dta);

        if (!f)
        {
            fat_readdir_pool[slot].used = FALSE;
            dir->aux = 0;
            return ENMFIL;
        }

        makbuf(f, &fat_readdir_pool[slot].dta);
        fat_readdir_result(name, namelen, outattr, &fat_readdir_pool[slot].dta);
    }

    return E_OK;
}

static LONG fat_mediach(struct pfs_ops *fs, WORD drive)
{
    (void)fs;
    return Mediach(drive);
}

/* DND-based directory cookies and sft[]-backed file handles both
 * outlive a single call already (the DND tree is a cache, and file
 * handles are freed by close), so there is nothing to release for
 * those. The one thing that does need releasing is a directory
 * cookie's attached fat_readdir_pool[] slot, if fat_readdir() gave it
 * one (cookie->aux != 0) and the search was abandoned before running
 * to end-of-directory - see fat_readdir()'s comment.
 */
static void fat_release(PFSCOOKIE *fc)
{
    if (fc->aux > 0)
    {
        WORD slot = (WORD)(fc->aux - 1);

        if ((slot >= 0) && (slot < CONF_PFS_MAX_SEARCHES))
            fat_readdir_pool[slot].used = FALSE;
        fc->aux = 0;
    }
}

struct pfs_ops fat_pfs_ops = {
    fat_root,
    fat_lookup,
    fat_open,
    fat_create,
    fat_close,
    fat_read,
    fat_write,
    fat_readdir,
    fat_mkdir,
    fat_rmdir,
    fat_remove,
    fat_rename,
    fat_chattr,
    fat_dfree,
    fat_mediach,
    fat_release,
    TRUE            /* native_handles: fat_open()/fat_create() already
                     * allocate a real sft[] slot - see fat_open()/
                     * fat_create() and pfs_do_open()/pfs_do_create()
                     * in fs/pfs.c. */
};

#endif /* CONF_WITH_PLUGGABLE_FS */

#if !CONF_WITH_PLUGGABLE_FS
/*
 * contains_wildcard_characters - check for wildcard chars in specified string
 */
static BOOL contains_wildcard_characters(const char *test)
{
    const char *t;

    for (t = test; *t; t++)
        if ((*t == '?') || (*t == '*'))
            return TRUE;

    return FALSE;
}

LONG fat_sfirst_path(char *name, int att)
{
    long result;
    DTAINFO *dt;

    dt = (DTAINFO *)(run->p_xdta);
    dt->dt_offset_drive = -1L;
    result = ixsfirst(name, att, dt);
    if ((result < 0) || !contains_wildcard_characters(name))
        return result;
    return E_OK;
}

LONG fat_snext_path(void)
{
    FCB *f;
    DTAINFO *dt;

    dt = (DTAINFO *)run->p_xdta;
    if (dt->dt_offset_drive < 0L)
        return ENMFIL;
    f = ixsnext(dt);
    if (f == NULL)
    {
        dt->dt_offset_drive = -1L;
        return ENMFIL;
    }
    makbuf(f, (DTAINFO *)run->p_xdta);
    return E_OK;
}

LONG fat_getfree_path(long *buf, int drv)
{
    WORD drive = drv ? (WORD)(drv - 1) : run->p_curdrv;
    return fat_dfree(NULL, drive, (ULONG *)buf);
}

LONG fat_open_path(char *name, int mod)
{
    DND *dn;
    const char *s;
    PFSCOOKIE dir, out;
    LONG rc;

    if ((long)(dn = findit(name, &s, 0)) < 0)
        return (long)dn;
    if (!dn)
        return EFILNF;
    dir.fs = NULL;
    dir.index = (LONG)dn;
    dir.aux = 0;
    dir.pos = 0;
    rc = fat_open(&dir, s, mod, &out);
    if (rc < 0)
        return rc;
    return out.index;
}

LONG fat_creat_path(char *name, char attr)
{
    DND *dn;
    const char *s;
    PFSCOOKIE dir, out;
    LONG rc;

    if ((long)(dn = findit(name, &s, 0)) < 0)
        return (long)dn;
    if (!dn)
        return EPTHNF;
    dir.fs = NULL;
    dir.index = (LONG)dn;
    dir.aux = 0;
    dir.pos = 0;
    rc = fat_create(&dir, s, (UWORD)(UBYTE)attr, &out);
    if (rc < 0)
        return rc;
    return out.index;
}

LONG fat_mkdir_path(char *s)
{
    DND *dn;
    const char *sp;
    PFSCOOKIE dir;

    if ((long)(dn = findit(s, &sp, 0)) < 0)
        return (long)dn;
    if (!dn)
        return EPTHNF;
    dir.fs = NULL;
    dir.index = (LONG)dn;
    dir.aux = 0;
    dir.pos = 0;
    return fat_mkdir(&dir, sp);
}

LONG fat_rmdir_path(char *p)
{
    DND *d;
    const char *s;

    if ((long)(d = findit(p, &s, 1)) < 0)
        return (long)d;
    if (!d)
        return EPTHNF;
    (void)s;

    return fat_rmdir_dnd(d);
}

LONG fat_chdir_path(char *p)
{
    DND *dnd;
    long rc;
    int olddir, newdir, dlog;
    const char *s;

    if (contains_wildcard_characters(p))
        return EPTHNF;

    if (p[1] == ':')
        dlog = toupper(p[0]) - 'A';
    else
        dlog = run->p_curdrv;

    olddir = run->p_curdir[dlog];

    rc = (long)(dnd = findit(p, &s, 1));
    if (rc < 0L)
        return rc;
    if (!dnd)
        return EPTHNF;

    newdir = incr_curdir_usage(dnd);
    if (newdir < 0)
        return EPTHNF;
    run->p_curdir[dlog] = newdir;

    if (olddir)
        decr_curdir_usage(olddir);

    return E_OK;
}

LONG fat_getdir_path(char *buf, int drv)
{
    DND *p;
    int n;
    int len;

    drv = (drv == 0) ? run->p_curdrv : drv - 1;

    if (!(Drvmap() & (1L << drv)) || (ckdrv(drv, FALSE) < 0))
    {
        *buf = 0;
        return EDRIVE;
    }

    n = run->p_curdir[drv];
    p = dirtbl[n].dnd;
    len = LEN_ZPATH - 3;
    buf = dopath(p, buf, &len);
    *--buf = 0;

    return E_OK;
}

LONG fat_unlink_path(char *name)
{
    DND *dn;
    const char *s;
    PFSCOOKIE dir;

    if ((long)(dn = findit(name, &s, 0)) < 0)
        return (long)dn;
    if (!dn)
        return EFILNF;
    dir.fs = NULL;
    dir.index = (LONG)dn;
    dir.aux = 0;
    dir.pos = 0;
    return fat_remove(&dir, s);
}

LONG fat_chmod_path(char *p, int wrt, char mod)
{
    DND *dn;
    const char *s;
    PFSCOOKIE dir;
    UWORD attr = (UWORD)(UBYTE)mod;
    LONG rc;

    if ((long)(dn = findit(p, &s, 0)) < 0)
        return (long)dn;
    if (!dn)
        return EPTHNF;
    dir.fs = NULL;
    dir.index = (LONG)dn;
    dir.aux = 0;
    dir.pos = 0;
    rc = fat_chattr(&dir, s, wrt ? TRUE : FALSE, &attr);
    if (rc < 0)
        return rc;
    return (long)(char)attr;
}

long fat_rename_path(char *p1, char *p2)
{
    DND *dn1, *dn2;
    const char *s1, *s2;
    PFSCOOKIE old, new;
    BOOL was_locked;

    if ((long)(dn1 = findit(p1, &s1, 0)) < 0)
        return (long)dn1;
    if (!dn1)
        return EPTHNF;
    was_locked = dn1->d_flag & DND_LOCKED;
    dn1->d_flag |= DND_LOCKED;
    dn2 = findit(p2, &s2, 0);
    if (!was_locked)
        dn1->d_flag &= ~DND_LOCKED;
    if ((long)dn2 < 0)
        return (long)dn2;
    if (!dn2)
        return EPTHNF;
    old.fs = NULL;
    old.index = (LONG)dn1;
    old.aux = 0;
    old.pos = 0;
    new.fs = NULL;
    new.index = (LONG)dn2;
    new.aux = 0;
    new.pos = 0;
    return fat_rename(&old, s1, &new, s2);
}
#endif
