/*
 * pfs.h - pluggable filesystem layer for GEMDOS drives
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 *
 * A drive letter can be claimed by a driver implementing 'struct pfs_ops'
 * instead of by the built-in FAT filesystem.  When CONF_WITH_PLUGGABLE_FS
 * is set, the bdos/ x* filesystem shims call the pfs_do_*() helpers below;
 * they resolve every drive, including built-in FAT drives served by
 * fat_pfs_ops, through this interface.
 */

#ifndef PFS_H
#define PFS_H

#include "portab.h"
#include "bdosdefs.h"   /* for PD, used by pfs_proc_exit() below */

struct pfs_ops;
struct fs_handle;
struct dta_info;

/* Opaque handle identifying a file or directory within one driver
 * instance.  The core never looks inside index/aux; a driver is free to
 * store whatever it needs there (fatfs_pfs.c stores a DND or OFD
 * pointer, a future 9p driver would store a fid/qid).
 */
typedef struct pfs_cookie {
    struct pfs_ops *fs;
    intptr_t index; /* native pointer, driver ID or native GEMDOS handle */
    LONG aux;
    LONG pos;   /* Core-managed sequential position for open files; drivers
                 * leave that position to pfs_handle_read()/write(). Directory
                 * cookies are not file positions: FAT uses -1 to identify an
                 * owned DND hold, independently of aux's search cursor. */
} PFSCOOKIE;

/* GEMDOS-native file/directory attributes, as used by Fsfirst/Fsnext,
 * Fattrib and Dfree.
 */
typedef struct pfs_attr {
    ULONG size;
    UWORD dos_attr;     /* FA_RDONLY / FA_DIR / ... , see include/fatfs.h or similar */
    UWORD date;         /* GEMDOS packed date, as returned by Fsfirst */
    UWORD time;         /* GEMDOS packed time, as returned by Fsfirst */
} PFSATTR;

/* Fcntl command metadata supplied by the opened file's provider. The size
 * is zero for NONE and SCALAR. The sized ioctl receives NULL/0 for NONE,
 * a kernel ULONG for SCALAR, or a kernel buffer of the specified size. */
typedef struct {
    enum {
        PFS_IOCTL_NONE, PFS_IOCTL_SCALAR, PFS_IOCTL_IN, PFS_IOCTL_OUT,
        PFS_IOCTL_INOUT
    } kind;
    UWORD size;
} PFS_IOCTL_SPEC;

/*
 * Per-driver operations vtable.  Every entry is optional; fs/pfs.c treats
 * a NULL entry as "this driver doesn't support that operation" and
 * returns a GEMDOS/BDOS error without calling it, but the exact code
 * depends on which entry: EINVFN for root()/dfree() (the driver can't
 * function at all without them), EPTHNF for lookup() (nothing beyond the
 * root is resolvable), EACCDN for the write-type calls
 * (open/create/mkdir/rmdir/remove/rename/chattr-when-setting, matching
 * GEMDOS's usual "not permitted" code for an unsupported operation), and
 * E_OK for a missing close() (nothing to do, see pfs_handle_close()) -
 * see each entry's own comment below for anything call-specific.  All
 * functions return a GEMDOS/BDOS status: 0 or positive on success, a
 * negative gemerror.h code on failure - the same convention used
 * throughout bdos/ and bios/.
 */
struct pfs_ops {
    /* Root directory cookie for 'drive' on this driver instance.  A
     * single 'fat_pfs_ops' instance serves every FAT drive letter, so
     * 'drive' - not just 'fs' - identifies which one; a driver bound to
     * one fixed drive (a foreign driver registered via
     * pfs_register_drive()) can simply ignore it.
     */
    LONG (*root)(struct pfs_ops *fs, WORD drive, PFSCOOKIE *out);

    /* Resolve a *directory* path (possibly several components, "." and
     * ".." allowed) starting at directory cookie 'dir'.  Used for
     * Dsetpath/current-directory tracking and to walk down to the
     * containing directory of a longer Fopen/Fcreate/... path before
     * calling one of the per-name entries below with the final component.
     * Never used to resolve a plain file - GEMDOS has no notion of a
     * standalone "file cookie" independent of a (directory, name) pair,
     * and not every driver can produce one (FAT only tree-caches
     * directories, not files).
     *
     * An empty 'path' ("") means "give me a fresh, independently
     * releasable reference to 'dir' itself" - a dup, not a no-op alias.
     * fs/pfs.c relies on this to upgrade a borrowed cookie (one it must
     * not release, e.g. the cached current directory) into one it owns
     * and must eventually release(), for state that outlives the call
     * that resolved 'dir' (see pfs_do_sfirst_at()'s search table).  For a
     * driver whose cookies carry no refcounted resource (like FAT's,
     * which just wrap a cached DND*), this can simply be `*out = *dir;`;
     * a driver that does refcount (e.g. a future 9p fid) must actually
     * bump it here.
     */
    LONG (*lookup)(PFSCOOKIE *dir, const char *path, PFSCOOKIE *out);

