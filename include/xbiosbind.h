/*
 * xbiosbind.h - Bindings for XBIOS access
 *
 * Copyright (C) 2001-2022 The EmuTOS development team
 *
 * Authors:
 *  MAD   Martin Doering
 *  LVL   Laurent Vogel
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */



#ifndef XBIOSBIND_H
#define XBIOSBIND_H

#if defined(__arm__) || defined(__x86_64__)
#include "biosargs.h"
#endif

/*
 * x86-64 calling convention (bios/arch/x86_64/trap.h): RAX = trap_class
 * << 32 | function_number (14 = the historic m68k XBIOS trap number,
 * kept here as a literal for the same reason biosbind.h's own copy of
 * this comment gives), real arguments in RDI/RSI/RDX/R10. The three
 * wide-argument calls (Rsconf, Floprd/Flopwr/Flopver, Flopfmt) pass a
 * struct pointer as their one real argument instead, same as ARM.
 */

#define Initmous(a,b,c) xbios_v_wll(0,a,(long)(b),(long)(c))
#define Ssbrk(a) xbios_l_w(1,a)
#define Physbase() xbios_l_v(2)
#define Logbase() xbios_l_v(3)
#define Getrez() xbios_w_v(4)
#define Setscreen(a,b,c,d) xbios_v_llww(5,a,b,c,d)
#define Setpalette(a) xbios_v_l(6,a)
#define Setcolor(a,b) xbios_w_ww(7,a,b)
#define Floprd(a,b,c,d,e,f,g) xbios_w_llwwwww(8,a,b,c,d,e,f,g)
#define Flopwr(a,b,c,d,e,f,g) xbios_w_llwwwww(9,a,b,c,d,e,f,g)
#define Flopfmt(a,b,c,d,e,f,g,h,i) xbios_w_llwwwwwlw(10,a,b,c,d,e,f,g,h,i)
#define Midiws(a,b) xbios_v_wl(12,a,b)
#define Mfpint(a,b) xbios_v_wl(13,a,b)
#define Iorec(a) xbios_l_w(14,a)
#define Rsconf(a,b,c,d,e,f) xbios_v_wwwwww(15,a,b,c,d,e,f)
#define Keytbl(a,b,c) xbios_l_lll(16,a,b,c)
#define Random() xbios_l_v(17)
#define Protobt(a,b,c,d) xbios_v_llww(18,a,b,c,d)
#define Flopver(a,b,c,d,e,f,g) xbios_w_llwwwww(19,a,b,c,d,e,f,g)
#define Scrdmp() xbios_v_v(20)
#define Cursconf(a,b) xbios_w_ww(21,a,b)
#define Settime(a) xbios_v_l(22,a)
#define Gettime() xbios_l_v(23)
#define Bioskeys() xbios_v_v(24)
#define Ikbdws(a,b) xbios_v_wl(25,a,b)
#define Jdisint(a) xbios_v_w(26,a)
#define Jenabint(a) xbios_v_w(27,a)
#define Giaccess(a,b) xbios_w_ww(28,a,b)
#define Offgibit(a) xbios_v_w(29,a)
#define Ongibit(a) xbios_v_w(30,a)
#define Xbtimer(a,b,c,d) xbios_v_wwwl(31,a,b,c,d)
#define Dosound(a) xbios_v_l(32,a)
#define Setprt(a) xbios_w_w(33,a)
#define Kbdvbase() xbios_l_v(34)
#define Kbrate(a,b) xbios_w_ww(35,a,b)
#define Prtblk() xbios_v_v(36)
#define Vsync() xbios_v_v(37)
#define Supexec(a) xbios_l_l(38,a)
#define Puntaes() xbios_v_v(39)
#define Blitmode(a) xbios_w_w(64, a)
#define EgetShift() xbios_w_v(81)
#define EsetColor(a,b) xbios_w_ww(83,a,b)
#define VsetMode(a) xbios_w_w(88,a)
#define VgetMonitor() xbios_w_v(89)
#define VgetSize(a) xbios_l_w(91,a)
#define VsetRGB(a,b,c) xbios_v_wwl(93,a,b,c)
#define VgetRGB(a,b,c) xbios_v_wwl(94,a,b,c)

