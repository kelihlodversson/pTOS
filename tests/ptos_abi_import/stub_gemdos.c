/*
 * stub_gemdos.c - libptos-abi.so.1's "gemdos" namespace stub bodies
 *
 * Copyright (C) 2026 The pTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 *
 * Built into libptos-abi.so.1 (doc/elfload.txt's "Native pTOS ABI
 * imports" section): a link-time-only fiction so an unmodified linker
 * produces ordinary ELF dynamic-import machinery (.dynsym/.rela.plt/
 * .got.plt) for every function include/ptos-abi/gemdos.h declares.
 * pTOS never loads this file, at build time or at runtime -- every body
 * here exists only to be a defined symbol for the linker to resolve the
 * application's reference against; none is ever actually called. Every
 * exported name and signature is kept in exact lockstep with
 * include/ptos-abi/gemdos.h -- see that header for the mapping back to
 * each GEMDOS opcode.
 */

#include "ptos-abi/gemdos.h"

void  Pterm0(void) { }

long  Cconin(void) { return 0; }
long  Cconout(PTOS_WORD ch) { (void)ch; return 0; }
long  Cauxin(void) { return 0; }
long  Cauxout(PTOS_WORD ch) { (void)ch; return 0; }
long  Cprnout(PTOS_WORD ch) { (void)ch; return 0; }
long  Crawio(PTOS_WORD parm) { (void)parm; return 0; }
long  Crawcin(void) { return 0; }
long  Cnecin(void) { return 0; }
void  Cconws(char *p) { (void)p; }
void  Cconrs(char *p) { (void)p; }
long  Cconis(void) { return 0; }

long  Dsetdrv(PTOS_WORD drv) { (void)drv; return 0; }

long  Cconos(void) { return 0; }
long  Cprnos(void) { return 0; }
long  Cauxis(void) { return 0; }
long  Cauxos(void) { return 0; }

long  Maddalt(unsigned char *start, long size) { (void)start; (void)size; return 0; }

void *Srealloc(long amount) { (void)amount; return (void *)0; }

long  Dgetdrv(void) { return 0; }
void  Fsetdta(void *dta) { (void)dta; }

long  Tgetdate(void) { return 0; }
long  Tsetdate(unsigned short d) { (void)d; return 0; }
long  Tgettime(void) { return 0; }
long  Tsettime(unsigned short t) { (void)t; return 0; }

void *Fgetdta(void) { return (void *)0; }
long  Sversion(void) { return 0; }
short Ptermres(long blkln, short rc) { (void)blkln; (void)rc; return 0; }

long  Dfree(long *buf, PTOS_WORD drv) { (void)buf; (void)drv; return 0; }

long  Dcreate(char *path) { (void)path; return 0; }
long  Ddelete(char *path) { (void)path; return 0; }
long  Dsetpath(char *path) { (void)path; return 0; }
long  Fcreate(char *name, unsigned char attr) { (void)name; (void)attr; return 0; }
long  Fopen(char *name, PTOS_WORD mode) { (void)name; (void)mode; return 0; }
long  Fclose(PTOS_WORD handle) { (void)handle; return 0; }
long  Fread(PTOS_WORD handle, long count, void *buf) { (void)handle; (void)count; (void)buf; return 0; }
long  Fwrite(PTOS_WORD handle, long count, void *buf) { (void)handle; (void)count; (void)buf; return 0; }
long  Fdelete(char *name) { (void)name; return 0; }
long  Fseek(long offset, PTOS_WORD handle, PTOS_WORD seekmode) { (void)offset; (void)handle; (void)seekmode; return 0; }
long  Fattrib(char *name, PTOS_WORD wflag, unsigned char attrib) { (void)name; (void)wflag; (void)attrib; return 0; }
void *Mxalloc(long amount, PTOS_WORD mode) { (void)amount; (void)mode; return (void *)0; }
long  Fdup(PTOS_WORD handle) { (void)handle; return 0; }
long  Fforce(PTOS_WORD std, PTOS_WORD handle) { (void)std; (void)handle; return 0; }
long  Dgetpath(char *buf, PTOS_WORD drv) { (void)buf; (void)drv; return 0; }
void *Malloc(long amount) { (void)amount; return (void *)0; }
long  Mfree(void *addr) { (void)addr; return 0; }
long  Mshrink(PTOS_WORD n, void *blk, long newlen) { (void)n; (void)blk; (void)newlen; return 0; }
long  Pexec(short mode, char *path, char *tail, char *env) { (void)mode; (void)path; (void)tail; (void)env; return 0; }
void  Pterm(unsigned short rc) { (void)rc; }

long  Fsfirst(char *name, PTOS_WORD attr) { (void)name; (void)attr; return 0; }
long  Fsnext(void) { return 0; }

long  Frename(PTOS_WORD n, char *oldname, char *newname) { (void)n; (void)oldname; (void)newname; return 0; }
long  Fdatime(void *timeptr, PTOS_WORD handle, PTOS_WORD wflag) { (void)timeptr; (void)handle; (void)wflag; return 0; }
