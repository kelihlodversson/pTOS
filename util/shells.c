/*
 * shells.c - the shell a kernel without one of its own looks for
 *
 * Copyright (C) 2026 The pTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "emutos.h"
#include "gemerror.h"
#include "tosvars.h"
#include "string.h"
#include "shells.h"

const char *const shell_names_gui[] =
{
    "EMUDESK.PRG", "DESKTOP.PRG", "EMUCON.TOS", "EMUCON2.TOS", "COMMAND.TOS",
    "SHELL.PRG", "SHELL.TOS", NULL
};

const char *const shell_names_desk[] =
{
    "EMUDESK.PRG", "DESKTOP.PRG", NULL
};

const char *const shell_names_cli[] =
{
    "EMUCON.TOS", "EMUCON2.TOS", "COMMAND.TOS", "SHELL.TOS", NULL
};

static BOOL is_missing(LONG rc)
{
    return rc == EFILNF || rc == EPTHNF || rc == EDRIVE;
}

LONG shell_start(const char *const *names, LONG (*start)(const char *path, void *arg),
                 void *arg, char *path)
{
    WORD drives[2];
    int nd = 0, d;
    LONG rc;

    drives[nd++] = bootdev;
    if (default_bootdev != bootdev)
        drives[nd++] = default_bootdev;

    for (d = 0; d < nd; d++)
    {
        const char *const *n;

        for (n = names; *n; n++)
        {
            path[0] = 'A' + drives[d];
            path[1] = ':';
            path[2] = '\\';
            strcpy(path + 3, *n);
            rc = start(path, arg);
            if (!is_missing(rc))
                return rc;
        }
    }
    return EFILNF;
}

void shell_drives(char *buf)
{
    *buf++ = 'A' + bootdev;
    *buf++ = ':';
    if (default_bootdev != bootdev)
    {
        strcpy(buf, " or ");
        buf += 4;
        *buf++ = 'A' + default_bootdev;
        *buf++ = ':';
    }
    *buf = '\0';
}
