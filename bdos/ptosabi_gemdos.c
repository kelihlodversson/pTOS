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
 * its official GEMDOS function name as documented by tos.hyp
 * (https://freemint.github.io/tos.hyp/en/gemdos_functions.html) --
 * verified name-for-name against that page's function list, not just
 * inferred from include/bdosbind.h's own (partial) macro set.
 *
 * Every entry is a thin wrapper around trap1()/trap1_pexec()
 * (util/arch/{m68k,arm}/miscasm.S, declared in "asm.h"), the same TRAP #1 (m68k)
 * / SVC (ARM) entry point include/bdosbind.h's own macros already use --
 * not the raw internal handler bdosmain.c's funcs[] table calls. This is
 * deliberate, not an indirection for its own sake: bdos/proc.c's
 * proc_go() starts every launched process in genuine CPU user mode (see
 * its own comment: "the process will start in user mode" on both m68k,
 * clearing SR's S-bit, and ARM, an SPSR with mode bits 0x10/usr) -- a
 * native ABI import slot is called directly by that user-mode process,
 * with no trap in between, so binding it to a raw handler address would
 * run kernel-internal code, including anything that touches a privileged
 * instruction (e.g. disable_interrupts()/enable_interrupts(), asm.h),
 * at user privilege: undefined behaviour on real hardware, not merely
 * "unsupported". The same bypass also loses bdosmain.c's osif() itself:
 * its per-call setjmp(errbuf) hard-error catch frame (see bdos/fs.h's
 * longjmp_rwabs()/longjmp(errbuf,...), reachable from any filesystem
 * call) and its standard-device redirection (the "std funcs" block
 * osif() runs before funcs[] for fn<12 etc., honouring a prior
 * Fforce()). trap1()/trap1_pexec() cannot be bypassed by construction:
 * they only ever reach osif() the normal way, by executing a real
 * "trap #1"/"svc 1" instruction and letting the CPU itself perform the
 * user -> supervisor transition, so every one of those three properties
 * -- privilege, hard-error recovery, device redirection -- comes for
 * free, identically to any classic PRG bound against bdosbind.h. bdos/
 * itself already relies on this same trap1() gate from supervisor-mode
 * kernel code for the identical reason -- see bios/bios.c, aes/gemshlib.c,
 * vdi/vdi_control.c and their own "#include bdosbind.h" -- so this is
 * an established pattern here, not a new one.
 *
 * Sversion is the one deliberate exception: bdosmain.c's xgetver() is a
 * pure compile-time constant with no privileged instruction, no disk
 * I/O, and no dependency on osif()'s redirection or hard-error recovery,
 * so it is exported directly, with no trap1() indirection at all -- an
 * optimisation that is safe only because of those specific properties,
 * not a precedent for exporting any other handler this way.
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
#include "asm.h"
#include "ptosabi.h"

/* bdosmain.c: see this file's own top comment on why Sversion is
 * exported directly rather than through a trap1() wrapper like every
 * other entry below. */
long xgetver(void);

#define GEMDOS_ABI_MAJOR    1
#define GEMDOS_ABI_MINOR    0

/* F(name, x) declares one export: its classic public GEMDOS name and the
 * trap1()-based wrapper function below implementing it. Every GEMDOS
 * export is a function (no data exports exist in this namespace), and
 * every one of them, like the FND table in bdosmain.c, is stored as a
 * PFLONG -- the caller's own bdos/elfld.c never calls through this
 * pointer directly with the wrong signature; only a resolved
 * .ptos.bind slot does, using the SDK header's matching prototype
 * (include/ptos-abi/gemdos.h). */
#define F(name, x)  { name, (PFLONG)(x), PTOSABI_KIND_FUNCTION }

/* Wrapper bodies below are intentionally one-line calls to trap1()/
 * trap1_pexec(), each opcode taken directly from its own GEMDOS number
 * (also given in the export table's comments below and matching
 * include/bdosbind.h's own opcodes for every call that header also
 * defines). trap1() takes a maximum of 3 real arguments beyond the
 * opcode on ARM (util/arch/arm/miscasm.S: "Can only handle max 3
 * arguments"); every wrapper below except Pexec, which uses the
 * dedicated trap1_pexec() stub exactly like bdosbind.h's own Pexec()
 * macro does, fits that limit. A handful of exports give a real
 * argument to a trap block slot include/bdosbind.h's own macro hardcodes
 * to 0 (Mshrink's leading WORD, Frename's leading WORD): that slot is
 * real GEMDOS wire format either way, bdosmain.c's osif() reads it the
 * same way regardless of what value is there, and include/ptos-abi/
 * gemdos.h already commits to exposing it (matching the internal
 * xsetblk()/xrename() handlers' own real parameter, not the classic
 * macro's convention of always passing 0 there). */

static long ptosabi_Pterm0(void)
{ return trap1(0x00); }

static long ptosabi_Cconin(void)
{ return trap1(0x01); }

static long ptosabi_Cconout(int ch)
{ return trap1(0x02, ch); }

static long ptosabi_Cauxin(void)
{ return trap1(0x03); }

static long ptosabi_Cauxout(int ch)
{ return trap1(0x04, ch); }

static long ptosabi_Cprnout(int ch)
{ return trap1(0x05, ch); }

static long ptosabi_Crawio(int parm)
{ return trap1(0x06, parm); }

static long ptosabi_Crawcin(void)
{ return trap1(0x07); }

static long ptosabi_Cnecin(void)
{ return trap1(0x08); }

static void ptosabi_Cconws(char *p)
{ trap1(0x09, p); }

static void ptosabi_Cconrs(char *p)
{ trap1(0x0A, p); }

static long ptosabi_Cconis(void)
{ return trap1(0x0B); }

static long ptosabi_Dsetdrv(int drv)
{ return trap1(0x0E, drv); }

static long ptosabi_Cconos(void)
{ return trap1(0x10); }

static long ptosabi_Cprnos(void)
{ return trap1(0x11); }

static long ptosabi_Cauxis(void)
{ return trap1(0x12); }

static long ptosabi_Cauxos(void)
{ return trap1(0x13); }

#if CONF_WITH_ALT_RAM
static long ptosabi_Maddalt(unsigned char *start, long size)
{ return trap1(0x14, start, size); }
#endif

#if CONF_WITH_VIDEL
static void *ptosabi_Srealloc(long amount)
{ return (void *)trap1(0x15, amount); }
#endif

static long ptosabi_Dgetdrv(void)
{ return trap1(0x19); }

static void ptosabi_Fsetdta(void *dta)
{ trap1(0x1A, dta); }

static long ptosabi_Tgetdate(void)
{ return trap1(0x2A); }

static long ptosabi_Tsetdate(unsigned short d)
{ return trap1(0x2B, d); }

static long ptosabi_Tgettime(void)
{ return trap1(0x2C); }

static long ptosabi_Tsettime(unsigned short t)
{ return trap1(0x2D, t); }

static void *ptosabi_Fgetdta(void)
{ return (void *)trap1(0x2F); }

static short ptosabi_Ptermres(long blkln, short rc)
{ return (short)trap1(0x31, blkln, rc); }

static long ptosabi_Dfree(long *buf, int drv)
{ return trap1(0x36, buf, drv); }

static long ptosabi_Dcreate(char *path)
{ return trap1(0x39, path); }

static long ptosabi_Ddelete(char *path)
{ return trap1(0x3A, path); }

static long ptosabi_Dsetpath(char *path)
{ return trap1(0x3B, path); }

static long ptosabi_Fcreate(char *name, unsigned char attr)
{ return trap1(0x3C, name, attr); }

static long ptosabi_Fopen(char *name, int mode)
{ return trap1(0x3D, name, mode); }

static long ptosabi_Fclose(int h)
{ return trap1(0x3E, h); }

static long ptosabi_Fread(int h, long len, void *buf)
{ return trap1(0x3F, h, len, buf); }

static long ptosabi_Fwrite(int h, long len, void *buf)
{ return trap1(0x40, h, len, buf); }

static long ptosabi_Fdelete(char *name)
{ return trap1(0x41, name); }

static long ptosabi_Fseek(long offset, int h, int mode)
{ return trap1(0x42, offset, h, mode); }

static long ptosabi_Fattrib(char *name, int wflag, unsigned char attrib)
{ return trap1(0x43, name, wflag, attrib); }

static void *ptosabi_Mxalloc(long amount, int mode)
{ return (void *)trap1(0x44, amount, mode); }

static long ptosabi_Fdup(int h)
{ return trap1(0x45, h); }

static long ptosabi_Fforce(int std, int h)
{ return trap1(0x46, std, h); }

static long ptosabi_Dgetpath(char *buf, int drv)
{ return trap1(0x47, buf, drv); }

static void *ptosabi_Malloc(long amount)
{ return (void *)trap1(0x48, amount); }

static long ptosabi_Mfree(void *addr)
{ return trap1(0x49, addr); }

static long ptosabi_Mshrink(int n, void *blk, long newlen)
{ return trap1(0x4A, n, blk, newlen); }

static long ptosabi_Pexec(short mode, char *path, char *tail, char *env)
{ return trap1_pexec(mode, path, tail, env); }

static void ptosabi_Pterm(unsigned short rc)
{ trap1(0x4C, rc); }

static long ptosabi_Fsfirst(char *name, int attr)
{ return trap1(0x4E, name, attr); }

static long ptosabi_Fsnext(void)
{ return trap1(0x4F); }

static long ptosabi_Frename(int n, char *oldname, char *newname)
{ return trap1(0x56, n, oldname, newname); }

static long ptosabi_Fdatime(void *timeptr, int h, int wflag)
{ return trap1(0x57, timeptr, h, wflag); }

static const PTOSABI_EXPORT gemdos_exports[] =
{
    F("Pterm0",   ptosabi_Pterm0),   /* 0x00 */

    F("Cconin",   ptosabi_Cconin),   /* 0x01 */
    F("Cconout",  ptosabi_Cconout),  /* 0x02 */
    F("Cauxin",   ptosabi_Cauxin),   /* 0x03 */
    F("Cauxout",  ptosabi_Cauxout),  /* 0x04 */
    F("Cprnout",  ptosabi_Cprnout),  /* 0x05 */
    F("Crawio",   ptosabi_Crawio),   /* 0x06 */
    F("Crawcin",  ptosabi_Crawcin),  /* 0x07 */
    F("Cnecin",   ptosabi_Cnecin),   /* 0x08 */
    F("Cconws",   ptosabi_Cconws),   /* 0x09 */
    F("Cconrs",   ptosabi_Cconrs),   /* 0x0A */
    F("Cconis",   ptosabi_Cconis),   /* 0x0B */

    F("Dsetdrv",  ptosabi_Dsetdrv),  /* 0x0E */

    F("Cconos",   ptosabi_Cconos),   /* 0x10 */
    F("Cprnos",   ptosabi_Cprnos),   /* 0x11 */
    F("Cauxis",   ptosabi_Cauxis),   /* 0x12 */
    F("Cauxos",   ptosabi_Cauxos),   /* 0x13 */

#if CONF_WITH_ALT_RAM
    F("Maddalt",  ptosabi_Maddalt),  /* 0x14 */
#endif

#if CONF_WITH_VIDEL
    F("Srealloc", ptosabi_Srealloc), /* 0x15 */
#endif

    F("Dgetdrv",  ptosabi_Dgetdrv),  /* 0x19 */
    F("Fsetdta",  ptosabi_Fsetdta),  /* 0x1A */

    F("Tgetdate", ptosabi_Tgetdate), /* 0x2A */
    F("Tsetdate", ptosabi_Tsetdate), /* 0x2B */
    F("Tgettime", ptosabi_Tgettime), /* 0x2C */
    F("Tsettime", ptosabi_Tsettime), /* 0x2D */

    F("Fgetdta",  ptosabi_Fgetdta),  /* 0x2F */
    F("Sversion", xgetver),          /* 0x30 -- direct: see this file's top comment */
    F("Ptermres", ptosabi_Ptermres), /* 0x31 */

    F("Dfree",    ptosabi_Dfree),    /* 0x36 */

    F("Dcreate",  ptosabi_Dcreate),  /* 0x39 */
    F("Ddelete",  ptosabi_Ddelete),  /* 0x3A */
    F("Dsetpath", ptosabi_Dsetpath), /* 0x3B */
    F("Fcreate",  ptosabi_Fcreate),  /* 0x3C */
    F("Fopen",    ptosabi_Fopen),    /* 0x3D */
    F("Fclose",   ptosabi_Fclose),   /* 0x3E */
    F("Fread",    ptosabi_Fread),    /* 0x3F */
    F("Fwrite",   ptosabi_Fwrite),   /* 0x40 */
    F("Fdelete",  ptosabi_Fdelete),  /* 0x41 */
    F("Fseek",    ptosabi_Fseek),    /* 0x42 */
    F("Fattrib",  ptosabi_Fattrib),  /* 0x43 */
    F("Mxalloc",  ptosabi_Mxalloc),  /* 0x44 */
    F("Fdup",     ptosabi_Fdup),     /* 0x45 */
    F("Fforce",   ptosabi_Fforce),   /* 0x46 */
    F("Dgetpath", ptosabi_Dgetpath), /* 0x47 */
    F("Malloc",   ptosabi_Malloc),   /* 0x48 */
    F("Mfree",    ptosabi_Mfree),    /* 0x49 */
    F("Mshrink",  ptosabi_Mshrink),  /* 0x4A */
    F("Pexec",    ptosabi_Pexec),    /* 0x4B */
    F("Pterm",    ptosabi_Pterm),    /* 0x4C */

    F("Fsfirst",  ptosabi_Fsfirst),  /* 0x4E */
    F("Fsnext",   ptosabi_Fsnext),   /* 0x4F */

    F("Frename",  ptosabi_Frename),  /* 0x56 */
    F("Fdatime",  ptosabi_Fdatime),  /* 0x57 */
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
