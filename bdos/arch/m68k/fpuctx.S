/*
 * fpuctx.S - the FPU state of a process that launches another (68040, 68060)
 *
 * Copyright (C) 2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

        .cpu    68040

#include "asmdefs.h"

#if CONF_WITH_USER_ASPACE

        .globl  C_SYM(m68k_fpu_save)
        .globl  C_SYM(m68k_fpu_restore)

        .text

/*
 * void m68k_fpu_save(struct fpuctx *ctx);
 * void m68k_fpu_restore(const struct fpuctx *ctx);
 *
 * The FPU state of a process that launches another (uspace.c): the frame of
 * FSAVE and, if the FPU was in use (the header longword of the frame is not
 * zero: the 68060 idle frame is 0x00006000), the
 * registers and the control registers.  Only called when there is an FPU.
 * The layout is struct fpuctx of uspace.c: the frame (216 bytes), the three
 * control registers, fp0-fp7.
 */
FPU_CTL = 216
FPU_REGS = 228

C_SYM(m68k_fpu_save):
#if CONF_WITH_MFASTCALL
        move.l  a0,a1
#else
        move.l  4(sp),a1
#endif
        fsave   (%a1)
        tst.l   (%a1)
        jeq     fpu_save_done            // a NULL frame: nothing in use
        fmovem.x %fp0-%fp7,FPU_REGS(%a1)
        fmovem.l %fpcr/%fpsr/%fpiar,FPU_CTL(%a1)
fpu_save_done:
        rts

C_SYM(m68k_fpu_restore):
#if CONF_WITH_MFASTCALL
        move.l  a0,a1
#else
        move.l  4(sp),a1
#endif
        tst.l   (%a1)
        jeq     fpu_restore_frame
        fmovem.x FPU_REGS(%a1),%fp0-%fp7
        fmovem.l FPU_CTL(%a1),%fpcr/%fpsr/%fpiar
fpu_restore_frame:
        frestore (%a1)
        rts

#endif /* CONF_WITH_USER_ASPACE */
