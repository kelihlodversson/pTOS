/*
 * idt.c - x86-64 interrupt descriptor table
 *
 * Copyright (C) 2025-2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "portab.h"
#include "gdt.h"
#include "idt.h"
#include "io.h"

#define IDT_ENTRIES 256

/* Interrupt-gate descriptor (Intel SDM Vol 3A 6.14.1). Using interrupt
 * gates rather than trap gates means the CPU clears IF on entry: harmless
 * here since interrupts are already off for the whole of this milestone
 * (see startup.c and panic.c), and correct once device IRQs exist later,
 * so a handler is never itself interrupted unless it explicitly allows
 * it. */
typedef struct {
    UWORD offset_low;
    UWORD selector;
    UBYTE ist;        /* bits 0-2: IST index (0 = none); 3-7 reserved */
    UBYTE type_attr;  /* P(1) DPL(2) 0(1) Type(4); 0x8E = present, DPL0,
                        * 64-bit interrupt gate */
    UWORD offset_mid;
    ULONG offset_high;
    ULONG reserved;
} PACKED idt_entry_t;

typedef struct {
    UWORD limit;
    UQUAD base;
} PACKED dtr_t;

static idt_entry_t idt[IDT_ENTRIES] __attribute__((aligned(16)));

/* One entry stub per CPU exception vector, defined in isr.S. Vectors
 * 32-255 have no stub and stay absent from the IDT -- see idt.h. */
#define ISR(n) extern void x86_64_isr##n(void);
ISR(0)  ISR(1)  ISR(2)  ISR(3)  ISR(4)  ISR(5)  ISR(6)  ISR(7)
ISR(8)  ISR(9)  ISR(10) ISR(11) ISR(12) ISR(13) ISR(14) ISR(15)
ISR(16) ISR(17) ISR(18) ISR(19) ISR(20) ISR(21) ISR(22) ISR(23)
ISR(24) ISR(25) ISR(26) ISR(27) ISR(28) ISR(29) ISR(30) ISR(31)
#undef ISR

#define ISR(n) x86_64_isr##n
static void (*const exception_stub[32])(void) = {
    ISR(0),  ISR(1),  ISR(2),  ISR(3),  ISR(4),  ISR(5),  ISR(6),  ISR(7),
    ISR(8),  ISR(9),  ISR(10), ISR(11), ISR(12), ISR(13), ISR(14), ISR(15),
    ISR(16), ISR(17), ISR(18), ISR(19), ISR(20), ISR(21), ISR(22), ISR(23),
    ISR(24), ISR(25), ISR(26), ISR(27), ISR(28), ISR(29), ISR(30), ISR(31),
};
#undef ISR

static void set_gate(int vector, UQUAD addr, int ist)
{
    idt[vector].offset_low = (UWORD)(addr & 0xFFFF);
    idt[vector].selector = X86_64_KERNEL_CODE_SEL;
    idt[vector].ist = (UBYTE)ist;
    idt[vector].type_attr = 0x8E;
    idt[vector].offset_mid = (UWORD)((addr >> 16) & 0xFFFF);
    idt[vector].offset_high = (ULONG)(addr >> 32);
    idt[vector].reserved = 0;
}

static inline void lidt(const dtr_t *idtr)
{
    __asm__ volatile ("lidt (%0)" :: "r"(idtr) : "memory");
}

/* PIC1 command/data: 0x20/0x21; PIC2 command/data: 0xA0/0xA1 (the IBM
 * PC/AT legacy convention every PC-compatible chipset still honours).
 * Masking every line (writing 0xFF to both data ports) makes both chips
 * inert without needing to remap or otherwise program them at all: no
 * IRQ they see can ever reach the CPU. IF stays clear for the whole of
 * this milestone regardless (see startup.c/panic.c), so this is
 * belt-and-suspenders against whichever later sub-issue turns interrupts
 * back on before it has device IRQ handling ready for what it unmasks. */
static void mask_legacy_pic(void)
{
    x86_64_outb(0x21, 0xFF);
    x86_64_outb(0xA1, 0xFF);
}

void x86_64_idt_init(void)
{
    dtr_t idtr;
    int i;

    /*
     * exception_stub[] is compile-time-initialized data: each entry was a
     * low address the PE loader's relocations fixed up once at load time,
     * but startup.c's x86_64_apply_higher_half_relocations() (#343) has
     * since re-applied the same relocation table a second time, for the
     * higher-half bias, so every entry here already holds its higher-half
     * virtual address by the time this runs -- no further translation
     * needed (or correct: translating an already-translated pointer a
     * second time would double-apply the bias).
     */
    for (i = 0; i < 32; i++)
        set_gate(i, (UQUAD)(uintptr_t)exception_stub[i], i == 8 ? X86_64_DF_IST : 0);

    idtr.limit = sizeof(idt) - 1;
    idtr.base = (UQUAD)(uintptr_t)idt;
    lidt(&idtr);

    mask_legacy_pic();
}