#define FLOPFMT_MAGIC   0x87654321UL


/*
 *
 */

static __inline__ void xbios_v_v(int op)
{
#ifdef __arm__
    register long _r0 __asm__("r0")=(long)(op);
    __asm__ volatile (
        "svc 14"
        :
        : "r"(_r0)
        : "r1", "r2", "r3", "r12", "lr",  "memory", "cc"
    );
#elif defined(__x86_64__)
    register long _rax __asm__("rax") = ((long)14 << 32) | (unsigned)op;
    __asm__ volatile (
        "syscall"
        : "+r"(_rax)
        :
        : "rcx", "r11", "rdi", "rsi", "rdx", "r10", "memory", "cc"
    );
#else
    __asm__ volatile (
        "move.w  %0,-(sp)\n\t"
        "trap    #14\n\t"
        "addq.l  #2,sp"
         :
         : "nr"(op)
         : "d0", "d1", "d2", "a0", "a1", "a2", "memory", "cc"
        );
#endif
}

static __inline__ void xbios_v_w(int op, short a)
{
#ifdef __arm__
    register long _r0 __asm__("r0")=(long)(op);
    register long _r1 __asm__("r1")=(long)(a);
    __asm__ volatile (
        "svc 14"
        :
        : "r"(_r0), "r"(_r1)
        : "r2", "r3", "r12", "lr",  "memory", "cc"
    );
#elif defined(__x86_64__)
    register long _rax __asm__("rax") = ((long)14 << 32) | (unsigned)op;
    register long _rdi __asm__("rdi") = a;
    __asm__ volatile (
        "syscall"
        : "+r"(_rax), "+r"(_rdi)
        :
        : "rcx", "r11", "rsi", "rdx", "r10", "memory", "cc"
    );
#else
    __asm__ volatile (
        "move.w  %1,-(sp)\n\t"
        "move.w  %0,-(sp)\n\t"
        "trap    #14\n\t"
        "addq.l  #4,sp"
         :
         : "nr"(op), "nr"(a)
         : "d0", "d1", "d2", "a0", "a1", "a2", "memory", "cc"
        );
#endif
}

static __inline__ void xbios_v_wl(int op, short a, long b)
{
#ifdef __arm__
    register long _r0 __asm__("r0")=(long)(op);
    register long _r1 __asm__("r1")=(long)(a);
    register long _r2 __asm__("r2")=(long)(b);
    __asm__ volatile (
        "svc 14"
        :
        : "r"(_r0), "r"(_r1), "r"(_r2)
        : "r3", "r12", "lr",  "memory", "cc"
    );
#elif defined(__x86_64__)
    register long _rax __asm__("rax") = ((long)14 << 32) | (unsigned)op;
    register long _rdi __asm__("rdi") = a;
    register long _rsi __asm__("rsi") = b;
    __asm__ volatile (
        "syscall"
        : "+r"(_rax), "+r"(_rdi), "+r"(_rsi)
        :
        : "rcx", "r11", "rdx", "r10", "memory", "cc"
    );
#else
    __asm__ volatile (
        "move.l  %2,-(sp)\n\t"
        "move.w  %1,-(sp)\n\t"
        "move.w  %0,-(sp)\n\t"
        "trap    #14\n\t"
        "addq.l  #8,sp"
         :
         : "nr"(op), "nr"(a), "ir"(b)
         : "d0", "d1", "d2", "a0", "a1", "a2", "memory", "cc"
        );
#endif
}

