/*
 * cmdasm.c - EmuCON startup code (x86-64)
 *
 * The generic cli/cmdasm.S implements coma_start() in m68k assembler
 * (basepage arrives in a register-passing convention only that trap
 * dispatch mechanism used); ARM's cli/arch/arm/cmdasm.S does the same
 * with its own SVC-based convention. Neither is reusable here: this
 * arch's real gouser()/termuser() coroutine switch (bdos/arch/x86_64/
 * rwa.c, not yet implemented -- see that file's own comment) will
 * eventually "return" into a launched process's entry point the same
 * way a plain SysV function call would, with the basepage pointer in
 * %rdi -- exactly a C function's first argument. So, unlike the other
 * two archs, this needs no assembler at all: an ordinary C function with
 * one PD * parameter already has the right ABI.
 *
 * Copyright (C) 2025-2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

/*
 * Deliberately not cli/cmd.h: that header duplicates bdosdefs.h/
 * bdosbind.h's declarations under its own names (for the STANDALONE_
 * CONSOLE build, which has no kernel headers to include), and including
 * both here redefines every GEMDOS macro and conflicts on DTA's struct
 * tag. cli/cmdmain.c and friends use cmd.h; this file, like the m68k/ARM
 * cmdasm.S it replaces, only needs the kernel's own headers.
 */
#include "config.h"
#include "portab.h"
#include "bdosdefs.h"
#include "bdosbind.h"

char *environment;      /* cli/cmd.h: "from cmdasm.S" on m68k/ARM; this
                          * is that same storage's x86-64 equivalent */

int cmdmain(void);      /* cmdmain.c: "called only from cmdasm.S" */

void coma_start(PD *bp) NORETURN;

void coma_start(PD *bp)
{
    LONG newsize;

    /* Save the environment string pointer from the basepage before the
     * Mshrink() call below reuses/frees anything else in it. */
    environment = (char *)USERPTR_TO_PTR(bp->p_env);

    /* Mshrink to the needed size: TEXT+DATA+BSS plus the basepage itself.
     *
     * `sizeof(PD)` here, not a hardcoded byte count: this matches the
     * shared, cross-arch process loader's own convention for exactly
     * this same computation (bdos/proc.c's `needed = h01_tlen + h01_dlen
     * + h01_blen + sizeof(PD)`), which already relies on `struct _pd`'s
     * real per-arch compiled size rather than assuming a fixed value.
     * On m68k/ARM (ILP32) that size happens to be 256, matching
     * cli/cmdasm.S's and cli/arch/arm/cmdasm.S's own hardcoded
     * 256/SIZEOF_PD -- those files are raw assembly with no `sizeof()`
     * to compute it from, so they carry a manually-maintained constant
     * instead, kept in sync by hand.
     *
     * On this arch (LP64), `struct _pd`'s eight pointer-typed fields are
     * 8 bytes each instead of 4, so `sizeof(PD)` is larger than 256 --
     * deliberately: this is this port's own native basepage layout for
     * a process built and run natively for the kernel's own LP64 ABI,
     * which is what `bp` is today (there is no real x32/ILP32 process
     * yet -- gouser() itself is NI, so this function has never actually
     * run). It is NOT yet the fixed 256-byte, 32-bit-pointer basepage
     * layout a genuine ELFCLASS32 x32-ABI TOS-compatible executable
     * would need to see at these same field offsets -- reconciling the
     * two (a native LP64 struct here vs. an ILP32-compatible one a real
     * user process's own compiled code expects) is exactly the
     * undecided design question #334 owns, not something this file can
     * safely guess at ahead of that. */
    newsize = bp->p_tlen + bp->p_dlen + bp->p_blen + sizeof(PD);
    Mshrink(bp, newsize);

    cmdmain();

    Pterm0();

    /* Not reached: Pterm0() never returns. */
    for (;;)
        ;
}
