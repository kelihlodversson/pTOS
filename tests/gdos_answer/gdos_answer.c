/*
 * gdos_answer.c - regression test for the built-in GDOS's answers (#582)
 *
 * With scalable fonts (CONF_WITH_VDI_SCALABLE_FONTS), pTOS answers as
 * SpeedoGDOS does once the AUTO folder has run: vq_gdos() (trap #2 with
 * d0 = -2) returns '_FSM', and the FSMC cookie points to a structure with
 * the magic '_SPD' and a version of at least 4.00.  Without them,
 * vq_gdos() leaves -2 and there is no FSMC cookie.  The suite checks that
 * the two agree, and which of the two it found.  Any other answer is a
 * GDOS loaded from disk, which the built-in one leaves alone: nothing is
 * checked then.  So is an FSMC cookie of another GDOS that also answers
 * '_FSM' (SpeedoGDOS 5, FSMGDOS): the built-in one is '_SPD' version 4.00.
 *
 * With the built-in GDOS, it also opens a workstation and checks
 * vst_load_fonts() and vqt_name(): the scalable fonts come after the
 * system fonts, flagged as outline fonts (intout[33] = 1) with ids from
 * 10000 on, and a second vst_load_fonts() adds none.  How many there are
 * depends on the fonts in \GEMSYS\FONTS of the test disk
 * (tools/run-regression-tests.sh copies DejaVu there when the host has
 * it).
 *
 * x86-64 programs cannot call the VDI at all (trap class 2 is refused
 * from ring 3), so it is skipped there.
 *
 * Copyright (C) 2026 The pTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "test.h"
#include <mint/osbind.h>
#include <mint/mintbind.h>
#include <stddef.h>

#define EINVFN          (-32L)
#define SS_GETCOOKIE    8
#define TAG(a, b, c, d) (((long)(a) << 24) | ((long)(b) << 16) | ((long)(c) << 8) | (d))

void test_gdos_answer(void);

#if !defined(__mc68000__) && !defined(__arm__)

void test_gdos_answer(void)
{
    ptest_begin("gdos_answer (#582)");
    conws("SKIP (no VDI for programs here)\r\n");
}

#else

struct cookie
{
    long tag;
    long value;
};

struct fsmc
{
    unsigned long magic;
    unsigned short version;
    unsigned short quality;
};

static short contrl[16], intin[16], ptsin[16], intout[64], ptsout[16];
static void *vdipb[5] = { contrl, intin, ptsin, intout, ptsout };

static short aes_control[5], aes_global[15];
static long aes_addr[2];
static void *aespb[6] = { aes_control, aes_global, intin, intout, aes_addr, aes_addr };

/* an AES call: without an AES, trap #2 leaves everything as it was */
static void aes(short op, short nintin, short nintout)
{
    aes_control[0] = op;
    aes_control[1] = nintin;
    aes_control[2] = nintout;
    aes_control[3] = 0;
    aes_control[4] = 0;
#ifdef __mc68000__
    {
        register long d0 __asm__("d0") = 200;
        register void *d1 __asm__("d1") = aespb;

        __asm__ volatile (
            "trap #2"
            : "+d" (d0), "+d" (d1)
            :
            : "d2", "a0", "a1", "memory", "cc");
    }
#else
    {
        register long r0 __asm__("r0") = 200;
        register void *r1 __asm__("r1") = aespb;

        __asm__ volatile (
            "svc 2"
            : "+r" (r0), "+r" (r1)
            :
            : "r2", "r3", "r12", "lr", "memory", "cc");
    }
#endif
}

/* a VDI call, with contrl[] already filled in */
static void vdi(void)
{
#ifdef __mc68000__
    register long d0 __asm__("d0") = 0x73;
    register void *d1 __asm__("d1") = vdipb;

    __asm__ volatile (
        "trap #2"
        : "+d" (d0), "+d" (d1)
        :
        : "d2", "a0", "a1", "memory", "cc");
#else
    register long r0 __asm__("r0") = 0x73;
    register void *r1 __asm__("r1") = vdipb;

    __asm__ volatile (
        "svc 2"
        : "+r" (r0), "+r" (r1)
        :
        : "r2", "r3", "r12", "lr", "memory", "cc");
#endif
}

static void vdi_op(short op, short nintin, short handle)
{
    contrl[0] = op;
    contrl[1] = 0;
    contrl[3] = nintin;
    contrl[5] = 0;
    contrl[6] = handle;
    vdi();
}

static void pnum(long n)
{
    char buf[12], *p = buf + sizeof(buf) - 1;

    *p = '\0';
    do
        *--p = (char)('0' + n % 10);
    while (n /= 10);
    conws(p);
}

/*
 * The scalable fonts on a workstation.  Run before ptest_begin(): opening
 * and closing the screen workstation writes to the console, which would
 * split the suite's result line.  The first failure is kept for later.
 */
static const char *font_failure;
static short fonts_loaded;

static void font_check(int ok, const char *msg)
{
    if (!ok && !font_failure)
        font_failure = msg;
}

