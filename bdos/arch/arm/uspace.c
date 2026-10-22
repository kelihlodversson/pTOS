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
#include "kproc.h"
#include "kprint.h"
#include "biosext.h"
#include "procmem.h"
#include "aspace32.h"
#include "arch/arm/fpusave.h"

extern long osif(long *pw);                     /* bdos/bdosmain.c */

void gouser(void);
void termuser(void) NORETURN;
extern void arm_enter_user(ULONG entry, ULONG user_sp, ULONG basepage, ULONG kstack_top) NORETURN;

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

/*
 * The resume point of each launch that waits: one jmp_buf per level of
 * Pexec() in progress, innermost first.
 */
static jmp_buf *uspace_resume;

void gouser(void)
{
    PD *p = run;
    ASPACE_ROOT root = kproc_user_root(p);
    ULONG entry = (ULONG)kproc_user_entry(p);
    ULONG user_sp = (ULONG)kproc_user_stack(p);
    jmp_buf buf;
    jmp_buf *saved_resume = uspace_resume;
    uintptr_t kstack_top;
    PADDR kstack_phys = kproc_take_kernel_stack(p, &kstack_top);

    if (!entry)                 /* no image: a basepage with its code in its TPA */
        entry = (ULONG)(uintptr_t)USERPTR_TO_PTR(p->p_tbase);
    if (!root || !kstack_phys || !user_sp)
        panic("process launched without an address space, stack or kernel stack\n");

    /*
     * Control never comes back through arm_enter_user(): the process leaves
     * for user mode, and what comes back is its own Pterm() (or a fault in
     * it), which unwinds to here by longjmp(), so that the Pexec() of the
     * launcher returns the exit code.  By then xterm() has destroyed the
     * address space of the process (switching to the kernel's) and made the
     * launcher `run` again.
     */
    uspace_resume = &buf;
    if (setjmp(buf) == 0) {
        aspace_hw_switch(root);
        arm_enter_user(entry, user_sp, (ULONG)(uintptr_t)p, (ULONG)kstack_top);
    }
    uspace_resume = saved_resume;
    kstack_free(kstack_phys);
    {
        ASPACE_ROOT launcher = kproc_user_root(run);

        aspace_hw_switch(launcher ? launcher : aspace_kernel_root());
    }
}

void termuser(void)
{
    /*
     * bdos/proc.c's xterm() already did `run = kproc_get_parent(p);
     * run->p_dreg[0] = rc;`: the exit code is where proc_go()'s caller reads it.
     */
    if (uspace_resume)
        longjmp(*uspace_resume, 1);

    panic("process terminated with no launch to resume, rc=%ld\n", (long)run->p_dreg[0]);
}

#endif /* CONF_WITH_USER_ASPACE */
