/*
 * goldfish_tty.h - access to the Goldfish TTY on QEMU's m68k 'virt'
 * machine
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef GOLDFISH_TTY_H
#define GOLDFISH_TTY_H

#ifdef MACHINE_VIRT_M68K

BOOL goldfish_tty_can_write(void);
void goldfish_tty_write_byte(UBYTE b);
BOOL goldfish_tty_can_read(void);
UBYTE goldfish_tty_read_byte(void);

#endif /* MACHINE_VIRT_M68K */

#endif /* GOLDFISH_TTY_H */
