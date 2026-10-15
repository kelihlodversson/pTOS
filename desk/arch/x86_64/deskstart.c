/*
 * deskstart.c - x86-64 startup code of the desktop
 *
 * Ports desk/arch/arm/deskstart.S to this arch's own conventions: a
 * "kernel-code process" entry (bdos/arch/x86_64/rwa.c's gouser(), same
 * as cli/arch/x86_64/cmdasm.c's coma_start() and aes/arch/x86_64/
 * gemstart.c's ui_start()) -- an ordinary C function taking one PD *
 * parameter, needing no assembly at all. See cmdasm.c's own header
 * comment for why.
 *
 * Unlike ui_start()/accdesk_start() (aes/arch/x86_64/gemstart.c), this
 * one needs no stack switch of its own: it is reached via
 * aes_run_rom_program(deskstart) -> dos_exec() -> Pexec(PE_GOTHENFREE)
 * from *within* accdesk_start()'s own call chain (aes/gemshlib.c's own
 * "AES reentrancy" dos_exec() mechanism), which is already running on
 * AES process 0's UDA stack by that point -- deskmain() below runs on
 * whatever stack this function itself was entered on, same as ARM/m68k's
 * own deskstart, which never switches stacks either.
 *
 * Copyright (C) 2025-2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "emutos.h"
#include "obdefs.h"
#include "bdosbind.h"
#include "deskmain.h"

void deskstart(PD *p) NORETURN;

void deskstart(PD *p)
{
    LONG newsize;

    /* Mshrink to the needed size for TEXT+DATA+BSS plus the basepage
     * itself -- same computation as coma_start()'s/ui_start()'s own. */
    newsize = p->p_tlen + p->p_dlen + p->p_blen + sizeof(PD);
    Mshrink(p, newsize);

    deskmain();

    Pterm0();

    /* Not reached: Pterm0() never returns. */
    for (;;)
        ;
}