static __inline__ void xbios_v_wll(int op, short a, long b, long c)
{
#ifdef __arm__
    register long _r0 __asm__("r0")=(long)(op);
    register long _r1 __asm__("r1")=(long)(a);
    register long _r2 __asm__("r2")=(long)(b);
    register long _r3 __asm__("r3")=(long)(c);
    __asm__ volatile (
        "svc 14"
        :
        : "r"(_r0), "r"(_r1), "r"(_r2), "r"(_r3)
        : "r12", "lr",  "memory", "cc"
    );
#elif defined(__x86_64__)
    register long _rax __asm__("rax") = ((long)14 << 32) | (unsigned)op;
    register long _rdi __asm__("rdi") = a;
    register long _rsi __asm__("rsi") = b;
    register long _rdx __asm__("rdx") = c;
    __asm__ volatile (
        "syscall"
        : "+r"(_rax), "+r"(_rdi), "+r"(_rsi), "+r"(_rdx)
        :
        : "rcx", "r11", "r10", "memory", "cc"
    );
#else
    __asm__ volatile (
        "move.l  %3,-(sp)\n\t"
        "move.l  %2,-(sp)\n\t"
        "move.w  %1,-(sp)\n\t"
        "move.w  %0,-(sp)\n\t"
        "trap    #14\n\t"
        "lea     12(sp),sp"
         :
         : "nr"(op), "nr"(a), "ir"(b), "ir"(c)
         : "d0", "d1", "d2", "a0", "a1", "a2", "memory", "cc"
        );
#endif
}

static __inline__ void
xbios_v_wwl(int op, short a, short b, long c)
{
#ifdef __arm__
    register long _r0 __asm__("r0")=(long)(op);
    register long _r1 __asm__("r1")=(long)(a);
    register long _r2 __asm__("r2")=(long)(b);
    register long _r3 __asm__("r3")=(long)(c);
    __asm__ volatile (
        "svc 14"
        :
        : "r"(_r0), "r"(_r1), "r"(_r2), "r"(_r3)
        : "r12", "lr",  "memory", "cc"
    );
#elif defined(__x86_64__)
    register long _rax __asm__("rax") = ((long)14 << 32) | (unsigned)op;
    register long _rdi __asm__("rdi") = a;
    register long _rsi __asm__("rsi") = b;
    register long _rdx __asm__("rdx") = c;
    __asm__ volatile (
        "syscall"
        : "+r"(_rax), "+r"(_rdi), "+r"(_rsi), "+r"(_rdx)
        :
        : "rcx", "r11", "r10", "memory", "cc"
    );
#else
    __asm__ volatile (
        "move.l  %3,-(sp)\n\t"
        "move.w  %2,-(sp)\n\t"
        "move.w  %1,-(sp)\n\t"
        "move.w  %0,-(sp)\n\t"
        "trap    #14\n\t"
        "lea     10(sp),sp"
         :
         : "nr"(op), "nr"(a), "nr"(b), "ir"(c)
         : "d0", "d1", "d2", "a0", "a1", "a2", "memory", "cc"
        );
#endif
}

static __inline__ void
xbios_v_wwwl(int op, short a, short b, short c, long d)
{
#ifdef __arm__
    register long _r0 __asm__("r0")=(long)(op);
    register long _r1 __asm__("r1")=(long)(a);
    register long _r2 __asm__("r2")=(long)(b);
    register long _r3 __asm__("r3")=(long)(c);
    register long _r4 __asm__("r4")=(long)(d);
    __asm__ volatile (
        "svc 14"
        :
        : "r"(_r0), "r"(_r1), "r"(_r2), "r"(_r3), "r"(_r4)
        : "r12", "lr",  "memory", "cc"
    );
#elif defined(__x86_64__)
    register long _rax __asm__("rax") = ((long)14 << 32) | (unsigned)op;
    register long _rdi __asm__("rdi") = a;
    register long _rsi __asm__("rsi") = b;
    register long _rdx __asm__("rdx") = c;
    register long _r10 __asm__("r10") = d;
    __asm__ volatile (
        "syscall"
        : "+r"(_rax), "+r"(_rdi), "+r"(_rsi), "+r"(_rdx), "+r"(_r10)
        :
        : "rcx", "r11", "memory", "cc"
    );
#else
    __asm__ volatile (
        "move.l  %4,-(sp)\n\t"
        "move.w  %3,-(sp)\n\t"
        "move.w  %2,-(sp)\n\t"
        "move.w  %1,-(sp)\n\t"
        "move.w  %0,-(sp)\n\t"
        "trap    #14\n\t"
        "lea     12(sp),sp"
         :
         : "nr"(op), "nr"(a), "nr"(b), "nr"(c), "ir"(d)
         : "d0", "d1", "d2", "a0", "a1", "a2", "memory", "cc"
        );
#endif
}

