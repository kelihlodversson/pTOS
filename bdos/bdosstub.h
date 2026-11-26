/*
 * bdosstub.h - BDOS entry points, called by BIOS
 *
 * Copyright (C) 2019-2021 The EmuTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef _BDOSSTUB_H
#define _BDOSSTUB_H

#include "bdosdefs.h"

/* BDOS initialization, called after BIOS initialization.
 * This is done in 2 parts: before and after Alt-RAM is available */
void osinit_before_xmaddalt(void);
void osinit_after_xmaddalt(void);

#if CONF_WITH_ALT_RAM
/* Register an Alt-RAM region to BDOS */
LONG xmaddalt(UBYTE *start, LONG size);

/* Get the total size of Alt-RAM regions */
LONG total_alt_ram(void);
#endif /* CONF_WITH_ALT_RAM */

/* BDOS quick pool.
 * Declared here because referenced by the BIOS OSHEADER */
#define MAXQUICK 5
extern WORD *root[MAXQUICK];

/* Pointer to the basepage of the current process.
 * Declared here because referenced by the BIOS OSHEADER,
 * and also by bios/kprint.c */
extern PD *run;

/* GEMDOS file calls for kernel code, without a trap (bdos/kfile.c).
 * The arguments are as for Fopen(), Fclose(), Fread() and Fseek(), as longs. */
long kdos_fopen(long name, long mode);
long kdos_fclose(long h);
struct disk_error;
long kdos_fclose_error(long h, struct disk_error *error);
long kdos_fread(long h, long count, long buf);
long kdos_fseek(long offset, long h, long mode);
long kdos_fsfirst_at(long name, long att, DTA *dta);
long kdos_fsnext_at(DTA *dta);

#if CONF_WITH_USER_ASPACE || CONF_WITH_ARM_SHARED_EXEC
struct dl_start_vectors;
/* Trusted kernel launch. The loader consumes the vectors before entry. */
long proc_exec_kernel_vectors(WORD mode, const char *path,
                              const struct dl_start_vectors *vectors);
#if ARCH_X86_64 && CONF_WITH_ELF_LOADER
struct prog_image;
LONG kproc_set_embedded_elf(PD *pd, const struct prog_image *image, const char *filename,
                           const struct dl_start_vectors *vectors);
#endif
#endif

/* BDOS current date/time.
 * Declared here because also updated by XBIOS Settime() */
extern UWORD current_date, current_time;

#endif /* _BDOSSTUB_H */
