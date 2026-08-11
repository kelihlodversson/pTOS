/*
 * fs_internal.h - FAT internals exposed for fs/fatfs_pfs.c
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 *
 * Everything declared here was 'static' in bdos/fsdir.c until the
 * pluggable filesystem layer (fs/fatfs_pfs.c) needed to call it directly.
 * Not part of bdos/fs.h's regular public API - only fatfs_pfs.c should
 * include this.
 */

#ifndef FS_INTERNAL_H
#define FS_INTERNAL_H

#include "fs.h"

/* Build the path from the root down to (and including) 'p' into 'buf',
 * as a sequence of "NAME\" components (no drive letter, no leading
 * backslash of its own - the recursion for the root DND, which has no
 * parent, emits exactly one separator).  '*len' is the remaining budget
 * in 'buf' and is decremented as bytes are written.  Returns the
 * position just past the last byte written; the caller is responsible
 * for null-terminating (see xgetdir() for the pattern this follows).
 */
char *dopath(DND *p, char *buf, int *len);

/* One directory-search step, given caller-owned search state 'dt'
 * (as primed by the already-extern ixsfirst()).  Returns a pointer to
 * the matching FCB still inside the live directory-buffer cache (not a
 * copy - the same convention ixsfirst()/scan() use), or NULL at
 * end-of-directory.  Updates *dt in place to resume from the next call.
 */
FCB *ixsnext(DTAINFO *dt);

/* Copy the found entry's attribute/time/date/length/name from FCB 'f'
 * into the public fields of DTA info area 'dt' - the same conversion
 * xsnext() itself applies after a successful ixsnext(), needed here
 * because fatfs_pfs.c's readdir() drives ixsfirst()/ixsnext() with its
 * own private DTAINFO buffers rather than the caller's DTA.
 */
void makbuf(FCB *f, DTAINFO *dt);

#endif /* FS_INTERNAL_H */
