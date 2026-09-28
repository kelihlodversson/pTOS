/*
 * gemdosif.c - x86-64 part of the GEMDOS/VDI interface for AES
 *
 * The generic aes/arch/arm/gemdosif.S and aes/arch/m68k/gemdosif.S
 * implement these hooks in raw assembly because on those arches they run
 * on a small dedicated scratch stack switched to for the duration of the
 * call (ARM's own comment: push_privstack/pop_privstack around
 * far_bcha/far_mcha/aes_wheel/tikcod) -- a precaution against running
 * short on stack space in the constrained context a raw SVC/IRQ-mode
 * vector chain leaves them in.
 *
 * On this arch none of that applies: every one of these is already
 * reached from an ordinary, already-safe C call chain with a generously
 * sized stack of its own --
 *   - far_bcha()/far_mcha() are installed as VDI's own linea_vars.user_but/
 *     user_mot (aes/gemgsxif.c's gsx_setmb(), called via vdi_vex_butv()/
 *     vdi_vex_motv()) and called from vdi/arch/x86_64/vdi_entry.c's
 *     mouse_int(), itself reached from bios/machine/pc-x86_64/ps2.c's
 *     x86_64_ps2_mouse_irq() -- an ordinary IDT interrupt-gate handler
 *     (bios/machine/pc-x86_64/irqasm.S), which per its own comment stays
 *     on the regular exception/IRQ machinery's stack throughout.
 *   - tikcod() is installed as etv_timer (bios.c's Setexc(0x100, ...))
 *     and called directly, as a plain C function pointer, from
 *     bios/arch/x86_64/vectors.c's int_timerc() -- the exact same
 *     already-C-safe context #335 already built and tested the rest of
 *     this arch's own timer/keyboard/mouse IRQ handling in.
 *   - drawrat() is a trivial indirect call with no stack concerns at all.
 *
 * Both of those IRQ call chains run with interrupts still disabled at
 * the CPU level throughout (idt.c's interrupt-gate comment; neither
 * irqasm.S's common trampoline nor bios/machine/pc-x86_64/ps2.c's own
 * per-byte state machine ever re-enables them before calling down into
 * here), exactly matching the implicit guarantee ARM's own raw IRQ-mode
 * vector chain provides -- so, like ARM's gemdosif.S, none of the
 * forkq() calls below need their own disable_interrupts()/
 * enable_interrupts() pair (gemdisp.c's forkq() itself already documents
 * that its caller must have interrupts disabled).
 *
 * Copyright (C) 2025-2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "emutos.h"
#include "struct.h"
#include "aesdefs.h"
#include "aesext.h"
#include "aesvars.h"
#include "gemdosif.h"
#include "gemdisp.h"
#include "geminput.h"
#include "gemflag.h"
#include "gemfmlib.h"
#include "biosbind.h"
#include "bdosbind.h"

/* aes/geminput.c's own comments mark these "called only from
 * aes/gemdosif.S", so they're declared only there, not in geminput.h --
 * this file is x86-64's own equivalent of that call site. */
extern void b_click(WORD state);
extern void b_delay(WORD amnt);

/* Arch-neutral storage this file is responsible for defining -- on
 * m68k/ARM these live in the .bss section at the end of gemdosif.S. */
PFVOID drwaddr;
void *tiksav;

LONG NUM_TICK;
LONG CMP_TICK;

/* aesext.h: "extern WORD enable_ceh; /" in gemdosif.S "/" -- this file is
 * x86-64's own equivalent, matching aes/arch/arm/gemdosifc.c's own
 * separate definition (ARM's gemdosif.S itself only reads/writes it). */
WORD enable_ceh;

/*
 * far_bcha() - the VDI's linea_vars.user_but hook (mouse button change).
 * Plain pass-through to b_click(), matching vdi/arch/x86_64/vdi_entry.c's
 * mouse_int()'s own "void (*user_but)(WORD)" call signature.
 */
void far_bcha(WORD state)
{
    b_click(state);
}

/*
 * far_mcha() - the VDI's linea_vars.user_mot hook (mouse motion change).
 * Records the change via forkq(), and returns the (unmodified) proposed
 * position packed the way vdi_entry.c's mouse_int() expects: x in the
 * high word, y in the low word (see default_user_mot(), vdi/vdi_mouse.c,
 * whose exact convention this must match since both are assigned to the
 * same linea_vars.user_mot slot).
 */