static __inline__ void
xbios_v_wwwwww(int op, short a, short b, short c, short d, short e, short f)
{
#if defined(__arm__) || defined(__x86_64__)
    /*
     * rsconf() needs 6 real arguments; the trap convention only delivers
     * 4 in registers, so pass them via a xbios_rsconf_args struct instead
     * (biosargs.h). See kelihlodversson/pTOS#217.
     */
    struct xbios_rsconf_args args;

    args.baud = a;
    args.ctrl = b;
    args.ucr = c;
    args.rsr = d;
    args.tsr = e;
    args.scr = f;
#endif
#ifdef __arm__
    register long _r0 __asm__("r0")=(long)(op);
    register long _r1 __asm__("r1");

    _r1 = (long)&args;

    __asm__ volatile (
        "svc 14"
        :
        : "r"(_r0), "r"(_r1)
        : "r2", "r3", "r4", "r12", "lr",  "memory", "cc"
    );
#elif defined(__x86_64__)
    register long _rax __asm__("rax") = ((long)14 << 32) | (unsigned)op;
    register long _rdi __asm__("rdi") = (long)&args;
    __asm__ volatile (
        "syscall"
        : "+r"(_rax), "+r"(_rdi)
        :
        : "rcx", "r11", "rsi", "rdx", "r10", "memory", "cc"
    );
#else
    __asm__ volatile (
        "move.w  %6,-(sp)\n\t"
        "move.w  %5,-(sp)\n\t"
        "move.w  %4,-(sp)\n\t"
        "move.w  %3,-(sp)\n\t"
        "move.w  %2,-(sp)\n\t"
        "move.w  %1,-(sp)\n\t"
        "move.w  %0,-(sp)\n\t"
        "trap    #14\n\t"
        "lea     14(sp),sp"
         :
         : "nr"(op), "nr"(a), "nr"(b), "nr"(c), "nr"(d), "nr"(e), "nr"(f)
         : "d0", "d1", "d2", "a0", "a1", "a2", "memory", "cc"
        );
#endif
}

static __inline__ void xbios_v_l(int op, long a)
{
#ifdef __arm__
    register long _r0 __asm__("r0")=(long)(op);
    register long _r1 __asm__("r1")=(long)(a);
    __asm__ volatile (
        "svc 14"
        :
        : "r"(_r0), "r"(_r1)
        : "r2", "r3", "r12", "lr",  "memory", "cc"
    );
#elif defined(__x86_64__)
    register long _rax __asm__("rax") = ((long)14 << 32) | (unsigned)op;
    register long _rdi __asm__("rdi") = a;
    __asm__ volatile (
        "syscall"
        : "+r"(_rax), "+r"(_rdi)
        :
        : "rcx", "r11", "rsi", "rdx", "r10", "memory", "cc"
    );
#else
    __asm__ volatile (
        "move.l  %1,-(sp)\n\t"
        "move.w  %0,-(sp)\n\t"
        "trap    #14\n\t"
        "addq.l  #6,sp"
         :
         : "nr"(op), "ir"(a)
         : "d0", "d1", "d2", "a0", "a1", "a2", "memory", "cc"
        );
#endif
}

