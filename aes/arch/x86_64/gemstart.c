/*
 * gemstart.c - x86-64 AES start-up code
 *
 * Ports aes/arch/arm/gemstart.S's ui_start()/accdesk_start() to this
 * arch's own conventions. Both are "kernel-code process" entries
 * (bdos/arch/x86_64/rwa.c's gouser(), same as cli/arch/x86_64/cmdasm.c's
 * coma_start()): plain C functions taking one PD * parameter, called
 * directly in ring 0 -- see cmdasm.c's own header comment for why that
 * needs no assembly at all on this arch, unlike ARM/m68k's raw
 * register-passing SVC convention.
 *
 * The one piece that genuinely does need a little assembly is moving
 * execution onto AES process 0's own UDA stack before running any of
 * gem_main()/run_accs_and_desktop() -- see x86_64_call_on_stack()
 * (gemasm.S) and its own comment.
 *
 * Copyright (C) 2025-2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "emutos.h"
#include "struct.h"
#include "aesdefs.h"
#include "aesvars.h"
#include "gemlib.h"
#include "geminit.h"
#include "gemshlib.h"
#include "gemdosif.h"
#include "gemgsxif.h"
#include "bdosbind.h"
#include "string.h"

extern void x86_64_call_on_stack(void *new_rsp, void (*fn)(PD *), PD *arg) NORETURN;

/* aes/geminit.c's own comments mark these "called only from gemstart.S",
 * so they're declared only there, not in any shared header -- this file
 * is x86-64's own equivalent of that call site. */
extern LONG init_p0_stkptr(void);
extern void run_accs_and_desktop(void);
extern void gem_main(void);

void ui_start(PD *p) NORETURN;
void accdesk_start(void) NORETURN; /* aes/geminit.c: "called only from gemstart.S" */

static void ui_start_on_scratch_stack(PD *p) NORETURN;
static void ui_start_run_gem_main(PD *p) NORETURN;

/* Saved across bzero(&D, ...) below, and restored into ad_envrn on
 * every AES restart -- ARM/m68k's own save_ad_envrn (gemstart.S). */
static char *saved_ad_envrn;

/*
 * A small dedicated scratch stack for the bzero(&D, sizeof(THEGLO))/
 * init_p0_stkptr() setup work every AES (re)start does -- ARM/m68k's own
 * "gemusp" temporary stack (aes/arch/arm/gemstart.S). This is NOT an
 * optional nicety: D.g_int[0].a_uda's own u_super[]/u_supstk fields
 * (struct.h) are what init_p0_stkptr() points u_spsuper at, and they sit
 * well within THEGLO's own bounds (g_int is THEGLO's first field) -- so
 * bzero()ing the whole of D while actually running ON the stack that
 * memory backs would overwrite ui_start_on_scratch_stack()'s own live
 * return address and bzero()'s own call frame mid-sweep, long before
 * bzero() itself returns. ARM's own restart loop avoids this the exact
 * same way: switch to gemusp, do the zeroing + recompute u_spsuper
 * there, and only switch onto D.g_int[0].a_uda's own stack once that is
 * done, immediately before calling gem_main() itself. This was found the
 * hard way: an earlier version of this file called bzero() after already
 * switching onto D.g_int[0].a_uda's own stack, corrupting it and
 * crashing (a double fault, rsp underflowed to 0xfffffffffffffff8) the
 * first time ui_start() ever ran under CONF_WITH_AES=y.
 */
static UBYTE ui_start_scratch_stack[4096] __attribute__((aligned(16)));
#define UI_START_SCRATCH_TOP (ui_start_scratch_stack + sizeof(ui_start_scratch_stack))

/*
 * ui_start(PD *p) - AES process 0's own entry point, reached the same
 * way cli/arch/x86_64/cmdasm.c's coma_start() is: bios.c's own
 * exec_os = ui_start (guarded #if CONF_WITH_AES), called directly by
 * bdos/arch/x86_64/rwa.c's gouser() as an ordinary
 * "((void (*)(PD *))exec_os)(p)" call, basepage in %rdi/the first C
 * argument.
 */
void ui_start(PD *p)
{
    LONG newsize;

    /* Mshrink to the needed size for TEXT+DATA+BSS plus the basepage
     * itself -- same computation as coma_start()'s own, see its comment
     * on why sizeof(PD) (not a hardcoded byte count) is correct here. */
    newsize = p->p_tlen + p->p_dlen + p->p_blen + sizeof(PD);
    Mshrink(p, newsize);

    /* ad_stail: address of the cmdline buffer within our own basepage
     * (aes/arch/arm/gemstart.S's own equivalent). p_cmdlin is a real
     * embedded array field (bdosdefs.h), not a USERPTR_T one, so no
     * widen/narrow is needed to get a real pointer to it. */
    ad_stail = p->p_cmdlin;

    /*
     * ARM/m68k's own ui_start() calls Super(0) here to switch from user
     * to supervisor mode before doing any of the rest of this, saving
     * the returned old-SSP in old_gem_ssp to hand back to Super() again
     * just before Pterm0(). Nothing here plays that role: this function
     * is already running in ring 0 (see this file's own header comment
     * on gouser()'s "kernel-code process" launch), the same way
     * coma_start() already is, so there is no privilege level to change
     * and no old_gem_ssp-equivalent value to save or restore.
     */

    /* Save the environment string pointer from the basepage before
     * anything downstream reuses/frees anything else in it -- restored
     * into ad_envrn at the top of every AES restart iteration below. */
    saved_ad_envrn = (char *)USERPTR_TO_PTR(p->p_env);

    x86_64_call_on_stack(UI_START_SCRATCH_TOP, ui_start_on_scratch_stack, p);
}

