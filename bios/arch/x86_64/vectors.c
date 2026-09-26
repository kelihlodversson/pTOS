/*
 * vectors.c - exception vector table setup
 *
 * Copyright (C) 2018-2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#include "portab.h"
#include "vectors.h"

/*
 * Deliberate no-ops, unlike ARM's own bios/arch/arm/vectors.c: this arch
 * has real hardware exception vectors (the IDT, bios/arch/x86_64/idt.c)
 * installed well before bios_init() ever runs, so there is no simulated
 * m68k-style low-memory vector table to populate here -- nothing reads
 * one back (bios/arch/x86_64/trap.c's own comment on why the GEMDOS/BIOS/
 * XBIOS vectors specifically need no such table applies equally to the
 * generic CPU exception vectors: idt.c's dispatch always panics
 * unconditionally, never consulting this table for a Setexc()-installed
 * user handler, since no user program can install one yet). Revisit once
 * that changes.
 */
void init_exc_vec(void)
{
}

void init_user_vec(UWORD first_boot)
{
    (void)first_boot;
}

/*
 * vbclock/int_timerc(): bios/arch/arm/vectors.c's real equivalents count
 * and dispatch the periodic ~200 Hz tick (VBL emulation, keyboard
 * repeat/timeout, kbd_int_timerc()'s work). This arch has no working
 * timer or interrupt source yet (bios.c's biosmain() deliberately never
 * enables interrupts -- see its own comment), so
 * bios/mfp.c's init_system_timer() only ever stores int_timerc's address
 * into vector_5ms (a seam nothing on this arch invokes); vbclock is only
 * ever read as diagnostic/informational data (bdos/ssystem.c). Real
 * storage and an empty body are all that is needed until this arch gets
 * a working timer (#329's later milestones).
 */
volatile LONG vbclock;

void int_timerc(void)
{
}
