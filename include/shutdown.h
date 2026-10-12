/*
 * shutdown.h - GEMDOS Shutdown() (0x151, MiNT 1.04 / MagiC)
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef SHUTDOWN_H
#define SHUTDOWN_H

#include "portab.h"

#define GEMDOS_SHUTDOWN 0x151

/* the modes of Shutdown(), as MiNT names them */
#define SHUT_POWER  0   /* turn the power off, else halt */
#define SHUT_BOOT   1   /* warm boot */
#define SHUT_COLD   2   /* cold boot */
#define SHUT_HALT   3   /* stop */

/* bdos/xshutdown.c: sync, say so, and stop or restart; negative GEMDOS error
 * code if it does not start, otherwise it does not return */
long xshutdown(LONG mode);

#endif /* SHUTDOWN_H */
