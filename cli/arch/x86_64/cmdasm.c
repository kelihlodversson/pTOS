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
    environment = bp->p_env;

    /* Mshrink to the needed size: TEXT+DATA+BSS plus the basepage itself
     * (sizeof(PD) == 256 == cli/cmdasm.S's (m68k) and cli/arch/arm/
     * cmdasm.S's own hardcoded 256/SIZEOF_PD). */
    newsize = bp->p_tlen + bp->p_dlen + bp->p_blen + sizeof(PD);
    Mshrink(bp, newsize);

    cmdmain();

    Pterm0();

    /* Not reached: Pterm0() never returns. */
    for (;;)
        ;
}
