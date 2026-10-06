/*
 * gdt.c - x86-64 kernel GDT and TSS
 *
 * Milestone 2 of the x86-64 port (issue #331): a kernel-mode GDT with the
 * code/data segments long mode needs, and a TSS.  UEFI firmware already
 * leaves the CPU in long mode with some GDT of its own, but that GDT (and
 * everything else firmware-owned) is unmapped once x86_64_build_page_tables()
 * runs (see startup.c) -- pTOS needs its own before it can safely take any
 * exception, since every IDT gate below references a segment selector into
 * this table.
 *
 * The TSS's ist1 slot points at a dedicated stack for #DF (double fault,
 * idt.c): #DF can be caused by the current stack itself being exhausted
 * or corrupt, and IST is exactly the mechanism that lets the CPU switch
 * to a known-good stack unconditionally on entry, rather than trying
 * (and failing) to push a frame onto the same broken one. Every other
 * gate still uses IST=0 (no switch), but does use rsp0 -- see gdt.h's own
 * comment on why that (unlike `syscall`) is still needed now that ring 3
 * code exists to fault. The remaining IST slots stay unused until #334
 * needs them.
 *
 * Also builds the ring-3 SYSRET descriptor triple `sysretq` needs (see
 * gdt.h's own comment on X86_64_USER32_CS_SEL_BASE) -- this milestone
 * (#333, folded into #349) only proves the entry/exit mechanism itself
 * with a throwaway in-kernel ring-3 harness, not real user processes
 * (#334's job), but the GDT layout is the same either way.
 *
 * Copyright (C) 2025-2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "portab.h"
#include "gdt.h"

/*
 * Minimal 64-bit code/data descriptors (OSDev-wiki style): base and limit
 * are both zero, which is fine because long mode ignores the base/limit
 * fields of code and data descriptors entirely (except FS/GS base, not
 * used here) -- only the present, type and L (long-mode code) bits matter.
 *
 * Byte layout of one 8-byte descriptor, low to high: limit[0:15],
 * base[0:15], base[16:23], access, flags|limit[16:19], base[24:31].
 * access = P(1) DPL(2) S(1) Type(4); flags = AVL(1) L(1) D/B(1) G(1).
 */
#define GDT_NULL         0x0000000000000000ULL
#define GDT_KERNEL_CODE  0x00209A0000000000ULL /* P DPL0 S=1 code,X,R; L=1 */
#define GDT_KERNEL_DATA  0x0000920000000000ULL /* P DPL0 S=1 data,W */
/* DPL=3 versions of the above, for ring-3 code/data -- see gdt.h's own
 * comment on why USER32_CS_SEL_BASE's own slot (the first of the three)
 * is left unused rather than a real descriptor. */
#define GDT_USER32_UNUSED 0x0000000000000000ULL
#define GDT_USER_DATA    0x0000F20000000000ULL /* P DPL3 S=1 data,W */
#define GDT_USER_CODE    0x0020FA0000000000ULL /* P DPL3 S=1 code,X,R; L=1 */

/* x86-64 Task State Segment (Intel SDM Vol 3A 8.7 "Task-State Segments"),
 * IA-32e mode format: no I/O-permission-bitmap fields other than the base
 * offset are used here, so iomap_base is set beyond the TSS's own limit,
 * which disables the I/O bitmap entirely (there is no ring 3 code yet to
 * apply it to). */
typedef struct {
    ULONG reserved0;
    UQUAD rsp0;
    UQUAD rsp1;
    UQUAD rsp2;
    UQUAD reserved1;
    UQUAD ist1;
    UQUAD ist2;
    UQUAD ist3;
    UQUAD ist4;
    UQUAD ist5;
    UQUAD ist6;
    UQUAD ist7;
    UQUAD reserved2;
    UWORD reserved3;
    UWORD iomap_base;
} PACKED tss_t;

static tss_t tss;

/* The #DF handler's dedicated stack (see the top-of-file comment). 4 KiB
 * is generous: the #DF path (isr.S's common trampoline, then
 * x86_64_exception_dispatch()) does not recurse and allocates nothing
 * beyond its own register-frame pushes and a handful of stack locals. */
#define DF_STACK_BYTES 4096
static UBYTE df_stack[DF_STACK_BYTES] __attribute__((aligned(16)));

