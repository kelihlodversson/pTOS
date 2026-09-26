/*
 * panicasm.c - support routines for panic debug messages (x86-64)
 *
 * Every other arch's panicasm.S (bios/arch/{m68k,arm}/panicasm.S) stashes
 * a register dump into the fixed-address proc_lives/proc_dregs/
 * proc_aregs/proc_enum/proc_usp/proc_stk system variables (tosvars.ld's
 * unconditional "Exception related variables" block -- unlike phystop/
 * membot/memtop and friends further down that file, this block is NOT
 * guarded by #if ARCH_M68K, so ARM already gets real storage for these
 * from the linker script the same way m68k does) before tail-calling
 * dopanic() (bios/kprint.c). x86-64 has no linker script at all (see the
 * ARCH_X86_64 branch of the top level Makefile's $(EMUTOS_IMG) rule), so
 * this file gives them ordinary storage instead, matching how
 * bios/tosvars.c already does the same for the *other* TOS variables
 * that block's own comment says are m68k-only.
 *
 * Only the "Call to panic(fmt, ...)" path (dopanic()'s proc_enum == 0
 * case) is implemented for real: the other proc_enum values identify
 * m68k/ARM CPU exception vectors this arch's own exception dispatch
 * (bios/arch/x86_64/panic.c's x86_64_exception_dispatch(), a *different*,
 * unrelated function despite the similar name) already handles on its
 * own, with a full register dump of its own -- nothing on this arch ever
 * sets proc_enum to anything but 0.
 *
 * Copyright (C) 2025-2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#include "portab.h"
#include "tosvars.h"
#include "biosext.h"
#include "bios.h"
#include "kprint.h"
#include "asm.h"
#include "earlycon.h"
#include <stdarg.h>

LONG proc_lives;
LONG proc_dregs[8];
LONG proc_aregs[8];
LONG proc_enum;
LONG proc_usp;
UWORD proc_stk[32];

void halt(void)
{
    for (;;)
        ;
}

/*
 * kill_program()/warm_reset()/cold_reset(): ARM/m68k's panicasm.S
 * fall-through design (kill_program() drops straight into warm_reset()
 * if Pterm() ever returns) is preserved here even though little of it is
 * exercised yet: Pterm() (trap1(0x4c,...)) reaches osif() -> xterm() ->
 * termuser() (bdos/arch/x86_64/rwa.c), which itself panics right now
 * (process launch/exit isn't implemented) -- so kill_program() recurses
 * into another panic() rather than actually returning, which is a stable
 * (if deep) dead end, not a crash. Neither this arch's warm_reset() nor
 * cold_reset() has anywhere real to restart into yet (no portable
 * "jump back to entry" the way ARM's "b main" is -- this arch's own
 * entry is efi_main(), which needs a fresh UEFI environment that has
 * long since been exited by the time either of these could run), so
 * both just say so and hang rather than silently doing nothing.
 */
void kill_program(void)
{
    (void)trap1(0x4c, (long)-1);
    warm_reset();
}

void warm_reset(void)
{
    earlycon_puts("warm_reset() reached -- not implemented on this arch, halting\n");
    halt();
}

void cold_reset(void)
{
    earlycon_puts("cold_reset() reached -- not implemented on this arch, halting\n");
    halt();
}

/*
 * void panic(const char *fmt, ...);
 *
 * dopanic()'s proc_enum == 0 case reads the "crashed pc" back out of
 * proc_stk as a UWORD* (cast, not indexed -- see its own struct trick),
 * purely for one diagnostic printf; __builtin_return_address(0) (this
 * function's own caller) is the closest x86-64 equivalent of what
 * panicasm.S's callers had left on their stack for it to read.
 */
void panic(const char *fmt, ...)
{
    va_list ap;

    /*
     * Unconditional, straight to COM1: cprintf()'s CONF_SERIAL_CONSOLE
     * output only reaches the serial port from inside the VT52 console
     * state handlers (vt52.c's normal_ascii() and friends), which do
     * nothing before vt52_init() has run (con_state is still NULL) --
     * so a panic() during early boot (before vt52_init(), e.g. from
     * bmem_init()/balloc_stram()) would otherwise be entirely silent,
     * then hang forever at dopanic()'s bconin2() with no way to tell
     * why. earlycon_puts() has no such dependency. The raw, unexpanded
     * format string is still far more useful than nothing even though
     * this does not substitute the "..." arguments.
     */
    earlycon_puts("panic: ");
    earlycon_puts(fmt);
    earlycon_puts("\n");

    *(void **)proc_stk = __builtin_return_address(0);
    proc_enum = 0;
    proc_lives = 0x12345678;

    va_start(ap, fmt);
    vkcprintf(fmt, ap);
    va_end(ap);

    dopanic("");
}
