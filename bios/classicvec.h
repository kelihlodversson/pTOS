/*
 * classicvec.h - classic-ABI entry points of the kernel's default BIOS vectors
 *
 * Copyright (C) 2025-2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef CLASSICVEC_H
#define CLASSICVEC_H

#include "config.h"
#include "portab.h"

/*
 * bconout_vec[], the Bconmap tables and hdv_rw/hdv_bpb/hdv_mediach are frozen,
 * user-replaceable TOS vectors: bios.c reaches them through protect_*(), which
 * call the handler with TOS's classic ABI, the arguments packed one after the
 * other as 16 and 32 bit values, and a third-party driver that replaced one
 * chains to the previous handler with that ABI as well.  The kernel's own
 * default handlers are ordinary C functions in this kernel's ABI (a 4 byte
 * slot for each WORD, or registers with -mfastcall), which the classic call
 * does not match: bconout2(dev, b) read its b from the stack beyond the
 * arguments and printed nothing at all.
 *
 * (The Rsconf() field of the Bconmap tables, and so rsconfptr, is one of these
 * frozen vectors too: six packed WORDs.)
 *
 * On the m68k the vectors therefore hold CLASSIC_ENTRY(f), a small entry point
 * (util/arch/m68k/miscasm.S) that takes the classic frame and calls f.  The ARM
 * and x86-64 ports have no classic ABI: the handler itself is installed.
 */
#if CONF_WITH_NATIVE_ABI
#define CLASSIC_ENTRY(f)    f
#else
#define CLASSIC_ENTRY(f)    f##_etv_entry

LONG bconout0_etv_entry(WORD dev, WORD b);
LONG bconout1_etv_entry(WORD dev, WORD b);
LONG bconout2_etv_entry(WORD dev, WORD b);
LONG bconout3_etv_entry(WORD dev, WORD b);
LONG bconout4_etv_entry(WORD dev, WORD b);
LONG bconout5_etv_entry(WORD dev, WORD b);
LONG bconoutA_etv_entry(WORD dev, WORD b);
LONG bconoutB_etv_entry(WORD dev, WORD b);
LONG bconoutTT_etv_entry(WORD dev, WORD b);
ULONG rsconf1_etv_entry(WORD baud, WORD ctrl, WORD ucr, WORD rsr, WORD tsr, WORD scr);
ULONG rsconfA_etv_entry(WORD baud, WORD ctrl, WORD ucr, WORD rsr, WORD tsr, WORD scr);
ULONG rsconfB_etv_entry(WORD baud, WORD ctrl, WORD ucr, WORD rsr, WORD tsr, WORD scr);
ULONG rsconfTT_etv_entry(WORD baud, WORD ctrl, WORD ucr, WORD rsr, WORD tsr, WORD scr);
LONG blkdev_rwabs_etv_entry(WORD rw, UBYTE *buf, WORD cnt, WORD recnr, WORD dev, LONG lrecnr);
LONG blkdev_getbpb_etv_entry(WORD dev);
LONG blkdev_mediach_etv_entry(WORD dev);

/* the C half of the entry points: the classic frame and the handler to call */
LONG classic_ww(const WORD *frame, LONG (*fn)(WORD, WORD));
LONG classic_w(const WORD *frame, LONG (*fn)(WORD));
ULONG classic_rsconf(const WORD *frame, ULONG (*fn)(WORD, WORD, WORD, WORD, WORD, WORD));
LONG classic_rwabs(const WORD *frame, LONG (*fn)(WORD, UBYTE *, WORD, WORD, WORD, LONG));
#endif

#endif /* CLASSICVEC_H */
