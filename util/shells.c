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

LONG shell_start(const char *const *names, LONG (*start)(const char *name, void *arg),
                 void *arg, const char **found)
{
    LONG rc;

    *found = NULL;
    for ( ; *names; names++)
    {
        rc = start(*names, arg);
        if (rc == EFILNF || rc == EPTHNF)
            continue;
        *found = *names;
        return rc;
    }
    return EFILNF;
}
