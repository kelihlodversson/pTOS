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
#endif

#endif /* BIOSACCESS_H */
