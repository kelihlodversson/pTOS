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

/* Raw byte-level I/O, for bios/serport.c's bconstat1()/bconin1()/
 * bcostat1()/bconout1() (CONF_WITH_PC_COM1) once CONF_SERIAL_CONSOLE
 * routes the BIOS console through this device -- unlike earlycon_puts(),
 * these do not translate '\n' to "\r\n". */
BOOL earlycon_can_read(void);
UBYTE earlycon_read_byte(void);
BOOL earlycon_can_write(void);
void earlycon_write_byte(UBYTE b);

#endif /* PC_X86_64_EARLYCON_H */
