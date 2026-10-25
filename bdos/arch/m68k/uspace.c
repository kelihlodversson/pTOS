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
#include "uaccess.h"

extern long osif(short *pw);                    /* bdos/bdosmain.c */

void gouser(void);
void termuser(void) NORETURN;
extern void m68k_enter_user(void) NORETURN;

/* what m68k_enter_user() (uspaceasm.S) reads */
struct {
    ULONG entry, user_sp, kstack_top;
} uenter;

/* the FPU state of a process (uspaceasm.S): the FSAVE frame (the longest, of
 * the 68881/2, is 216 bytes), fpcr/fpsr/fpiar, fp0-fp7 */
struct fpuctx {
    UBYTE frame[216];
    ULONG ctl[3];
    UBYTE regs[96];
};
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
static jmp_buf *uspace_resume;

void gouser(void)
{
    PD *p = run;
    ASPACE_ROOT root = kproc_user_root(p);
    ULONG entry = (ULONG)kproc_user_entry(p);
    ULONG user_sp = (ULONG)kproc_user_stack(p);
    jmp_buf buf;
    jmp_buf *saved_resume = uspace_resume;
    struct fpuctx fpu;
    uintptr_t kstack_top;
    PADDR kstack_phys = kproc_take_kernel_stack(p, &kstack_top);

    if (!entry)                 /* no image: a basepage with its code in its TPA */
        entry = (ULONG)(uintptr_t)USERPTR_TO_PTR(p->p_tbase);
    if (!root || !kstack_phys || !user_sp)
        panic("process launched without an address space, stack or kernel stack\n");

    /*
     * Control never comes back through m68k_enter_user(): the process leaves
     * for user mode, and what comes back is its own Pterm() (or a fault in
     * it), which unwinds to here by longjmp(), so that the Pexec() of the
     * launcher returns the exit code.  By then xterm() has destroyed the
     * address space of the process (switching to the kernel's) and made the
     * launcher `run` again.
     */
    uspace_resume = &buf;
    /* the FPU of the launcher is what the child will not see, and what the
     * launcher gets back when it ends */
    if (fputype)
        m68k_fpu_save(&fpu);
    if (setjmp(buf) == 0) {
        ULONG initial[2];

        aspace_hw_switch(root);
        /* the code of an ELF image was written through the data side (the loader reads
         * the file into the pages of the new space): publish it before the first fetch */
        aspace_hw_code_changed();
        /* what a program finds on its stack: a return address, and the basepage.
         * Written through the kernel's copy, which backs the page of a lazy stack
         * (a store from here would be a fault of the kernel) */
        if (!kproc_standard_entry(p)) {
            user_sp -= 8;
            initial[0] = 0;
            initial[1] = (ULONG)(uintptr_t)p;
            if (!kproc_copy_to_user(user_sp, initial, sizeof(initial)))
                panic("cannot write the first words of the stack of a process\n");
        }       /* (a standard entry has the initial stack of the ELF ABI, built by the kernel) */
        uenter.entry = entry;
        uenter.user_sp = user_sp;
        uenter.kstack_top = (ULONG)kstack_top;
        m68k_enter_user();
    }
    uspace_resume = saved_resume;
    if (fputype)
        m68k_fpu_restore(&fpu);
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
