/*
 * biosargs.h - argument-marshaling structs for wide-argument x86-64 BIOS/XBIOS calls
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef BIOSARGS_H
#define BIOSARGS_H

#include "portab.h"

/*
 * trap.c's dispatcher (bios/arch/x86_64/trap.c) calls BIOS/XBIOS handlers
 * with at most 4 real arguments in RDI/RSI/RDX/R10 -- the same limit
 * ARM's own _biostrap/_xbiostrap have, for the same reason (trap.h's own
 * comment). The handful of BIOS/XBIOS functions that need more than that
 * (Rwabs/Lrwabs, Floprd/Flopwr/Flopver, Flopfmt, Rsconf) take a pointer
 * to one of these structs as their single real argument instead.
 *
 * Not shared with include/arch/arm/biosargs.h's identically-named,
 * identically-shaped struct: the `void *` fields are 8 bytes wide here
 * (LP64) versus 4 on ARM (ILP32), so the two are only conceptually the
 * same struct, not binary-compatible ones -- keep field order and types
 * in sync with the ARM copy by hand, the same way that file is itself
 * kept in sync with libcmini's ARM osbind.h.
 *
 * Used on both ends of the call: bios/bios.c and bios/xbios.c's own
 * *_arm()-named handlers (reused verbatim for x86-64 too -- the C code
 * is arch-neutral, only the struct's own field widths differ per arch)
 * as the callee, and include/biosbind.h and include/xbiosbind.h's
 * __x86_64__ branches of bios_l_wlwwwl/xbios_w_llwwwww/
 * xbios_w_llwwwwwlw/xbios_v_wwwwww as the caller.
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
