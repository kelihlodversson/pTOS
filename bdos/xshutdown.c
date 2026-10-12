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

/* write out every dirty disk buffer */
static void sync_buffers(void)
{
    BCB *b;
    int i;

    if (setjmp(errbuf))
        return;                 /* a write error: the rest is lost, go on */

    for (i = 0; i < 2; i++)
        for (b = bufl[i]; b; b = b->b_link)
            if (b->b_bufdrv >= 0 && b->b_dirty)
                flush(b);
}

long xshutdown(LONG mode)
{
    int i;

    if (mode < SHUT_POWER || mode > SHUT_HALT)
        return ERANGE;
    if (shutting_down)
        return EACCDN;       /* already on its way */
    shutting_down = TRUE;

    /* every open file of every process, then the buffers */
    for (i = 0; i < OPNFILES; i++)
        if (sft[i].f_own)
            xclose(i + NUMSTD);
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
