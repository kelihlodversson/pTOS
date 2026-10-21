/*
 * ptos_ftstdlib.h - FreeType's C library, mapped onto pTOS's own
 *
 * Copyright (C) 2026 The pTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

/*
 * Selected with FT_CONFIG_STANDARD_LIBRARY_H (see vdi/freetype/freetype.mk)
 * in place of FreeType's own ftstdlib.h, which includes the host C
 * library's headers.  pTOS has no C library: the limits come from the
 * compiler, the routines from util/ (util/clibext.c has those only
 * FreeType needs).
 *
 * There is deliberately no FT_FILE, no ft_fopen() & co., no ft_smalloc()
 * & co. and no ft_getenv(): FreeType uses them in its own ftsystem.c and
 * in its debugging code, neither of which is built.  pTOS's system layer
 * is vdi/vdi_ftsystem.c.
 */

#ifndef PTOS_FTSTDLIB_H
#define PTOS_FTSTDLIB_H

#include <stddef.h>
#include "emutos.h"
#include "string.h"
#include "stdlib.h"
#include "setjmp.h"

#define ft_ptrdiff_t  ptrdiff_t

/* integer limits, from the compiler's predefined macros */
#define FT_CHAR_BIT    __CHAR_BIT__
#define FT_USHORT_MAX  (__SHRT_MAX__ * 2U + 1U)
#define FT_INT_MAX     __INT_MAX__
#define FT_INT_MIN     (-__INT_MAX__ - 1)
#define FT_UINT_MAX    (__INT_MAX__ * 2U + 1U)
#define FT_LONG_MIN    (-__LONG_MAX__ - 1L)
#define FT_LONG_MAX    __LONG_MAX__
#define FT_ULONG_MAX   (__LONG_MAX__ * 2UL + 1UL)
#define FT_LLONG_MAX   __LONG_LONG_MAX__
#define FT_LLONG_MIN   (-__LONG_LONG_MAX__ - 1LL)
#define FT_ULLONG_MAX  (__LONG_LONG_MAX__ * 2ULL + 1ULL)

/* the <limits.h> names, which some of FreeType's headers use directly */
#ifndef USHRT_MAX
#define USHRT_MAX      FT_USHORT_MAX
#endif

/* memory and strings */
#define ft_memchr   memchr
#define ft_memcmp   memcmp
#define ft_memcpy   memcpy
#define ft_memmove  memmove
#define ft_memset   memset
#define ft_strcat   strcat
#define ft_strcmp   strcmp
#define ft_strcpy   strcpy
#define ft_strlen   strlen
#define ft_strncmp  strncmp
#define ft_strncpy  strncpy
#define ft_strrchr  strrchr
#define ft_strstr   strstr

/* sorting and numbers */
#define ft_qsort   qsort
#define ft_strtol  strtol

/*
 * non-local jumps: pTOS's jmp_buf is an array, as the standard's, so
 * FreeType's own definitions work unchanged
 */
#define ft_jmp_buf     jmp_buf
#define ft_longjmp     longjmp
#define ft_setjmp( b ) setjmp( *(ft_jmp_buf*) &(b) )

#endif /* PTOS_FTSTDLIB_H */
