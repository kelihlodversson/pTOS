/*
 * vectors.c - exception vector table setup
 *
 * Copyright (C) 2018-2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#include "portab.h"
#include "bios.h"
#include "tosvars.h"
#include "ikbd.h"
#include "vectors.h"
#include "vt52.h"
#include "xbios.h"
#include "sound.h"
#include "floppy.h"
#include "mfp.h"

/*
 * Deliberate no-ops, unlike ARM's own bios/arch/arm/vectors.c: this arch
 * has real hardware exception vectors (the IDT, bios/arch/x86_64/idt.c)
 * installed well before bios_init() ever runs, so there is no simulated
 * m68k-style low-memory vector table to populate here -- nothing reads
 * one back (bios/arch/x86_64/trap.c's own comment on why the GEMDOS/BIOS/
 * XBIOS vectors specifically need no such table applies equally to the
 * generic CPU exception vectors: idt.c's dispatch always panics
 * unconditionally, never consulting this table for a Setexc()-installed
 * user handler, since no user program can install one yet). Revisit once
 * that changes.
 */
void init_exc_vec(void)
{
}

void init_user_vec(UWORD first_boot)
{
    (void)first_boot;
}

/*
 * This arch has no real vsync/HBL-VBL hardware interrupt (no video chip
 * to raise one -- the EFI GOP framebuffer is plain memory, see bios/
 * machine/pc-x86_64/gop.c), the same situation ARM's raspi/virt machines
 * are in, so VBL is faked off the 200 Hz system tick exactly as
 * bios/arch/arm/vectors.c's own int_vbl()/timer_vbl_hook do -- every 4th
 * tick, i.e. 50 Hz. A machine that later gets a real vsync source can
 * still point timer_vbl_hook elsewhere, same as raspi_vsync.c does on
 * ARM.
 */
volatile LONG vbclock;

void int_vbl(void)
{
    int i;
    PFVOID *vbl_list;

    frclock++;
    if (!(--vblsem)) {
        vbclock++;
        blink();
#if CONF_WITH_FDC
        flopvbl();
#endif
        vbl_list = (PFVOID *)vblqueue;
        if (vbl_list) {
            for (i = 0; i < nvbls; i++) {
                if (vbl_list[i])
                    vbl_list[i]();
            }
        }
    }
    vblsem++;
}

void (*timer_vbl_hook)(void) = int_vbl;

/*
 * vector_5ms's real target now that #335 gives this arch a working
 * 200 Hz timer interrupt (bios/machine/pc-x86_64/pit.c/irq.c) --
 * mirrors bios/arch/arm/vectors.c's own int_timerc() exactly: count the
 * tick, fake a 50 Hz VBL every 4th one via the sieve, and drive keyboard
 * repeat/timeout and the AES/VDI 50 Hz user timer off the same edge.
 */
void int_timerc(void)
{
    hz_200++;
    /* rotate left, as m68k's rol.w: unsigned, or the sign bit smears in
     * and the sieve ends up 0xffff, making every call the "4th" one */
    timer_c_sieve = (WORD)(((UWORD)timer_c_sieve << 1) | ((UWORD)timer_c_sieve >> 15));
    if (timer_c_sieve & 4) {
        kb_timerc_int();
#if CONF_WITH_YM2149
        sndirq();
#endif
        timer_vbl_hook();
        etv_timer(timer_ms);
    }
}
