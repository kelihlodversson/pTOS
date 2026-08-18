/*
 * biosargs.h - argument-marshaling structs for wide-argument ARM BIOS/XBIOS calls
 *
 * Copyright (C) 2026 The EmuTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef BIOSARGS_H
#define BIOSARGS_H

#include "portab.h"

/*
 * _biostrap/_xbiostrap (vectorsasm.S) call the vectored handler with at
 * most 4 real arguments in r0-r3 -- that's all _arm_dispatch_svc's callers
 * put in registers, and nothing marshals a 5th+ argument to the stack.
 * The handful of BIOS/XBIOS functions that need more than that (Rwabs/
 * Lrwabs, Floprd/Flopwr/Flopver, Flopfmt, Rsconf) take a pointer to one of
 * these structs as their single real argument instead, on ARM only.
 *
 * This is part of the trap ABI shared with libcmini's ARM osbind.h
 * (include/mint/arch/arm/biosargs.h in the libcmini repo) -- keep the
 * field order and types in sync between the two.
 */

struct bios_lrwabs_args         /* Rwabs/Lrwabs -- BIOS function 0x04 */
{
    LONG r_w;
    void *adr;
    LONG numb;
    LONG first;
    LONG drive;
    LONG lfirst;
};

struct xbios_flop_io_args       /* Floprd/Flopwr/Flopver -- XBIOS 0x08/0x09/0x13 */
{
    void *buf;
    LONG filler;
    LONG dev;
    LONG sect;
    LONG track;
    LONG side;
    LONG count;
};

struct xbios_flopfmt_args       /* Flopfmt -- XBIOS 0x0a */
{
    void *buf;
    void *skew;
    LONG dev;
    LONG spt;
    LONG track;
    LONG side;
    LONG interlv;
    LONG magic;
    LONG virgin;
};

struct xbios_rsconf_args        /* Rsconf -- XBIOS 0x0f */
{
    LONG baud;
    LONG ctrl;
    LONG ucr;
    LONG rsr;
    LONG tsr;
    LONG scr;
};

#endif /* BIOSARGS_H */
