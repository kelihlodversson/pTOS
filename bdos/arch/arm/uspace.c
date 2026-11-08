/*
 * uspace.c - GEMDOS process switching of processes in address spaces of their
 *            own (ARM)
 *
 * The ARM counterpart of bdos/arch/x86_64/rwa.c, for CONF_WITH_USER_ASPACE
 * (doc/mmu-address-space.txt).  Pexec() waits in gouser() on the kernel stack
 * of the launcher, with a jmp_buf of its own; the new process runs on a kernel
 * stack and in an address space of its own, and when it ends -- by Pterm() or
 * by a fault -- termuser() returns to the launcher by longjmp(), which then
 * gets its own address space back.  The entry of a call (uspaceasm.S) keeps
 * what the caller had in a frame on the stack of the process, so a launcher
 * suspended in Pexec() keeps its frame while its child makes calls.
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
#include "fs_error.h"
#include "kproc.h"
#include "kprint.h"
#include "biosext.h"
#include "procmem.h"
#include "aspace32.h"
#if CONF_WITH_NATIVE_SCHED
#include "ksched.h"
#include "native_sched_backend.h"
#include "arch/arm/irqframe.h"
#include "thread_startup32.h"
#include "string.h"
#include "fs.h"
#endif
#include "arch/arm/fpusave.h"

extern long osif(long *pw);                     /* bdos/bdosmain.c */

void gouser(void);
void termuser(void) NORETURN;
extern void arm_enter_user(ULONG entry, ULONG user_sp, ULONG basepage, ULONG kstack_top) NORETURN;
#if CONF_WITH_NATIVE_SCHED
extern void arm_enter_prepared_user(const THREAD_STARTUP32 *image, ULONG kstack_top) NORETURN;
#endif

/* the frame of a GEMDOS call (uspaceasm.S), lowest address first */
struct uframe {
#if FPU_ALL_BYTES
    ULONG fpu[FPU_ALL_BYTES / 4];
#endif
    ULONG usp, ulr;
    ULONG r[13];
    ULONG pad;
    ULONG lr, spsr;
};

#define SUPER   0x20

/*
 * The call of a process: the function in r0 and its arguments in r1-r4, the
 * way osif() takes them (a native long each), which checks every pointer
 * against the pages of the caller.  Super() is the one call that is not
 * GEMDOS's: a process stays in user mode (Super(1) says so).
 */
void arm_gemdos_dispatch(struct uframe *f);

void arm_gemdos_dispatch(struct uframe *f)
{
    long pw[8];
    int i;

    for (i = 0; i < 8; i++)
        pw[i] = (long)f->r[i];
    if (pw[0] == SUPER)
        f->r[0] = (ULONG)(pw[1] == 1 ? 0L : EACCDN);
    else
        f->r[0] = (ULONG)osif(pw);
}

#if CONF_WITH_DEMAND_PAGING
/*
 * A data or prefetch abort in the process that is running (bios/arch/arm/vectors.c
 * asks before it ends the process): if it is a translation or permission fault
 * on a page of a lazy area -- stack, Malloc(), Mmap() memory -- or a write to a
 * file page that is shared until then, back or copy it with the access the
 * instruction made.  0: not a lazy page; 1: done, the instruction runs again;
 * 2: it is one and no physical page is left; 3: I/O error of a file mapping.
 */
int arm_demand_page(ULONG address, ULONG fsr, int fetch);

int arm_demand_page(ULONG address, ULONG fsr, int fetch)
{
    ULONG status = (fsr & 0xf) | ((fsr >> 6) & 0x10);
    ASPACE_ROOT root = kproc_user_root(run);

    /* 0b00101 / 0b00111: translation fault (section / page), 0b01101 / 0b01111:
     * permission fault; anything else (alignment, external abort, domain) is
     * the process's own error */
    if (status != 0x05 && status != 0x07 && status != 0x0d && status != 0x0f)
        return 0;
    if (address >= USER_VA_LIMIT || !root || root != aspace_hw_current())
        return 0;
    return kproc_demand_page(run, address,
                             fetch ? ASPACE_PROT_EXEC
                                   : ((fsr & 0x800) ? ASPACE_PROT_WRITE : 0));
}
#endif

/*
 * The resume point of each launch that waits: one jmp_buf per level of
 * Pexec() in progress, innermost first.
 */
struct uspace_launch {
    FS_ERROR_STATE error; /* immutable enclosing scope across setjmp/longjmp */
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
/* Borrowed only during IRQ capture on this CPU. The launcher retains the
 * transferred syscall stack; no ownership is moved into the captured image. */
BOOL arm_process_irq_stack(PD *pd, uintptr_t *base, uintptr_t *top)
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
    ksched_verify_switch_boundary();
    context->pd = run;
    context->root = aspace_hw_current();
    __asm__ volatile ("stmia %0,{sp,lr}^" :: "r"(&context->user_sp) : "memory");
#ifdef __ARM_PCS_VFP
    __asm__ volatile ("vmrs %0,fpscr" : "=r"(context->fpscr));
#endif
    context->launch = get_uspace_launch();
    context->native_task = ksched_context_id();
    context->disk_error = *fs_error_current();
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
    __asm__ volatile ("ldmia %0,{sp,lr}^; nop" :: "r"(&context->user_sp) : "memory");
#ifdef __ARM_PCS_VFP
    __asm__ volatile ("vmsr fpscr,%0" :: "r"(context->fpscr));
#endif
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
     * Control never comes back through arm_enter_user(): the process leaves
     * for user mode, and what comes back is its own Pterm() (or a fault in
     * it), which unwinds to here by longjmp(), so that the Pexec() of the
     * launcher returns the exit code. xterm() restores `run`; deferred
     * finalization keeps the child mappings alive until this safe continuation.
     */
    launch.error = *fs_error_current();
    launch.previous = get_uspace_launch();
    launch.pd = p;
    launch.stack_phys = kstack_phys;
    launch.stack_top = kstack_top;
    launch.exit_process = NULL;
    set_uspace_launch(&launch);
    __asm__ volatile ("mrs %0, cpsr" : "=r" (launcher_status));
    if (setjmp(launch.resume) == 0) {
        aspace_hw_switch(root);
        /* the code of an ELF image was written through the data side (the loader reads the
         * file into the pages of the new space): publish it before the first fetch */
        aspace_hw_code_changed();
#if CONF_WITH_NATIVE_SCHED
        if (prepared) {
            thread_startup_entering(startup);
            arm_enter_prepared_user(prepared, (ULONG)kstack_top);
        }
#endif
        arm_enter_user(entry, user_sp, (ULONG)(uintptr_t)p, (ULONG)kstack_top);
    }
    set_uspace_launch(launch.previous);
    *fs_error_current() = launch.error;
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
    __asm__ volatile ("msr cpsr_c, %0" :: "r" (launcher_status) : "memory", "cc");
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
