/*
 * goldfish_pic.h - Goldfish PIC interrupt routing for QEMU's m68k
 * 'virt' machine
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef GOLDFISH_PIC_H
#define GOLDFISH_PIC_H

#ifdef MACHINE_VIRT_M68K

void goldfish_pic_init(void);
void goldfish_pic_enable(WORD pic, WORD bit);

#endif /* MACHINE_VIRT_M68K */

#endif /* GOLDFISH_PIC_H */