/* rsp0's dedicated stack (see gdt.h's own comment on why ring-3 code
 * makes this necessary now). 8 KiB matches the headroom trap.c's own
 * syscall-entry kernel stack gives itself, for the same reason: this
 * path is x86_64_exception_dispatch() again, just reached via an
 * ordinary (non-IST) gate instead of #DF's. */
#define RSP0_STACK_BYTES 8192
static UBYTE rsp0_stack[RSP0_STACK_BYTES] __attribute__((aligned(16)));

/* One null, one code, one data, three ring-3 SYSRET descriptors (8 bytes
 * each) plus one TSS descriptor (16 bytes in long mode: gdt[6] and
 * gdt[7] together). */
static UQUAD gdt[8] __attribute__((aligned(16)));

typedef struct {
    UWORD limit;
    UQUAD base;
} PACKED dtr_t;

/* Encodes a 16-byte long-mode TSS descriptor (Intel SDM Vol 3A 8.2.3)
 * across gdt[6] (base[0:31], limit, access/flags -- the same shape as an
 * 8-byte descriptor, but with S=0 and Type=0x9, "64-bit TSS (available)")
 * and gdt[7] (base[32:63] in its low 32 bits, reserved above). */
static void set_tss_descriptor(UQUAD base, UWORD limit)
{
    gdt[6] = (UQUAD)limit
           | ((base & 0xFFFFFFULL) << 16)
           | (0x89ULL << 40)                          /* P DPL0 S=0 Type=0x9 */
           | (((UQUAD)(limit >> 16) & 0xFULL) << 48)
           | (((base >> 24) & 0xFFULL) << 56);
    gdt[7] = (base >> 32) & 0xFFFFFFFFULL;
}

static inline void lgdt(const dtr_t *gdtr)
{
    __asm__ volatile ("lgdt (%0)" :: "r"(gdtr) : "memory");
}

static inline void ltr(UWORD selector)
{
    __asm__ volatile ("ltr %0" :: "r"(selector));
}

/*
 * Reloads every segment register to reference the new GDT. CS cannot be
 * reloaded with a plain mov (there is no such instruction); the standard
 * technique in long mode is a far return to the very next instruction,
 * with the target CS pushed as if this were a callee returning to a
 * different code segment.  DS/ES/SS are reloaded to the flat data
 * selector -- long mode ignores their base/limit, but SS still requires a
 * present, correctly-typed descriptor for privileged instructions like
 * iretq to succeed -- and FS/GS are cleared, since nothing uses segment
 * bases yet.
 */
#define STR(x) #x
#define XSTR(x) STR(x)

static inline void reload_segments(void)
{
    /* Both selectors are embedded as asm-immediate literals (rather than
     * passed as "i" operands) so this needs no numbered operands at all,
     * which would otherwise have to survive across the far return. */
    __asm__ volatile (
        "pushq $" XSTR(X86_64_KERNEL_CODE_SEL) "\n\t"
        "leaq 1f(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"
        "lretq\n\t"
        "1:\n\t"
        "movw $" XSTR(X86_64_KERNEL_DATA_SEL) ", %%ax\n\t"
        "movw %%ax, %%ds\n\t"
        "movw %%ax, %%es\n\t"
        "movw %%ax, %%ss\n\t"
        "xorw %%ax, %%ax\n\t"
        "movw %%ax, %%fs\n\t"
        "movw %%ax, %%gs\n\t"
        ::: "rax", "memory"
    );
}

void x86_64_gdt_init(void)
{
    dtr_t gdtr;

    gdt[0] = GDT_NULL;
    gdt[1] = GDT_KERNEL_CODE;
    gdt[2] = GDT_KERNEL_DATA;
    gdt[3] = GDT_USER32_UNUSED;
    gdt[4] = GDT_USER_DATA;
    gdt[5] = GDT_USER_CODE;
    set_tss_descriptor((UQUAD)(uintptr_t)&tss, sizeof(tss) - 1);

    tss.iomap_base = sizeof(tss);
    tss.ist1 = (UQUAD)(uintptr_t)&df_stack[DF_STACK_BYTES];
    tss.rsp0 = (UQUAD)(uintptr_t)&rsp0_stack[RSP0_STACK_BYTES];

    gdtr.limit = sizeof(gdt) - 1;
    gdtr.base = (UQUAD)(uintptr_t)gdt;
    lgdt(&gdtr);

    reload_segments();
    ltr(X86_64_TSS_SEL);
}
