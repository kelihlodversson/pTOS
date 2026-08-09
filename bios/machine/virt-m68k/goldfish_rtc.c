/*
 * goldfish_rtc.c - Goldfish RTC periodic tick for QEMU's m68k
 * 'virt' machine
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#ifndef MACHINE_VIRT_M68K
#error This file must only be compiled for the QEMU m68k virt target
#endif

#include "portab.h"
#include "vectors.h"
#include "goldfish_pic.h"
#include "goldfish_rtc.h"
#if CONF_WITH_GOLDFISH_TTY
#include "goldfish_tty.h"
#endif

#define GOLDFISH_RTC_BASE  0xff006000UL   /* first of 2 instances; only this one is used */

#define RTC_TIME_LOW         (*(volatile ULONG*)(GOLDFISH_RTC_BASE + 0x00))
#define RTC_TIME_HIGH        (*(volatile ULONG*)(GOLDFISH_RTC_BASE + 0x04))
#define RTC_ALARM_LOW        (*(volatile ULONG*)(GOLDFISH_RTC_BASE + 0x08))
#define RTC_ALARM_HIGH       (*(volatile ULONG*)(GOLDFISH_RTC_BASE + 0x0c))
#define RTC_IRQ_ENABLED      (*(volatile ULONG*)(GOLDFISH_RTC_BASE + 0x10))
#define RTC_CLEAR_INTERRUPT  (*(volatile ULONG*)(GOLDFISH_RTC_BASE + 0x1c))

/* QEMU wires this RTC instance to PIC index 5 ("PIC #6"), bit 0 (see
 * hw/m68k/virt.c: VIRT_GF_RTC_IRQ_BASE = PIC_IRQ(6, 1)), which the m68k
 * IRQ controller (hw/intc/m68k_irqc.c) takes at CPU autovector level 6 --
 * VEC_LEVEL6 (bios/vectors.h), the same fixed sysvar address (0x78)
 * init_exc_vec() defaults to just_rte before this overwrites it. */
#define GOLDFISH_RTC_PIC_INDEX  5
#define GOLDFISH_RTC_PIC_BIT    0

#define TICK_NS  5000000UL   /* 5 ms = 200 Hz, matches the classic Atari timer C rate */

extern void goldfish_rtc_isr(void);   /* defined in goldfish_rtc_isr.S */

static void goldfish_rtc_arm_next(void)
{
    ULONG lo, hi, new_lo, new_hi;

    lo = RTC_TIME_LOW;    /* latches TIME_HIGH as a side effect (see the real device
                           * model's goldfish_rtc_read(), hw/rtc/goldfish_rtc.c) */
    hi = RTC_TIME_HIGH;

    new_lo = lo + TICK_NS;
    new_hi = hi + (new_lo < lo ? 1 : 0);   /* 64-bit carry */

    RTC_ALARM_HIGH = new_hi;   /* write high first: writing low is what arms the alarm */
    RTC_ALARM_LOW = new_lo;
}

/* Called from goldfish_rtc_isr.S, once per interrupt. */
void goldfish_rtc_service(void)
{
    RTC_CLEAR_INTERRUPT = 1;
    goldfish_rtc_arm_next();

#if CONF_SERIAL_CONSOLE && CONF_WITH_GOLDFISH_TTY
    goldfish_tty_poll_rx();
#endif
}

void goldfish_rtc_init(void)
{
    VEC_LEVEL6 = goldfish_rtc_isr;

    RTC_IRQ_ENABLED = 1;
    goldfish_rtc_arm_next();

    goldfish_pic_enable(GOLDFISH_RTC_PIC_INDEX, GOLDFISH_RTC_PIC_BIT);
}
