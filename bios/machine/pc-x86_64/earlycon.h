/*
 * earlycon.h - early PC COM1 debug console
 *
 * Copyright (C) 2025-2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef PC_X86_64_EARLYCON_H
#define PC_X86_64_EARLYCON_H

#include "portab.h"

void earlycon_init(void);
void earlycon_puts(const char *s);
void earlycon_puthex(UQUAD value);

#endif /* PC_X86_64_EARLYCON_H */
