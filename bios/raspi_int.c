#include "config.h"
#ifndef MACHINE_RPI
#error This file must only be compiled for raspberry PI targets
#endif

#include "portab.h"
#include "raspi_io.h"
#include "raspi_int.h"
#include "tosvars.h"
#include "ikbd.h"
#include "string.h"
#include "kprint.h"
#include "delay.h"
#include "asm.h"
#include "vectors.h"
#include "mfp.h"

#define HZ                                200      // ticks per second
#define CLOCKHZ                        1000000     // Sytem timer runs at 1MHz

typedef struct {
        volatile ULONG irq_basic_pending;
        volatile ULONG irq_pending_1;
        volatile ULONG irq_pending_2;
        volatile ULONG fiq_control;
        volatile ULONG enable_irqs_1;
        volatile ULONG enable_irqs_2;
        volatile ULONG enable_basic_irqs;
        volatile ULONG disable_irqs_1;
        volatile ULONG disable_irqs_2;
        volatile ULONG disable_basic_irqs;
} arm_interrupt_controller_t;

typedef struct {
    volatile ULONG control;
    volatile ULONG count_lo;
    volatile ULONG count_hi;
    volatile ULONG compare[4];
} arm_systimer_t;

#ifndef TARGET_RPI1

#ifdef TARGET_RPI4
#define ARM_LOCAL_BASE 0xff800000
#else
#define ARM_LOCAL_BASE 0x40000000
#endif 

typedef struct {
    volatile ULONG control;
    volatile ULONG res0;
    volatile ULONG prescaler;
    volatile ULONG gpu_int_routing;
    struct {
        volatile ULONG set;
        volatile ULONG clear;
    } pm_routing;
    volatile ULONG res1;
    struct {
        volatile ULONG ls;
        volatile ULONG ms;
    } timer;
    volatile ULONG int_routing;
    volatile ULONG res2;
    volatile ULONG axi_count;
    volatile ULONG axi_irq;
    volatile ULONG timer_control;
    volatile ULONG timer_write;
    volatile ULONG res3;
    volatile ULONG timer_int_control[4];
    volatile ULONG mailbox_int_control[4];
    volatile ULONG irq_pending[4];
    volatile ULONG fiq_pending[4];
    volatile ULONG mailbox_set0[4];
    volatile ULONG mailbox_set1[4];
    volatile ULONG mailbox_set2[4];
    volatile ULONG mailbox_set3[4];
    volatile ULONG mailbox_clr0[4];
    volatile ULONG mailbox_clr1[4];
    volatile ULONG mailbox_clr2[4];
    volatile ULONG mailbox_clr3[4];
} arm_local_t;

// Bit values for the arm local control var
#define CTRL_TIMER_INCREMENT (1 << 8) // If set, increment timer by two, else one
#define CTRL_PROC_CLK_TIMER (1 << 7) // 1=AXI/APB clock, 0 = crystal clock
#define CTRL_AXIERRIRW_EN (1 << 6) // 1 to mask AXI error interrupt

#define ARM_LOCAL (*((arm_local_t*)ARM_LOCAL_BASE))

#endif

#define ARM_IC_BASE             ( ARM_IO_BASE + 0xB200 )
#define ARM_SYSTIMER_BASE       ( ARM_IO_BASE + 0x3000 )

#define ARM_IC (*((arm_interrupt_controller_t*)ARM_IC_BASE))
#define ARM_SYSTIMER (*((arm_systimer_t*)ARM_SYSTIMER_BASE))

#define ARM_IRQ_MASK(irq) (1 << ((irq) & (ARM_IRQS_PER_REG-1)))

static PFVOID raspi_irq_handlers[IRQ_LINES];

static inline void enable_irq(int num)
{
    if (num < ARM_IRQ2_BASE)
        ARM_IC.enable_irqs_1 = ARM_IRQ_MASK(num);
    else if ( num < ARM_IRQBASIC_BASE)
        ARM_IC.enable_irqs_2 = ARM_IRQ_MASK(num);
    else if ( num < ARM_IRQLOCAL_BASE)
        ARM_IC.enable_basic_irqs = ARM_IRQ_MASK(num);
    else 
    {
#ifdef TARGET_RPI1 
        ASSERT(0);
#else
        ARM_LOCAL.timer_int_control[0] |= (1 << (num-ARM_IRQLOCAL_BASE));
#endif
    }
}

static inline void disable_irq(int num)
{
    if (num < ARM_IRQ2_BASE)
        ARM_IC.disable_irqs_1 = ARM_IRQ_MASK(num);
    else if ( num < ARM_IRQBASIC_BASE)
        ARM_IC.disable_irqs_2 = ARM_IRQ_MASK(num);
    else if ( num < ARM_IRQLOCAL_BASE)
        ARM_IC.disable_basic_irqs = ARM_IRQ_MASK(num);
    else 
    {
#ifdef TARGET_RPI1 
        ASSERT(0);
#else
        ARM_LOCAL.timer_int_control[0] &= ~(1 << (num-ARM_IRQLOCAL_BASE));
#endif
    }
}

void raspi_timer3_handler(void)
{
    vector_5ms();
#ifdef TARGET_RPI4
    ULONG cntp_cval_low, cntp_cval_high;
  	asm volatile ("mrrc p15, 2, %0, %1, c14" : "=r" (cntp_cval_low), "=r" (cntp_cval_high));
    UQUAD cntp_cval = ((UQUAD) cntp_cval_high << 32 | cntp_cval_low) + CLOCKHZ / HZ;
	asm volatile ("mcrr p15, 2, %0, %1, c14" :: "r" (cntp_cval & 0xffffffffU),
						    "r" (cntp_cval >> 32));    
  
#else
    peripheral_begin();
    ULONG compare = ARM_SYSTIMER.compare[3] + CLOCKHZ / HZ;
    ARM_SYSTIMER.compare[3] = compare;

    if (compare < ARM_SYSTIMER.count_lo)
    {
        compare = ARM_SYSTIMER.count_lo + CLOCKHZ / HZ;
        ARM_SYSTIMER.compare[3] = compare;
    }
    ARM_SYSTIMER.control = (1 << 3);
    peripheral_end();
#endif
}

