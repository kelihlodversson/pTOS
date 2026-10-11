/*
 * rwa.c - GEMDOS process-switch primitives (x86-64)
 *
 * Every other arch's rwa.S (bdos/arch/{m68k,arm}/rwa.S) implements a
 * cooperative "stack-swapping coroutine": gouser()/termuser() switch the
 * live stack pointer between a parent's suspended kernel call chain
 * (frozen mid-Pexec) and a child's, using a per-arch struct gouser_stack
 * (bdos/proc.c) laid out to match that arch's own trap-entry register
 * save area. On those arches, a GEMDOS call from AES/kernel-internal
 * code goes through a *real* trap #1, which switches to a single shared
 * supervisor stack (supstk) automatically, at the CPU level, on trap
 * entry -- so gouser()'s own reuse of that same shared stack for a
 * nested/reentrant launch never clobbers the caller's own AES-level
 * call chain, which is safely sitting on its own separate, untouched
 * per-process stack (aes/struct.h's struct uda) the whole time.
 *
 * This arch has no such automatic isolation: AES-internal code calls
 * BDOS directly, as an ordinary nested C function call, with no re-trap
 * (see this file's own enter()/bdos_trap2() comment below) -- so a
 * "kernel-code process" launch (the p_tlen==p_dlen==p_blen==0 branch of
 * gouser() below: accdesk_start/deskstart/coma_start/ui_start, all
 * reached via aes/gemshlib.c's aes_run_rom_program() or bios.c's own
 * boot-time Pexec(PE_GO/PE_GOTHENFREE, ...)) executes on top of
 * whatever the caller's own C call stack currently is. Resuming that
 * caller once the launched process's own Pterm()/Pterm0() runs -- deep
 * inside an entirely different, much-later call chain -- is exactly
 * what setjmp()/longjmp() are for: x86_64_kexec_resume (below) is a
 * LIFO stack of one jmp_buf per currently-suspended nesting level,
 * saved/restored around each gouser() call the same way ordinary nested
 * C calls naturally nest, with no need to touch any actual stack
 * pointer at all (found via Copilot's review of #376, which caught two
 * symptoms of this: aes/arch/x86_64/gemdosif.c's dos_exec() and aes/
 * arch/x86_64/gemstart.c's accdesk_start() both blindly reset %rsp back
 * to D.g_int[0].a_uda.u_spsuper on every reentrant launch, discarding
 * whatever of gem_main()'s own call chain was still live there).
 *
 * A *real*, ring-3 process (the other branch of gouser() below, via
 * x86_64_enter_user()) leaves the kernel entirely by iretq, so there is no
 * suspended C call chain to resume -- but gouser() saves a jmp_buf of its own
 * right before iretq, and termuser() (reached from xterm() once the address
 * space is gone, whether by Pterm() or by a fault) unwinds to it, exactly as
 * for a kernel-code process.  That resumes the launcher with the exit code,
 * which is what the built-in EmuCON, and its parent bios.c, rely on.  Each
 * ring-3 process makes its system calls on a kernel stack of its own, which
 * is what lets one launch another from inside a call (EmuCON running a
 * program): the suspended launcher's frames and the jmp_buf stay on ITS stack
 * while the child runs, and gouser() restores the launcher's stack pointer,
 * page tables, saved user RSP and GS state when the child is gone (#399).
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
#include "setjmp.h"
#include "kproc.h"
#include "kprint.h"

/*
 * Declared directly, not through a shared header: bdos/build.mk's
 * include path has no bios/arch/x86_64 entry (unlike bios/'s own), so
 * these arch-level declarations (pgtable.h/trap.h/pmem.h) aren't
 * reachable by #include here -- matching this file's own established
 * precedent (bdos/proc.c's identical approach for
 * x86_64_low_tpa_alloc()).
 */
extern void x86_64_enter_user(UQUAD pml4_phys, UQUAD entry_rip, UQUAD user_rsp,
                              UQUAD basepage, UQUAD entry_type) NORETURN;
extern BOOL x86_64_take_kernel_code_pd(PD *p);
extern void x86_64_syscall_abandoned(void);     /* bios/arch/x86_64/trap.c */
extern UQUAD x86_64_get_kernel_stack(void);
extern void x86_64_set_kernel_stack(UQUAD rsp);
extern UQUAD x86_64_get_saved_user_rsp(void);
extern void x86_64_set_saved_user_rsp(UQUAD rsp);
extern int x86_64_gs_to_user(void);
extern void x86_64_gs_back_to_syscall(void);
extern void x86_64_write_cr3(UQUAD pml4_phys);  /* bios/arch/x86_64/pgtable.c */
extern UQUAD x86_64_kernel_pml4_phys(void);

