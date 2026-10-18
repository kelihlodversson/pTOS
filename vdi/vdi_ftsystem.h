/*
 * vdi_ftsystem.h - pTOS's system layer for FreeType, for the VDI's use
 *
 * Copyright (C) 2026 The pTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef VDI_FTSYSTEM_H
#define VDI_FTSYSTEM_H

/*
 * The FreeType library the VDI uses, set up while the system starts; NULL
 * if that failed, in which case there are no scalable fonts.  Every call
 * into FreeType goes through vdi_ft_call(), which runs it on FreeType's
 * own stack (see vdi_ftsystem.c).
 */
extern struct FT_LibraryRec_ *vdi_ft_library;

long vdi_ft_call(long (*fn)(void *), void *arg);

#endif /* VDI_FTSYSTEM_H */
