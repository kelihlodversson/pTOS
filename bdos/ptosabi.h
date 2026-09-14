/*
 * ptosabi.h - kernel-side pTOS ABI export tables
 *
 * Copyright (C) 2026 The pTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 *
 * A PTOSABI_TABLE is the versioned set of functions/data pTOS exports
 * under one native ABI namespace (e.g. "gemdos"); bdos/elfld.c resolves
 * a native ELF binary's .ptos.imports entries against these tables at
 * Pexec() time.  See doc/elfload.txt's "Native pTOS ABI imports" section
 * for the on-disk side of this mechanism.
 */

#ifndef PTOSABI_H
#define PTOSABI_H

#include "config.h"

#if CONF_WITH_PTOS_ABI_IMPORTS

#define PTOSABI_KIND_FUNCTION   0
#define PTOSABI_KIND_DATA       1

/* one exported symbol: a bare name within its table's namespace (no
 * "namespace:" prefix -- the caller already matched the namespace to
 * pick this table), its address, and whether it is a function or a
 * data object -- must agree with the import's own recorded kind, since
 * the two are bound very differently by a future FDPIC-aware loader. */
typedef struct {
    const char *name;
    PFLONG      addr;
    UBYTE       kind;      /* PTOSABI_KIND_* */
} PTOSABI_EXPORT;

/* one whole namespace's export table, at one ABI major.minor version. */
typedef struct {
    const char           *namespace_name;  /* e.g. "gemdos", no ':' */
    UWORD                  abi_major;
    UWORD                  abi_minor;
    const PTOSABI_EXPORT  *exports;
    UWORD                  nexports;
} PTOSABI_TABLE;

/* bdos/ptosabi_gemdos.c: every implemented GEMDOS (TRAP #1) call */
extern const PTOSABI_TABLE ptosabi_gemdos_table;

#endif /* CONF_WITH_PTOS_ABI_IMPORTS */

#endif /* PTOSABI_H */