static __inline__ void xbios_v_llw(int op, long a, long b, short c)
{
#ifdef __arm__
    register long _r0 __asm__("r0")=(long)(op);
    register long _r1 __asm__("r1")=(long)(a);
    register long _r2 __asm__("r2")=(long)(b);
    register long _r3 __asm__("r3")=(long)(c);
    __asm__ volatile (
        "svc 14"
        :
        : "r"(_r0), "r"(_r1), "r"(_r2), "r"(_r3)
        : "r12", "lr",  "memory", "cc"
    );
#elif defined(__x86_64__)
    register long _rax __asm__("rax") = ((long)14 << 32) | (unsigned)op;
    register long _rdi __asm__("rdi") = a;
    register long _rsi __asm__("rsi") = b;
    register long _rdx __asm__("rdx") = c;
    __asm__ volatile (
        "syscall"
        : "+r"(_rax), "+r"(_rdi), "+r"(_rsi), "+r"(_rdx)
        :
        : "rcx", "r11", "r10", "memory", "cc"
    );
#else
    __asm__ volatile (
        "move.w  %3,-(sp)\n\t"
        "move.l  %2,-(sp)\n\t"
        "move.l  %1,-(sp)\n\t"
        "move.w  %0,-(sp)\n\t"
        "trap    #14\n\t"
        "lea     12(sp),sp"
         :
         : "nr"(op), "ir"(a), "ir"(b), "nr"(c)
         : "d0", "d1", "d2", "a0", "a1", "a2", "memory", "cc"
        );
#endif
}

static __inline__ void
xbios_v_llww(int op, long a, long b, short c, short d)
{
#ifdef __arm__
    register long _r0 __asm__("r0")=(long)(op);
    register long _r1 __asm__("r1")=(long)(a);
    register long _r2 __asm__("r2")=(long)(b);
    register long _r3 __asm__("r3")=(long)(c);
    register long _r4 __asm__("r4")=(long)(d);
    __asm__ volatile (
        "svc 14"
        :
        : "r"(_r0), "r"(_r1), "r"(_r2), "r"(_r3), "r"(_r4)
        : "r12", "lr",  "memory", "cc"
    );
#elif defined(__x86_64__)
    register long _rax __asm__("rax") = ((long)14 << 32) | (unsigned)op;
    register long _rdi __asm__("rdi") = a;
    register long _rsi __asm__("rsi") = b;
    register long _rdx __asm__("rdx") = c;
    register long _r10 __asm__("r10") = d;
    __asm__ volatile (
        "syscall"
        : "+r"(_rax), "+r"(_rdi), "+r"(_rsi), "+r"(_rdx), "+r"(_r10)
        :
        : "rcx", "r11", "memory", "cc"
    );
#else
    __asm__ volatile (
        "move.w  %4,-(sp)\n\t"
        "move.w  %3,-(sp)\n\t"
        "move.l  %2,-(sp)\n\t"
        "move.l  %1,-(sp)\n\t"
        "move.w  %0,-(sp)\n\t"
        "trap    #14\n\t"
        "lea     14(sp),sp"
         :
         : "nr"(op), "ir"(a), "ir"(b), "nr"(c), "nr"(d)
         : "d0", "d1", "d2", "a0", "a1", "a2", "memory", "cc"
        );
#endif
}

static __inline__ short xbios_w_v(int op)
{
#ifdef __arm__
    register long _r0 __asm__("r0")=(long)(op);
    __asm__ volatile (
        "svc 14"
        : "=r"(_r0)
        : "r"(_r0)
        : "r1", "r2", "r3", "r12", "lr",  "memory", "cc"
    );
    return _r0;
#elif defined(__x86_64__)
    register long _rax __asm__("rax") = ((long)14 << 32) | (unsigned)op;
    __asm__ volatile (
        "syscall"
        : "+r"(_rax)
        :
        : "rcx", "r11", "rdi", "rsi", "rdx", "r10", "memory", "cc"
    );
    return (short)_rax;
#else
    register long retval __asm__("d0");

    __asm__ volatile (
        "move.w  %1,-(sp)\n\t"
        "trap    #14\n\t"
        "addq.l  #2,sp"
         : "=r"(retval)
         : "nr"(op)
         : "d1", "d2", "a0", "a1", "a2", "memory", "cc"
        );
    return retval;
#endif
}

