/*
 * Native source interface for FreeMiNT XFS drivers.
 * Declarations adapted from FreeMiNT sys/mint/{fsops,file,stat,time}.h
 * (2afc6cd66501d6f1a959f5709626c3ffaae1ff56).
 * Copyright 1991,1992 Eric R. Smith.
 * Copyright 1992,1993,1994 Atari Corporation.
 * Distributed under the GPL version 2 or later. See doc/license.txt.
 */

#ifndef PTOS_XFS_H
#define PTOS_XFS_H

/* On classic m68k FreeMiNT callbacks use the stack even in a fastcall
 * kernel. On other CPUs use the compiler's native C calling convention. */
#ifndef _cdecl
#if defined(__m68k__) && defined(__FASTCALL__)
#define _cdecl __attribute__((__cdecl__))
#else
#define _cdecl
#endif
#endif

typedef unsigned short ushort;
typedef unsigned long ulong;
typedef long long llong;
typedef long fs_ino_t;
typedef struct fcookie fcookie;
typedef struct dirstruct DIR;
typedef struct filesys FILESYS;
typedef struct devdrv DEVDRV;
typedef struct file FILEPTR;
typedef struct xattr XATTR;
typedef struct stat STAT;

struct fcookie
{
    FILESYS *fs;        /* filesystem that knows about this cookie */
    ushort  dev;        /* device info (e.g. Rwabs device number) */
    ushort  aux;        /* extra data that the file system may want */
    fs_ino_t    index;      /* this+dev uniquely identifies a file */
};

struct dirstruct
{
    fcookie fc;     /* cookie for this directory */
    ushort  index;      /* index of the current entry */
    ushort  flags;      /* flags (e.g. tos or not) */
# define TOS_SEARCH 0x01
    char    fsstuff[60];    /* anything else the file system wants */
                /* NOTE: this must be at least 45 bytes */
    DIR *next;      /* linked together so we can close them
                 * on process termination */
    short   fd;     /* associated fd, for use with dirfd */
};

struct xattr
{
    ushort  mode;
    long    index;
    ushort  dev;
    ushort  rdev;
    ushort  nlink;
    ushort  uid;
    ushort  gid;
    long    size;
    long    blksize;
    long    nblocks;
    ushort  mtime, mdate;
    ushort  atime, adate;
    ushort  ctime, cdate;
    short   attr;
    short   reserved2;
    long    reserved3[2];
};

/* Source layout for stat64. The initial bridge uses DOS-time getxattr. */
struct time {
    union {
        struct {
            long high_time;
            long time;
        };
        llong time64;
    };
    ulong nanoseconds;
};

struct stat
{
    llong       dev;        /* inode's device */
    ulong       ino;        /* inode's number */
    ulong       mode;       /* inode protection mode */
    ulong       nlink;      /* number of hard links */
    ulong       uid;        /* user ID of the file's owner */
    ulong       gid;        /* group ID of the file's group */
    llong       rdev;       /* device type */
    struct time atime;      /* time of last access, UTC */
    struct time mtime;      /* time of last data modification, UTC */
    struct time ctime;      /* time of last file status change, UTC */
    llong       size;       /* file size, in bytes */
    llong       blocks;     /* blocks allocated for file */
    ulong       blksize;    /* optimal blocksize for I/O */
    ulong       flags;      /* user defined flags for file */
    ulong       gen;        /* file generation number */

    long        res[7];
};

struct devdrv
{
    long _cdecl (*open) (FILEPTR *f);
    long _cdecl (*write)    (FILEPTR *f, const char *buf, long bytes);
    long _cdecl (*read) (FILEPTR *f, char *buf, long bytes);
    long _cdecl (*lseek)    (FILEPTR *f, long where, int whence);
    long _cdecl (*ioctl)    (FILEPTR *f, int mode, void *buf);
    long _cdecl (*datime)   (FILEPTR *f, ushort *timeptr, int rwflag);
    long _cdecl (*close)    (FILEPTR *f, int pid);
    long _cdecl (*select)   (FILEPTR *f, long proc, int mode);
    void _cdecl (*unselect) (FILEPTR *f, long proc, int mode);

    /* Upstream extensions for raw terminal I/O and shutdown. */
    long _cdecl (*writeb)   (FILEPTR *f, const char *buf, long bytes);
    long _cdecl (*readb)    (FILEPTR *f, char *buf, long bytes);

    /* what about: scatter/gather io for DMA devices...
     * long _cdecl (*writev)(FILEPTR *f, const struct iovec *iov, long cnt);
     * long _cdecl (*readv) (FILEPTR *f, struct iovec *iov, long cnt);
     */

    void    _cdecl (*shutdown)(void);
};

struct file
{
    short   links;      /* number of copies of this descriptor */
    ushort  flags;      /* file open mode and other file flags */
    long    pos;        /* position in file */
    long    devinfo;    /* device driver specific info */
    fcookie fc;     /* file system cookie for this file */
    DEVDRV  *dev;       /* device driver that knows how to deal with this */
    FILEPTR *next;      /* link to next fileptr for this file */
};

