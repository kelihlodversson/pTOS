/*
 * super.h - Super() and SuperToUser() macros
 *
 * Copyright (C) 2015 The EmuTOS development team
 *
 * Authors:
 *  RFB    Roger Burrows
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef _SUPER_H
#define _SUPER_H

#undef Super
/* Standard Super() binding */
#ifdef __arm__
static __inline__ long Super(void* ptr)
{
    register long _r0 __asm__("r0") = 0x20;
    register long _r1 __asm__("r1") = (long)ptr;
    __asm__ volatile
    (
        "svc 1"
        : "=r"(_r0)
        : "r"(_r0), "r"(_r1)
        : "r2", "r3", "r12", "lr",  "memory", "cc"
    );
    return _r0;
}
#else
#define Super(ptr)                          \
__extension__                               \
({                                          \
    register long retvalue __asm__("d0");   \
    long  _ptr = (long) (ptr);              \
                                            \
    __asm__ volatile                        \
    (                                       \
        "move.l %1,-(sp)\n\t"               \
        "move.w #0x20,-(sp)\n\t"             \
        "trap   #1\n\t"                     \
        "addq.l #6,sp"                      \
    : "=r"(retvalue)                    /* outputs */       \
    : "r"(_ptr)                         /* inputs  */       \
    : __CLOBBER_RETURN("d0") "d1", "d2", "a0", "a1", "a2"   \
      AND_MEMORY                            \
    );                                      \
    retvalue;                               \
})
#endif

#undef SuperToUser      /* prevent "redefined" warning message */
/*
 * Safe binding to switch back from supervisor to user mode.
 * On TOS or EmuTOS, if the stack pointer has changed between Super(0)
 * and Super(oldssp), the resulting user stack pointer is wrong.
 * This bug does not occur with FreeMiNT.
 * So the safe way to return from supervisor to user mode is to backup
 * the stack pointer then restore it after the trap.
 * Sometimes, GCC optimizes the stack usage, so this matters.
 *
 * Binding originally by Vincent Rivière, from MiNTlib's osbind.h
 */
#ifdef __arm__
/* Let's start by assuming we don't have this bug on PI */
#define SuperToUser(ptr) Super(ptr)
#else
#define SuperToUser(ptr)                    \
(void)__extension__                         \
({                                          \
    register long retvalue __asm__("d0");   \
    register long sp_backup;                \
                                            \
    __asm__ volatile                        \
    (                                       \
        "move.l  sp,%1\n\t"                 \
        "move.l  %2,-(sp)\n\t"              \
        "move.w  #0x20,-(sp)\n\t"           \
        "trap    #1\n\t"                    \
        "move.l  %1,sp"                     \
    : "=r"(retvalue), "=&r"(sp_backup)  /* outputs */       \
    : "g"((long)(ptr))                  /* inputs */        \
    : __CLOBBER_RETURN("d0") "d1", "d2", "a0", "a1", "a2"   \
    );                                      \
})
#endif
#endif  /* _SUPER_H */
