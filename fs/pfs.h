/*
 * pfs.h - pluggable filesystem layer for GEMDOS drives
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 *
 * A drive letter can be claimed by a driver implementing 'struct pfs_ops'
 * instead of by the built-in FAT filesystem.  When CONF_WITH_PLUGGABLE_FS
 * is set, every drive - including the built-in FAT ones, wrapped by
 * fatfs_pfs.c as fat_pfs_ops - is dispatched through this interface from
 * bdos/bdosmain.c's osif().  See docs/superpowers/specs for the design
 * this implements.
 */

#ifndef PFS_H
#define PFS_H

#include "portab.h"
#include "pd.h"         /* for PD, used by pfs_proc_exit() below */

struct pfs_ops;

/* Opaque handle identifying a file or directory within one driver
 * instance.  The core never looks inside index/aux; a driver is free to
 * store whatever it needs there (fatfs_pfs.c stores a DND or OFD
 * pointer, a future 9p driver would store a fid/qid).
 */
typedef struct pfs_cookie {
    struct pfs_ops *fs;
    LONG index;
    LONG aux;
    LONG pos;   /* core-managed sequential Fread/Fwrite position for an
                 * open file; drivers never touch this directly - see
                 * pfs_handle_read()/pfs_handle_write() in fs/pfs.c */
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

/*
 * Per-driver operations vtable.  Every entry is optional; a NULL entry
 * makes the layer return EINVFN (or EACCDN for a write-type call) to the
 * GEMDOS caller.  All functions return a GEMDOS/BDOS status: 0 or positive
 * on success, a negative gemerror.h code on failure - the same convention
 * used throughout bdos/ and bios/.
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
     * that resolved 'dir' (see pfs_do_sfirst()'s search table).  For a
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

    /* Fattrib-style get-or-set: reads *dos_attr when 'set' is FALSE,
     * writes it when TRUE.  Fdatime is deliberately not covered here -
     * unlike Fattrib it addresses an already-open handle, not a
     * (directory, name) pair, so it belongs with the handle-based
     * Fread/Fwrite/Fclose dispatch (see pfs_handle_read() etc. above)
     * rather than this vtable; left as a follow-up since nothing in
     * this PR's scope needs it yet. */
    LONG (*chattr)(PFSCOOKIE *dir, const char *name, BOOL set, UWORD *dos_attr);

    /* out[0..3] = {free clusters, total clusters, bytes/sector,
     * sectors/cluster}, matching Dfree's on-stack result order. */
    LONG (*dfree)(struct pfs_ops *fs, WORD drive, ULONG out[4]);

    /* 0 = media unchanged, 1 = media changed, matching BIOS Mediach(). */
    LONG (*mediach)(struct pfs_ops *fs, WORD drive);

    /* Release any driver resources tied to a cookie the core is done
     * with (a directory cookie returned by lookup()/root(), or a file
     * cookie after close()).  May be NULL if the driver needs no
     * per-cookie cleanup.
     */
    void (*release)(PFSCOOKIE *fc);

    /* TRUE if open()/create() already returns a real, live GEMDOS
     * handle in the successful cookie's 'index' - fat_pfs_ops does,
     * since xopen()/ixcreat() already allocate their own sft[] slot
     * internally.  The core then hands that handle straight back to
     * the caller instead of also wrapping it in a second sft[] slot of
     * its own (which would needlessly consume two system handles per
     * open file).  Leave FALSE (the default for an omitted trailing
     * field) for a driver with no handle management of its own, e.g. a
     * future 9p driver - the core allocates and owns the handle for
     * those, same as before this field existed.
     */
    BOOL native_handles;
};

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

/* The built-in FAT filesystem, wrapped as a pfs_ops instance (see
 * fs/fatfs_pfs.c).  One shared instance serves every FAT drive letter -
 * pfs_dispatch()'s drive resolution falls back to this for any drive
 * pfs_register_drive() hasn't explicitly claimed.
 */
extern struct pfs_ops fat_pfs_ops;

/* True if GEMDOS function number 'fn' is one this layer handles
 * (Fopen/Fread/Dsetpath/Fsfirst/... - see fs/pfs.c). Used by osif()'s
 * dispatch hook.
 */
BOOL pfs_is_fs_call(WORD fn);

/* Dispatch GEMDOS function 'fn' with BDOS's raw argument words 'pw'
 * (pw[0] is the function number itself, pw[1..] the arguments) to the
 * pluggable filesystem layer.  Only called when pfs_is_fs_call(fn) is
 * true.  Note Fread(0x3F)/Fwrite(0x40)/Fclose(0x3E) are NOT among the
 * calls pfs_is_fs_call() recognises: those stay on GEMDOS's normal
 * handle-dispatch path and are instead made pluggable-aware directly in
 * xread()/xwrite()/xclose() (bdos/fsio.c, fsopnclo.c) via the three
 * functions below, so every caller of those three - not just osif() -
 * reaches a pluggable handle correctly (e.g. process-termination
 * cleanup in bdos/proc.c). Returns the same LONG osif() itself would
 * return to the caller.  'pw' is exactly osif()'s own argument-words
 * pointer, which is LONG-sized on ARM (one slot per real argument) and
 * WORD-sized on m68k (one slot per 16-bit stack unit) - see osif() in
 * bdos/bdosmain.c.
 */
#ifdef __arm__
LONG pfs_dispatch(WORD fn, LONG *pw);
#else
LONG pfs_dispatch(WORD fn, WORD *pw);
#endif

/* Handle-based dispatch for a handle already resolved (by the bdos/
 * caller, via getpfsslot()) to belong to a pluggable driver.  'fc' is
 * the FTAB slot's embedded cookie; position tracking is entirely
 * internal to these three (see PFSCOOKIE.pos above) - callers just
 * pass the handle's ongoing sequential position through, GEMDOS-style.
 *
 * pfs_handle_close() only calls the driver's close(), not release() -
 * the same cookie can be referenced by more than one sft[] entry at once
 * (Fforce()-shared slots, xdup()-copied slots), so the caller must call
 * fs->release() itself, and only once the last sft[] reference is gone -
 * see bdos/fsopnclo.c's xclose().
 */
LONG pfs_handle_read(PFSCOOKIE *fc, LONG len, UBYTE *buf);
LONG pfs_handle_write(PFSCOOKIE *fc, LONG len, const UBYTE *buf);
LONG pfs_handle_close(PFSCOOKIE *fc);

/* Called from bdos/proc.c's ixterm() when process 'r' terminates: frees
 * any current-directory cookie and Fsfirst/Fsnext search slots (fs/pfs.c
 * internal tables - not sft[], which the caller already handles) 'r'
 * owned, so they don't leak.
 */
void pfs_proc_exit(PD *r);

/* Called from bdos/proc.c's init_pd_files() for each non-zero p_curdir[]
 * entry ('n', an index into fs/pfs.c's own directory table) it copies
 * into a newly created process - mirrors the bump init_pd_files()
 * already does to the legacy dirtbl[n].use for a non-pluggable drive,
 * so a pluggable current-directory slot shared by a parent and child
 * process is not invalidated by one of them exiting first.
 */
void pfs_cwd_addref(WORD n);

#endif /* PFS_H */
