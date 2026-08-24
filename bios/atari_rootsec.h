/*
 * atari_rootsec.h: Atari TOS root sector layout
 *
 * originally derived from Linux definitions (see below)
 *
 * Copyright (C) 2022 The EmuTOS development team
 */
#ifndef ATARI_ROOTSEC_H
#define ATARI_ROOTSEC_H

/*
 * linux/include/linux/atari_rootsec.h
 * definitions for Atari Rootsector layout
 * by Andreas Schwab (schwab@ls5.informatik.uni-dortmund.de)
 *
 * modified for ICD/Supra partitioning scheme restricted to at most 12
 * partitions
 * by Guenther Kelleter (guenther@pool.informatik.rwth-aachen.de)
 */

/*
 * both structs mirror the on-disk Atari rootsector layout exactly, with
 * no gaps: __attribute__((packed)) is required, not decorative. Without
 * it, a strict-alignment target (ARM) inserts 2 padding bytes before
 * icdpart[] to align its ULONG fields to a 4-byte boundary, shifting every
 * field from icdpart[] onwards (icdpart, hd_siz, part, bsl_st, bsl_cnt,
 * checksum) 2 bytes off from where the real data is - confirmed via
 * -g/DWARF: struct rootsector compiled to 516 bytes instead of the 512
 * a disk sector actually is. m68k's own struct-layout defaults happen
 * not to need the padding here, which is presumably why this went
 * unnoticed: this header was carried over from Linux's m68k-only
 * atari_rootsec.h with no port-specific adjustment.
 *
 * packed alone drops the struct's own alignment requirement to 1, which
 * matters beyond internal layout: struct rootsector is a member of the
 * PHYSSECT union (bios/disk.c), so PHYSSECT's alignment - and hence the
 * physsect/physsect2 instances' actual placement - would be free to fall
 * on an odd address. On m68k a word/long access to an odd address traps
 * with an address error, unlike the individual packed-struct field
 * accesses themselves (the compiler already generates alignment-safe
 * code for those). aligned(2) restores the minimum m68k needs without
 * reintroducing any internal padding: verified via -g/DWARF that sizeof
 * and every field offset are unchanged from the packed-only layout.
 */
struct partition_info
{
  UBYTE flg;                    /* bit 0: active; bit 7: bootable */
  char id[3];                   /* "GEM", "BGM", "XGM", or other */
  ULONG st;                     /* start of partition */
  ULONG siz;                    /* length of partition */
} __attribute__((packed, aligned(2)));

struct rootsector
{
  char unused[0x156];                   /* room for boot code */
  struct partition_info icdpart[8];     /* info for ICD-partitions 5..12 */
  char unused2[0xc];
  ULONG hd_siz;                         /* size of disk in blocks */
  struct partition_info part[4];
  ULONG bsl_st;                         /* start of bad sector list */
  ULONG bsl_cnt;                        /* length of bad sector list */
  UWORD checksum;                       /* checksum for bootable disks */
} __attribute__((packed, aligned(2)));

#endif /* ATARI_ROOTSEC_H */
