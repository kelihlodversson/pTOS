/*
 * sysvar.h - system variables placed by the machine (CONF_WITH_LOW_SYSVARS)
 *
 * Copyright (C) 2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */
#ifndef SYSVAR_H
#define SYSVAR_H

#include "portab.h"

#if CONF_WITH_LOW_SYSVARS

/*
 * Bump-allocates `needed` bytes (16-byte aligned) of memory with a 32-bit
 * address, for a system variable whose address a 32-bit field (a cookie value)
 * hands out.  Implemented by the machine (bios/machine/<name>/); NULL if its
 * pool is exhausted, which for the few small variables allocated once at boot
 * cannot happen.
 */
void *sysvar_alloc(LONG needed);

/*
 * Reserves the storage of every such variable (vector_5ms in bios/vectors.h,
 * scsidriv_root in bios/scsidriv.h).  The machine calls it once its pool is
 * ready, before fill_cookie_jar() reads their addresses back for the cookies.
 */
void sysvars_alloc(void);

#endif /* CONF_WITH_LOW_SYSVARS */

#endif /* SYSVAR_H */
