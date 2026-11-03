/*
 * biosaccess.h - checking the arguments of a BIOS or XBIOS call from ring 3
 *
 * Copyright (C) 2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */
#ifndef BIOSACCESS_H
#define BIOSACCESS_H

#include "portab.h"

#if CONF_WITH_USER_COPY
/*
 * Check the arguments of the BIOS (bios_check_user_args) or XBIOS call `fn`
 * that a user process made, a[0..3] being the four argument registers as
 * the syscall convention (bios/arch/x86_64/trap.h) delivers them.  Returns
 * E_OK, EIMBA for a pointer that does not name memory the calling process
 * may use for the whole length the call will touch, ERANGE for a bad
 * length or vector number, EUNDEV for a device the call cannot use, or
 * EACCDN for a call (or an argument) a user process may not use.  Only the
 * pointers the call really uses are looked at.  Memory that passes can be
 * used directly: the call runs under the process's own page tables.
 */
long bios_check_user_args(WORD fn, const long *a);
long xbios_check_user_args(WORD fn, const long *a);

/*
 * The result `r` of the BIOS or XBIOS call `fn` that a user process made,
 * once it has run, as the process may see it: a result that is, or holds, a
 * kernel address is 0 (doc/kernel-pointers.txt).  The calls whose result is
 * a pointer to kernel data (Getbpb(), Iorec(), Kbdvbase()) are refused
 * before they run instead, with EACCDN, in the check above.
 */
long bios_user_result(WORD fn, long r);
long xbios_user_result(WORD fn, long r);
#endif

#endif /* BIOSACCESS_H */
