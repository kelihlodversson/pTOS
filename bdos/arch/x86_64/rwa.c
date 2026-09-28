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
extern void x86_64_map_low_tpa_into(UQUAD pml4_phys, UQUAD virt_start, UQUAD virt_end, int user);
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

#if CONF_WITH_AES
extern void accdesk_start(void) NORETURN;      /* aes/arch/x86_64/gemstart.c */
extern void deskstart(PD *) NORETURN;          /* desk/arch/x86_64/deskstart.c */
#endif
#if CONF_WITH_CLI
extern void coma_start(PD *) NORETURN;         /* cli/arch/x86_64/cmdasm.c */
#endif

#if CONF_WITH_AES || CONF_WITH_CLI
/*
 * Captures sym's own address via `lea` (never a plain C address-of --
 * see bios.c's own comment on why: a plain `exec_os = coma_start`-style
 * assignment was seen reading back a garbage 64-bit value under this
 * arch's -mcmodel=large + non-ELF-relaxed-PE-link combination) and,
 * if its low 32 bits match `want` (an already-truncated PD->p_tbase
 * value -- see this function's own comment on why p_tbase can only ever
 * hold that much), updates *ptarget to the real, untruncated address.
 * Used below to recover which specific ROM entry a zero-length PD
 * launched via aes_run_rom_program()/coma_start's own Pexec(PE_GOTHENFREE)
 * path (aes/gemshlib.c) actually names, since p_tbase itself cannot
 * carry more than those low 32 bits on this arch.
 */
static void x86_64_match_rom_candidate(void (**ptarget)(PD *), ULONG want,
                                        void *candidate_addr)
{
    if ((ULONG)(uintptr_t)candidate_addr == want)
        *ptarget = (void (*)(PD *))candidate_addr;
}
#define X86_64_ROM_CANDIDATE(ptarget, want, sym) \
    do { \
        void *_addr; \
        __asm__("lea " #sym "(%%rip), %0" : "=r"(_addr)); \
        x86_64_match_rom_candidate((ptarget), (want), _addr); \
    } while (0)
#endif

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
         * set to its own ROM entry) for launching a GEM ROM program --
         * accdesk_start (run once, from ui_start's own gem_main()) or
         * deskstart/coma_start (run repeatedly, each time sh_ldapp()
         * wants the desktop shell or EmuCON, via the same "AES
         * reentrancy" dos_exec() path aes/arch/arm/gemstart.S's own
         * comment documents at length). p_tbase can only ever hold each
         * one's own low 32 bits (the same reason exec_os itself can't
         * be stored there either -- see USERPTR_T's own comment,
         * bdosdefs.h), so below, before falling back to exec_os,
         * candidate ROM entries are matched by comparing p_tbase
         * against each one's own low 32 bits, captured via the same
         * `lea` technique bios.c's own exec_os assignment uses --
         * recovering the real, untruncated address to actually call
         * once a match is found. AES process 0 (ui_start) itself is not
         * one of these candidates: exec_os already holds it (bios.c),
         * so the plain fallback below already covers it correctly.
         */
        void (*target)(PD *) = (void (*)(PD *))exec_os;
#if CONF_WITH_AES || CONF_WITH_CLI
        ULONG want = p->p_tbase;
#endif

#if CONF_WITH_AES
        X86_64_ROM_CANDIDATE(&target, want, accdesk_start);
        X86_64_ROM_CANDIDATE(&target, want, deskstart);
#endif
#if CONF_WITH_CLI
        X86_64_ROM_CANDIDATE(&target, want, coma_start);
#endif

        target(p);
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
         * process's own p_env..p_hitpa range -- where alloc_env() then
         * alloc_tpa() (bdos/proc.c) draw p_env then p_tbase/p_hitpa from,
         * strictly in that order and with nothing else from the pool
         * allocated in between for the same launch, so the two are
         * always contiguous (p_env < p_lowtpa) -- in explicitly before
         * entering ring 3, not the whole shared pool (a second review
         * round caught that too: every other process's/the kernel's own
         * bookkeeping sharing this pool would otherwise be reachable
         * from ring 3). Starting the range at p_env instead of p_lowtpa
         * is itself a fix: an earlier version of this call mapped only
         * [p_lowtpa, p_hitpa), leaving p_env's own page(s) unmapped, so
         * a process reading its own basepage environment would fault
         * (a later review round caught this too).
         *
         * p_parent (initial_basepage, per this function's own bottom
         * comment) is a separate, non-contiguous allocation from
         * earlier in the same pool (bdosmain.c's own one-time setup, at
         * the very start of it) -- xterm() widens and writes through it
         * on this process's own Pterm, while still running under this
         * process's own CR3, so it needs its own explicit mapping too
         * (another review round caught this): map just its one PD-sized
         * allocation, not the pool in between (which belongs to no
         * currently-running process and stays unmapped). Mapped
         * supervisor-only (user=0): xterm()'s write happens from ring 0
         * (the syscall handler hasn't dropped privilege back down yet),
         * so the kernel can still reach it through this same CR3, but a
         * user=1 leaf here would let ring 3 itself read or corrupt the
         * kernel's own initial_basepage PD directly -- a still later
         * review round caught the initial fix leaving this mapping
         * user-writable like every other leaf this function had ever
         * produced.
         *
         * The low system-vector page is deliberately still not mapped
         * here: #352 tracks whether (and how safely) a real process
         * should ever see it.
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
        x86_64_map_low_tpa_into(pml4_phys, (UQUAD)p->p_env, (UQUAD)p->p_hitpa, 1);
        x86_64_map_low_tpa_into(pml4_phys, (UQUAD)p->p_parent, (UQUAD)p->p_parent + sizeof(PD), 0);
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
