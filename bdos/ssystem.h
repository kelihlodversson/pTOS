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
#define S_OSNAME        0x0000
#define S_OSVERSION     0x0002
#define S_GETCOOKIE     0x0008
#define S_SETCOOKIE     0x0009
#define S_GETLVAL       0x000a
#define S_GETWVAL       0x000b
#define S_GETBVAL       0x000c
#define S_SETLVAL       0x000d
#define S_SETWVAL       0x000e
#define S_SETBVAL       0x000f
#define S_DELCOOKIE     0x001a

/*
 * S_CONSOLE_DIM - pTOS-only extension, NOT part of MiNT's Ssystem()
 * protocol. Deliberately given a mode value outside the documented MiNT
 * range (a negative WORD, the same idiom S_INQUIRE already uses) so it
 * can never collide with a real MiNT mode.
 *
 * Returns the console's text-cell dimensions without Line-A: on m68k
 * that's a genuine CPU trap, but it doesn't exist at all on ARM (see
 * kelihlodversson/pTOS#235), where the only other way to read
 * v_cel_mx/v_cel_my (bios/lineavars.h) is compiling straight into the
 * kernel's own address space -- not usable from a standalone userland
 * program.
 *
 * arg1 is unused (reserved, pass 0). arg2 is a LONG* the call writes the
 * dimensions to: width (columns) in the lower 16 bits, height (rows) in
 * the upper 16. Returns E_OK on success, or EINVFN if arg2 is NULL.
 */
#define S_CONSOLE_DIM   ((WORD)0xfffe)

LONG xssystem(WORD mode, LONG arg1, LONG arg2);

#endif /* SSYSTEM_H */
