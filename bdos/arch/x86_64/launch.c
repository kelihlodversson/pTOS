/*
 * launch.c - the first-run state of a GEMDOS process (x86-64)
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

void proc_align_stack(PD *p)
{
    /*
     * p_hitpa becomes gouser()'s own initial ring-3 RSP directly
     * (bdos/arch/x86_64/rwa.c), landing sp at exactly p_hitpa before the
     * process's first instruction ever runs. The x86-64 SysV/x32 ABI's
     * process-entry convention requires (RSP + 8) to be a multiple of
     * 16 at that point (equivalently, RSP itself is 8 mod 16) -- p itself
     * comes from a page-aligned allocator (procmem_alloc()),
     * but max (the process's own TPA size, ultimately from arbitrary
     * ELF segment sizes) is not, so p+max need not satisfy this (#356's
     * own review caught it: the first `call` in a real process can run
     * with the wrong alignment, breaking anything that assumes the
     * standard entry convention). Round down to the largest value
     * satisfying it; losing at most 15 bytes of TPA is harmless. */
    p->p_hitpa = ((p->p_hitpa + 8UL) & ~15UL) - 8UL;
}

void proc_launch_prepare(PD *p, void *super_sp)
{
    /*
     * Nothing to build: gouser() (rwa.c) enters ring 3 from the process's
     * own record (entry point, stack, address space) instead of unwinding a
     * saved register stack.
     */
    (void)p;
    (void)super_sp;
}