    /* Every one of the following takes the containing directory cookie
     * plus a single final path component ('name'), matching how GEMDOS
     * itself always addresses a file: by (directory, name), never by a
     * pre-resolved file handle of its own.
     */
    LONG (*open)(PFSCOOKIE *dir, const char *name, WORD mode, PFSCOOKIE *out);
    LONG (*create)(PFSCOOKIE *dir, const char *name, UWORD attr, PFSCOOKIE *out);
    /* close reports flush/operation errors but does not consume the cookie.
     * The SFT consumes one alias after a normal return, even on error. A
     * nonlocal disk-error unwind must leave that alias/cookie owned. Final
     * release below consumes the cookie and must not unwind after consumption. */
    LONG (*close)(PFSCOOKIE *fc);
    LONG (*read)(PFSCOOKIE *fc, LONG pos, LONG len, UBYTE *buf);
    LONG (*write)(PFSCOOKIE *fc, LONG pos, LONG len, const UBYTE *buf);

    /* One directory entry per call.  '*cursor' is 0 to start a new
     * listing; the driver updates it to whatever it needs to resume, and
     * returns ENMFIL once the listing is exhausted.
     */
    LONG (*readdir)(PFSCOOKIE *dir, LONG *cursor, char *name, int namelen,
                     PFSATTR *outattr);

    LONG (*mkdir)(PFSCOOKIE *dir, const char *name);
    LONG (*rmdir)(PFSCOOKIE *dir, const char *name);
    LONG (*remove)(PFSCOOKIE *dir, const char *name);
    LONG (*rename)(PFSCOOKIE *olddir, const char *oldname,
                    PFSCOOKIE *newdir, const char *newname);

    /* Fattrib-style get-or-set: on entry, '*dos_attr' holds the attribute
     * bits to apply when 'set' is TRUE and is unused when 'set' is FALSE
     * (a plain "get"). On a successful return, '*dos_attr' must hold
     * whatever pfs_do_chmod() should hand straight back as the GEMDOS
     * Fattrib() result: the current on-disk attributes for a "get", and
     * for a "set", the value actually applied - i.e. an echo of the
     * input, NOT the pre-change attributes, matching xchmod()'s own
     * convention (bdos/fsdir.c) which fat_chattr() (fs/fatfs_pfs.c)
     * already follows.
     *
     * Fdatime is deliberately not covered here - unlike Fattrib it
     * addresses an already-open handle, not a (directory, name) pair, so
     * it goes through fstat() below and pfs_handle_datime(). */
    LONG (*chattr)(PFSCOOKIE *dir, const char *name, BOOL set, UWORD *dos_attr);

    /* out[0..3] = {free clusters, total clusters, bytes/sector,
     * sectors/cluster}, matching Dfree's on-stack result order. */
    LONG (*dfree)(struct pfs_ops *fs, WORD drive, ULONG out[4]);

    /* 0 = media unchanged, 1 = media changed, matching BIOS Mediach(). */
    LONG (*mediach)(struct pfs_ops *fs, WORD drive);

    /* Release any driver resources tied to a cookie the core is done
     * with (a directory cookie returned by lookup()/root(), or a file
     * cookie after close()).  May be NULL if the driver needs no
     * per-cookie cleanup. Directory release consumes that cookie on normal
     * return and must not escape through a disk-error/nonlocal continuation.
     * Path dispatch guards disk faults before releasing temporary cookies;
     * they do not make a failing consuming destructor retryable.
     */
    void (*release)(PFSCOOKIE *fc);

    /* TRUE if open()/create() already returns a real, live GEMDOS
     * handle in the successful cookie's 'index' - fat_pfs_ops does,
     * since fat_open()/fat_create() already allocate their own sft[] slot
     * internally.  The core then hands that handle straight back to
     * the caller instead of also wrapping it in a second sft[] slot of
     * its own (which would needlessly consume two system handles per
     * open file).  Leave FALSE (the default for an omitted trailing
     * field) for a driver with no handle management of its own, e.g. a
     * future 9p driver - the core allocates and owns the handle for
     * those, same as before this field existed.
     */
    BOOL native_handles;

