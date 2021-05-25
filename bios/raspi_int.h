/*
 * raspi_int.h control of the raspberry PI interrupt controller
 *
 * Copyright (C) 2013-2017 The EmuTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef RASPI_INT_H
#define RASPI_INT_H

#ifdef MACHINE_RPI

void raspi_interrupt_init(void);
void raspi_init_system_timer(void);

PFVOID raspi_connect_irq(int irq, PFVOID handler);

void raspi_int_handler(void);
void raspi_timer3_handler(void);
ULONG raspi_get_ticks(void);
void raspi_delay_us(ULONG us);
ULONG raspi_get_timer(ULONG base);

extern inline void raspi_delay_ms(ULONG ms)
{
    raspi_delay_us(ms * 1000);
}

/*
 * The IRQ list is taken from Linux and is:
 *	Copyright (C) 2010 Broadcom
 *	Copyright (C) 2003 ARM Limited
 *	Copyright (C) 2000 Deep Blue Solutions Ltd.
 *	Licensed under GPL2
 * IRQs
 */
#define ARM_IRQS_PER_REG        32
#define ARM_IRQS_BASIC_REG	8
#if defined(TARGET_RPI1) 
#define ARM_IRQS_LOCAL_REG	0
#else
#define ARM_IRQS_LOCAL_REG	12
#endif

#define ARM_IRQ1_BASE           0
#define ARM_IRQ2_BASE           (ARM_IRQ1_BASE + ARM_IRQS_PER_REG)
#define ARM_IRQBASIC_BASE       (ARM_IRQ2_BASE + ARM_IRQS_PER_REG)
#define ARM_IRQLOCAL_BASE	(ARM_IRQBASIC_BASE + ARM_IRQS_BASIC_REG)

#define ARM_IRQ_TIMER0          (ARM_IRQ1_BASE + 0)
#define ARM_IRQ_TIMER1          (ARM_IRQ1_BASE + 1)
#define ARM_IRQ_TIMER2          (ARM_IRQ1_BASE + 2)
#define ARM_IRQ_TIMER3          (ARM_IRQ1_BASE + 3)
#define ARM_IRQ_CODEC0          (ARM_IRQ1_BASE + 4)
#define ARM_IRQ_CODEC1          (ARM_IRQ1_BASE + 5)
#define ARM_IRQ_CODEC2          (ARM_IRQ1_BASE + 6)
#define ARM_IRQ_JPEG            (ARM_IRQ1_BASE + 7)
#define ARM_IRQ_ISP             (ARM_IRQ1_BASE + 8)
#define ARM_IRQ_USB             (ARM_IRQ1_BASE + 9)
#define ARM_IRQ_3D              (ARM_IRQ1_BASE + 10)
#define ARM_IRQ_TRANSPOSER      (ARM_IRQ1_BASE + 11)
#define ARM_IRQ_MULTICORESYNC0  (ARM_IRQ1_BASE + 12)
#define ARM_IRQ_MULTICORESYNC1  (ARM_IRQ1_BASE + 13)
#define ARM_IRQ_MULTICORESYNC2  (ARM_IRQ1_BASE + 14)
#define ARM_IRQ_MULTICORESYNC3  (ARM_IRQ1_BASE + 15)
#define ARM_IRQ_DMA0            (ARM_IRQ1_BASE + 16)
#define ARM_IRQ_DMA1            (ARM_IRQ1_BASE + 17)
#define ARM_IRQ_DMA2            (ARM_IRQ1_BASE + 18)
#define ARM_IRQ_DMA3            (ARM_IRQ1_BASE + 19)
#define ARM_IRQ_DMA4            (ARM_IRQ1_BASE + 20)
#define ARM_IRQ_DMA5            (ARM_IRQ1_BASE + 21)
#define ARM_IRQ_DMA6            (ARM_IRQ1_BASE + 22)
#define ARM_IRQ_DMA7            (ARM_IRQ1_BASE + 23)
#define ARM_IRQ_DMA8            (ARM_IRQ1_BASE + 24)
#define ARM_IRQ_DMA9            (ARM_IRQ1_BASE + 25)
#define ARM_IRQ_DMA10           (ARM_IRQ1_BASE + 26)
#define ARM_IRQ_DMA11           (ARM_IRQ1_BASE + 27)
#define ARM_IRQ_DMA12           (ARM_IRQ1_BASE + 28)
#define ARM_IRQ_AUX             (ARM_IRQ1_BASE + 29)
#define ARM_IRQ_ARM             (ARM_IRQ1_BASE + 30)
#define ARM_IRQ_VPUDMA          (ARM_IRQ1_BASE + 31)

