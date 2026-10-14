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
#include "biosext.h"
#include "delay.h"
#include "asm.h"
#include "vectors.h"
#include "mfp.h"
#include "raspi_uart.h"

#define HZ                                200      // ticks per second

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

#define ARM_LOCAL (*((arm_local_t*)raspi_board.local_base))

#endif

#define ARM_IC_BASE             ( ARM_IO_BASE + 0xB200 )
#define ARM_SYSTIMER_BASE       ( ARM_IO_BASE + 0x3000 )

#define ARM_IC (*((arm_interrupt_controller_t*)ARM_IC_BASE))
#define ARM_SYSTIMER (*((arm_systimer_t*)ARM_SYSTIMER_BASE))

#define ARM_IRQ_MASK(irq) (1 << ((irq) & (ARM_IRQS_PER_REG-1)))

#if defined(TARGET_RPI4)

static ULONG ticks_per_hz;

#endif

#if !defined(TARGET_RPI4)

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
        assert(0);
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
        assert(0);
#else
        ARM_LOCAL.timer_int_control[0] &= ~(1 << (num-ARM_IRQLOCAL_BASE));
#endif
    }
}

#endif /* !TARGET_RPI4 */

void raspi_timer3_handler(void)
{
#if defined(TARGET_RPI4)
    ULONG cntp_cval_low, cntp_cval_high;
    UQUAD cntp_cval;
#else
    ULONG compare;
#endif

#if CONF_SERIAL_CONSOLE && CONF_WITH_RASPI_UART0
    raspi_uart0_poll_rx();
#endif

    vector_5ms();

#if defined(TARGET_RPI4)
    asm volatile ("mrrc p15, 2, %0, %1, c14" : "=r" (cntp_cval_low),
                                               "=r" (cntp_cval_high));
    cntp_cval = ((UQUAD) cntp_cval_high << 32 | cntp_cval_low) + ticks_per_hz;
    asm volatile ("mcrr p15, 2, %0, %1, c14" :: "r" (cntp_cval & 0xffffffffU),
                                                "r" (cntp_cval >> 32));
#else
    peripheral_begin();
    compare = ARM_SYSTIMER.compare[3] + CLOCKHZ / HZ;
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

// int_timerc(), the Timer C interrupt handler this machine's tick drives
// through vector_5ms, is machine-independent and lives in
// bios/arch/arm/vectors.c, shared by every ARM machine.

void raspi_interrupt_init(void)
{
#if defined(TARGET_RPI4)
    raspi_gic_init();
#else
    // peripheral_begin();
    ARM_IC.fiq_control          = 0;
    ARM_IC.disable_irqs_1       = (ULONG) -1;
    ARM_IC.disable_irqs_2       = (ULONG) -1;
    ARM_IC.disable_basic_irqs   = (ULONG) -1;
#endif
}


void raspi_init_system_timer(void)
{
#if defined(TARGET_RPI4)
    ULONG cntpct_low, cntpct_high;
    ULONG cntfrq;
    UQUAD cntp_cval;

    asm volatile ("mrc p15, 0, %0, c14, c0, 0" : "=r" (cntfrq)); /* CNTFRQ */
    ticks_per_hz = cntfrq / HZ;

    raspi_gic_connect_irq(30, raspi_timer3_handler);

    asm volatile ("mrrc p15, 0, %0, %1, c14" : "=r" (cntpct_low),
                                                "=r" (cntpct_high));
    cntp_cval = ((UQUAD) cntpct_high << 32 | cntpct_low) + ticks_per_hz;
    asm volatile ("mcrr p15, 2, %0, %1, c14" :: "r" (cntp_cval & 0xffffffffU),
                                                 "r" (cntp_cval >> 32)); /* CNTP_CVAL */
    asm volatile ("mcr p15, 0, %0, c14, c2, 1" :: "r" (1)); /* CNTP_CTL: ENABLE */
    flush_prefetch_buffer();
#else
#if CPU_ARMV7
    if (raspi_board.timer_kind == RASPI_TIMER_GENERIC)
    {
        ULONG cntpct_low, cntpct_high;
        ULONG cnt_frq, prescaler;
        UQUAD cntp_cval;

        // Use the physical counter of the ARM generic timer
        raspi_connect_irq (raspi_board.timer_irq, raspi_timer3_handler);

        asm volatile ("mrrc p15, 0, %0, %1, c14" : "=r" (cntpct_low),
                                                   "=r" (cntpct_high));

        cntp_cval = ((UQUAD) cntpct_high << 32 | cntpct_low) + CLOCKHZ / HZ;
        asm volatile ("mcrr p15, 2, %0, %1, c14" :: "r" (cntp_cval & 0xffffffffU),
                                                    "r" (cntp_cval >> 32));

        asm volatile ("mcr p15, 0, %0, c14, c2, 1" :: "r" (1));

        // The tick arithmetic above assumes the firmware programmed the
        // prescaler so that the counter advances at CLOCKHZ.
        asm volatile ("mrc p15, 0, %0, c14, c0, 0" : "=r" (cnt_frq));
        prescaler = ARM_LOCAL.prescaler;

        if (cnt_frq != raspi_board.timer_freq
            || prescaler != raspi_board.timer_prescaler)
        {
            panic("USE_PHYSICAL_COUNTER is not supported (freq %lu, pre 0x%lx)\n", cnt_frq, prescaler);
        }
        return;
    }
#endif

    ARM_SYSTIMER.count_lo = (ULONG) -(30 * CLOCKHZ);
    ARM_SYSTIMER.compare[3] = ARM_SYSTIMER.count_lo + CLOCKHZ / HZ;
    // peripheral_end();

    // Set up timer 3 interrupt to emulate the ST 200Hz timer
    raspi_connect_irq (raspi_board.timer_irq, raspi_timer3_handler);
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
#if defined(TARGET_RPI4)
    return raspi_gic_connect_irq(irq, handler);
#else
    PFVOID old_handler = raspi_irq_handlers[irq];
    raspi_irq_handlers[irq] = handler;

    // peripheral_begin();
    if (handler != NULL)
        enable_irq(irq);
    else
        disable_irq(irq);
    // peripheral_end();

    return old_handler;
#endif
}


void raspi_int_handler(void)
{
#if defined(TARGET_RPI4)
    raspi_gic_handle_irq();
#else
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
#endif
}
