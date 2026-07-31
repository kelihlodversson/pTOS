/*
 * goldfish_rtc.h - Goldfish RTC periodic tick for QEMU's m68k
 * 'virt' machine
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef GOLDFISH_RTC_H
#define GOLDFISH_RTC_H

#ifdef MACHINE_VIRT_M68K

void goldfish_rtc_init(void);

/* Called only from goldfish_rtc_isr.S's exception-vector entry point. */
void goldfish_rtc_service(void);

#endif /* MACHINE_VIRT_M68K */

#endif /* GOLDFISH_RTC_H */