#if WITH_USB
extern void usb_mouse_timerc (void);
#endif
extern void int_vbl(void);

// ==== Timer C interrupt handler ============================================
void int_timerc(void)
{
    hz_200++;
    timer_c_sieve = (timer_c_sieve << 1) | (timer_c_sieve >> 15);
    if (timer_c_sieve & 4) // If the highest bit in any nybble is 1, we are in the 4th call
    {
        kb_timerc_int();
#       if CONF_WITH_YM2149
            sndirq();   // dosound support
#       endif
#       if WITH_USB
            usb_mouse_timerc();
#       endif

        // Fake vbl interrupt every 4 timer_c calls (50Hz)
        int_vbl();
    }
}

void raspi_interrupt_init(void)
{
    // peripheral_begin();
    ARM_IC.fiq_control          = 0;
    ARM_IC.disable_irqs_1       = (ULONG) -1;
    ARM_IC.disable_irqs_2       = (ULONG) -1;
    ARM_IC.disable_basic_irqs   = (ULONG) -1;
}


void raspi_init_system_timer(void)
{
#ifdef TARGET_RPI4 
    // Use physical counter on Raspberry 4
    raspi_connect_irq (ARM_IRQLOCAL0_CNTPNS, raspi_timer3_handler);

    ULONG cntpct_low, cntpct_high;
	asm volatile ("mrrc p15, 0, %0, %1, c14" : "=r" (cntpct_low), "=r" (cntpct_high));

	UQUAD cntp_cval = ((UQUAD) cntpct_high << 32 | cntpct_low) + CLOCKHZ / HZ;
	asm volatile ("mcrr p15, 2, %0, %1, c14" :: "r" (cntp_cval & 0xffffffffU),
						    "r" (cntp_cval >> 32));

	asm volatile ("mcr p15, 0, %0, c14, c2, 1" :: "r" (1));    

	ULONG cnt_frq;
	asm volatile ("mrc p15, 0, %0, c14, c0, 0" : "=r" (cnt_frq));

	ULONG prescaler = ARM_LOCAL.prescaler;
    
#if defined(TARGET_RPI2) || defined(TARGET_RPI3)
    if (cnt_frq != 19200000 || prescaler != 0x6AAAAAB)
#else
    if (cnt_frq != 54000000 || prescaler != 39768216U)
#endif
    {
        panic("USE_PHYSICAL_COUNTER is not supported (freq %lu, pre 0x%lx)\n", cnt_frq, prescaler);
    }
#else
    ARM_SYSTIMER.count_lo = (ULONG) -(30 * CLOCKHZ);
    ARM_SYSTIMER.compare[3] = ARM_SYSTIMER.count_lo + CLOCKHZ / HZ;
    // peripheral_end();

    // Set up timer 3 interrupt to emulate the ST 200Hz timer
    raspi_connect_irq (ARM_IRQ_TIMER3, raspi_timer3_handler);
#endif
}

ULONG raspi_get_ticks(void)
{
    return ARM_SYSTIMER.count_lo;
}

ULONG raspi_get_timer(ULONG base)
{
    return (ARM_SYSTIMER.count_lo / (CLOCKHZ / 1000)) - base;
}

void raspi_delay_us(ULONG us)
{
    if (us > 0)
    {
        ULONG ticks = us * (CLOCKHZ / 1000000) + 1;
        ULONG start = raspi_get_ticks();
        while (raspi_get_ticks() - start < ticks)
        {
            // wait
        }
    }
}

PFVOID raspi_connect_irq(int irq, PFVOID handler)
{
    PFVOID old_handler = raspi_irq_handlers[irq];
    raspi_irq_handlers[irq] = handler;

    // peripheral_begin();
    if (handler != NULL)
        enable_irq(irq);
    else
        disable_irq(irq);
    // peripheral_end();

    return old_handler;
}


void raspi_int_handler(void)
{
    int reg, irq;
    ULONG curr;
#ifndef TARGET_RPI1
    ULONG pending_local = ARM_LOCAL.irq_pending[0];
    irq = ARM_IRQLOCAL_BASE;
    while(pending_local != 0) {
        if(pending_local & 1) {
            if(raspi_irq_handlers[irq] != NULL)
            {
                raspi_irq_handlers[irq]();
                return;
            }
            else 
            {
                disable_irq(irq);
            }
        }
        pending_local >>= 1;
        irq ++;
    }
#endif
    // peripheral_begin();

    ULONG pending[3];
    pending[0] = ARM_IC.irq_pending_1;
    pending[1] = ARM_IC.irq_pending_2;
    pending[2] = ARM_IC.irq_basic_pending & 0xFF;

    // peripheral_end();

    for (reg = 0; reg < 3; reg++)
    {
        curr = pending[reg];
        irq = reg * ARM_IRQS_PER_REG;
        while (curr != 0)
        {
            if (curr & 1)
            {
                if (raspi_irq_handlers[irq] != NULL)
                {
                    raspi_irq_handlers[irq]();
                    return;
                }
                else
                {
                    disable_irq(irq);
                }
            }
            curr >>= 1;
            irq++;
        }
    }
}
