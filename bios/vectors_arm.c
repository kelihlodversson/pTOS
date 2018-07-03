/*
 * vectors_rpi.c - exception vectors
 * The ARM processor has a much leaner exception vector table.
 * In order to simplify porting of the OS (and eventually TSRs hooking into)
 * interrupts, we attempt to simulate the 68k setup by performing some initial
 * decoding in the native handlers and then fetch the address of a 68-k like
 * handler from the same offsets as defined on the 68000.
 *
 * Copyright (C) 2001-2017 by the EmuTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#include "portab.h"
#include "biosext.h"
#include "bios.h"
#include "tosvars.h"
#include "lineavars.h"
#include "ikbd.h"
#include "iorec.h"
#include "vectors.h"
#include "kprint.h"
#include "asm.h"
#include "xbios.h"
#include "sound.h"

// ==== Definitions ==========================================================


// ==== References ===========================================================

// TOS System variables
extern volatile LONG vbclock;
extern void (*etv_timer)(int);
extern const UWORD bios_ent;
extern const UWORD xbios_ent;
extern const PFLONG bios_vecs[];
extern const PFLONG xbios_vecs[];

typedef struct {
    ULONG reg[13];
    ULONG user_sp;
    ULONG user_lr;
    ULONG* user_pc;
    ULONG spsr;
} exception_frame_t;

static void any_vec(int vector_addr, exception_frame_t* stack_frame, ULONG fsr, ULONG far);

/* basically initialize the 62 exception vectors. */
void init_exc_vec(void)
{
    volatile ULONG* vector_addr = (ULONG*)0x8;
    int i;
    proc_lives = 0;
    for(i=0; i<62; i++)
    {
        *(vector_addr++) = (ULONG)any_vec;
    }
}

void init_user_vec(void)
{
    volatile ULONG* vector_addr = (ULONG*)0x100;
    int i;
    for(i=0; i<192; i++)
    {
        *(vector_addr++) = (ULONG)any_vec;
    }

}

// A side effect of that we are using a dispatch routine to calculate the correct
// vector number, the r0 register should contain the (simulated) vector address.
static void any_vec(int vector_addr, exception_frame_t* stack_frame, ULONG fsr, ULONG far)
{
    int i;
    cpsr_id(); // Disable interrupts
    for(i = 0; i<12; i++)
    {
        proc_dregs[i] = stack_frame->reg[i];
    }
    proc_usp = stack_frame->user_sp;
    proc_dregs[12] = stack_frame->user_lr;

    proc_enum = vector_addr >> 2;
    struct {
        ULONG fsr;
        ULONG far;
        ULONG* pc;
        ULONG spsr;
    } *s = (void*)proc_stk;
    s->fsr = fsr;
    s->far = far;
    s->pc =stack_frame->user_pc;
    s->spsr =stack_frame->spsr;

    proc_lives = 0x12345678;

    // Note we assume the stack is okay here as we've already been reading from it
    dopanic("dummy"); // The printf arguments are not used when proc_enum anbd proc_lives are set
}
