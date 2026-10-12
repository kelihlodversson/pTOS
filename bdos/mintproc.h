/*
 * mintproc.h - the MiNT process calls (bdos/mintproc.c)
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef MINTPROC_H
#define MINTPROC_H

#include "portab.h"

#define GEMDOS_PGETPID      0x10b
#define GEMDOS_PGETPPID     0x10c
#define GEMDOS_PGETUID      0x10f
#define GEMDOS_PGETGID      0x114
#define GEMDOS_PRUSAGE      0x11e
#define GEMDOS_PGETEUID     0x138
#define GEMDOS_PGETEGID     0x139
#define GEMDOS_DGETCWD      0x13b
#define GEMDOS_PGETAUID     0x145
#define GEMDOS_PSETAUID     0x146
#define GEMDOS_PGETPRIORITY 0x158
#define GEMDOS_PSETPRIORITY 0x159

/* the call pw[0] (the arguments after it), if it is one of these: *handled is
 * TRUE and the result returned; else *handled is FALSE */
long mintproc_call(const long *pw, BOOL *handled);

#endif /* MINTPROC_H */
