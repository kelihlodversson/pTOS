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
 * gouser() is NOT marked NORETURN: on m68k/ARM it never returns from
 * this specific call in the ordinary sense (control instead resumes,
 * much later, via termuser()'s own raw-asm jump into the middle of
 * gouser()'s own body, invisible to the compiler either way, so the
 * annotation was previously harmless there); on x86-64, where a
 * "kernel-code process" launch (aes/gemshlib.c's aes_run_rom_program())
 * is an ordinary nested C call with no trap involved, gouser() DOES
 * genuinely return via setjmp()/longjmp() once the launched process
 * calls Pterm() -- marking it NORETURN there risked the compiler
 * eliding proc_go()'s/xexec()'s own subsequent code as unreachable.
 */
void gouser(void);
void termuser(void)  NORETURN;

#endif /* PROC_H */
