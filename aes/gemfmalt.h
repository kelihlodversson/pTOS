/*
 * gemfmalt.h - header for the EmuTOS AES Form Library's form_alert() function
 *
 * Copyright (C) 2002-2020 The EmuTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */
#include "aescontext.h"
#ifndef GEMFMALT_H
#define GEMFMALT_H

OBJECT *fm_alert_tree(AES_ALERT_PARAM WORD defbut, char *palstr, GRECT *rectangle);
WORD fm_alert(WORD defbut, char *palstr);

#endif
