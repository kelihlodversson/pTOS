/*
 * gemrslib.h - header for EmuTOS AES Resource Library functions
 *
 * Copyright (C) 2002-2020 The EmuTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */
#include "aescontext.h"
#ifndef GEMRSLIB_H
#define GEMRSLIB_H

#include "config.h"
#include "obdefs.h"                 /* for OBJECT */
#include "rsdefs.h"                 /* for RSHDR */

#include "aesglobal.h"

void rs_obfix(OBJECT *tree, WORD curob);
char *rs_str(UWORD stnum);
WORD rs_free(AESGLOBAL *pglobal);
WORD rs_gaddr(AESGLOBAL *pglobal, UWORD rtype, UWORD rindex, void **rsaddr);
WORD rs_saddr(AESGLOBAL *pglobal, UWORD rtype, UWORD rindex, void *rsaddr);
void rs_fixit(AESGLOBAL *pglobal);
WORD rs_load(AESGLOBAL *pglobal, BYTE *rsfname);
#if CONF_WITH_COLOUR_ICONS && CONF_WITH_VDI_BACKEND_TRUECOLOR
/* Caller supplies plane-major input and w*h*pixel_size bytes of output. */
void rs_cicon_pack(const WORD *data, void *pixels, WORD planes, WORD w, WORD h, WORD pixel_size);
#endif
#if !CONF_WITH_LEGACY_RSC_LOAD
OBJECT *rs_loadmem(AES_RESOURCE_PARAM AESGLOBAL *pglobal, const void *rsmem, LONG size);
#endif

#endif
