/*
 * uaccess.h - checking the pointers a GEMDOS call is given
 *
 * Copyright (C) 2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */
#ifndef UACCESS_H
#define UACCESS_H

#include "portab.h"

#if CONF_WITH_USER_COPY
/*
 * Checks the pointer arguments of the GEMDOS call whose function number and
 * arguments are pw[0], pw[1], ... (osif()'s layout: one native long per
 * parameter).  Returns E_OK, EIMBA for a pointer that does not name memory the
 * calling process may use for the whole length the call will touch, ERANGE for
 * a bad length or a string with no end in range, or EACCDN for a call a user
 * process may not make.  Only the pointers the call really uses are looked at;
 * the others are scalars and whatever they hold is none of its business.
 *
 * The calling process is `run`.  Memory the check accepts can be used
 * directly: the call runs under the process's own page tables.
 */
long bdos_check_user_args(const long *pw);
#endif

#endif /* UACCESS_H */