    /* Size and GEMDOS date/time of an open file cookie ('dos_attr' may
     * be left 0), for the handle-based calls that need them: Fseek
     * (pfs_handle_seek(), the size bounds every seek), Fdatime
     * (pfs_handle_datime()) and the end-of-file check behind Cconis()
     * and friends on a redirected handle (pfs_handle_eof()).  May be
     * NULL; those calls then fail with EINVFN.  Unused, and left NULL,
     * by a native_handles driver, whose handles have a real OFD.
     */
    LONG (*fstat)(PFSCOOKIE *fc, PFSATTR *out);

    /* Optional consuming final-file release, replacing release() for SFT
     * cookies only. A normal return consumes the cookie even on error;
     * return the disposal error rather than hiding it in a void destructor.
     * Must not nonlocally escape after consumption. Directory release()
     * semantics are unchanged. NULL uses the historical void release(). */
    LONG (*release_file)(PFSCOOKIE *fc);

    /* Optional directory-iterator ownership contract (not a GEMDOS ABI).
     * clone receives an owned live iterator; it must preserve source state,
     * acquire a fresh directory reference and fresh private cursor storage,
     * and leave outputs empty on failure. Cookies/opaque positions are never
     * copied by callers. valid performs no I/O or callbacks. close consumes
     * one owner even on error and must not escape through a nonlocal unwind.
     * These operations can also serve future canonical directory handles. */
    LONG (*clone_iterator)(const PFSCOOKIE *,LONG,PFSCOOKIE *,LONG *);
    BOOL (*iterator_valid)(const PFSCOOKIE *,LONG);
    LONG (*close_iterator)(PFSCOOKIE *,LONG);

    /* Optional level check on an opened file held by a live SFT reference.
     * Return requested FS_READY_* bits, terminal ERROR/HANGUP even when
     * unrequested, or a negative error. The level check cannot block, do I/O,
     * or reenter GEMDOS. Notify using fpoll_notify() after publishing a
     * readiness change on configurations that support blocking poll. */
    LONG (*ready)(const PFSCOOKIE *, UWORD events);

    /* Optional control on a pinned open file. The provider validates the
     * command and buffer size and may use the buffer only during the call. */
    LONG (*ioctl)(PFSCOOKIE *, UWORD command, void *arg, ULONG size);

    /* An open description can supply its native seek operation. A
     * successful return is the new shared GEMDOS position. */
    LONG (*seek)(PFSCOOKIE *, LONG offset, WORD whence);

    /* Pure, nonblocking query. EINVFN for unsupported public commands. */
    LONG (*ioctl_spec)(const PFSCOOKIE *, UWORD command, PFS_IOCTL_SPEC *);

    /* The provider validates the entire Fopen mode word. Other providers
     * continue to receive only the legacy access bits. */
    BOOL full_open_mode;
};

typedef struct {
    PFSCOOKIE directory;
    LONG position;
    BOOL owned;
} PFS_ITERATOR;
#define PFS_ITERATOR_INIT { { NULL, 0, 0, 0 }, 0, FALSE }
LONG pfs_iterator_clone(const PFS_ITERATOR *,PFS_ITERATOR *);
BOOL pfs_iterator_valid(const PFS_ITERATOR *);
LONG pfs_iterator_close(PFS_ITERATOR *);

#if CONF_WITH_PROCESS_FILES
struct kproc;
LONG pfs_search_snapshot_prepare(struct kproc *,struct kproc *,UQUAD,
                                 struct dta_info *const *,WORD,WORD *);
BOOL pfs_search_snapshot_valid(struct kproc *,UQUAD,WORD);
void pfs_search_snapshot_commit(struct kproc *,UQUAD,WORD);
void pfs_search_snapshot_abort(struct kproc *,UQUAD);
void pfs_search_close(struct kproc *,struct dta_info *);
#endif

/* Claim 'drive' (0 = A:, 1 = B:, ...) for 'fs', up front (e.g. from a
 * driver's own boot-time init, like virtio_blk_init() claims a block
 * device letter).  Calls fs->root() once to confirm the driver can
 * mount; on success sets drvbits so Drvmap()/the desktop see the new
 * drive.  Returns E_OK or a negative gemerror.h code.
 *
 * Not used for the built-in FAT code: any drive letter nothing else has
 * claimed falls back to fat_pfs_ops lazily, on first access, exactly
 * like ckdrv() mounts a drive on first touch today - see fs/pfs.c.
 */
LONG pfs_register_drive(WORD drive, struct pfs_ops *fs);

