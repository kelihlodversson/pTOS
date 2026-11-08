/*
 * uspace.c - GEMDOS process switching of processes in address spaces of their
 *            own (68040, 68060)
 *
 * The m68k counterpart of bdos/arch/arm/uspace.c, for CONF_WITH_USER_ASPACE
 * (doc/mmu-address-space.txt).  Pexec() waits in gouser() on the kernel stack
 * of the launcher, with a jmp_buf of its own; the new process runs on a kernel
 * stack and in an address space of its own, and when it ends -- by Pterm() or
 * by a fault -- termuser() returns to the launcher by longjmp(), which then
 * gets its own address space back.  The entry of a call (uspaceasm.S) keeps
 * what the caller had in a frame on the kernel stack of the process, so a
 * launcher suspended in Pexec() keeps its frame while its child makes calls.
 *
 * Copyright (C) 2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"

#if CONF_WITH_USER_ASPACE

#include "portab.h"
#include "bdosdefs.h"
#include "tosvars.h"
#include "gemerror.h"
#include "bdosstub.h"
#include "setjmp.h"
#include "kproc.h"
#include "kprint.h"
#include "biosext.h"
#include "procmem.h"
#include "aspace32.h"
#if CONF_WITH_NATIVE_SCHED
#include "ksched.h"
#include "native_sched_backend.h"
#include "arch/m68k/irqframe.h"
#include "thread_startup32.h"
#include "string.h"
#include "fs.h"
#endif
#include "uaccess.h"
#include "arch/m68k/userentry.h"

extern long osif(short *pw);                    /* bdos/bdosmain.c */

void gouser(void);
void termuser(void) NORETURN;

/* the FPU state of a process (uspaceasm.S): the FSAVE frame (the longest, of
 * the 68881/2, is 216 bytes), fpcr/fpsr/fpiar, fp0-fp7 */
#if !CONF_WITH_NATIVE_SCHED
struct fpuctx {
    UBYTE frame[216];
    ULONG ctl[3];
    UBYTE regs[96];
};
#endif
extern void m68k_fpu_save(struct fpuctx *ctx);
extern void m68k_fpu_restore(const struct fpuctx *ctx);
extern LONG fputype;                            /* 0: no FPU (bios/bios.c) */

/* the frame of a GEMDOS call (uspaceasm.S), lowest address first */
struct uframe {
    ULONG usp;
    ULONG d[8];
    ULONG a[7];
};

#define SUPER       0x20
#define ARG_WORDS   13                  /* fn, and the most a call takes: Mmap(), 6 longs */

/*
 * Copies the words the caller put on its stack for a call into pw, as many
 * as are readable: what is not (a call with no arguments at the top of the
 * stack) reads as 0, so that the kernel never touches an unmapped address.
 * Returns FALSE if not even the function number is.
 */
BOOL user_stack_words(ULONG usp, WORD *pw, int n)
{
    int k;

    for (k = 0; k < n; k++)
        pw[k] = 0;
    if (usp & 1)
        return FALSE;
    for (k = n; k > 0; k--)
        if (user_buf_check((long)usp, 2L * k, FALSE) == E_OK)
            break;
    if (k == 0)
        return FALSE;
    for (n = 0; n < k; n++)
        pw[n] = ((volatile WORD *)usp)[n];
    return TRUE;
}

/*
 * The call of a process: the words on its stack, the way the trap takes them.
 * osif() checks every pointer against the pages of the caller.  Super() is the
 * one call that is not GEMDOS's: a process stays in user mode (Super(1) says
 * so).
 */
void m68k_gemdos_dispatch(struct uframe *f);

void m68k_gemdos_dispatch(struct uframe *f)
{
    WORD pw[ARG_WORDS];

    if (!user_stack_words(f->usp, pw, ARG_WORDS)) {
        f->d[0] = (ULONG)ERANGE;
        return;
    }
    if (pw[0] == SUPER) {
        LONG arg = ((LONG)(UWORD)pw[1] << 16) | (UWORD)pw[2];

        f->d[0] = (ULONG)(arg == 1 ? 0L : EACCDN);
    } else
        f->d[0] = (ULONG)osif(pw);
}

/*
 * The resume point of each launch that waits: one jmp_buf per level of
 * Pexec() in progress, innermost first.
 */
