/*
 * stdlib.h - pTOS's own version of the parts of the ANSI header it has
 *
 * Copyright (C) 2026 The pTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef STDLIB_H
#define STDLIB_H

#include <stddef.h>

/* only FreeType needs these, see util/clibext.c */
void qsort(void *base, size_t nmemb, size_t size,
           int (*compar)(const void *, const void *));
long strtol(const char *RESTRICT nptr, char **RESTRICT endptr, int base);

#endif /* STDLIB_H */
