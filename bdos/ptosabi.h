/*
 * ptosabi.h - kernel-side pTOS ABI export tables
 *
 * Copyright (C) 2026 The pTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 *
 * A PTOSABI_TABLE is the versioned set of functions/data pTOS would export
 * under one native ABI namespace (e.g. "aes", "vdi"); bdos/elfld.c resolves
 * a native ELF binary's .ptos.imports entries against these tables at
 * Pexec() time.  See doc/elfload.txt's "Native pTOS ABI imports" section
 * for the on-disk side of this mechanism.
 *
 * No namespace table is registered yet: an earlier iteration of this
 * mechanism exported every GEMDOS (TRAP #1) call this way, but the
 * classic trap1()/TRAP #1 interface already serves that namespace well
 * enough that the added indirection was not worth it there (see
 * doc/elfload.txt). This format and bdos/elfld.c's resolution mechanism
 * are kept as-is, ready for a future namespace (most plausibly aes: or
 * vdi:, once part of one of those moves to userspace) to register a
 * PTOSABI_TABLE the same way and add a lookup arm to
 * bdos/elfld.c's ptosabi_resolve().
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

#endif /* CONF_WITH_PTOS_ABI_IMPORTS */

#endif /* PTOSABI_H */