/*
 * ARM/m68k's own "aes_restart" label, running on the scratch stack (see
 * ui_start_scratch_stack's own comment on why bzero(&D, ...) must not
 * run on D.g_int[0].a_uda's own stack).
 */
static void ui_start_on_scratch_stack(PD *p)
{
    (void)p;

    /* restore original environment pointer */
    ad_envrn = saved_ad_envrn;

    /* clear the 'global memory' zone */
    bzero(&D, sizeof(THEGLO));

    /* drwaddr starts out a benign do-nothing vector until gsx_init()
     * (called from run_accs_and_desktop()) installs the real cursor-draw
     * routine. */
    drwaddr = (PFVOID)justretf;

    /* init_p0_stkptr() (geminit.c) sets D.g_int[0].a_uda.u_spsuper to
     * the top of process 0's own private stack -- only valid from this
     * point on, which is why gem_main() itself only ever runs after
     * switching onto it, immediately below. */
    init_p0_stkptr();

    x86_64_call_on_stack(D.g_int[0].a_uda.u_spsuper, ui_start_run_gem_main, NULL);
}

/*
 * Now running on AES process 0's own UDA stack -- safe at last, since
 * nothing from here on ever bzero()s D as a whole again.
 */
static void ui_start_run_gem_main(PD *p)
{
    (void)p;

    gem_main();

    /*
     * Check for resolution change - if so start over again, by bouncing
     * back to the scratch stack to safely redo the bzero()/
     * init_p0_stkptr() setup -- this arch has no video driver capable of
     * changing resolution yet (#332), so gl_changerez can never actually
     * become nonzero today, but checking it costs nothing and keeps this
     * in step with the arch-neutral contract gem_main()'s own callers
     * are expected to honor, exactly like ARM/m68k's own restart loop.
     */
    if (gl_changerez)
        x86_64_call_on_stack(UI_START_SCRATCH_TOP, ui_start_on_scratch_stack, NULL);

    Pterm0();

    /* Not reached: Pterm0() never returns. */
    for (;;)
        ;
}

/*
 * accdesk_start(void) - runs accessories and the desktop shell, under a
 * separate GEMDOS process from AES process 0 itself (see
 * run_accs_and_desktop()'s own comment in geminit.c on why: so that any
 * memory desk accessories allocate is automatically freed if this
 * process ever terminates). Reached the same way ui_start() is -- via
 * aes_run_rom_program(accdesk_start) (aes/geminit.c) -> dos_exec() ->
 * Pexec(PE_GOTHENFREE, ...) -> gouser()'s own "kernel-code process"
 * branch -- except aes_run_rom_program() builds its own zero-length PD
 * itself and this function, unlike ui_start()/coma_start(), takes no
 * basepage parameter at all (matching ARM/m68k's own accdesk_start,
 * which never uses the one it receives beyond the Mshrink() call ARM's
 * own comment shows -- not needed here since this process is never
 * itself Pexec()'d with a real TEXT/DATA/BSS to shrink to; skip that
 * step entirely, unlike a real loaded program's own entry).
 *
 * Unlike ui_start(), this needs no stack switch of its own: it is
 * called from *inside* gem_main()'s own still-live call chain (already
 * running on D.g_int[0].a_uda.u_spsuper by the time ui_start_run_gem_main()
 * calls gem_main()), so it's already on the right stack, just deeper
 * into it -- exactly like desk/arch/x86_64/deskstart.c's own deskstart(),
 * which never switches stacks either. An earlier version of this
 * function *did* call x86_64_call_on_stack(u_spsuper, ...) here, which
 * was a real bug (Copilot's review of #376 caught it): jumping back to
 * u_spsuper -- the same fixed top of stack gem_main() itself started
 * running from -- discarded gem_main()'s own already-live call frames
 * (aes_run_rom_program()/dos_exec()/Pexec()/gouser(), all still on the
 * stack at this exact point) instead of merely continuing to descend
 * from wherever they currently are, corrupting them the moment
 * run_accs_and_desktop() itself pushed anything. bdos/arch/x86_64/
 * rwa.c's own x86_64_kexec_resume mechanism (added alongside this fix)
 * is what makes it safe to just call this directly now: gouser() saves
 * a resume point right before calling this, and this function's own
 * eventual Pterm0() unwinds back to exactly that point via longjmp(),
 * resuming gem_main()'s own call chain intact.
 */
void accdesk_start(void)
{
    run_accs_and_desktop();

    Pterm0();

    /* Not reached: Pterm0() never returns. */
    for (;;)
        ;
}
