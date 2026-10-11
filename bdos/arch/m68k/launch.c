/*
 * launch.c - the first-run state of a GEMDOS process (m68k)
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

/*
 * The stack gouser() (rwa.S) unwinds to start a new process: the registers
 * it reloads, in the order it reads them.  See the excerpt of gouser() in
 * bdos/proc.c's comment on proc_go().
 */
struct gouser_stack {
  LONG other_sp;   /* a4, the other stack pointer */
  WORD sr;         /* d0, the status register */
  LONG retaddr;    /* a3, the return address */
  LONG fill[11];   /* 10 registers d1-d7/a0-a2 and one dummy so that ... */
  PD * basepage;   /* ... upon startup the basepage is in 4(sp) */
};

void proc_align_stack(PD *p)
{
    MAYBE_UNUSED(p);        /* nothing to align on m68k */
}

void proc_launch_prepare(PD *p, void *super_sp)
{
    struct gouser_stack *sp;

    /* create a stack at the end of the TPA */
    sp = (struct gouser_stack *) (p->p_hitpa - sizeof(struct gouser_stack));

    sp->basepage = p;      /* the stack contains the basepage */

    sp->retaddr = (long)p->p_tbase; /* return address a3 is text start */
    sp->sr = get_sr() & 0x0700;  /* the process will start in user mode, same IPL */

    /* the other stack is the supervisor stack */
    sp->other_sp = (long) super_sp;

    /* store this new stack in the saved a7 field of the PD */
    p->p_areg[7-3] = (long) sp;

    /* the following settings are not documented, and hence theoretically
     * the assignments below are not necessary.
     * However, many programs test if A0 = 0 to check if they are running
     * as a normal program or as an accessory, so we need to clear at least
     * this register!
     */
    {   /* d1-d7/a0-a2 and dummy return address set to zero */
        int i;
        for(i = 0; i < 11 ; i++)
            sp->fill[i] = 0;
    }
    p->p_areg[6-3] = (long) sp;    /* a6 to hold a copy of the stack */
    p->p_areg[5-3] = (long)p->p_dbase;  /* a5 to point to the DATA segt */
    p->p_areg[4-3] = (long)p->p_bbase;  /* a4 to point to the BSS segt */
}