static void check_fonts(void)
{
    short handle, system_fonts, i, aes_there;

    /*
     * Under the AES (the suite started from the desktop) the screen is
     * the AES's: open a virtual workstation on it.  Opening and closing
     * the physical one would take it from the desktop.  Without the AES
     * (EmuCON), the physical workstation is free.
     */
    aes_global[0] = 0;
    aes(10, 0, 1);                      /* appl_init() */
    aes_there = aes_global[0] != 0;

    for (i = 0; i < 10; i++)
        intin[i] = 1;
    intin[10] = 2;                      /* raster coordinates */
    if (aes_there)
    {
        short phys;

        aes(77, 0, 5);                  /* graf_handle() */
        phys = intout[0];
        for (i = 0; i < 10; i++)
            intin[i] = 1;
        intin[10] = 2;
        vdi_op(100, 11, phys);          /* v_opnvwk() */
    }
    else
        vdi_op(1, 11, 0);               /* v_opnwk(), the screen */
    handle = contrl[6];
    font_check(handle > 0, "opening a workstation failed");
    if (handle <= 0)
    {
        if (aes_there)
            aes(19, 0, 1);              /* appl_exit() */
        return;
    }
    system_fonts = intout[10];

    intin[0] = 0;
    vdi_op(119, 1, handle);             /* vst_load_fonts() */
    fonts_loaded = intout[0];
    font_check(fonts_loaded >= 0, "vst_load_fonts() returned less than 0");

    for (i = 1; i <= system_fonts + fonts_loaded; i++)
    {
        intin[0] = i;
        intout[33] = -1;
        vdi_op(130, 1, handle);         /* vqt_name() */
        font_check(contrl[4] >= 34, "vqt_name() has no outline flag");
        if (i <= system_fonts)
            font_check(intout[33] == 0, "a system font flagged as outline");
        else
        {
            font_check(intout[33] == 1, "a scalable font not flagged as outline");
            font_check(intout[0] >= 10000, "a scalable font's id is below 10000");
        }
    }

    intin[0] = 0;
    vdi_op(119, 1, handle);             /* again: nothing more */
    font_check(intout[0] == 0, "a second vst_load_fonts() added fonts");

    vdi_op(120, 1, handle);             /* vst_unload_fonts() */
    if (aes_there)
    {
        vdi_op(101, 0, handle);         /* v_clsvwk() */
        aes(19, 0, 1);                  /* appl_exit() */
    }
    else
        vdi_op(2, 0, handle);           /* v_clswk() */
}

/* vq_gdos(): what trap #2 leaves in d0/r0 when called with -2 */
static long vq_vgdos(void)
{
#ifdef __mc68000__
    register long d0 __asm__("d0") = -2;

    __asm__ volatile (
        "trap #2"
        : "+d" (d0)
        :
        : "d1", "d2", "a0", "a1", "memory", "cc");

    return d0;
#else
    register long r0 __asm__("r0") = -2;

    __asm__ volatile (
        "svc 2"
        : "+r" (r0)
        :
        : "r1", "r2", "r3", "r12", "lr", "memory", "cc");

    return r0;
#endif
}

void test_gdos_answer(void)
{
    unsigned long value = 0;
    const struct fsmc *fsmc = NULL;
    long gdos;
    int builtin;

    gdos = vq_vgdos();
    if (Ssystem(-1, 0, 0L) != EINVFN)
    {
        if (Ssystem(SS_GETCOOKIE, TAG('F', 'S', 'M', 'C'), (long)&value) == 0)
            fsmc = (const struct fsmc *)value;
    }
    else
    {
        /* no Ssystem(): the jar itself, through _p_cookies */
        const struct cookie *jar;

        jar = (const struct cookie *)Setexc(0x5a0 / 4, (void (*)(void))-1L);
        for (; jar && jar->tag; jar++)
            if (jar->tag == TAG('F', 'S', 'M', 'C'))
                fsmc = (const struct fsmc *)jar->value;
    }

    /*
     * The built-in GDOS: '_FSM', and its own FSMC cookie or (a fault the
     * suite reports) none.  Another FSMC cookie is another GDOS's.
     */
    builtin = gdos == TAG('_', 'F', 'S', 'M')
              && (!fsmc || (fsmc->magic == (unsigned long)TAG('_', 'S', 'P', 'D')
                            && fsmc->version == 0x0400));
    if (builtin)
    {
        check_fonts();
        conws("\n");   /* past what the workstation left on the console line */
    }

    ptest_begin("gdos_answer (#582)");

    if (gdos == -2)
    {
        ptest_assert_msg(!fsmc, "an FSMC cookie, but vq_gdos() says no GDOS");
        if (!fsmc)
            conws("(no GDOS) ");
    }
    else if (!builtin)
    {
        /* a GDOS loaded from disk: the built-in one stayed out of its way */
        conws("(a GDOS from disk) ");
    }
    else
    {
        ptest_assert_msg(fsmc != NULL, "the built-in GDOS, but no FSMC cookie");
        ptest_assert_msg(!font_failure, font_failure);
        conws("(SpeedoGDOS, ");
        pnum(fonts_loaded);
        conws(" scalable fonts) ");
    }

    ptest_pass();
}

#endif
