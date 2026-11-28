/* GPL version 2 or later. See doc/license.txt. */
#ifndef XFS_PFS_H
#define XFS_PFS_H

#include "pfs.h"
#include "ptos_xfs.h"

#define PTOS_XFS_EXT_VERSION 1

#define XFS_IFMT      0170000
#define XFS_IFDIR     0040000
#define XFS_IFREG     0100000
#define XFS_DENY_NONE 0x0040
#define XFS_CREAT     0x0200
#define XFS_TRUNC     0x0400

/* Optional compatibility for built-in providers moved from PFS. */
#define PTOS_XFS_LEGACY_OPEN          0x0001
#define PTOS_XFS_UNSIGNED_DOS_FIELDS 0x0002
#define PTOS_XFS_NO_DOS_ATTRIB        0x0004
#define PTOS_XFS_EXT_FLAGS            0x0007

/* Optional pTOS services outside the upstream vtables. A provider's
 * clone_dir gets a fresh owned fc in dst. On failure it must leave no
 * iterator state to close. ready is a nonblocking level query. */
struct ptos_xfs_extensions {
    UWORD version;
    UWORD size;
    long _cdecl (*clone_dir)(DIR *dst, const DIR *src);
    long _cdecl (*ready)(const FILEPTR *, UWORD events);
    long _cdecl (*ioctl)(FILEPTR *, UWORD command, void *buffer, ULONG size);
    long _cdecl (*ioctl_spec)(const FILEPTR *, UWORD command, PFS_IOCTL_SPEC *);
    UWORD flags;
};

/* Static storage, initially zero. The existing PFS drive table is the
 * mount registry. Mounted drivers and extension metadata stay live. */
struct ptos_xfs_mount {
    struct pfs_ops ops;
    FILESYS *driver;
    const struct ptos_xfs_extensions *extensions;
};

FILESYS *ptos_xfs_init_driver(PTOS_XFS_INIT);
LONG ptos_xfs_mount_drive(WORD, struct ptos_xfs_mount *, FILESYS *,
                          const struct ptos_xfs_extensions *);

#endif
