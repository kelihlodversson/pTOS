/*
 * font.h - font specific definitions
 *
 * Copyright (C) 2001 Lineo, Inc.
 * Copyright (C) 2004 by Authors (see below)
 * Copyright (C) 2015-2020 The EmuTOS development team
 *
 * Authors:
 *  MAD     Martin Doering
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef FONT_H
#define FONT_H

#include "fonthdr.h"

/* prototypes */

void font_init(void);           /* initialize BIOS font ring */
void font_set_default(void);    /* choose the default font */
#if CONF_WITH_VDI_SCALABLE_FONTS
void font_forms_changed(void);  /* the forms of the system fonts have been replaced (#600) */
#endif

#endif /* FONT_H */
