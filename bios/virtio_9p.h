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

#endif /* CONF_WITH_VIRTIO_9P */

#endif /* VIRTIO_9P_H */