#define ARM_IRQ_HOSTPORT        (ARM_IRQ2_BASE + 0)
#define ARM_IRQ_VIDEOSCALER     (ARM_IRQ2_BASE + 1)
#define ARM_IRQ_CCP2TX          (ARM_IRQ2_BASE + 2)
#define ARM_IRQ_SDC             (ARM_IRQ2_BASE + 3)
#define ARM_IRQ_DSI0            (ARM_IRQ2_BASE + 4)
#define ARM_IRQ_AVE             (ARM_IRQ2_BASE + 5)
#define ARM_IRQ_CAM0            (ARM_IRQ2_BASE + 6)
#define ARM_IRQ_CAM1            (ARM_IRQ2_BASE + 7)
#define ARM_IRQ_HDMI0           (ARM_IRQ2_BASE + 8)
#define ARM_IRQ_HDMI1           (ARM_IRQ2_BASE + 9)
#define ARM_IRQ_PIXELVALVE1     (ARM_IRQ2_BASE + 10)
#define ARM_IRQ_I2CSPISLV       (ARM_IRQ2_BASE + 11)
#define ARM_IRQ_DSI1            (ARM_IRQ2_BASE + 12)
#define ARM_IRQ_PWA0            (ARM_IRQ2_BASE + 13)
#define ARM_IRQ_PWA1            (ARM_IRQ2_BASE + 14)
#define ARM_IRQ_CPR             (ARM_IRQ2_BASE + 15)
#define ARM_IRQ_SMI             (ARM_IRQ2_BASE + 16)
#define ARM_IRQ_GPIO0           (ARM_IRQ2_BASE + 17)
#define ARM_IRQ_GPIO1           (ARM_IRQ2_BASE + 18)
#define ARM_IRQ_GPIO2           (ARM_IRQ2_BASE + 19)
#define ARM_IRQ_GPIO3           (ARM_IRQ2_BASE + 20)
#define ARM_IRQ_I2C             (ARM_IRQ2_BASE + 21)
#define ARM_IRQ_SPI             (ARM_IRQ2_BASE + 22)
#define ARM_IRQ_I2SPCM          (ARM_IRQ2_BASE + 23)
#define ARM_IRQ_SDIO            (ARM_IRQ2_BASE + 24)
#define ARM_IRQ_UART            (ARM_IRQ2_BASE + 25)
#define ARM_IRQ_SLIMBUS         (ARM_IRQ2_BASE + 26)
#define ARM_IRQ_VEC             (ARM_IRQ2_BASE + 27)
#define ARM_IRQ_CPG             (ARM_IRQ2_BASE + 28)
#define ARM_IRQ_RNG             (ARM_IRQ2_BASE + 29)
#define ARM_IRQ_ARASANSDIO      (ARM_IRQ2_BASE + 30)
#define ARM_IRQ_AVSPMON         (ARM_IRQ2_BASE + 31)

#define ARM_IRQ_ARM_TIMER       (ARM_IRQBASIC_BASE + 0)
#define ARM_IRQ_ARM_MAILBOX     (ARM_IRQBASIC_BASE + 1)
#define ARM_IRQ_ARM_DOORBELL_0  (ARM_IRQBASIC_BASE + 2)
#define ARM_IRQ_ARM_DOORBELL_1  (ARM_IRQBASIC_BASE + 3)
#define ARM_IRQ_VPU0_HALTED     (ARM_IRQBASIC_BASE + 4)
#define ARM_IRQ_VPU1_HALTED     (ARM_IRQBASIC_BASE + 5)
#define ARM_IRQ_ILLEGAL_TYPE0   (ARM_IRQBASIC_BASE + 6)
#define ARM_IRQ_ILLEGAL_TYPE1   (ARM_IRQBASIC_BASE + 7)


#if defined(TARGET_RPI2) || defined(TARGET_RPI3) || defined(TARGET_RPI4)
#define ARM_IRQLOCAL0_CNTPS	(ARM_IRQLOCAL_BASE + 0)
#define ARM_IRQLOCAL0_CNTPNS	(ARM_IRQLOCAL_BASE + 1)
#define ARM_IRQLOCAL0_CNTHP	(ARM_IRQLOCAL_BASE + 2)
#define ARM_IRQLOCAL0_CNTV	(ARM_IRQLOCAL_BASE + 3)
#define ARM_IRQLOCAL0_MAILBOX0	(ARM_IRQLOCAL_BASE + 4)
#define ARM_IRQLOCAL0_MAILBOX1	(ARM_IRQLOCAL_BASE + 5)
#define ARM_IRQLOCAL0_MAILBOX2	(ARM_IRQLOCAL_BASE + 6)
#define ARM_IRQLOCAL0_MAILBOX3	(ARM_IRQLOCAL_BASE + 7)
#define ARM_IRQLOCAL0_GPU	(ARM_IRQLOCAL_BASE + 8)		// cascaded GPU interrupts
#define ARM_IRQLOCAL0_PMU 	(ARM_IRQLOCAL_BASE + 9)
#define ARM_IRQLOCAL0_AXI_IDLE	(ARM_IRQLOCAL_BASE + 10)	// on core 0 only
#define ARM_IRQLOCAL0_LOCALTIMER (ARM_IRQLOCAL_BASE + 11)
#endif

#define IRQ_LINES		(ARM_IRQS_PER_REG * 2 + ARM_IRQS_BASIC_REG + ARM_IRQS_LOCAL_REG)


#endif /* MACHINE_RPI */

#endif /* RASPI_INT_H */
