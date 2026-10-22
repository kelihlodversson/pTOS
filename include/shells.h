/*
 * shells.h - the shell a kernel without one of its own looks for
 *
 * Copyright (C) 2026 The pTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef SHELLS_H
#define SHELLS_H

/*
 * A kernel that is started without a built-in shell (the desktop and the
 * command line are programs, CONF_EXTERNAL_DESK and CONF_EXTERNAL_CLI) looks
 * for one in the root directory of the boot drive, and starts the first of
 * these that is there:
 *
 *   with an AES   EMUDESK.PRG, DESKTOP.PRG, EMUCON.TOS, EMUCON2.TOS,
 *                 COMMAND.TOS, SHELL.PRG, SHELL.TOS
 *   without one   EMUCON.TOS, EMUCON2.TOS, COMMAND.TOS, SHELL.TOS
 *
 * (the programs that need the AES are only looked for when there is one).
 * Both lists end with a NULL.
 */
extern const char *const shell_names_gui[];
extern const char *const shell_names_desk[];    /* the first two of them: the desktops */
extern const char *const shell_names_cli[];

/*
 * The first name of the list, in order, for which start(name, arg) does not
 * say that the file or its directory is not there (EFILNF or EPTHNF); its
 * result is returned, and *found is its name.  EFILNF if none is there.
 */
LONG shell_start(const char *const *names, LONG (*start)(const char *name, void *arg),
                 void *arg, const char **found);

#endif /* SHELLS_H */
