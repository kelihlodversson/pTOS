/*
 * vdi_gdos.h - the built-in GDOS layer, for the rest of the VDI
 *
 * Copyright (C) 2026 The pTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef VDI_GDOS_H
#define VDI_GDOS_H

#include "fonthdr.h"

#if CONF_WITH_VDI_SCALABLE_FONTS

/*
 * What vq_gdos() answers when the built-in GDOS is active: '_FSM', as
 * SpeedoGDOS does.  0 while it is not (before the AUTO folder has run,
 * or when a GDOS was loaded from disk), and the trap #2 handlers then
 * leave d0 at -2.  Read by the trap #2 entry of each architecture.
 */
extern ULONG vdi_gdos_id;

#define GDOS_ID_FSM         0x5f46534dUL    /* '_FSM' */

/* font_format of a scalable font, as NVDI's vqt_ext_name() reports it */
#define SFONT_SPEEDO        0x02
#define SFONT_TRUETYPE      0x04
#define SFONT_TYPE1         0x08

/* its flags */
#define SFONT_MONOSPACE     0x01

/* a scalable font the built-in GDOS found */
typedef struct
{
    WORD id;                        /* its font id, stable from boot to boot */
    UBYTE format;                   /* SFONT_TRUETYPE, ... */
    UBYTE flags;                    /* SFONT_MONOSPACE */
    char name[FONT_NAME_LEN + 1];   /* "Family Style", as vqt_name() gives it */
    char file[14];                  /* its 8.3 file name in CONF_VDI_FONT_DIR */
} VDI_SFONT;

/* the scalable fonts, sorted by name; none unless the GDOS is active */
extern WORD vdi_sfont_count;
extern VDI_SFONT *vdi_sfonts;

#endif /* CONF_WITH_VDI_SCALABLE_FONTS */

#endif /* VDI_GDOS_H */
