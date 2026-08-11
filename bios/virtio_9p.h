/*
 * virtio_9p.h - virtio-9p (9P2000.L) transport/protocol client for the
 * QEMU virt-arm/virt-m68k boards
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */
#ifndef VIRTIO_9P_H
#define VIRTIO_9P_H

#include "portab.h"

#if CONF_WITH_VIRTIO_9P

/* A 9P qid: a stable per-file/directory identity the server hands back
 * from root()/Twalk/Tlopen/... - not a GEMDOS concept, opaque to callers
 * outside this driver. */
typedef struct
{
    UBYTE type;
    ULONG version;
    UQUAD path;
} QID9P;

/* Probes for a virtio-9p device on the virtio-mmio transport, negotiates
 * the 9P2000.L dialect via Tversion, and attaches (Tattach) to get the
 * persistent root fid v9p_root_fid() returns. No effect if no matching
 * device is found. Called once from bios/bios.c, after blkdev_init() and
 * before fs/virtio_9p_pfs.c's own v9p_pfs_init(), which registers the
 * pfs_ops adapter against the fid-level API below.
 */
void virtio_9p_init(void);

/* TRUE once virtio_9p_init() has found a device, completed Tversion, and
 * successfully attached (Tattach) - i.e. the fid-level API below is safe
 * to use. */
BOOL virtio_9p_present(void);

/* The fid Tattach established at init; never Tclunk'd, lives for the
 * driver's whole lifetime. A directory pfs_ops.root() implementation
 * Twalks from this fid (0 components) to get its own, independently
 * releasable fid. */
ULONG v9p_root_fid(void);

/* Clones 'fromfid' via a Twalk of 'nwname' components ('wname', a plain
 * array of NUL-terminated strings - v9p_walk() encodes them onto the
 * wire, the caller keeps ownership), allocating a fresh fid from the
 * internal pool for the result. On success, '*outfid' is the new fid
 * (the caller owns it - release it with v9p_clunk()) and '*outqid' (if
 * non-NULL) is its qid. 'nwname' must be 0..16 (9P2000.L's own per-call
 * component cap, V9P_MAXWELEM) - chain further calls for a longer path.
 * On failure, no fid is allocated. */
LONG v9p_walk(ULONG fromfid, WORD nwname, const char *const *wname,
              ULONG *outfid, QID9P *outqid);

/* Releases 'fid' (Tclunk) and returns its pool slot, regardless of
 * whether the server's reply indicated success - a fid this driver can
 * no longer account for must never be reused. */
LONG v9p_clunk(ULONG fid);

/* Opens 'fid' for I/O (Tlopen) with Linux-style open(2) flags - GEMDOS's
 * Fopen mode 0/1/2 (RO_MODE/WO_MODE/RW_MODE, bdos/fs.h) map directly onto
 * O_RDONLY/O_WRONLY/O_RDWR (0/1/2), confirmed against fs/fatfs_pfs.c's
 * fat_open(), which passes GEMDOS's mode straight through to xopen()
 * unmodified - the caller (fs/virtio_9p_pfs.c) can pass its WORD mode
 * argument here as-is, widened to ULONG. */
LONG v9p_lopen(ULONG fid, ULONG flags);

/* Reads up to 'count' bytes at 'offset' from 'fid' (already Tlopen'd)
 * into 'buf'. Internally caps 'count' to whatever fits in one Tread/Rread
 * round trip given the negotiated msize - the caller loops (adjusting
 * 'offset') for a request larger than that. Returns the number of bytes
 * actually read (0 at EOF), or a negative gemerror.h code. */
LONG v9p_read(ULONG fid, UQUAD offset, ULONG count, UBYTE *buf);

/* The handful of Tgetattr/Rgetattr fields this driver has a GEMDOS use
 * for; see v9p_getattr()'s own comment for why the rest (uid/gid/nlink/
 * rdev/blksize/blocks/atime/ctime/btime/gen/data_version) aren't kept. */