ULONG far_mcha(WORD x, WORD y)
{
    ULONG fdata = MAKE_ULONG(x, y);

    forkq(mchange, fdata);
    return fdata;
}

/*
 * drawrat() - redraw the mouse cursor at the given position by jumping
 * through whichever VDI cursor-draw routine drwaddr currently holds
 * (saved/restored around ap_tplay()'s own cursor takeover -- see
 * gemaplib.c).
 */
void drawrat(WORD newx, WORD newy)
{
    ((void (*)(WORD, WORD))drwaddr)(newx, newy);
}

/*
 * tikcod() - AES/VDI's own 50 Hz-ish user timer hook, installed as
 * etv_timer via gsx_tick()'s Setexc(0x100, tikcod) (aes/gemgsxif.c) and
 * called directly by bios/arch/x86_64/vectors.c's int_timerc() at the
 * 200 Hz system tick rate. Ports aes/arch/arm/gemdosif.S's own tikcod
 * logic: count down CMP_TICK, forkq() a tchange once it reaches zero,
 * then chain to whatever etv_timer held before (saved in tiksav by
 * gsx_tick()) -- an ordinary nested call and return, unlike ARM's own
 * tail-jump into tiksav, since this arch's Setexc() is a plain C
 * function-pointer swap (bios.c's own case 0x100), not a CPU exception
 * vector replacement: int_timerc() expects tikcod() to behave like any
 * other etv_timer implementation and actually return to it.
 */
void tikcod(int ms)
{
    if (CMP_TICK != 0) {
        NUM_TICK++;
        CMP_TICK--;
        if (CMP_TICK == 0)
            forkq(tchange, NUM_TICK);
    }

    b_delay(1);

    if (tiksav)
        ((void (*)(int))tiksav)(ms);
}

/*
 * justretf() - the do-nothing placeholder vector ui_start() installs as
 * the initial drwaddr (aes/arch/arm/gemstart.S's own "set to just_rts"
 * comment): a benign no-op until gsx_init() (called from
 * run_accs_and_desktop(), aes/geminit.c) installs the real cursor-draw
 * routine.
 */
void justretf(void)
{
}

/*
 * set_aestrap()/unset_aestrap(): on m68k/ARM these install/restore
 * AES's own trap#2 handler over whatever raw CPU vector was there
 * before (chaining to it for ordinary VDI opcodes -- see aes/arch/arm/
 * gemdosif.S's own aestrap). There is no equivalent concept on this
 * arch: trap#2 is not a real, independently-swappable CPU vector here
 * at all -- bios/arch/x86_64/trap.c's X86_64_TRAP_GEM dispatch always
 * handles it directly (AES's two meta-opcodes to super(), everything
 * else straight to GSX_ENTRY()), unconditionally, the same way on every
 * boot; nothing else could ever have "gotten there first" the way a
 * third-party driver like NVDI can hook m68k/ARM's real trap#2 vector.
 * No-ops, therefore, rather than something to genuinely install or
 * restore.
 */
void set_aestrap(void)
{
}

void unset_aestrap(void)
{
}

/*
 * This table converts an error number to an (internal-only) alert
 * number: entry[n-1] contains the alert number to use for error -n.
 * Must stay synchronized with gemfmlib.c's own alert-string arrays --
 * see aes/arch/arm/gemdosifc.c's identical copy of this same table for
 * the full explanation; kept here rather than shared since every other
 * arch's own copy lives in its own arch-specific trap#2 implementation
 * too (raw assembly there, this file's C equivalent here).
 */
static const WORD err_tbl[17] = {
        4,1,1,2,1,1,2,2,     /* errors -1 to -8 */
        4,2,2,2,0,3,4,2,     /* errors -9 to -16 */
        5                   /* error -17 (EOTHER, currently not implemented) */
};

typedef LONG (*criterr_handler_t)(WORD, WORD);
static criterr_handler_t save_etv_critic;
static ULONG *save_super;

