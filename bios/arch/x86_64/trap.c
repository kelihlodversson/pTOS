/*
 * trap.c - x86-64 GEMDOS/BIOS/XBIOS trap dispatch
 *
 * Copyright (C) 2025-2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "portab.h"
#include "gdt.h"
#include "io.h"
#include "trap.h"

/*
 * osif() (bdos/bdosmain.c) is GEMDOS's own C-level trap #1 handler,
 * declared there to take a uniform LONG pw[] on this arch (see
 * bdosmain.c's own comment on the #if defined(__arm__) ||
 * defined(__x86_64__) split) -- not in any shared header, so declared
 * here exactly as ARM's bdos/arch/arm/rwa.S implicitly relies on it too.
 *
 * bios_vecs[]/xbios_vecs[] (bios/bios.c, bios/xbios.c) are the BIOS/XBIOS
 * opcode-indexed function tables every other arch's own trap dispatcher
 * (bios/arch/m68k/vectors.S's biosxbios, ARM's arm_dispatch_svc via
 * VEC_BIOS/VEC_XBIOS) already calls into the same way: index by function
 * number, call through the generic PFLONG type with however many real
 * arguments that call actually needs -- safe and already this
 * codebase's own established convention, since a callee only ever reads
 * the argument registers its own real signature declares.
 */
extern long osif(LONG *pw);
extern const PFLONG bios_vecs[];
extern const UWORD bios_ent;
extern const PFLONG xbios_vecs[];
extern const UWORD xbios_ent;

extern void x86_64_syscall_entry(void);

/* IA32_EFER/STAR/LSTAR/FMASK (Intel SDM Vol 2B "SYSCALL"/"SYSRET"; Vol 4
 * 2.1 for the MSR indices). */
#define MSR_EFER 0xC0000080UL
#define MSR_STAR 0xC0000081UL
#define MSR_LSTAR 0xC0000082UL
#define MSR_FMASK 0xC0000084UL

#define EFER_SCE 0x1ULL /* SYSCALL/SYSRET enable */

void x86_64_trap_dispatch(x86_64_trap_frame_t *frame)
{
    UQUAD trap_class = frame->rax >> 32;
    ULONG fn = (ULONG)frame->rax;

    switch (trap_class) {
    case X86_64_TRAP_GEMDOS: {
        LONG pw[4];

        pw[0] = (LONG)fn;
        pw[1] = (LONG)frame->rdi;
        pw[2] = (LONG)frame->rsi;
        pw[3] = (LONG)frame->rdx;
        frame->rax = (UQUAD)osif(pw);
        break;
    }
    case X86_64_TRAP_BIOS:
        /* Out-of-range returns the function number itself as the result
         * -- matching bios/arch/m68k/vectors.S's biosxbios and ARM's
         * equivalent, both of which use the same convention. */
        if (fn >= bios_ent)
            frame->rax = fn;
        else
            frame->rax = (UQUAD)((LONG (*)(UQUAD, UQUAD, UQUAD, UQUAD))bios_vecs[fn])
                             (frame->rdi, frame->rsi, frame->rdx, frame->r10);
        break;
    case X86_64_TRAP_XBIOS:
        if (fn >= xbios_ent)
            frame->rax = fn;
        else
            frame->rax = (UQUAD)((LONG (*)(UQUAD, UQUAD, UQUAD, UQUAD))xbios_vecs[fn])
                             (frame->rdi, frame->rsi, frame->rdx, frame->r10);
        break;
    default:
        frame->rax = (UQUAD)-1L;
        break;
    }
}

void x86_64_trap_init(void)
{
    UQUAD efer = x86_64_rdmsr(MSR_EFER);

    x86_64_wrmsr(MSR_EFER, efer | EFER_SCE);

    /*
     * STAR[47:32] is both the kernel CS `syscall` loads directly and the
     * base of the kernel SS it loads as that value + 8 -- exactly
     * X86_64_KERNEL_CODE_SEL immediately followed by X86_64_KERNEL_DATA_SEL
     * in this GDT already (gdt.c), so no dedicated syscall segments are
     * needed. STAR[63:48] (the SYSRET/user side) is deliberately left 0:
     * nothing here ever executes sysretq yet (see trap.h), and there are
     * no ring-3 GDT entries for it to name regardless.
     */
    x86_64_wrmsr(MSR_STAR, (UQUAD)X86_64_KERNEL_CODE_SEL << 32);

    /*
     * Not a plain C `(UQUAD)(uintptr_t)x86_64_syscall_entry`: taking the
     * address of an extern symbol that way lets the compiler pick
     * GOT-indirected addressing (`mov x86_64_syscall_entry@GOTPCREL(%rip),
     * %reg`, R_X86_64_REX_GOTPCRELX) for a symbol it can't prove is local
     * at compile time -- normally harmless (GNU ld's ELF backend relaxes
     * it back to a direct `lea` when linking a static executable), but
     * this image's objects are ELF while the final link is PE
     * (`ld -m i386pep`, see the X86_64_LD comment in the top level
     * Makefile), whose backend does not perform that relaxation or
     * otherwise populate a GOT: the load reads whatever unrelated bytes
     * happen to sit at that spot instead of a real address, and `syscall`
     * later jumps straight into that garbage. Forcing `lea` here (which
     * `-fpie` never routes through the GOT) sidesteps the whole class,
     * and -- being RIP-relative -- self-adjusts to wherever this code is
     * actually executing, so unlike idt.c's exception_stub[] (compile-time
     * data fixed to the pre-relocation load address) this needs no
     * x86_64_low_to_high() translation even though it also runs
     * post-relocation.
     */
    {
        UQUAD entry_addr;

        __asm__("lea x86_64_syscall_entry(%%rip), %0" : "=r"(entry_addr));
        x86_64_wrmsr(MSR_LSTAR, entry_addr);
    }

    /* Cleared in RFLAGS on syscall entry: IF (bit 9), so a trap handler
     * is never itself interrupted -- consistent with interrupts already
     * being off for the whole of this milestone (startup.c/panic.c). */
    x86_64_wrmsr(MSR_FMASK, 0x200);
}
