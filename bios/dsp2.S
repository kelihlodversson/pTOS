/*
 * dsp2.S - assembler functions for DSP handling
 *
 * Copyright (C) 2020-2021 The EmuTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "asmdefs.h"

#if CONF_WITH_DSP

        .globl  C_SYM(dsp_inout_asm)
        .globl  C_SYM(dsp_io_asm)
        .globl  C_SYM(dsp_sv_asm)
        .extern C_SYM(dsp_inout_handler)
        .extern C_SYM(dsp_io_handler)
        .extern C_SYM(dsp_sv_handler)

        .text

/*
 * interrupt handler for Dsp_InStream() & Dsp_OutStream()
 */
C_SYM(dsp_inout_asm):
        movem.l d0-d2/a0-a2,-(sp)       // save registers
        jsr     C_SYM(dsp_inout_handler)
        movem.l (sp)+,d0-d2/a0-a2       // restore registers
        rte

/*
 * interrupt handler for Dsp_IOStream()
 */
C_SYM(dsp_io_asm):
        movem.l d0-d2/a0-a2,-(sp)       // save registers
        jsr     C_SYM(dsp_io_handler)
        movem.l (sp)+,d0-d2/a0-a2       // restore registers
        rte

/*
 * interrupt handler for Dsp_SetVectors()
 */
C_SYM(dsp_sv_asm):
        movem.l d0-d2/a0-a2,-(sp)       // save registers
        jsr     C_SYM(dsp_sv_handler)
        movem.l (sp)+,d0-d2/a0-a2       // restore registers
        rte

#endif  /* CONF_WITH_DSP */
