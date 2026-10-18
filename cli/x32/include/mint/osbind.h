/*
 * mint/osbind.h - system call bindings for the x32 (ring 3) EmuCON
 *
 * Stands in for libcmini's <mint/osbind.h> (see cli/Makefile) for the one
 * userland build that has no libcmini yet: the built-in EmuCON the x86-64
 * port runs in ring 3 (#398).  Every call is a `syscall` with the register
 * convention of bios/arch/x86_64/trap.h:
 *
 *     RAX = (trap_class << 32) | function_number
 *     RDI, RSI, RDX, R10 = up to four arguments
 *
 * Every argument register is written on every call (unused ones with 0):
 * the kernel inspects all four, so a stale value left in one by the
 * compiler must never reach it.  Arguments are 32-bit values in this ILP32
 * program and are sign-extended to the kernel's 64-bit `long`; a pointer
 * (below 2 GiB in the layout of include/procmem.h) sign-extends to itself.
 * The result is the kernel's 64-bit return value truncated to a 32-bit
 * `long`.
 *
 * Copyright (C) 2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */
#ifndef X32_OSBIND_H
#define X32_OSBIND_H

#include "x32rt.h"        /* Malloc()/Mfree(): a static arena, not system calls */

#define X32_TRAP_GEMDOS 1
#define X32_TRAP_BIOS   13
#define X32_TRAP_XBIOS  14

static __inline__ long x32_syscall(unsigned trap, unsigned fn,
                                   long a, long b, long c, long d)
{
    register unsigned long long rax __asm__("rax") =
        ((unsigned long long)trap << 32) | fn;
    register long long rdi __asm__("rdi") = a;      /* sign-extended */
    register long long rsi __asm__("rsi") = b;
    register long long rdx __asm__("rdx") = c;
    register long long r10 __asm__("r10") = d;

    __asm__ volatile ("syscall"
                      : "+r"(rax)
                      : "r"(rdi), "r"(rsi), "r"(rdx), "r"(r10)
                      : "rcx", "r11", "memory");
    return (long)rax;
}

#define GEMDOS0(fn)             x32_syscall(X32_TRAP_GEMDOS, fn, 0, 0, 0, 0)
#define GEMDOS1(fn,a)           x32_syscall(X32_TRAP_GEMDOS, fn, (long)(a), 0, 0, 0)
#define GEMDOS2(fn,a,b)         x32_syscall(X32_TRAP_GEMDOS, fn, (long)(a), (long)(b), 0, 0)
#define GEMDOS3(fn,a,b,c)       x32_syscall(X32_TRAP_GEMDOS, fn, (long)(a), (long)(b), (long)(c), 0)
#define GEMDOS4(fn,a,b,c,d)     x32_syscall(X32_TRAP_GEMDOS, fn, (long)(a), (long)(b), (long)(c), (long)(d))
#define BIOS1(fn,a)             x32_syscall(X32_TRAP_BIOS, fn, (long)(a), 0, 0, 0)
#define BIOS2(fn,a,b)           x32_syscall(X32_TRAP_BIOS, fn, (long)(a), (long)(b), 0, 0)
#define XBIOS0(fn)              x32_syscall(X32_TRAP_XBIOS, fn, 0, 0, 0, 0)
#define XBIOS1(fn,a)            x32_syscall(X32_TRAP_XBIOS, fn, (long)(a), 0, 0, 0)
#define XBIOS2(fn,a,b)          x32_syscall(X32_TRAP_XBIOS, fn, (long)(a), (long)(b), 0, 0)
#define XBIOS4(fn,a,b,c,d)      x32_syscall(X32_TRAP_XBIOS, fn, (long)(a), (long)(b), (long)(c), (long)(d))

/* GEMDOS: the function numbers are bdos/bdosmain.c's and include/bdosbind.h's */
#define Malloc(n)               x32_malloc(n)
#define Mfree(p)                x32_free(p)
#define Pterm0()                GEMDOS0(0x00)
#define Cconws(buf)             GEMDOS1(0x09, buf)
#define Dsetdrv(drv)            GEMDOS1(0x0e, drv)
#define Dgetdrv()               GEMDOS0(0x19)
#define Sversion()              GEMDOS0(0x30)
#define Fgetdta()               GEMDOS0(0x2f)
#define Dfree(buf,drv)          GEMDOS2(0x36, buf, drv)
#define Dcreate(path)           GEMDOS1(0x39, path)
#define Ddelete(path)           GEMDOS1(0x3a, path)
#define Dsetpath(path)          GEMDOS1(0x3b, path)
#define Fcreate(name,attr)      GEMDOS2(0x3c, name, attr)
#define Fopen(name,mode)        GEMDOS2(0x3d, name, mode)
#define Fclose(h)               GEMDOS1(0x3e, h)
#define Fread(h,n,buf)          GEMDOS3(0x3f, h, n, buf)
#define Fwrite(h,n,buf)         GEMDOS3(0x40, h, n, buf)
#define Fdelete(name)           GEMDOS1(0x41, name)
#define Fattrib(name,w,attr)    GEMDOS3(0x43, name, w, attr)
#define Fdup(h)                 GEMDOS1(0x45, h)
#define Fforce(std,h)           GEMDOS2(0x46, std, h)
#define Dgetpath(buf,drv)       GEMDOS2(0x47, buf, drv)
#define Pexec(mode,name,cmd,env) GEMDOS4(0x4b, mode, name, cmd, env)
#define Pterm(rc)               GEMDOS1(0x4c, rc)
#define Fsfirst(name,attr)      GEMDOS2(0x4e, name, attr)
#define Fsnext()                GEMDOS0(0x4f)
#define Frename(zero,old,new)   GEMDOS3(0x56, zero, old, new)
#define Ssystem(mode,a,b)       GEMDOS3(0x154, mode, a, b)

/* BIOS */
#define Bconstat(dev)           BIOS1(1, dev)
#define Bconin(dev)             BIOS1(2, dev)
#define Bconout(dev,c)          BIOS2(3, dev, c)
#define Bcostat(dev)            BIOS1(8, dev)

/* XBIOS */
#define Getrez()                XBIOS0(4)
#define Setcolor(n,c)           XBIOS2(7, n, c)
#define Cursconf(fn,rate)       XBIOS2(21, fn, rate)
#define Kbrate(init,rep)        XBIOS2(35, init, rep)
/* pTOS's Setscreen has a 4th (font height) argument: see cli/cmd.h */
#define trap_14_wllww(fn,l,p,rez,h) XBIOS4(fn, l, p, rez, h)

#endif /* X32_OSBIND_H */
