/*
 * shutdown.c - restart or power off, for Shutdown() (bdos/xshutdown.c)
 *
 * The default for a machine that has nothing of its own: a restart is the
 * CPU's warm_reset()/cold_reset() (panicasm.S), and there is no power off.
 * A machine with its own way replaces this file with
 * bios/machine/<name>/shutdown.c, which provides the same two functions.
 *
 * Copyright (C) 2025-2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "emutos.h"
#include "biosext.h"
#include "bios.h"

void machine_reset(BOOL cold)
{
    if (cold)
        cold_reset();
    else
        warm_reset();
}

void machine_poweroff(void)
{
#if CONF_WITH_SHUTDOWN
    /* what the desktop's Quit does: the emulator's native feature, the
     * FireBee's PIC, the Amiga's ...; comes back where it cannot */
    shutdown();
#endif
}
