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
 * The optional x32 VDI namespace resolves vdi:vdi, ABI 1.0, to an entry
 * in the process's read-only user client image. It never exports an LP64
 * kernel function. CONF_WITH_X32_AES additionally registers the typed AES
 * client and caller-local helpers in the aes: namespace, also ABI 1.0.
 * Other namespaces remain unavailable. The table types below describe
 * future namespaces; elfld.c resolves the explicit native client entries.
 */

#ifndef PTOSABI_H
#define PTOSABI_H

#include "config.h"
#include "portab.h"

#if CONF_WITH_PTOS_ABI_IMPORTS

#define PTOSABI_KIND_FUNCTION   0
#define PTOSABI_KIND_DATA       1

/* an export's address: PFLONG for PTOSABI_KIND_FUNCTION, a plain data
 * pointer for PTOSABI_KIND_DATA. A union, not a single PFLONG field,
 * because converting an object pointer to a function pointer type to
 * store it there (or back) is not a conversion ISO C itself guarantees
 * -- even though every architecture this code actually targets (m68k,
 * ARM: flat 32-bit addressing, one pointer representation for code and
 * data alike) makes it safe in practice. Always write and read through
 * the member matching PTOSABI_EXPORT's own "kind" field; never round-trip
 * a value through the other one. */
typedef union {
    PFLONG func;
    void  *data;
} PTOSABI_ADDR;

/* one exported symbol: a bare name within its table's namespace (no
 * "namespace:" prefix -- the caller already matched the namespace to
 * pick this table), its address, and whether it is a function or a
 * data object -- must agree with the import's own recorded kind, since
 * the two are bound very differently by a future FDPIC-aware loader. */
typedef struct {
    const char   *name;
    PTOSABI_ADDR  addr;
    UBYTE         kind;      /* PTOSABI_KIND_* */
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
