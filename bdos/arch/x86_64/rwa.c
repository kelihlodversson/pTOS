/*
 * rwa.c - GEMDOS process-switch primitives (x86-64)
 *
 * Every other arch's rwa.S (bdos/arch/{m68k,arm}/rwa.S) implements a
 * cooperative "stack-swapping coroutine": gouser()/termuser() switch the
 * live stack pointer between a parent's suspended kernel call chain
 * (frozen mid-Pexec) and a child's, using a per-arch struct gouser_stack
 * (bdos/proc.c) laid out to match that arch's own trap-entry register
 * save area. gouser() below does the #334 equivalent for a single
 * process (a genuine per-process address space and a real ring0->ring3
 * transition, via bios/arch/x86_64/pgtable.h and trap.h) -- but not yet
 * the full coroutine: termuser() (resuming a suspended parent once a
 * child exits) needs a per-process kernel stack this arch does not have
 * yet (this arch's single shared percpu.kernel_rsp, bios/arch/x86_64/
 * trap.c, cannot be reused across a suspended parent and a running
 * child without one clobbering the other's in-progress kernel call
 * chain) -- tracked as follow-up work (#334's own "context switch
 * support" scope item).
 *
 *  - enter()/bdos_trap2() are never actually invoked as functions on this
 *    arch: bdos/bdosmain.c only ever takes their *address* (Setexc(0x21,
 *    (long)enter), Setexc(0x22, (long)bdos_trap2)) to install as the
 *    m68k/ARM-style TRAP #1/#2 vectors nothing on x86-64 reads back --
 *    GEMDOS calls reach osif() directly through this arch's own
 *    syscall/sysretq dispatch (bios/arch/x86_64/trap.c). Empty bodies are
 *    therefore correct, not just a placeholder.
 *
 * Copyright (C) 2025-2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#include "portab.h"
#include "biosext.h"
#include "bdosdefs.h"
#include "tosvars.h"
#include "bdosstub.h"

/*
 * Declared directly, not through a shared header: bdos/build.mk's
 * include path has no bios/arch/x86_64 entry (unlike bios/'s own), so
 * these arch-level declarations (pgtable.h/trap.h/pmem.h) aren't
 * reachable by #include here -- matching this file's own established
 * precedent (bdos/proc.c's identical approach for
 * x86_64_low_tpa_alloc()).
 */