static __inline__ short xbios_w_w(int op, short a)
{
#ifdef __arm__
    register long _r0 __asm__("r0")=(long)(op);
    register long _r1 __asm__("r1")=(long)(a);
    __asm__ volatile (
        "svc 14"
        : "=r"(_r0)
        : "r"(_r0), "r"(_r1)
        : "r2", "r3", "r12", "lr",  "memory", "cc"
    );
    return _r0;
#elif defined(__x86_64__)
    register long _rax __asm__("rax") = ((long)14 << 32) | (unsigned)op;
    register long _rdi __asm__("rdi") = a;
    __asm__ volatile (
        "syscall"
        : "+r"(_rax), "+r"(_rdi)
        :
        : "rcx", "r11", "rsi", "rdx", "r10", "memory", "cc"
    );
    return (short)_rax;
#else
    register long retval __asm__("d0");

    __asm__ volatile (
        "move.w  %2,-(sp)\n\t"
        "move.w  %1,-(sp)\n\t"
        "trap    #14\n\t"
        "addq.l  #4,sp"
         : "=r"(retval)
         : "nr"(op), "nr"(a)
         : "d1", "d2", "a0", "a1", "a2", "memory", "cc"
        );
    return retval;
#endif
}

static __inline__ short xbios_w_ww(int op, short a, short b)
{
#ifdef __arm__
    register long _r0 __asm__("r0")=(long)(op);
    register long _r1 __asm__("r1")=(long)(a);
    register long _r2 __asm__("r2")=(long)(b);
    __asm__ volatile (
        "svc 14"
        : "=r"(_r0)
        : "r"(_r0), "r"(_r1), "r"(_r2)
        : "r3", "r12", "lr",  "memory", "cc"
    );
    return _r0;
#elif defined(__x86_64__)
    register long _rax __asm__("rax") = ((long)14 << 32) | (unsigned)op;
    register long _rdi __asm__("rdi") = a;
    register long _rsi __asm__("rsi") = b;
    __asm__ volatile (
        "syscall"
        : "+r"(_rax), "+r"(_rdi), "+r"(_rsi)
        :
        : "rcx", "r11", "rdx", "r10", "memory", "cc"
    );
    return (short)_rax;
#else
    register long retval __asm__("d0");

    __asm__ volatile (
        "move.w  %3,-(sp)\n\t"
        "move.w  %2,-(sp)\n\t"
        "move.w  %1,-(sp)\n\t"
        "trap    #14\n\t"
        "addq.l  #6,sp"
         : "=r"(retval)
         : "nr"(op), "nr"(a), "nr"(b)
         : "d1", "d2", "a0", "a1", "a2", "memory", "cc"
        );
    return retval;
#endif
}

static __inline__ short xbios_w_llwwwww(int op,
    long a, long b, short c, short d, short e, short f, short g)
{
#if defined(__arm__) || defined(__x86_64__)
    /*
     * floprd()/flopwr()/flopver() need 7 real arguments; the trap
     * convention only delivers 4 in registers, so pass them via a
     * xbios_flop_io_args struct instead (biosargs.h).
     * See kelihlodversson/pTOS#217.
     */
    struct xbios_flop_io_args args;

    args.buf = (void *)a;
    args.filler = b;
    args.dev = c;
    args.sect = d;
    args.track = e;
    args.side = f;
    args.count = g;
#endif
#ifdef __arm__
    register long _r0 __asm__("r0")=(long)(op);
    register long _r1 __asm__("r1");

    _r1 = (long)&args;

    __asm__ volatile (
        "svc 14"
        : "=r"(_r0)
        : "r"(_r0), "r"(_r1)
        : "r2", "r3", "r4", "r12", "lr",  "memory", "cc"
    );
    return _r0;
#elif defined(__x86_64__)
    register long _rax __asm__("rax") = ((long)14 << 32) | (unsigned)op;
    register long _rdi __asm__("rdi") = (long)&args;
    __asm__ volatile (
        "syscall"
        : "+r"(_rax), "+r"(_rdi)
        :
        : "rcx", "r11", "rsi", "rdx", "r10", "memory", "cc"
    );
    return (short)_rax;
#else
    register long retval __asm__("d0");

    __asm__ volatile (
        "move.w  %8,-(sp)\n\t"
        "move.w  %7,-(sp)\n\t"
        "move.w  %6,-(sp)\n\t"
        "move.w  %5,-(sp)\n\t"
        "move.w  %4,-(sp)\n\t"
        "move.l  %3,-(sp)\n\t"
        "move.l  %2,-(sp)\n\t"
        "move.w  %1,-(sp)\n\t"
        "trap    #14\n\t"
        "lea     20(sp),sp"
         : "=r"(retval)
         : "nr"(op), "ir"(a), "ir"(b), "nr"(c), "nr"(d), "nr"(e), "nr"(f),
           "nr"(g)
         : "d1", "d2", "a0", "a1", "a2", "memory", "cc"
        );
    return retval;
#endif
}

