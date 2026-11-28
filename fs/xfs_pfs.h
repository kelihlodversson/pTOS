/* GPL version 2 or later. See doc/license.txt. */
#ifndef XFS_PFS_H
#define XFS_PFS_H

#include "pfs.h"
#include "ptos_xfs.h"

#define PTOS_XFS_EXT_VERSION 1

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
