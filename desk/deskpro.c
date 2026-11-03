/*      DESKPRO.C       4/18/84 - 03/19/85      Lee Lorenzen            */
/*      for 3.0         3/11/86 - 01/28/87      MDF                     */
/*      merge source    5/27/87 - 5/28/87       mdf                     */

/*
*       Copyright 1999, Caldera Thin Clients, Inc.
*                 2002-2021 The EmuTOS development team
*
*       This software is licenced under the GNU Public License.
*       Please see LICENSE.TXT for further information.
*
*                  Historical Copyright
*       -------------------------------------------------------------
*       GEM Desktop                                       Version 2.3
*       Serial No.  XXXX-0000-654321              All Rights Reserved
*       Copyright (C) 1985 - 1987                       Digital Research Inc.
*       -------------------------------------------------------------
*/

/* #define ENABLE_KDEBUG */

#include "emutos.h"
#include "string.h"

#include "aesdefs.h"
#include "obdefs.h"

#include "deskbind.h"
#include "deskglob.h"
#include "aesbind.h"
#include "desksupp.h"
#include "deskpro.h"
#if CONF_WITH_LIBAES_DESK && CONF_WITH_X32_DESKTOP_TEST
void desk_test_record(const char *message);
#endif


static WORD pro_exec(WORD isgraf, WORD isover, char *pcmd, char *ptail)
{
    WORD ret;

    desk_busy_on();

#if CONF_WITH_LIBAES_DESK
    /* AES4 launches return a child ID; the desktop keeps its event loop. */
    (void)isover;
    ret = shel_write(0, isgraf, 0, pcmd, ptail);
    desk_busy_off();
#if CONF_WITH_X32_DESKTOP_TEST
    {
        char message[160];
        sprintf(message,"x32 desktop LAUNCH %d %s\r\n",ret,pcmd);
        desk_test_record(message);
    }
#endif
#else
    ret = shel_write(SHW_EXEC, isgraf, isover, pcmd, ptail);
    if (!ret)
        desk_busy_off();
#endif
    return ret;
}


/*
 * run a program via shel_write()
 * when leaving the desktop, deselect the icon & zoom to desktop size.
 * Return TRUE only when the desktop must exit for the launched program.
 */
WORD pro_run(WORD isgraf, char *cmd, char *tail, WORD wh, WORD curr)
{
    WORD ret, len;

    tail[0] = len = strlen(tail+1);
    tail[len+2] = 0x0D;     /* follows the nul byte, just like Atari TOS */
    ret = pro_exec(isgraf, 1, cmd, tail);

    if (wh != -1 && !CONF_WITH_LIBAES_DESK)
        do_wopen(FALSE, wh, curr, &G.g_desk);

    if (ret && CONF_WITH_LIBAES_DESK)
        return FALSE; /* A launched child does not terminate this desktop. */
    return ret;
}
