/*
 * doprintf.h - Header for doprintf()
 *
 * Copyright (C) 2011-2020 The EmuTOS development team
 *
 * Authors:
 *        Eero Tamminen
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef DOPRINTF_H
#define DOPRINTF_H

/* This is an OLD one, and does not support floating point. */
#ifdef LIBAES_CONTEXTS
extern int doprintf_context(void (*outc)(void *, int), void *context, const char *RESTRICT fmt, va_list ap);
#endif
extern int doprintf(void (*outc)(int), const char *RESTRICT fmt, va_list ap);

#endif /* DOPRINTF_H */
