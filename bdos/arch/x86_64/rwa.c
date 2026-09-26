/*
 * rwa.c - GEMDOS process-switch primitives (x86-64 stand-in)
 *
 * Every other arch's rwa.S (bdos/arch/{m68k,arm}/rwa.S) implements a
 * cooperative "stack-swapping coroutine": gouser()/termuser() switch the
 * live stack pointer between a parent's suspended kernel call chain
 * (frozen mid-Pexec) and a child's, using a per-arch struct gouser_stack
 * (bdos/proc.c) laid out to match that arch's own trap-entry register
 * save area. Doing the same correctly on x86-64 needs a genuine
 * per-process kernel stack (this arch's single shared percpu.kernel_rsp,
 * bios/arch/x86_64/trap.c, cannot be reused across a suspended parent and
 * a running child without one clobbering the other's in-progress kernel
 * call chain) and a real ring0->ring3 transition, reusing the GDT/TSS
 * this arch's trap.c/gdt.c already set up. Not yet designed or
 * implemented -- tracked as follow-up work.
 *
 * Until then, these are C stand-ins rather than real assembler entry
 * points:
 *
 *  - enter()/bdos_trap2() are never actually invoked as functions on this
 *    arch: bdos/bdosmain.c only ever takes their *address* (Setexc(0x21,
 *    (long)enter), Setexc(0x22, (long)bdos_trap2)) to install as the
 *    m68k/ARM-style TRAP #1/#2 vectors nothing on x86-64 reads back --
 *    GEMDOS calls reach osif() directly through this arch's own
 *    syscall/sysretq dispatch (bios/arch/x86_64/trap.c). Empty bodies are
 *    therefore correct, not just a placeholder.
 *
 *  - gouser()/termuser() ARE really called, from bdos/proc.c's
 *    proc_go()/xterm() -- the moment any process (including the one
 *    CONF_WITH_CLI's EmuCON itself launches via Pexec) actually starts or
 *    exits. Panicking with a clear message here, rather than falling
 *    through into logic that would assume m68k/ARM register-save layouts
 *    that were never set up (see proc_go()'s own __x86_64__ branch),
 *    marks exactly where this port's boot sequence currently ends.
 *
 * Copyright (C) 2025-2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#include "portab.h"
#include "biosext.h"

void enter(void);
void bdos_trap2(void);
void gouser(void) NORETURN;
void termuser(void) NORETURN;

void enter(void)
{
}

void bdos_trap2(void)
{
}

void gouser(void)
{
    panic("x86-64: gouser() reached -- process launch not implemented yet\n");
}

void termuser(void)
{
    panic("x86-64: termuser() reached -- process exit not implemented yet\n");
}
