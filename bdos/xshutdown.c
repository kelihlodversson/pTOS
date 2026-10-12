/*
 * xshutdown.c - GEMDOS Shutdown() (function 0x151)
 *
 * The sequence is the same everywhere: close every open file, write out the
 * disk buffers, switch the floppy motors off, then stop or restart the
 * machine by whatever means it has (the primitives are in bios/shutdown.c and
 * bios/machine/<name>/shutdown.c).  What a platform cannot do it leaves
 * undone, and the sequence ends in the message and a halt.
 *
 * pTOS is single-tasking, so there is nothing to unwind: no process runs
 * again after this.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "emutos.h"
#include "fs.h"
#include "gemerror.h"
#include "biosext.h"
#include "kprint.h"
#include "shutdown.h"

/* set when a Shutdown() has started; a second one is refused */
static BOOL shutting_down;

/*
 * A disk error in xclose() or flush() leaves through longjmp(errbuf), and
 * nothing installed errbuf for this call (osif() does it only for the
 * ordinary calls), so each of them is guarded here and the rest goes on: a
 * file that cannot be closed or a buffer that cannot be written is lost, the
 * others are not.
 */

/* close every file that is open, whoever owns it */
static void close_files(void)
{
    volatile int i;
    volatile int k;

    for (i = 0; i < OPNFILES; i++)
        /* a slot used more than once (a redirected or inherited standard
         * handle) needs one close for each use before it is free; a disk
         * error gives the slot up */
        for (k = sft[i].f_use; k > 0 && sft[i].f_own; k--)
        {
            if (setjmp(errbuf))
                break;
            xclose(i + NUMSTD);
        }
}

/* write out every dirty disk buffer */
static void sync_buffers(void)
{
    BCB *volatile b;
    volatile int i;

    for (i = 0; i < 2; i++)
        for (b = bufl[i]; b; b = b->b_link)
            if (b->b_bufdrv >= 0 && b->b_dirty && !setjmp(errbuf))
                flush(b);
}

long xshutdown(LONG mode)
{
    if (mode < SHUT_POWER || mode > SHUT_HALT)
        return ERANGE;
    if (shutting_down)
        return EACCDN;       /* already on its way */
    shutting_down = TRUE;

    /* every open file of every process, then the buffers */
    close_files();
    sync_buffers();
    flop_shutdown();

    switch (mode)
    {
    case SHUT_POWER:
        machine_poweroff();
        break;
    case SHUT_BOOT:
        machine_reset(FALSE);
        break;
    case SHUT_COLD:
        machine_reset(TRUE);
        break;
    default:
        break;
    }

    /* SHUT_HALT, or a machine that could not do what was asked */
    cprintf("\033E\r\n  It is now safe to turn off your computer.\r\n");
    kprintf("Shutdown: halted\n");
    halt();
}
