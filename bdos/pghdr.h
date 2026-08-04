/*
 * pghdr.h - header file for program loaders, 'size' pgms, etc.
 *
 * Copyright (C) 2001 Lineo, Inc.
 * Copyright (C) 2015-2017 The EmuTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef PGHDR_H
#define PGHDR_H

/*
 * executable formats understood by the program loader.  The format is
 * detected from the file's magic number by kpgmhdrld() and drives the
 * dispatch in kpgmld().
 */
#define PGMLD_PRG   0       /* classic Atari GEMDOS PRG (m68k)      */
#define PGMLD_ELF   1       /* ELF linked with ld --emit-relocs     */

typedef struct
{
        /*  magic number is already read  */
        LONG    h01_tlen ;      /*  length of text segment              */
        LONG    h01_dlen ;      /*  length of data segment              */
        LONG    h01_blen ;      /*  length of bss  segment              */
        LONG    h01_slen ;      /*  length of symbol table              */
        LONG    h01_res1 ;      /*  reserved - always zero              */
        ULONG   h01_flags ;     /*  flags                               */
        WORD    h01_abs ;       /*  not zero if no relocation           */
} PGMHDR01;

typedef struct
{
        LONG    pi_tpalen ;             /*  length of tpa area          */
        BYTE    *pi_tbase ;             /*  start addr of text seg      */
        LONG    pi_tlen ;               /*  length of text seg          */
        BYTE    *pi_dbase ;             /*  start addr of data seg      */
        LONG    pi_dlen ;               /*  length of data seg          */
        BYTE    *pi_bbase ;             /*  start addr of bss  seg      */
        LONG    pi_blen ;               /*  length of bss  seg          */
        LONG    pi_slen ;               /*  length of symbol table      */
} PGMINFO;

#endif /* PGHDR_H */