void enter(void);
void bdos_trap2(void);
void gouser(void);
void termuser(void) NORETURN;
/* declared directly like the above: proc.h needs file-system types this file
 * has no other use for */
extern void xterm(UWORD rc) NORETURN;               /* bdos/proc.c */
void x86_64_user_fault(ULONG vector, UQUAD error_code, UQUAD rip, UQUAD cr2, UQUAD rsp) NORETURN;

void enter(void)
{
}

void bdos_trap2(void)
{
}

/*
 * LIFO stack of one jmp_buf per currently-suspended launch nesting level
 * (a "kernel-code process" or a ring-3 one) -- see this file's own top
 * comment.  NULL when nothing is currently launched.
 */
static jmp_buf *x86_64_kexec_resume;

#if CONF_WITH_AES
extern void accdesk_start(void) NORETURN;      /* aes/arch/x86_64/gemstart.c */
extern void deskstart(PD *) NORETURN;          /* desk/arch/x86_64/deskstart.c */
#endif

#if CONF_WITH_AES
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

    if (x86_64_take_kernel_code_pd(p)) {
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
         * aes/gemshlib.c's aes_run_rom_program() builds a
         * PE_BASEPAGEFLAGS basepage, then explicitly marks it as a
         * kernel-code launch before Pexec() so this does not infer trust
         * from mutable PD fields.  It sets p_tbase to its ROM entry for
         * launching a GEM ROM program --
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
         *
         * Reentrancy: this whole branch can run nested inside an
         * already-running kernel-code process (accdesk_start's own
         * aes_run_rom_program(deskstart)/(coma_start), called from deep
         * inside gem_main()'s own call chain) -- see this file's own top
         * comment on why that's safe here (an ordinary nested C call,
         * no stack pointer touched) and how the eventual Pterm()/
         * Pterm0() finds its way back via x86_64_kexec_resume.
         */
        void (*target)(PD *) = (void (*)(PD *))exec_os;
#if CONF_WITH_AES
        ULONG want = p->p_tbase;
#endif
        jmp_buf buf;
        jmp_buf *saved_resume = x86_64_kexec_resume;

#if CONF_WITH_AES
        X86_64_ROM_CANDIDATE(&target, want, accdesk_start);
        X86_64_ROM_CANDIDATE(&target, want, deskstart);
#endif

        x86_64_kexec_resume = &buf;
        if (setjmp(buf) == 0) {
            target(p);
            panic("x86-64: kernel-code process entry returned unexpectedly\n");
        }
        /* Reached only via termuser()'s longjmp() below, once this
         * launch's own Pterm()/Pterm0() runs: restore the enclosing
         * level's own resume point (NULL if this was the outermost) and
         * fall through to gouser()'s own ordinary C return, unwinding
         * normally back through proc_go()/xexec() to this call's own
         * original caller -- run has already been reassigned to it by
         * xterm() by this point. */
        x86_64_kexec_resume = saved_resume;
    } else {
        /*
         * A real, loaded process (#334).  Its address space was already
         * built, and can have failed with ENSMEM, back in Pexec():
         * kproc_prepare_user() (bdos/kproc.c) makes the PML4 -- the
         * kernel + physical direct map shared in, the kernel's low data
         * mapped supervisor-only (aspace.c's map_kernel_low()) -- and maps
         * this process's own environment block, basepage/TPA (user) and,
         * for a built-in image, its segments and stack, plus its parent's
         * basepage (kernel-only: xterm() writes the exit code through it
         * from ring 0 while this CR3 is still loaded).  Nothing else in the
         * process window is mapped, except the blocks of a child it is
         * itself launching (kproc_borrow()).  That address space belongs to
         * the process's KPROC record and is freed with it (kproc_destroy(),
         * from xterm()), so nothing here allocates or needs to unwind.
         *
         * Both branches resume the launcher at Pterm() the same way (the
         * jmp_buf saved below), so Pexec() returns the exit code.  What a
         * ring-3 process has, which this branch switches to and back, is a
         * kernel stack of its own (see this file's own top comment).
         */
        UQUAD pml4_phys = kproc_user_pml4(p);
        UQUAD entry_rip = kproc_user_entry(p);
        /* Every process has a private stack of its own, mapped when it was
         * prepared (kproc_prepare_user()), whether it has an image or only a
         * basepage whose p_tbase it starts at; its top is in the KPROC record,
         * not in the user-writable basepage (the TPA is no stack).  The entry
         * contract of doc/process-entry.txt: RSP + 8 divisible by 16. */
        UQUAD user_rsp = kproc_user_stack(p);
        jmp_buf buf;
        jmp_buf *saved_resume = x86_64_kexec_resume;
        UQUAD kstack_top, kstack_phys = kproc_take_kernel_stack(p, &kstack_top);
        UQUAD launcher_stack = x86_64_get_kernel_stack();
        UQUAD launcher_user_rsp = x86_64_get_saved_user_rsp();
        int launcher_in_syscall;

        if (!entry_rip)         /* no image: a basepage with its code in its TPA */
            entry_rip = (UQUAD)(uintptr_t)USERPTR_TO_PTR(p->p_tbase);
        if (!pml4_phys || !kstack_phys || !user_rsp)
            panic("x86-64: process launched without an address space, stack or kernel stack\n");
        if ((user_rsp & 15) != 8)
            panic("x86-64: process stack not aligned for entry\n");

        /*
         * Control never comes back through x86_64_enter_user(): the process
         * leaves ring 0 by iretq.  What comes back is its own Pterm() (or a
         * fault in it), which unwinds with longjmp() to this exact point,
         * exactly as for a kernel-code process above -- so the parent's
         * Pexec() returns the exit code.  By then xterm() has already
         * destroyed the address space (switching CR3 to the kernel tables)
         * and made the launcher `run` again.
         *
         * The process's system calls run on a kernel stack of its own, so a
         * launcher that is itself a ring-3 process, suspended in its Pexec()
         * system call, keeps its frames (this one, and the jmp_buf) on ITS
         * stack while the child makes calls.  Everything the launcher's own
         * context needs is put back below: its stack for the next call, its
         * page tables, its GS state, its saved user RSP.
         */
        x86_64_kexec_resume = &buf;
        x86_64_set_kernel_stack(kstack_top);
        launcher_in_syscall = x86_64_gs_to_user();
        if (setjmp(buf) == 0)
            x86_64_enter_user(pml4_phys, entry_rip, user_rsp,
                              (UQUAD)(uintptr_t)p, 0);
        x86_64_kexec_resume = saved_resume;
        /* Pterm() was a system call, still on the child's stack with the
         * kernel GS base swapped in: normalise that first, then restore the
         * launcher's own state.  Only now is the child's stack unused. */
        x86_64_syscall_abandoned();
        x86_64_set_kernel_stack(launcher_stack);
        x86_64_set_saved_user_rsp(launcher_user_rsp);
        x86_64_kstack_free(kstack_phys);
        {
            UQUAD launcher_pml4 = kproc_user_pml4(run);

            x86_64_write_cr3(launcher_pml4 ? launcher_pml4 : x86_64_kernel_pml4_phys());
        }
        if (launcher_in_syscall)
            x86_64_gs_back_to_syscall();
    }
}

