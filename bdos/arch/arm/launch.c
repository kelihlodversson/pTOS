/*
 * launch.c - the first-run state of a GEMDOS process (ARM)
 *
 * Copyright (C) 2025-2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "emutos.h"
#include "bdosdefs.h"
#include "fs.h"
#include "proc.h"
#include "asm.h"
#include "arch/arm/entry.h"

/* The stack gouser() (rwa.S) unwinds to start a new process. */
struct gouser_stack {
    LONG regs[8];    /* argument registers from previous call (r7, r0-r6) */
    LONG other_sp;   /* the other stack pointer */
    LONG retaddr;
    LONG spsr;       /* note the basepage is passed in r0 and not on the stack */
};

void proc_align_stack(PD *p)
{
    /*
     * p_hitpa becomes the actual initial user-mode sp of any process
     * launched from this PD: proc_go()'s gouser_stack is unwound entirely
     * by gouser() (bdos/arch/arm/rwa.S), landing sp at exactly p_hitpa, not
     * at a gouser_stack-relative offset. AAPCS requires sp to be 8-byte
     * aligned at every public interface, which gcc's auto-vectorizer
     * (-mfpu=neon-vfpv4) relies on, but the free-memory allocator this
     * pointer comes from (alloc_tpa()/ffit()/getmpb()) only guarantees
     * 4-byte alignment. Round down; losing at most 7 bytes of TPA is
     * harmless.
     */
    p->p_hitpa = (UBYTE *)((ULONG)p->p_hitpa & ~7UL);
}

void proc_launch_prepare(PD *p, void *super_sp)
{
    struct gouser_stack *sp;

    /* create a stack at the end of the TPA */
    sp = (struct gouser_stack *) (p->p_hitpa - sizeof(struct gouser_stack));

    p->p_dreg[0] = (LONG)p;  /* base page is passed in r0 */
    sp->regs[1] = ARM_ENTRY_PROGRAM;
    sp->spsr = ((get_cpsr() & ~0x1f) | 0x10); /* the process will start in user mode, same interrupts */
    sp->retaddr = (long)p->p_tbase; /* return address is text start */
    /* the other stack is the supervisor stack */
    sp->other_sp = (long) super_sp;
    /* store this new stack in the saved sp field of the PD */
    p->p_areg[7-3] = (long) sp;
}
