/*
 * ssystem.h - GEMDOS Ssystem() -- the cookie jar and system variables,
 * without direct memory access or Supexec()
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef SSYSTEM_H
#define SSYSTEM_H

#include "portab.h"

#define GEMDOS_SSYSTEM  0x154

/*
 * Ssystem() mode values, as documented for MiNT. Only the subset below
 * is implemented (plus the mandatory S_INQUIRE discovery probe) --
 * every other mode returns EINVFN. See
 * https://github.com/kelihlodversson/pTOS/issues/219.
 *
 * S_INQUIRE is -1, not 0xffff: mode arrives here as a signed WORD, so
 * a case label of plain 0xffff (type int, value 65535) would never
 * match it after the switch's usual arithmetic promotion.
 */
#define S_INQUIRE       ((WORD)0xffff)
#define S_GETCOOKIE     0x0008
#define S_SETCOOKIE     0x0009
#define S_GETLVAL       0x000a
#define S_GETWVAL       0x000b
#define S_GETBVAL       0x000c
#define S_SETLVAL       0x000d
#define S_SETWVAL       0x000e
#define S_SETBVAL       0x000f

LONG xssystem(WORD mode, LONG arg1, LONG arg2);

#endif /* SSYSTEM_H */