struct uspace_launch {
    jmp_buf resume;
    struct uspace_launch *previous;
    PD *pd;
    PADDR stack_phys;
    uintptr_t stack_top;
    KPROC * volatile exit_process;
    volatile UWORD exit_status;
    volatile BOOL exit_resident;
    volatile ULONG exit_resident_len;
    volatile BOOL exit_final;
};
#if CONF_WITH_NATIVE_SCHED
static struct uspace_launch *get_uspace_launch(void) { return ksched_loaded_launcher(); }
static void set_uspace_launch(struct uspace_launch *launch) { ksched_set_loaded_launcher(launch); }
#else
static struct uspace_launch *legacy_launch;
static struct uspace_launch *get_uspace_launch(void) { return legacy_launch; }
static void set_uspace_launch(struct uspace_launch *launch) { legacy_launch=launch; }
#endif
#if CONF_WITH_NATIVE_SCHED
BOOL m68k_process_irq_stack(PD *pd, uintptr_t *base, uintptr_t *top)
{
    struct uspace_launch *launch=get_uspace_launch();
    if (!launch || launch->pd!=pd || !launch->stack_phys || launch->exit_process) return FALSE;
    *base=(uintptr_t)pmem32_virt(launch->stack_phys); *top=launch->stack_top;
    return TRUE;
}
/* IRQ exclusion covers the serialized cooperative handoff, not execution.
 * User registers stay in the suspended trap frame on this context's stack.
 * Never publish another task until its continuation and resource owners exist. */
void native_sched_save(NATIVE_SCHED_CONTEXT *context)
{
    context->flags = native_sched_irq_save();
    context->pd = run;
    context->root = aspace_hw_current();
    __asm__ volatile ("move.l %%usp,%0" : "=a"(context->user_sp));
    context->launch = get_uspace_launch();
    context->native_task = ksched_context_id();
    context->disk_error = *fs_error_current();
    if (fputype) m68k_fpu_save(&context->fpu);
    aspace_hw_switch(aspace_kernel_root());
}
void native_sched_restore(const NATIVE_SCHED_CONTEXT *context)
{
    native_sched_irq_disable();
    run = context->pd;
    set_uspace_launch(context->launch);
    ksched_context_restore(context->native_task);
    *fs_error_current() = context->disk_error;
    aspace_hw_switch(context->root);
    __asm__ volatile ("move.l %0,%%usp" :: "a"(context->user_sp) : "memory");
    if (fputype) m68k_fpu_restore(&context->fpu);
    native_sched_irq_restore(context->flags);
}
#endif
#if CONF_WITH_EXIT_HANDOFF_TEST
static BOOL injected_exit_failure;
#endif