extern UQUAD x86_64_pmem_alloc_pages(UQUAD count);
extern void x86_64_new_address_space(UQUAD pml4_phys);
extern void x86_64_map_low_tpa_into(UQUAD pml4_phys, UQUAD virt_start, UQUAD virt_end);
extern void x86_64_enter_user(UQUAD pml4_phys, UQUAD entry_rip, UQUAD user_rsp) NORETURN;

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
    PD *p = run;

    if (p->p_tlen == 0 && p->p_dlen == 0 && p->p_blen == 0) {
        /*
         * bios.c's CLI/ROM-shell bootstrap (BOOTFLAG_EARLY_CLI, or the
         * default exec_os launch): p_tbase is a deliberately truncated,
         * unusable slice of a kernel-code address (coma_start or
         * ui_start, both higher-half -- see bios.c's own comment on
         * this exact PD) -- not a real user process's text segment, and
         * never ring-3-executable regardless of any truncation, since
         * ring 3 can never reach kernel-mapped pages at all on this
         * arch. exec_os (tosvars.h) is the very same kernel-code entry
         * bios_init() already decided on, still held as a real,
         * untruncated function pointer -- call it directly, in ring 0,
         * exactly as cli/arch/x86_64/cmdasm.c's own header comment
         * anticipates ("an ordinary C function with one PD * parameter
         * already has the right ABI"). PRG_ENTRY itself is declared
         * with no parameters (portab.h) -- true for how m68k/ARM invoke
         * it (a raw jump, basepage in a register, not a C call) but not
         * this arch's own coma_start(PD *)/ui_start(PD *), hence the
         * cast. Never returns: coma_start()/ui_start() call Pterm0()
         * themselves once their own main loop exits.
         *
         * The zero-length test itself is not a perfect distinguisher
         * (Copilot's review of #356 caught this): aes/gemshlib.c's
         * aes_run_rom_program() builds an identically-shaped PD (a
         * PE_BASEPAGEFLAGS basepage, zero p_tlen/p_dlen/p_blen, p_tbase
         * set to its own ROM entry) for launching a GEM ROM program,
         * and would land here too, calling exec_os -- the wrong
         * function -- instead of that program's own entry. Not
         * reachable today: pc-x86_64_defconfig builds with
         * CONF_WITH_AES=n (no x86-64 framebuffer driver yet, #332), so
         * aes_run_rom_program() is never compiled in. A real fix needs
         * more than switching to p->p_tbase: that field holds the same
         * kind of deliberately-truncated, unusable kernel-code address
         * as coma_start/ui_start above for exactly the same reason
         * (never 32-bit-representable on this arch), so it can't
         * substitute for exec_os here either -- untruncated ROM-program
         * entries would need their own exec_os-style mechanism before
         * CONF_WITH_AES could ever be turned on for this arch.
         */
        ((void (*)(PD *))exec_os)(p);
        panic("x86-64: kernel-code process entry returned unexpectedly\n");
    } else {
        /*
         * A real, loaded process (#334): build its own address space --
         * x86_64_new_address_space() shares the kernel + physical
         * direct map (high half) in (PML4 slots 256-511) and clears
         * every low slot, including slot 0, exactly like every other
         * process-private slot (see that function's own comment) -- it
         * does NOT share the low TPA pool or the low system-vector page
         * in; an earlier version of this comment claimed it did, which
         * was wrong (#356's own review caught it: a real loaded process
         * faulted on its first instruction, since nothing had ever
         * mapped its own text/stack into its new PML4). Map this
         * process's own p_lowtpa..p_hitpa range -- where alloc_tpa()/
         * alloc_env() (bdos/proc.c) already draw p_tbase/p_hitpa/p_env
         * from -- in explicitly before entering ring 3, not the whole
         * shared pool (a second review round caught that too: every
         * other process's/the kernel's own bookkeeping sharing this
         * pool would otherwise be reachable from ring 3). The low
         * system-vector page is deliberately still not mapped here:
         * #352 tracks whether (and how safely) a real process should
         * ever see it.
         *
         * No per-process kernel stack or termuser()-style resumption
         * yet (see this file's own top comment): this is the one-shot
         * primitive for #334's "run a single process to completion"
         * milestone, not the full parent/child coroutine multiple
         * concurrent processes would need.
         */
        UQUAD pml4_phys = x86_64_pmem_alloc_pages(1);
        UQUAD entry_rip = (UQUAD)(uintptr_t)USERPTR_TO_PTR(p->p_tbase);
        UQUAD user_rsp = (UQUAD)(uintptr_t)USERPTR_TO_PTR(p->p_hitpa);

        x86_64_new_address_space(pml4_phys);
        x86_64_map_low_tpa_into(pml4_phys, (UQUAD)p->p_lowtpa, (UQUAD)p->p_hitpa);
        x86_64_enter_user(pml4_phys, entry_rip, user_rsp);
    }
}

void termuser(void)
{
    /*
     * bdos/proc.c's xterm() already did `run = run->p_parent;
     * run->p_dreg[0] = rc;` before calling here -- the m68k/ARM
     * convention for "the exit code is in D0 once the parent resumes"
     * (its own comment above xterm()'s definition), read back here
     * since `run` now points at whichever PD launched this one (on
     * this arch, so far, always the kernel's own placeholder
     * initial_basepage -- see bdosmain.c -- since #334's own scope
     * excludes multiple concurrent processes).
     *
     * A real parent/child coroutine resume (the m68k/ARM rwa.S
     * equivalent of what this function's name promises) needs a
     * per-process kernel stack this arch does not have yet -- see this
     * file's own top comment. Until then, this is where "run a single
     * process to completion" (#334's own success bar) actually ends:
     * there is no suspended kernel call chain to resume back into, so
     * report the exit code and stop cleanly instead of pretending to
     * resume something that was never frozen in the first place.
     */
    panic("x86-64: process exited, rc=%ld\n", (long)run->p_dreg[0]);
}
