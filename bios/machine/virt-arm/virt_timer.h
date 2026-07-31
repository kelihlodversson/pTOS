/*
 * virt_timer.h - ARM generic timer periodic tick for QEMU's ARM
 * 'virt' machine
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef VIRT_TIMER_H
#define VIRT_TIMER_H

#ifdef MACHINE_VIRT_ARM

void virt_timer_init(void);

#endif /* MACHINE_VIRT_ARM */

#endif /* VIRT_TIMER_H */