static __inline__ short xbios_w_llwwwwwlw(int op,
    long a, long b, short c, short d, short e, short f, short g, long h, short i)
{
#if defined(__arm__) || defined(__x86_64__)
    /*
     * flopfmt() needs 9 real arguments; the trap convention only delivers
     * 4 in registers, so pass them via a xbios_flopfmt_args struct
     * instead (biosargs.h). See kelihlodversson/pTOS#217.
     */
    struct xbios_flopfmt_args args;

    args.buf = (void *)a;
    args.skew = (void *)b;
    args.dev = c;
    args.spt = d;
    args.track = e;
    args.side = f;
    args.interlv = g;
    args.magic = h;
    args.virgin = i;
#endif
#ifdef __arm__
    register long _r0 __asm__("r0")=(long)(op);
    register long _r1 __asm__("r1");

    _r1 = (long)&args;

    __asm__ volatile (
        "svc 14"
        : "=r"(_r0)
        : "r"(_r0), "r"(_r1)
        : "r2", "r3", "r4", "r12", "lr",  "memory", "cc"
    );
    return _r0;
#elif defined(__x86_64__)
    register long _rax __asm__("rax") = ((long)14 << 32) | (unsigned)op;
    register long _rdi __asm__("rdi") = (long)&args;
    __asm__ volatile (
        "syscall"
        : "+r"(_rax), "+r"(_rdi)
        :
        : "rcx", "r11", "rsi", "rdx", "r10", "memory", "cc"
    );
    return (short)_rax;
#else
    register long retval __asm__("d0");

    __asm__ volatile (
        "move.w  %10,-(sp)\n\t"
        "move.l  %9,-(sp)\n\t"
        "move.w  %8,-(sp)\n\t"
        "move.w  %7,-(sp)\n\t"
        "move.w  %6,-(sp)\n\t"
        "move.w  %5,-(sp)\n\t"
        "move.w  %4,-(sp)\n\t"
        "move.l  %3,-(sp)\n\t"
        "move.l  %2,-(sp)\n\t"
        "move.w  %1,-(sp)\n\t"
        "trap    #14\n\t"
        "lea     26(sp),sp"
         : "=r"(retval)
         : "nr"(op), "ir"(a), "ir"(b), "nr"(c), "nr"(d), "nr"(e), "nr"(f),
           "nr"(g), "ir"(h), "nr"(i)
         : "d1", "d2", "a0", "a1", "a2", "memory", "cc"
        );
    return retval;
#endif
}

static __inline__ long xbios_l_v(int op)
{
#ifdef __arm__
    register long _r0 __asm__("r0")=(long)(op);
    __asm__ volatile (
        "svc 14"
        : "=r"(_r0)
        : "r"(_r0)
        : "r1", "r2", "r3", "r12", "lr",  "memory", "cc"
    );
    return _r0;
#elif defined(__x86_64__)
    register long _rax __asm__("rax") = ((long)14 << 32) | (unsigned)op;
    __asm__ volatile (
        "syscall"
        : "+r"(_rax)
        :
        : "rcx", "r11", "rdi", "rsi", "rdx", "r10", "memory", "cc"
    );
    return _rax;
#else
    register long retval __asm__("d0");

    __asm__ volatile (
        "move.w  %1,-(sp)\n\t"
        "trap    #14\n\t"
        "addq.l  #2,sp"
         : "=r"(retval)
         : "nr"(op)
         : "d1", "d2", "a0", "a1", "a2", "memory", "cc"
        );
    return retval;
#endif
}