/*
 * A ring-3 process faulted (bios/arch/x86_64/panic.c).  The fault is the
 * process's own: report it and terminate just that process, as Pterm(-1)
 * would, so the launcher resumes and the kernel carries on.  Called on the
 * exception stack, in ring 0, with the process's address space still
 * loaded; xterm() tears that down and never returns here.
 */
void x86_64_user_fault(ULONG vector, UQUAD error_code, UQUAD rip, UQUAD cr2, UQUAD rsp)
{
    ULONG word[4];

    kcprintf("x32 process fault: vector %lu error=0x%lx rip=0x%lx addr=0x%lx rsp=0x%lx\n",
            (long)vector, (long)error_code, (long)rip, (long)cr2, (long)rsp);
    if (kproc_copy_from_user(word, rsp, sizeof(word)))
        kcprintf("  stack: 0x%lx 0x%lx 0x%lx 0x%lx\n",
                (long)word[0], (long)word[1], (long)word[2], (long)word[3]);
    xterm((UWORD)-1);
    panic("x86-64: terminated faulting process returned\n");
}

void termuser(void)
{
    /*
     * bdos/proc.c's xterm() already did `run = kproc_get_parent(p);
     * run->p_dreg[0] = rc;` before calling here -- the m68k/ARM
     * convention for "the exit code is in D0 once the parent resumes"
     * (its own comment above xterm()'s definition), read back here
     * since `run` now points at whichever PD launched this one.  On this
     * arch the launcher comes from the process's kernel-private KPROC
     * record, never from p_parent, which the process can rewrite.
     *
     * x86_64_kexec_resume is the resume point gouser() pushed right before
     * running this process, whether a "kernel-code process" (an ordinary
     * nested C call) or a real ring-3 one (which left the kernel by iretq,
     * so there is no suspended call chain, but the saved jmp_buf stands in
     * for one): unwind back to exactly that point, an ordinary nested C
     * call away from wherever this launch's own caller is (proc_go()/
     * xexec(), which then returns run->p_dreg[0] -- the exit code this
     * comment's own first paragraph describes -- back up to its caller).
     * It is only ever NULL if nothing was launched, which cannot get here.
     */
    if (x86_64_kexec_resume)
        longjmp(*x86_64_kexec_resume, 1);

    panic("x86-64: process terminated with no launch to resume, rc=%ld\n",
          (long)run->p_dreg[0]);
}
