/*
 * proc.h - processes defines
 *
 * Copyright (C) 2001-2022 The EmuTOS development team.
 *
 * Authors:
 *  LVL   Laurent Vogel
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef PROC_H
#define PROC_H

#include "pghdr.h"

/*
 *  process management
 */

/*
 * in proc.c
 */

long xexec(WORD, char *, char *, char *);
void x0term(void);
void xterm(UWORD rc)  NORETURN ;
WORD xtermres(long blkln, WORD rc);

#ifdef __x86_64__
void x86_64_mark_kernel_code_pd(PD *p);
BOOL x86_64_take_kernel_code_pd(PD *p);
/* in bdos/arch/x86_64/rwa.c */
BOOL x86_64_user_active(void);      /* a ring-3 process is running */
void x86_64_user_fault(ULONG vector, UQUAD error_code, UQUAD rip, UQUAD cr2,
                       UQUAD rsp) NORETURN;
#endif

/*
 * in kpgmld.c
 */

LONG kpgmhdrld(char *s, PGMHDR01 *hd, FH *h);
LONG kpgmld(PD *p, FH h, PGMHDR01 *hd);

#if CONF_WITH_ELF_LOADER
/*
 * in elfld.c
 */
LONG elf_pgmhdrld(FH h, PGMHDR01 *hd);
LONG elf_pgmld(FH h, PD *p);
#endif

#if DETECT_NATIVE_FEATURES
LONG kpgm_relocate( PD *p, long length); /* SOP */
#endif

/*
 * in rwa.S (or, on x86-64, rwa.c -- see that file's own header comment)
 *
 * On m68k/ARM gouser() never returns from this specific call in the ordinary
 * sense: control resumes later via termuser()'s raw-asm jump. On x86-64, a
 * "kernel-code process" launch (aes/gemshlib.c's aes_run_rom_program())
 * is an ordinary nested C call with no trap involved, gouser() DOES
 * genuinely return via setjmp()/longjmp() once the launched process
 * calls Pterm().
 */
#ifdef __x86_64__
void gouser(void);
#else
void gouser(void) NORETURN;
#endif
void termuser(void)  NORETURN;

#endif /* PROC_H */
