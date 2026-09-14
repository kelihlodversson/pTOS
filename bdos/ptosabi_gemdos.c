/*
 * ptosabi_gemdos.c - the "gemdos" native pTOS ABI export table
 *
 * Copyright (C) 2026 The pTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 *
 * Exports every GEMDOS (TRAP #1) call the funcs[] dispatch table in
 * bdos/bdosmain.c actually implements (i.e. every entry there that isn't
 * "NI") as a pTOS ABI import, under the "gemdos" namespace, named after
 * its classic public GEMDOS binding (see include/bdosbind.h and
 * doc/pgmconv.txt-style opcode numbering).  Each entry is the same
 * already-typed internal handler function bdosmain.c's own trap
 * dispatcher calls -- no new implementation, no marshalling layer: a
 * native ELF application importing e.g. "gemdos:Fopen" calls exactly
 * bdos/fsopnclo.c's xopen(), with its real C signature, exactly as if it
 * were any other extern function.
 *
 * This table is looked up by bdos/elfld.c at Pexec() time (see
 * doc/elfload.txt's "Native pTOS ABI imports" section); it is not used
 * by, and does not replace, the classic TRAP #1 dispatcher in
 * bdosmain.c's osif(), which keeps working unchanged for PRG and
 * unpacked/packed ELF binaries that do not import anything.
 */

#include "config.h"

#if CONF_WITH_PTOS_ABI_IMPORTS

#include "emutos.h"
#include "fs.h"
#include "console.h"
#include "proc.h"
#include "mem.h"
#include "time.h"
#include "bdosstub.h"
#include "ptosabi.h"

#define GEMDOS_ABI_MAJOR    1
#define GEMDOS_ABI_MINOR    0

/* F(name, x) declares one export: its classic public GEMDOS name and the
 * internal handler function implementing it.  Every GEMDOS handler is a
 * function (no data exports exist in this namespace), and every one of
 * them, like the FND table in bdosmain.c, is stored as a PFLONG -- the
 * caller's own bdos/elfld.c never calls through this pointer directly
 * with the wrong signature; only a resolved .ptos.bind slot does, using
 * the SDK header's matching prototype (include/ptos-abi/gemdos.h). */
#define F(name, x)  { name, (PFLONG)(x), PTOSABI_KIND_FUNCTION }

static const PTOSABI_EXPORT gemdos_exports[] =
{
    F("Pterm0",   x0term),      /* 0x00 */

    F("Cconin",   xconin),      /* 0x01 */
    F("Cconout",  xconout),     /* 0x02 */
    F("Cauxin",   xauxin),      /* 0x03 */
    F("Cauxout",  xauxout),     /* 0x04 */
    F("Cprnout",  xprtout),     /* 0x05 */
    F("Crawio",   xrawio),      /* 0x06 */
    F("Crawcin",  xrawcin),     /* 0x07 */
    F("Cnecin",   xnecin),      /* 0x08 */
    F("Cconws",   xconws),      /* 0x09 */
    F("Cconrs",   xconrs),      /* 0x0A */
    F("Cconis",   xconstat),    /* 0x0B */

    F("Dsetdrv",  xsetdrv),     /* 0x0E */

    F("Cconos",   xconostat),   /* 0x10 */
    F("Cprnos",   xprtostat),   /* 0x11 */
    F("Cauxis",   xauxistat),   /* 0x12 */
    F("Cauxos",   xauxostat),   /* 0x13 */

#if CONF_WITH_ALT_RAM
    F("Maddalt",  xmaddalt),    /* 0x14 */
#endif

#if CONF_WITH_VIDEL
    F("Srealloc", srealloc),    /* 0x15 */
#endif

    F("Dgetdrv",  xgetdrv),     /* 0x19 */
    F("Fsetdta",  xsetdta),     /* 0x1A */

    F("Tgetdate", xgetdate),    /* 0x2A */
    F("Tsetdate", xsetdate),    /* 0x2B */
    F("Tgettime", xgettime),    /* 0x2C */
    F("Tsettime", xsettime),    /* 0x2D */

    F("Fgetdta",  xgetdta),     /* 0x2F */
    F("Sversion", xgetver),     /* 0x30 */
    F("Ptermres", xtermres),    /* 0x31 */

    F("Dfree",    xgetfree),    /* 0x36 */

    F("Dcreate",  xmkdir),      /* 0x39 */
    F("Ddelete",  xrmdir),      /* 0x3A */
    F("Dsetpath", xchdir),      /* 0x3B */
    F("Fcreate",  xcreat),      /* 0x3C */
    F("Fopen",    xopen),       /* 0x3D */
    F("Fclose",   xclose),      /* 0x3E */
    F("Fread",    xread),       /* 0x3F */
    F("Fwrite",   xwrite),      /* 0x40 */
    F("Fdelete",  xunlink),     /* 0x41 */
    F("Fseek",    xlseek),      /* 0x42 */
    F("Fattrib",  xchmod),      /* 0x43 */
    F("Mxalloc",  xmxalloc),    /* 0x44 */
    F("Fdup",     xdup),        /* 0x45 */
    F("Fforce",   xforce),      /* 0x46 */
    F("Dgetpath", xgetdir),     /* 0x47 */
    F("Malloc",   xmalloc),     /* 0x48 */
    F("Mfree",    xmfree),      /* 0x49 */
    F("Mshrink",  xsetblk),     /* 0x4A */
    F("Pexec",    xexec),       /* 0x4B */
    F("Pterm",    xterm),       /* 0x4C */

    F("Fsfirst",  xsfirst),     /* 0x4E */
    F("Fsnext",   xsnext),      /* 0x4F */

    F("Frename",  xrename),     /* 0x56 */
    F("Fdatime",  xgsdtof),     /* 0x57 */
};

#undef F

const PTOSABI_TABLE ptosabi_gemdos_table =
{
    "gemdos",
    GEMDOS_ABI_MAJOR,
    GEMDOS_ABI_MINOR,
    gemdos_exports,
    ARRAY_SIZE(gemdos_exports)
};

#endif /* CONF_WITH_PTOS_ABI_IMPORTS */