typedef struct
{
    QID9P qid;
    ULONG mode;         /* Linux-style st_mode bits (permissions + type) */
    UQUAD size;
    UQUAD mtime_sec;    /* seconds since the Unix epoch, UTC */
} P9GETATTR;

/* Fetches attributes for 'fid' (Tgetattr, requesting P9_GETATTR_BASIC).
 * Returns E_OK with '*out' filled, or a negative gemerror.h code. */
LONG v9p_getattr(ULONG fid, P9GETATTR *out);

/* Reads exactly one directory entry (Treaddir) from 'dir_fid', starting
 * at the opaque 9P offset '*offset' (0 to start a fresh listing) and
 * advancing '*offset' to the next entry's own offset on success - the
 * caller (fs/virtio_9p_pfs.c) is responsible for remembering that value
 * across calls (e.g. in its own Fsfirst/Fsnext search-slot pool, mirroring
 * fs/fatfs_pfs.c's fat_readdir_pool[]), since a 9P offset is a full UQUAD
 * and doesn't fit PFSCOOKIE's plain LONG fields. 'name' receives the
 * entry's real (long) name, truncated to fit 'namelen' if necessary;
 * '*is_dir' (if non-NULL) reports whether the entry's own qid is a
 * directory (qid.type's QTDIR bit) - this is the authoritative source
 * pfs_ops.readdir() callers should use, not a fresh Tgetattr, since it
 * comes for free with the entry itself. Returns E_OK, ENMFIL once the
 * directory is exhausted at or after '*offset', or a negative gemerror.h
 * code. */
LONG v9p_readdir_one(ULONG dir_fid, UQUAD *offset, char *name, int namelen,
                      QID9P *outqid, BOOL *is_dir);

/* Linux's AT_REMOVEDIR, for v9p_unlinkat()'s 'flags' - confirmed against
 * include/uapi/linux/fcntl.h. */
#define V9P_AT_REMOVEDIR  0x200UL

/* Creates 'name' inside the directory 'fid' names and opens it for I/O in
 * one round trip (Tlcreate). Like Tlopen, 'flags' are Linux-style open(2)
 * flags. Unlike v9p_walk(), this does NOT allocate a new fid: 'fid' stops
 * being usable as a directory fid and becomes the new file's fid instead
 * - callers must pass a throwaway dup (a 0-component v9p_walk()) of the
 * directory fid they still need afterwards, never the directory cookie's
 * own fid. */
LONG v9p_lcreate(ULONG fid, const char *name, ULONG flags, ULONG mode, QID9P *outqid);

/* Writes 'count' bytes from 'buf' at 'offset' to 'fid' (already
 * Tlopen'd/v9p_lcreate()'d). Internally caps 'count' to whatever fits in
 * one Twrite/Rwrite round trip given the negotiated msize - the caller
 * loops (adjusting 'offset') for a request larger than that. Returns the
 * number of bytes actually written, or a negative gemerror.h code. */
LONG v9p_write(ULONG fid, UQUAD offset, ULONG count, const UBYTE *buf);

/* Creates a subdirectory 'name' inside the directory 'dfid' names
 * (Tmkdir) - unlike v9p_lcreate(), 'dfid' remains a valid directory fid
 * afterwards, since Tmkdir never repurposes it. */
LONG v9p_mkdir(ULONG dfid, const char *name, ULONG mode, QID9P *outqid);

/* Removes 'name' from the directory 'dfid' names (Tunlinkat) - pass
 * V9P_AT_REMOVEDIR in 'flags' for an (empty) subdirectory, 0 for a plain
 * file. */
LONG v9p_unlinkat(ULONG dfid, const char *name, ULONG flags);

/* Renames/moves 'oldname' (inside the directory 'olddfid' names) to
 * 'newname' (inside the directory 'newdfid' names) - Trenameat. The two
 * directories may be the same fid for a plain same-directory rename. */
LONG v9p_renameat(ULONG olddfid, const char *oldname, ULONG newdfid, const char *newname);

#endif /* CONF_WITH_VIRTIO_9P */

#endif /* VIRTIO_9P_H */
