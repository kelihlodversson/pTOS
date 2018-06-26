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

#define HZ		                200			// ticks per second
#define CLOCKHZ	                1000000     // Sytem timer runs at 1MHz

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
    else
        ARM_IC.enable_basic_irqs = ARM_IRQ_MASK(num);
}

static inline void disable_irq(int num)
{
    if (num < ARM_IRQ2_BASE)
        ARM_IC.disable_irqs_1 = ARM_IRQ_MASK(num);
    else if ( num < ARM_IRQBASIC_BASE)
        ARM_IC.disable_irqs_2 = ARM_IRQ_MASK(num);
    else
        ARM_IC.disable_basic_irqs = ARM_IRQ_MASK(num);
}

void raspi_timer3_handler(void)
{
    // peripheral_begin();
	ULONG compare = ARM_SYSTIMER.compare[3] + CLOCKHZ / HZ;
    ARM_SYSTIMER.compare[3] = compare;

	if (compare < ARM_SYSTIMER.count_lo)
	{
		compare = ARM_SYSTIMER.count_lo + CLOCKHZ / HZ;
		ARM_SYSTIMER.compare[3] = compare;
	}
    vector_5ms();

    ARM_SYSTIMER.control = (1 << 3);
    // peripheral_end();
}

// ==== Timer C interrupt handler ============================================

void int_timerc(void)
{
    hz_200++;
    timer_c_sieve = (timer_c_sieve << 1) | (timer_c_sieve >> 15);
    if (timer_c_sieve & 4) // If the highest bit in any nibble is 1, we are in the 4th call
    {
        kb_timerc_int();
#       if CONF_WITH_YM2149
            sndirq();   // dosound support
#       endif
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
    ARM_SYSTIMER.count_lo = (ULONG) -(30 * CLOCKHZ);
    ARM_SYSTIMER.compare[3] = ARM_SYSTIMER.count_lo + CLOCKHZ / HZ;
    // peripheral_end();

    // Set up timer 3 interrupt to emulate the ST 200Hz timer
    raspi_connect_irq (ARM_IRQ_TIMER3, raspi_timer3_handler);
}

ULONG raspi_get_ticks(void)
{
    return ARM_SYSTIMER.count_lo;
}

void raspi_delay_ms(ULONG ms)
{
    raspi_delay_us(ms * 1000);
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