static __inline__ long xbios_l_w(int op, short a)
{
#ifdef __arm__
    register long _r0 __asm__("r0")=(long)(op);
    register long _r1 __asm__("r1")=(long)(a);
    __asm__ volatile (
        "svc 14"
        : "=r"(_r0)
        : "r"(_r0), "r"(_r1)
        : "r2", "r3", "r12", "lr",  "memory", "cc"
    );
    return _r0;
#elif defined(__x86_64__)
    register long _rax __asm__("rax") = ((long)14 << 32) | (unsigned)op;
    register long _rdi __asm__("rdi") = a;
    __asm__ volatile (
        "syscall"
        : "+r"(_rax), "+r"(_rdi)
        :
        : "rcx", "r11", "rsi", "rdx", "r10", "memory", "cc"
    );
    return _rax;
#else
    register long retval __asm__("d0");

    __asm__ volatile (
        "move.w  %2,-(sp)\n\t"
        "move.w  %1,-(sp)\n\t"
        "trap    #14\n\t"
        "addq.l  #4,sp"
         : "=r"(retval)
         : "nr"(op), "nr"(a)
         : "d1", "d2", "a0", "a1", "a2", "memory", "cc"
        );
    return retval;
#endif
}

static __inline__ long xbios_l_l(int op, long a)
{
#ifdef __arm__
    register long _r0 __asm__("r0")=(long)(op);
    register long _r1 __asm__("r1")=(long)(a);
    __asm__ volatile (
        "svc 14"
        : "=r"(_r0)
        : "r"(_r0), "r"(_r1)
        : "r2", "r3", "r12", "lr",  "memory", "cc"
    );
    return _r0;
#elif defined(__x86_64__)
    register long _rax __asm__("rax") = ((long)14 << 32) | (unsigned)op;
    register long _rdi __asm__("rdi") = a;
    __asm__ volatile (
        "syscall"
        : "+r"(_rax), "+r"(_rdi)
        :
        : "rcx", "r11", "rsi", "rdx", "r10", "memory", "cc"
    );
    return _rax;
#else
    register long retval __asm__("d0");

    __asm__ volatile (
        "move.l  %2,-(sp)\n\t"
        "move.w  %1,-(sp)\n\t"
        "trap    #14\n\t"
        "addq.l  #6,sp"
         : "=r"(retval)
         : "nr"(op), "nr"(a)
         : "d1", "d2", "a0", "a1", "a2", "memory", "cc"
        );
    return retval;
#endif
}

static __inline__ long xbios_l_lll(int op, long a, long b, long c)
{
#ifdef __arm__
    register long _r0 __asm__("r0")=(long)(op);
    register long _r1 __asm__("r1")=(long)(a);
    register long _r2 __asm__("r2")=(long)(b);
    register long _r3 __asm__("r3")=(long)(c);
    __asm__ volatile (
        "svc 14"
        : "=r"(_r0)
        : "r"(_r0), "r"(_r1), "r"(_r2), "r"(_r3)
        : "r12", "lr",  "memory", "cc"
    );
    return _r0;
#elif defined(__x86_64__)
    register long _rax __asm__("rax") = ((long)14 << 32) | (unsigned)op;
    register long _rdi __asm__("rdi") = a;
    register long _rsi __asm__("rsi") = b;
    register long _rdx __asm__("rdx") = c;
    __asm__ volatile (
        "syscall"
        : "+r"(_rax), "+r"(_rdi), "+r"(_rsi), "+r"(_rdx)
        :
        : "rcx", "r11", "r10", "memory", "cc"
    );
    return _rax;
#else
    register long retval __asm__("d0");

    __asm__ volatile (
        "move.l  %4,-(sp)\n\t"
        "move.l  %3,-(sp)\n\t"
        "move.l  %2,-(sp)\n\t"
        "move.w  %1,-(sp)\n\t"
        "trap    #14\n\t"
        "lea     14(sp),sp"
         : "=r"(retval)
         : "nr"(op), "ir"(a), "ir"(b), "ir"(c)
         : "d1", "d2", "a0", "a1", "a2", "memory", "cc"
        );
    return retval;
#endif
}


#endif /* XBIOSBIND_H */
