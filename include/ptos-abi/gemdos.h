/*
 * gemdos.h - native pTOS ABI imports: every implemented GEMDOS call
 *
 * Copyright (C) 2026 The pTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 *
 * Declares the same functions bdos/ptosabi_gemdos.c exports under the
 * "gemdos" namespace, with the same C signature that file's own
 * trap1()-based wrapper for it uses (see that file for the exact mapping
 * to each GEMDOS opcode). Every name here is the official GEMDOS function
 * name as documented by tos.hyp
 * (https://freemint.github.io/tos.hyp/en/gemdos_functions.html),
 * verified name-for-name against that page. A native ELF application
 * includes this header,
 * calls e.g. Fopen(name, mode) as an ordinary extern function, and links
 * against the SDK's libptos-abi.so.1 stub (see doc/elfload.txt's "Native
 * pTOS ABI imports" section) so the linker produces the dynamic-import
 * machinery tools/ptos-elf-pack.c converts into .ptos.imports/.ptos.bind.
 *
 * Deliberately freestanding and self-contained: this is userland code,
 * built with a target cross compiler but without pTOS's own kernel
 * headers (no "config.h", no portab.h -- see the Makefile's TEST_CFLAGS
 * comment on why test/userland code never includes those).
 *
 * m68k callers of this header MUST be compiled with -mshort (see
 * Makefile's PTOSABI_CFLAGS). This is not just a matter of matching
 * PTOS_WORD's width to the kernel's: on m68k, -mshort also changes the
 * *stack argument width* GCC generates for a WORD-sized parameter (2
 * bytes instead of 4), independently of the parameter's declared C type
 * -- a "short"-typed argument compiled without -mshort is still pushed
 * as a 4-byte stack slot, one that a -mshort-compiled callee (every
 * kernel GEMDOS handler) reads as only the first 2 bytes of, corrupting
 * every argument after it. Confirmed directly: dropping -mshort here
 * while keeping every type below unchanged reproduced exactly that --
 * Fwrite(handle, len, buf) reached the kernel's xwrite() as
 * xwrite(0, 393216, <garbage>) instead of the real values. PTOS_WORD
 * exists on top of that requirement, not instead of it, to keep this
 * header's own declarations self-documenting and to match ARM (which
 * has no -mshort and needs none: its native int is already 32 bits,
 * matching the kernel exactly): 16 bits on m68k, 32 on ARM. A handful of
 * calls (Pexec's mode, Pterm/Ptermres's rc, Tsetdate/Tsettime) instead
 * use the kernel's own WORD/UWORD (portab.h) types directly, which are
 * always exactly 16 bits on every architecture -- those use a plain
 * "short" here, unconditionally, rather than PTOS_WORD.
 *
 * -mshort must cover every translation unit that calls a function
 * declared here, not just the ones that #include this header: it is a
 * property of the *call*, so a WORD-sized argument crosses correctly
 * only when both the caller's compile and the callee's compile agree on
 * the stack width, regardless of which file the declaration lives in.
 * That includes any other library linked into the same executable whose
 * functions you call with a WORD/int-sized stack argument (e.g. an
 * ordinary libcmini build, which is NOT built with -mshort -- see the
 * m68k side of tools/ptos-elf-pack's test payload build in the Makefile,
 * whose comment on PTOSABI_CFLAGS explains why linking a non -mshort
 * libcmini/crt0 into the same executable as this header's callers is
 * still safe there specifically: that payload never calls a libcmini
 * function with a WORD-sized stack argument in the first place). Mixing
 * an -mshort translation unit's calls into such a library carelessly
 * reproduces the exact argument-corruption bug described above, just
 * against a userland callee instead of a kernel one.
 */

#ifndef PTOS_ABI_GEMDOS_H
#define PTOS_ABI_GEMDOS_H

#ifdef __arm__
typedef int            PTOS_WORD;
#else
typedef short           PTOS_WORD;
#endif

/* 0x00 */
void  Pterm0(void);

/* console (0x01-0x0B) */
long  Cconin(void);
long  Cconout(PTOS_WORD ch);
long  Cauxin(void);
long  Cauxout(PTOS_WORD ch);
long  Cprnout(PTOS_WORD ch);
long  Crawio(PTOS_WORD parm);
long  Crawcin(void);
long  Cnecin(void);
void  Cconws(char *p);
void  Cconrs(char *p);
long  Cconis(void);

/* 0x0E */
long  Dsetdrv(PTOS_WORD drv);

/* extended console (0x10-0x13) */
long  Cconos(void);
long  Cprnos(void);
long  Cauxis(void);
long  Cauxos(void);

/* 0x14: only present when this kernel was built with CONF_WITH_ALT_RAM */
long  Maddalt(unsigned char *start, long size);

/* 0x15: only present when this kernel was built with CONF_WITH_VIDEL */
void *Srealloc(long amount);

/* 0x19-0x1A */
long  Dgetdrv(void);
void  Fsetdta(void *dta);

/* time/date (0x2A-0x2D): xsetdate/xsettime take the kernel's own UWORD
 * (always exactly 16 bits), not PTOS_WORD */
long  Tgetdate(void);
long  Tsetdate(unsigned short d);
long  Tgettime(void);
long  Tsettime(unsigned short t);

/* 0x2F-0x31: xtermres's parameter and return are the kernel's own WORD
 * (always exactly 16 bits), not PTOS_WORD */
void *Fgetdta(void);
long  Sversion(void);
short Ptermres(long blkln, short rc);

/* 0x36 */
long  Dfree(long *buf, PTOS_WORD drv);

/* filesystem (0x39-0x4C) */
long  Dcreate(char *path);
long  Ddelete(char *path);
long  Dsetpath(char *path);
long  Fcreate(char *name, unsigned char attr);
long  Fopen(char *name, PTOS_WORD mode);
long  Fclose(PTOS_WORD handle);
long  Fread(PTOS_WORD handle, long count, void *buf);
long  Fwrite(PTOS_WORD handle, long count, void *buf);
long  Fdelete(char *name);
long  Fseek(long offset, PTOS_WORD handle, PTOS_WORD seekmode);
long  Fattrib(char *name, PTOS_WORD wflag, unsigned char attrib);
void *Mxalloc(long amount, PTOS_WORD mode);
long  Fdup(PTOS_WORD handle);
long  Fforce(PTOS_WORD std, PTOS_WORD handle);
long  Dgetpath(char *buf, PTOS_WORD drv);
void *Malloc(long amount);
long  Mfree(void *addr);
long  Mshrink(PTOS_WORD n, void *blk, long newlen);
/* Pexec's mode is the kernel's own WORD (always exactly 16 bits), not
 * PTOS_WORD */
long  Pexec(short mode, char *path, char *tail, char *env);
/* Pterm's rc is the kernel's own UWORD (always exactly 16 bits), not
 * PTOS_WORD */
void  Pterm(unsigned short rc);

/* 0x4E-0x4F */
long  Fsfirst(char *name, PTOS_WORD attr);
long  Fsnext(void);

/* 0x56-0x57 */
long  Frename(PTOS_WORD n, char *oldname, char *newname);
long  Fdatime(void *timeptr, PTOS_WORD handle, PTOS_WORD wflag);

#endif /* PTOS_ABI_GEMDOS_H */