/*
 * default critical error handler in graphics mode -- ports aes/arch/
 * arm/gemdosifc.c's own criterr_handler() verbatim aside from its
 * return type (LONG here, matching etv_critic's real "LONG (*)(WORD,
 * WORD)" type exactly -- bios.c's own etv_critic assignment -- rather
 * than relying on AAPCS's int/LONG coincidence the way ARM's own int
 * does). See that file's own NOTE 1/NOTE 2 comments for why
 * u_spsuper/the live stack pointer are saved and restored around
 * eralert(), and why enable_ceh gates falling back to the previous
 * handler until the desktop's main loop is actually running.
 */
static LONG criterr_handler(WORD error, WORD drive)
{
    LONG retval;
    WORD error_index;

    if (!enable_ceh)
        return save_etv_critic(error, drive);

    save_super = rlr->p_uda->u_spsuper;

    error_index = ~error;
    if (error_index > 16 || error_index < 0)
        error_index = 0;

    retval = eralert(err_tbl[error_index], drive) ? 0x00010000 : 0;

    rlr->p_uda->u_spsuper = save_super;

    return retval;
}

/*
 * DOS error trapping code: restores the critical error vector -- called
 * when entering graphics mode to run a graphics application after a
 * character mode application (which may have stepped on it). Unlike
 * ARM/m68k's own retake(), there is no "VEC_GEM = aestrap" line here:
 * see set_aestrap()'s own comment on why that has no equivalent on this
 * arch.
 */
void retake(void)
{
    Setexc(0x0101, (long)criterr_handler);
}

/*
 * restore the previous critical error handler -- called when leaving
 * graphics mode, either to run a character-based application, or
 * during desktop shutdown.
 */
void giveerr(void)
{
    Setexc(0x0101, (long)save_etv_critic);
}

/*
 * install the GEM critical error handler -- called during desktop
 * startup, just before entering graphics mode.
 */
void takeerr(void)
{
    enable_ceh = 0;
    save_etv_critic = (criterr_handler_t)Setexc(0x0101, -1);
    Setexc(0x0101, (long)criterr_handler);
}

/*
 * dos_exec() - a wrapper around Pexec(), called instead of using the
 * Pexec() macro directly (aes/gemshlib.c's aes_run_rom_program()/
 * sh_ldapp()) "for AES reentrancy issues".
 *
 * ARM/m68k's own dos_exec() (aes/arch/arm/gemstart.S's own lengthy
 * comment) must temporarily redirect rlr->p_uda->u_spsuper to a safe
 * point on its own live stack before calling Pexec(): on those arches,
 * the raw trap#2 entry unconditionally switches to whatever
 * rlr->p_uda->u_spsuper currently holds on *every* call, including the
 * newly launched child's very first one, and that value is otherwise
 * left stale (pointing at wherever AES process 0 was last suspended,
 * not anywhere on dos_exec()'s own current call chain) -- corrupting
 * dos_exec()'s own stack the moment the child makes its first AES call.
 *
 * That hazard does not exist here: bios/arch/x86_64/trap.c's dispatch
 * never switches %rsp based on rlr->p_uda->u_spsuper (or anything else)
 * on entry -- a "kernel-code process" launch (ui_start/accdesk_start/
 * deskstart/coma_start, bdos/arch/x86_64/rwa.c's gouser()) is an
 * ordinary nested C function call, all the way down, on whatever stack
 * dos_exec() itself is already running on. u_spsuper is touched only by
 * this arch's own dsptch()/switchto() (gemasm.S), and only ever set to
 * wherever the *live* stack genuinely is at the moment a process
 * suspends itself -- never stale, and never in need of protecting from
 * a raw trap prologue that, on this arch, simply doesn't exist. A plain
 * passthrough is therefore correct here, not merely simpler: there is
 * nothing left for it to protect against.
 *
 * (A real loaded process launched from here, e.g. sh_ldapp()'s
 * PE_LOADGO case, has its own separate, already-documented limitation --
 * bdos/arch/x86_64/rwa.c's own top-of-file comment on this arch's
 * single shared percpu.kernel_rsp -- unrelated to the hazard this
 * function's ARM/m68k equivalent exists to avoid.)
 */
LONG dos_exec(WORD mode, const char *path, const char *tail, const char *env)
{
    return Pexec(mode, path, tail, env);
}
