/* XFS pointer-return callbacks on m68k fastcall kernels.
 * GPL version 2 or later. See doc/license.txt. */

#include "asmdefs.h"

        .text

/* XFS takes stack arguments and returns pointers in D0. The kernel
 * passes the first two pointers in A0/A1 and returns pointers in A0. */
        .globl  C_SYM(ptos_xfs_call_init)
C_SYM(ptos_xfs_call_init):
        move.l  a1,-(sp)                // services
        jsr     (a0)
        addq.l  #4,sp
        move.l  d0,a0
        rts

        .globl  C_SYM(ptos_xfs_call_getdev)
C_SYM(ptos_xfs_call_getdev):
        move.l  4(sp),-(sp)             // special, spilled by the caller
        move.l  a1,-(sp)                // cookie
        jsr     (a0)
        addq.l  #8,sp
        move.l  d0,a0
        rts

/* Preserve the stack ABI's D2 and mirror D0 to A0 for XFS providers
 * built by the SDK with fastcall enabled. */
        .globl  C_SYM(ptos_xfs_cdecl_kmalloc)
C_SYM(ptos_xfs_cdecl_kmalloc):
        move.l  4(sp),d0
        move.l  d2,-(sp)
        jsr     C_SYM(ptos_xfs_alloc)
        move.l  (sp)+,d2
        move.l  a0,d0
        rts
