/*
 * delay.c - initialise values used to provide microsecond-order delays
 *
 * note that the timings are quite imprecise (but conservative) unless
 * you are running on at least a 32MHz 68030 processor
 *
 * Copyright (C) 2013-2024 The EmuTOS development team
 *
 * Authors:
 *  RFB    Roger Burrows
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

/* #define ENABLE_KDEBUG */

#include "emutos.h"
#include "biosdefs.h"
#include "mfp.h"
#include "serport.h"
#include "processor.h"
#include "delay.h"
#include "coldfire.h" /* For cookie jar info. */

/*
 * initial 1 millisecond delay loop values
 */
#define LOOPS_68080         38125   /* Apollo 68080 timing measured on Amiga */
#define LOOPS_68060         110000  /* 68060 timing assumes 110MHz for safety */
#define LOOPS_68030         3800    /* 68030 timing assumes 32MHz */
#define LOOPS_68000         760     /* 68000 timing assumes 16MHz */
#define LOOPS_X86_64        110000  /* x86-64: no calibrated clock yet, assumes
                                     * 110MHz for safety -- real hardware and
                                     * any reasonable emulator far exceed this,
                                     * so the resulting delay is never shorter
                                     * than intended, just possibly longer */

#define CALIBRATION_TIME    100     /* target # millisecs to run calibration */

/*
 * global variables
 */
ULONG loopcount_1_msec;

/*
 * function prototypes (functions in delayasm.S)
 */
ULONG run_calibration(ULONG loopcount);
void calibration_timer(void);

/*
 * initialise delay values
 *
 * NOTE: this is called before interrupts are allowed, so initialises
 * the delay values based on processor type.  the main reason for having
 * an early init is to be able to use the Falcon SCC early for debugging
 * purposes.
 */
void init_delay(void)
{
#if defined (MACHINE_FIREBEE) || defined (MACHINE_M548X)
    loopcount_1_msec = SDCLK_FREQUENCY_MHZ * 1000UL;
#elif defined(MACHINE_PC_X86_64)
    /* detect_cpu()'s mcpu here holds a raw CPUID signature, not an
     * m68k-style CPU-type code (see its own comment) -- deliberately
     * never matching the switch below, which would otherwise silently
     * fall through to the 68000 case's LOOPS_68000 (assumes 16MHz),
     * making delay400ns/delay5us (bios/ide.c) round down to zero and
     * silently degenerate to a no-op. Use a dedicated, conservative
     * estimate instead until this arch has a real calibrated delay
     * loop (#329's later milestones).
     */
    loopcount_1_msec = LOOPS_X86_64;
#else
# if CONF_WITH_APOLLO_68080
    if (is_apollo_68080)
        loopcount_1_msec = LOOPS_68080;
    else
# endif
    {
        switch((int)mcpu) {
        case 60:
            loopcount_1_msec = LOOPS_68060;
            break;
        case 40:
        case 30:            /* assumes 68030 */
            loopcount_1_msec = LOOPS_68030;
            break;
        default:            /* assumes 68000 */
            loopcount_1_msec = LOOPS_68000;
        }
    }
#endif

    KDEBUG(("init_delay loopcount_1_msec=%ld\n", loopcount_1_msec));
}

/*
 * calibrate delay values: must only be called *after* interrupts are allowed
 *
 * NOTE1: we use TimerD so we restore the RS232 stuff
 * NOTE2: some systems (e.g. ARAnyM) do not implement TimerD; we leave
 *        the default delay values as-is in this case
 * NOTE3: ColdFire systems are not calibrated, since there is no
 *        independent clock that can be used to measure time
 */
void calibrate_delay(void)
{
#if CONF_WITH_MFP
    ULONG loopcount, intcount;

    /*
     * disable interrupts then run the calibration
     */
    jdisint(MFP_TIMERD);
    loopcount = CALIBRATION_TIME * loopcount_1_msec;
    intcount = run_calibration(loopcount);

    /*
     * disable interrupts then restore the RS232
     * serial port stuff (in case we're using it)
     */
    jdisint(MFP_TIMERD);
    rsconf1(DEFAULT_BAUDRATE, 0, 0x88, 1, 1, 0);   /* just like init_serport() */

    /*
     * intcount is the number of interrupts that occur during 'loopcount'
     * loops.  an interrupt occurs every 1/960 sec (see delayasm.S).
     * so the number of loops per second = loopcount/(intcount/960).
     * so, loops per millisecond = (loopcount*960)/(intcount*1000)
     * = (loopcount*24)/(intcount*25).
     */
    if (intcount)       /* check for valid */
        loopcount_1_msec = (loopcount * 24) / (intcount * 25);
#elif defined(__mcoldfire__)
    loopcount_1_msec = (ULONG)cookie_mcf.sysbus_frequency * 1000;
#else
    KDEBUG(("Warning: loopcount_1_msec isn't calibrated.\n"));
#endif

    KDEBUG(("calibrate_delay loopcount_1_msec=%ld\n", loopcount_1_msec));
}
