/*
 * fatfs.h - GEMDOS-shaped entry points into the built-in FAT driver
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 *
 * Called from bdos/'s thin GEMDOS shims when CONF_WITH_PLUGGABLE_FS is
 * off (the majority case): there is then exactly one filesystem, so the
 * shims call the compiled-in FAT implementation directly instead of
 * going through fs/pfs.c.  When the option is on the shims call the
 * pfs_do_*() helpers (fs/pfs.h) instead and none of these are used.
 */

#ifndef FATFS_H
#define FATFS_H

#include "portab.h"

long fat_open_path(char *name, int mod);
long fat_creat_path(char *name, char attr);
long fat_unlink_path(char *name);
long fat_getfree_path(long *buf, int drv);
long fat_mkdir_path(char *s);
long fat_rmdir_path(char *p);
long fat_chmod_path(char *p, int wrt, char mod);
long fat_chdir_path(char *p);
long fat_getdir_path(char *buf, int drv);
LONG fat_sfirst_path(char *name, int att);
LONG fat_snext_path(void);
long fat_rename_path(char *p1, char *p2);

#endif /* FATFS_H */
