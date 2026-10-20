/*
 * fpusave.h - saving the FPU registers on kernel entry (ARM)
 *
 * Copyright (C) 2026 The pTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

/*
 * The kernel is built with the hard-float ABI, so its C code uses the FPU
 * registers as AAPCS lets it: d0-d7 and d16-d31 are its to overwrite (a
 * FreeType call or a memcpy() will), d8-d15 it keeps.  A program that has
 * values in them when it traps into the kernel, or is interrupted, must get
 * them back, and none of the program's bindings say otherwise.  Every
 * entry that runs kernel C code therefore saves them:
 *
 * - an interrupt can come anywhere, and a later multitasking kernel will
 *   switch processes from it: all of d0-d31 and FPSCR (_ALL)
 * - GEMDOS and AES calls can switch to another process (Pexec(), the AES
 *   dispatcher) and come back by another route than the one they left by,
 *   so they too keep the whole state, in the frame that is returned to
 *   (_ALL)
 * - the VDI, BIOS and XBIOS calls return the way they came: what the
 *   kernel may change is saved, that is d0-d7, d16-d31 and FPSCR
 *   (_SCRATCH); the rest the kernel keeps itself
 *
 * What is saved is a block of at most FPU_ALL_BYTES, a multiple of 8 bytes
 * so that the stack stays aligned: FPSCR first (lowest address), then
 * d16-d31 if the FPU has them, then d0-d15 (_ALL) or d0-d7 (_SCRATCH).
 * The macros take a general register that is free to use, and are empty
 * when the kernel does not use the FPU registers (a soft-float build).
 */

#ifndef ARM_FPUSAVE_H
#define ARM_FPUSAVE_H

/* the FPU registers are the kernel's own: hard float; D32 if it has d16-d31 */
#ifdef __ARM_PCS_VFP
# define FPU_SAVE 1
# ifdef __ARM_NEON
#  define FPU_D32 1
# endif
#endif

/* the size of a block of all registers saved, for a frame that holds one */
#ifdef FPU_SAVE
# ifdef FPU_D32
#  define FPU_ALL_BYTES    (8 + 32 * 8)
# else
#  define FPU_ALL_BYTES    (8 + 16 * 8)
# endif
#else
# define FPU_ALL_BYTES     0
#endif

#ifdef ASM_SOURCE

#ifdef FPU_SAVE

/* push all: d0-d15, d16-d31 (if any), FPSCR */
.macro FPU_PUSH_ALL rs
        vpush   {d0-d15}
#ifdef FPU_D32
        vpush   {d16-d31}
#endif
        vmrs    \rs, fpscr
        sub     sp, sp, #8
        str     \rs, [sp]
.endm

/* load all from the block at \base, which moves up past it */
.macro FPU_LOAD_ALL base, rs
        ldr     \rs, [\base], #8
        vmsr    fpscr, \rs
#ifdef FPU_D32
        vldmia  \base!, {d16-d31}
#endif
        vldmia  \base!, {d0-d15}
.endm

.macro FPU_POP_ALL rs
        FPU_LOAD_ALL sp, \rs
.endm

/* push what the kernel's C code may change: d0-d7, d16-d31 (if any), FPSCR */
.macro FPU_PUSH_SCRATCH rs
        vpush   {d0-d7}
#ifdef FPU_D32
        vpush   {d16-d31}
#endif
        vmrs    \rs, fpscr
        sub     sp, sp, #8
        str     \rs, [sp]
.endm

.macro FPU_POP_SCRATCH rs
        ldr     \rs, [sp], #8
        vmsr    fpscr, \rs
#ifdef FPU_D32
        vpop    {d16-d31}
#endif
        vpop    {d0-d7}
.endm

#else /* !FPU_SAVE */

.macro FPU_PUSH_ALL rs
.endm
.macro FPU_LOAD_ALL base, rs
.endm
.macro FPU_POP_ALL rs
.endm
.macro FPU_PUSH_SCRATCH rs
.endm
.macro FPU_POP_SCRATCH rs
.endm

#endif /* FPU_SAVE */

#endif /* ASM_SOURCE */

#endif /* ARM_FPUSAVE_H */