BOOL proc_defer_exit(PD *pd, UWORD status, BOOL resident, ULONG resident_len)
{
    struct uspace_launch *launch = get_uspace_launch();
    KPROC *process;
    if (!launch || launch->pd != pd || !launch->stack_phys || launch->exit_process)
        return FALSE;
    if ((uintptr_t)&process < (uintptr_t)pmem32_virt(launch->stack_phys) ||
        (uintptr_t)&process >= launch->stack_top)
        panic("exit preparation off child syscall stack");
    process = kproc_retain(pd);
    if (!process)
        return FALSE;
#if CONF_WITH_EXIT_HANDOFF_TEST
    if (!injected_exit_failure
#if CONF_WITH_NATIVE_SCHED
        && !ksched_exit_thread(pd)
#endif
    ) {
        injected_exit_failure = TRUE;
        kproc_release(process);
        kcprintf("exit handoff: injected preparation rollback\n");
        return FALSE;
    }
#endif
#if CONF_WITH_NATIVE_SCHED
    launch->exit_final = !ksched_exit_thread(pd) ||
                         kproc_thread_is_sole(process, ksched_exit_thread(pd));
    if (!kproc_prepare_thread_exit_handoff(process, ksched_exit_thread(pd))) {
#else
    launch->exit_final = TRUE;
    if (!kproc_prepare_exit_handoff(process)) {
#endif
        kproc_release(process);
        return FALSE;
    }
    launch->exit_status = status;
    launch->exit_resident = resident;
    launch->exit_resident_len = resident_len;
    launch->exit_process = process;
    return TRUE;
}

void gouser(void)
{
    PD *p = run;
    ASPACE_ROOT root = kproc_user_root(p);
    ULONG entry = (ULONG)kproc_user_entry(p);
    ULONG user_sp = (ULONG)kproc_user_stack(p);
    struct uspace_launch launch;
    struct fpuctx fpu;
    ULONG launcher_status;
    uintptr_t kstack_top;
    PADDR kstack_phys;
#if CONF_WITH_NATIVE_SCHED
    THREAD_STARTUP_DRAFT *startup = ksched_thread_startup(p);
    const THREAD_STARTUP32 *prepared = startup ? thread_startup_image(startup) : NULL;
    THREAD_STARTUP_STACK syscall_owner = { 0, 0 };
    if (ksched_take_syscall_stack(p, &syscall_owner)) {
        kstack_phys = syscall_owner.physical;
        kstack_top = syscall_owner.top;
    } else
#endif
        kstack_phys = kproc_take_kernel_stack(p, &kstack_top);
#if CONF_WITH_NATIVE_SCHED
    if (prepared) { entry = prepared->entry; user_sp = prepared->sp; }
#endif

    if (!entry)                 /* no image: a basepage with its code in its TPA */
        entry = (ULONG)(uintptr_t)USERPTR_TO_PTR(p->p_tbase);
    if (!root || !kstack_phys || !user_sp)
        panic("process launched without an address space, stack or kernel stack\n");

    /*
     * Control never comes back through m68k_enter_user(): the process leaves
     * for user mode, and what comes back is its own Pterm() (or a fault in
     * it), which unwinds to here by longjmp(), so that the Pexec() of the
     * launcher returns the exit code. xterm() restores `run`; deferred
     * finalization keeps the child mappings alive until this safe continuation.
     */
    launch.previous = get_uspace_launch();
    launch.pd = p;
    launch.stack_phys = kstack_phys;
    launch.stack_top = kstack_top;
    launch.exit_process = NULL;
    set_uspace_launch(&launch);
    /* the FPU of the launcher is what the child will not see, and what the
     * launcher gets back when it ends */
    if (fputype)
        m68k_fpu_save(&fpu);
    __asm__ volatile ("move.w %%sr,%0" : "=d" (launcher_status));
    if (setjmp(launch.resume) == 0) {
        ULONG initial[2];
        M68K_USER_ENTRY initial_entry;

        aspace_hw_switch(root);
        /* the code of an ELF image was written through the data side (the loader reads
         * the file into the pages of the new space): publish it before the first fetch */
        aspace_hw_code_changed();
        /* what a program finds on its stack: a return address, and the basepage.
         * Written through the kernel's copy, which backs the page of a lazy stack
         * (a store from here would be a fault of the kernel) */
        if (
#if CONF_WITH_NATIVE_SCHED
            !prepared &&
#endif
            !kproc_standard_entry(p)) {
            user_sp -= 8;
            initial[0] = 0;
            initial[1] = (ULONG)(uintptr_t)p;
            if (!kproc_copy_to_user(user_sp, initial, sizeof(initial)))
                panic("cannot write the first words of the stack of a process\n");
        }       /* (a standard entry has the initial stack of the ELF ABI, built by the kernel) */
        initial_entry.entry = entry;
        initial_entry.user_sp = user_sp;
        initial_entry.kstack_top = (ULONG)kstack_top;
        initial_entry.argument = 0;
#if CONF_WITH_NATIVE_SCHED
        if (prepared) initial_entry.argument = prepared->argument;
#endif
#if CONF_WITH_NATIVE_SCHED
        if (prepared) thread_startup_entering(startup);
#endif
        m68k_enter_user(&initial_entry);
    }
    set_uspace_launch(launch.previous);
    {
        ASPACE_ROOT launcher = kproc_user_root(run);

        aspace_hw_switch(launcher ? launcher : aspace_kernel_root());
    }
    /* longjmp restored the launcher's kernel SP. The child stack is now
     * unused and has exactly one owner: this suspended launcher. */
    if ((uintptr_t)&launch >= (uintptr_t)pmem32_virt(kstack_phys) &&
        (uintptr_t)&launch < kstack_top)
        panic("exit finalization on child syscall stack");
    if (launch.exit_process && !kproc_user_root(launch.pd))
        panic("exit handoff lost child mappings");
    kstack_free(kstack_phys);
    if (launch.exit_process) {
        KPROC *process = launch.exit_process;
#if CONF_WITH_NATIVE_SCHED
        if (ksched_finish_exit(process, launch.pd, launch.exit_status) != launch.exit_final)
            panic("thread exit finalization changed");
#endif
        if (launch.exit_final)
            proc_finalize_exit(process, launch.exit_status,
                               launch.exit_resident, launch.exit_resident_len);
        run->p_dreg[0] = launch.exit_status;
        kproc_release(process);
    }
    if (fputype)
        m68k_fpu_restore(&fpu);
    __asm__ volatile ("move.w %0,%%sr" :: "d" (launcher_status) : "memory", "cc");
}

void termuser(void)
{
    /*
     * bdos/proc.c's xterm() already did `run = kproc_get_parent(p);
     * run->p_dreg[0] = rc;`: the exit code is where proc_go()'s caller reads it.
     */
    if (get_uspace_launch())
        longjmp(get_uspace_launch()->resume, 1);

    panic("process terminated with no launch to resume, rc=%ld\n", (long)run->p_dreg[0]);
}

#endif /* CONF_WITH_USER_ASPACE */
