/*
 * fpu_regs.c - regression test for issue #615
 *
 * The ARM kernel is built with the hard-float ABI and NEON, so its C code
 * (FreeType, memcpy(), a compiler-vectorized loop) overwrites the FPU
 * registers; if no kernel entry saves them, a program that keeps a value
 * in one of them across a trap, or has one in it when an interrupt comes,
 * gets the kernel's value back.  This put four characters into d16, made
 * a VDI call and found a kernel address there in gdos_answer.
 *
 * The test loads a distinct value into every FPU register (and a rounding
 * mode into FPSCR), makes a call of each kind (BIOS, XBIOS, GEMDOS, VDI),
 * and, for interrupts, loops in user mode for a while with the timer
 * running, and then checks that all of them are as they were.  Only the
 * kernel paths that use the FPU registers can fail it: today that is the
 * GEMDOS and VDI calls; the others guard against a handler that comes to.
 *
 * It is ARM only, and skipped on a soft-float build, where the kernel
 * leaves the FPU registers alone.
 *
 * Copyright (C) 2026 The pTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "test.h"
#include <mint/osbind.h>

void test_fpu_regs(void);

#if !defined(__arm__) || !defined(__ARM_PCS_VFP)

void test_fpu_regs(void)
{
    ptest_begin("fpu_regs (#615)");
    conws("SKIP (no hard-float ARM)\r\n");
}

#else   /* hard-float ARM */

#ifdef __ARM_NEON
# define NREGS      32      /* d0-d31 */
# define LOAD_HI    "        vldmia  r0!, {d16-d31}\n"
# define STORE_HI   "        vstmia  r5!, {d16-d31}\n"
#else
# define NREGS      16      /* d0-d15 */
# define LOAD_HI    ""
# define STORE_HI   ""
#endif

/* what the call in fpu_regs_run() is */
#define KIND_BIOS   1       /* Kbshift(-1) */
#define KIND_XBIOS  2       /* Random() */
#define KIND_GEMDOS 3       /* Fsfirst() on a directory: copies structs with NEON */
#define KIND_VDI    4       /* a VDI call, for a handle that is not open */
#define KIND_SPIN   5       /* no call: loop in user mode, for interrupts */

/*
 * unsigned long fpu_regs_run(const unsigned long long *pat,
 *                            unsigned long long *out, long kind, long arg)
 *
 * Loads d0-d31 (or d0-d15) from pat[], sets FPSCR to round toward plus
 * infinity, makes the call, and stores the registers to out[]; returns
 * FPSCR as the call left it.  arg is a pointer for the calls that take
 * one, the loop count for KIND_SPIN.  It keeps its state in r5-r11, which
 * the trap bindings leave alone.  As it overwrites d8-d15, which are the
 * caller's, it saves them.
 */
__asm__(
"        .text\n"
"        .arm\n"
"        .align  2\n"
"        .globl  fpu_regs_run\n"
"        .type   fpu_regs_run, %function\n"
"fpu_regs_run:\n"
"        push    {r4-r11, lr}\n"
"        vpush   {d8-d15}\n"
"        mov     r5, r1\n"                      /* out */
"        mov     r6, r2\n"                      /* kind */
"        mov     r7, r3\n"                      /* arg */
"        vmrs    r9, fpscr\n"                   /* the caller's FPSCR */
"        orr     r8, r9, #(1 << 22)\n"
"        bic     r8, r8, #(1 << 23)\n"          /* RMode 01: plus infinity */
"        vldmia  r0!, {d0-d15}\n"
LOAD_HI
"        vmsr    fpscr, r8\n"
"        cmp     r6, #1\n"
"        beq     1f\n"
"        cmp     r6, #2\n"
"        beq     2f\n"
"        cmp     r6, #3\n"
"        beq     3f\n"
"        cmp     r6, #4\n"
"        beq     4f\n"
"5:      mov     r0, r7\n"                      /* KIND_SPIN */
"6:      subs    r0, r0, #1\n"
"        bne     6b\n"
"        b       9f\n"
"1:      mov     r0, #11\n"                     /* Kbshift(-1) */
"        mvn     r1, #0\n"
"        svc     #13\n"
"        b       9f\n"
"2:      mov     r0, #17\n"                     /* Random() */
"        svc     #14\n"
"        b       9f\n"
"3:      mov     r0, #0x4e\n"                   /* Fsfirst(path, 0) */
"        mov     r1, r7\n"
"        mov     r2, #0\n"
"        svc     #1\n"
"        b       9f\n"
"4:      mov     r0, #0x73\n"                   /* the VDI, parameter block in arg */
"        mov     r1, r7\n"
"        svc     #2\n"
"9:      vmrs    r10, fpscr\n"
"        vstmia  r5!, {d0-d15}\n"
STORE_HI
"        vmsr    fpscr, r9\n"
"        mov     r0, r10\n"
"        vpop    {d8-d15}\n"
"        pop     {r4-r11, pc}\n"
"        .size   fpu_regs_run, .-fpu_regs_run\n"
);