struct filesys
{
    /* kernel data
     */
    FILESYS *next;          /* link to next file system on chain */
    long    fsflags;
# define FS_KNOPARSE        0x0001  /* kernel shouldn't do parsing */
# define FS_CASESENSITIVE   0x0002  /* file names are case sensitive */
# define FS_NOXBIT      0x0004  /* if a file can be read, it can be executed */
# define FS_LONGPATH        0x0008  /* file system understands "size" argument to "getname" */
# define FS_NO_C_CACHE      0x0010  /* don't cache cookies for this filesystem */
# define FS_DO_SYNC     0x0020  /* file system has a sync function */
# define FS_OWN_MEDIACHANGE 0x0040  /* filesystem control self media change (dskchng) */
# define FS_REENTRANT_L1    0x0080  /* fs is level 1 reentrant */
# define FS_REENTRANT_L2    0x0100  /* fs is level 2 reentrant */
# define FS_EXT_1       0x0200  /* extensions level 1 - mknod & unmount */
# define FS_EXT_2       0x0400  /* extensions level 2 - additional place at the end */
# define FS_EXT_3       0x0800  /* extensions level 3 - stat & native UTC timestamps */

    /* filesystem functions
     */
    long    _cdecl (*root)      (int drv, fcookie *fc);
    long    _cdecl (*lookup)    (fcookie *dir, const char *name, fcookie *fc);
    long    _cdecl (*creat)     (fcookie *dir, const char *name, unsigned mode, int attrib, fcookie *fc);
    DEVDRV* _cdecl (*getdev)    (fcookie *file, long *devspecial);
    long    _cdecl (*getxattr)  (fcookie *file, XATTR *xattr);
    long    _cdecl (*chattr)    (fcookie *file, int attr);
    long    _cdecl (*chown)     (fcookie *file, int uid, int gid);
    long    _cdecl (*chmode)    (fcookie *file, unsigned mode);
    long    _cdecl (*mkdir)     (fcookie *dir, const char *name, unsigned mode);
    long    _cdecl (*rmdir)     (fcookie *dir, const char *name);
    long    _cdecl (*remove)    (fcookie *dir, const char *name);
    long    _cdecl (*getname)   (fcookie *relto, fcookie *dir, char *pathname, int size);
    long    _cdecl (*rename)    (fcookie *olddir, char *oldname, fcookie *newdir, const char *newname);
    long    _cdecl (*opendir)   (DIR *dirh, int tosflag);
    long    _cdecl (*readdir)   (DIR *dirh, char *name, int namelen, fcookie *fc);
    long    _cdecl (*rewinddir) (DIR *dirh);
    long    _cdecl (*closedir)  (DIR *dirh);
    long    _cdecl (*pathconf)  (fcookie *dir, int which);
    long    _cdecl (*dfree)     (fcookie *dir, long *buf);
    long    _cdecl (*writelabel)    (fcookie *dir, const char *name);
    long    _cdecl (*readlabel) (fcookie *dir, char *name, int namelen);
    long    _cdecl (*symlink)   (fcookie *dir, const char *name, const char *to);
    long    _cdecl (*readlink)  (fcookie *dir, char *buf, int len);
    long    _cdecl (*hardlink)  (fcookie *fromdir, const char *fromname, fcookie *todir, const char *toname);
    long    _cdecl (*fscntl)    (fcookie *dir, const char *name, int cmd, long arg);
    long    _cdecl (*dskchng)   (int drv, int mode);
    long    _cdecl (*release)   (fcookie *);
    long    _cdecl (*dupcookie) (fcookie *new, fcookie *old);
    long    _cdecl (*sync)      (void);
    long    _cdecl (*mknod)     (fcookie *dir, const char *name, ulong mode);
    long    _cdecl (*unmount)   (int drv);
    long    _cdecl (*stat64)    (fcookie *file, STAT *stat);

    long    res1, res2, res3;   /* reserved */

    /* experimental extension
     */
    ulong   lock;           /* for non-blocking DMA */
    ulong   sleepers;       /* sleepers on this filesystem */
    void    _cdecl (*block)     (FILESYS *fs, ushort dev, const char *);
    void    _cdecl (*deblock)   (FILESYS *fs, ushort dev, const char *);
};

/* Minimal legacy service prefix. Drivers must use only services supplied
 * by the selected pTOS XFS contract. This is not a FreeMiNT kernel version
 * advertisement and has no modules, trap tables or scheduler services. */
struct kerinfo {
    ushort maj_version, min_version, default_perm, version;
    struct bios_vecs *bios_tab;
    struct dos_vecs *dos_tab;
    void _cdecl (*drvchng)(ushort);
    void _cdecl (*trace)(const char *, ...);
    void _cdecl (*debug)(const char *, ...);
    void _cdecl (*alert)(const char *, ...);
    void _cdecl (*fatal)(const char *, ...);
    void * _cdecl (*kmalloc)(ulong);
    void _cdecl (*kfree)(void *);
};

typedef FILESYS * _cdecl (*PTOS_XFS_INIT)(struct kerinfo *);
FILESYS * _cdecl init_xfs(struct kerinfo *) __asm__("init_xfs");

#endif /* PTOS_XFS_H */