/* The built-in FAT filesystem as a pfs_ops instance (see fs/fatfs_pfs.c).
 * One shared instance serves every FAT drive letter -
 * pfs_do_*() drive resolution falls back to this for any drive
 * pfs_register_drive() hasn't explicitly claimed.
 */
extern struct pfs_ops fat_pfs_ops;

/* Handle-based I/O and cursor tracking on the single cookie in the SFT.
 * The close owner calls pfs_handle_close() to flush before consuming a
 * reference. It calls the driver's final release after the last reference.
 */
LONG pfs_handle_read(const struct fs_handle *handle, LONG len, UBYTE *buf);
LONG pfs_handle_write(const struct fs_handle *handle, LONG len, const UBYTE *buf);
LONG pfs_handle_close(PFSCOOKIE *fc);
#if CONF_WITH_USER_ASPACE
LONG pfs_handle_ioctl(const struct fs_handle *handle, UWORD command, void *arg, ULONG size);
LONG pfs_handle_user_ioctl(const struct fs_handle *handle, UWORD command, UQUAD arg);
#endif

/* Fseek on a pluggable handle: 'flg' 0/1/2 = from the start, the current
 * position, the end.  Returns the new position, ERANGE for one before
 * the start or past the end (as ixlseek() does for FAT), or EINVFN.
 */
LONG pfs_bounded_seek(LONG position, ULONG size, LONG offset, WORD whence);
LONG pfs_handle_seek(const struct fs_handle *handle, LONG n, WORD flg);

/* 1 at the end of the file, 0 before it, or a negative error code -
 * bdos/fsio.c's eof() for a pluggable handle.
 */
LONG pfs_handle_eof(const struct fs_handle *handle);

/* Fdatime on a pluggable handle, with the time and date in native byte
 * order.  Only getting them is supported (wrt = 0): pfs_ops has no way
 * to set them, so wrt != 0 gives EACCDN.
 */
LONG pfs_handle_datime(const struct fs_handle *handle, UWORD *time, UWORD *date, WORD wrt);

/* Called from bdos/proc.c's ixterm() when process 'r' terminates: frees
 * any current-directory cookie and Fsfirst/Fsnext search slots (fs/pfs.c
 * internal tables - not sft[], which the caller already handles) 'r'
 * owned, so they don't leak.
 */
void pfs_proc_exit(PD *r);

/* Transactional inheritance: acquire all slots before publishing child CWD.
 * No driver callbacks or allocations. Failure leaves the child unchanged. */
LONG pfs_cwd_inherit(PD *child, PD *parent);
/* FAT-only native path consumers borrow the DND held by the current CWD.
 * The PROCESS table is never interpreted as a legacy FAT dirtbl index. */
struct _dnd;
struct _dnd *pfs_fat_current_directory(PD *process, WORD drive);
/* Media change: invalidate durable FAT cookies before freeing their DND tree.
 * Referenced table slots remain reserved tombstones until their owners drop. */
void pfs_invalidate_fat_drive(WORD drive);

/* The 12 GEMDOS-shaped helpers the bdos/ x* shims call when
 * CONF_WITH_PLUGGABLE_FS is on.  Signatures mirror the GEMDOS opcodes:
 * dfree/getdir take the 1-based-or-0 drive word GEMDOS passes; the path
 * ones take raw path strings.  See the mapping table in
 * docs/superpowers/specs/2026-08-11-invert-pluggable-fs-dispatch-design.md */
LONG pfs_do_dfree(WORD drv, ULONG *buf);
LONG pfs_do_mkdir(const char *path);
LONG pfs_do_rmdir(const char *path);
LONG pfs_do_chdir(const char *path);
/* Trusted loader operation: resolve against the target PROCESS, select its
 * drive only on success, and never switch the calling PROCESS/global run. */
LONG pfs_set_process_directory(const char *path, PD *process);
LONG pfs_do_getdir(char *buf, WORD drv);
LONG pfs_do_open(const char *path, WORD mode);
LONG pfs_do_create(const char *path, UWORD attr);
LONG pfs_do_unlink(const char *path);
LONG pfs_do_chmod(const char *path, WORD wrt, WORD mod);
LONG pfs_do_rename(const char *p1, const char *p2);
LONG pfs_do_sfirst_to(char *,WORD,struct dta_info *address,struct dta_info *output);
LONG pfs_do_snext_to(struct dta_info *address,struct dta_info *output);
LONG pfs_do_sfirst_at(char *path, WORD att, struct dta_info *dta);
LONG pfs_do_snext_at(struct dta_info *dta);

#endif /* PFS_H */