extern unsigned long fpu_regs_run(const unsigned long long *pat,
                                  unsigned long long *out, long kind, long arg);

static unsigned long long pat[32], out[32];

/* a VDI parameter block for opcode 102 on a handle that is not open */
static short contrl[16], intin[4], ptsin[4], intout[16], ptsout[16];
static void *vdipb[5] = { contrl, intin, ptsin, intout, ptsout };

static char msg[64];

static void put_dec(char **p, int n)
{
    char tmp[12];
    int i = 0;

    do {
        tmp[i++] = (char)('0' + n % 10);
        n /= 10;
    } while (n);
    while (i)
        *(*p)++ = tmp[--i];
}

/* run a kind of call (n times) and look for what changed; NULL if nothing did */
static const char *run(const char *what, long kind, long arg, int times)
{
    int i, t;
    char *p;

    for (t = 0; t < times; t++)
    {
        unsigned long fpscr;

        for (i = 0; i < 32; i++)
            out[i] = 0;
        fpscr = fpu_regs_run(pat, out, kind, arg);
        for (i = 0; i < NREGS; i++)
            if (out[i] != pat[i])
                break;
        if (i < NREGS || (fpscr & (3UL << 22)) != (1UL << 22))
        {
            p = msg;
            while (*what)
                *p++ = *what++;
            *p++ = ' ';
            if (i < NREGS)
            {
                *p++ = 'd';
                put_dec(&p, i);
                *p++ = ' ';
                *p++ = 'c';
                *p++ = 'h';
                *p++ = 'a';
                *p++ = 'n';
                *p++ = 'g';
                *p++ = 'e';
                *p++ = 'd';
            }
            else
            {
                const char *s = "FPSCR changed";

                while (*s)
                    *p++ = *s++;
            }
            *p = '\0';
            return msg;
        }
    }

    return 0;
}

void test_fpu_regs(void)
{
    const char *bad;
    int i;

    ptest_begin("fpu_regs (#615)");

    for (i = 0; i < 32; i++)
        pat[i] = 0x0123456789abcdefULL ^ ((unsigned long long)(i + 1) * 0x1111111111111111ULL);

    contrl[0] = 102;            /* vq_extnd() */
    contrl[6] = 77;             /* no workstation has this handle */
    intin[0] = 0;

    bad = run("BIOS call", KIND_BIOS, 0, 20);
    if (!bad)
        bad = run("XBIOS call", KIND_XBIOS, 0, 20);
    if (!bad)
        bad = run("GEMDOS call", KIND_GEMDOS, (long)"C:\\TESTS\\*.*", 20);
    if (!bad)
    {
        contrl[1] = 0;
        contrl[3] = 1;
        bad = run("VDI call", KIND_VDI, (long)vdipb, 20);
    }
    /* about 200 ms: the timer interrupts a few dozen times */
    if (!bad)
        bad = run("interrupt", KIND_SPIN, 20000000L, 10);
    ptest_assert_msg(!bad, bad ? bad : "");

    ptest_pass();
}

#endif  /* hard-float ARM */
