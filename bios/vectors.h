/*
 * vectors.h - exception vectors, interrupt routines and system hooks
 *
 * Copyright (C) 2001-2025 The EmuTOS development team
 *
 * Authors:
 *  LVL     Laurent Vogel
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef VECTORS_H
#define VECTORS_H

/* initialize default exception vectors */

void init_exc_vec(void);
void init_user_vec(UWORD first_boot);

/* initialise acia vectors */

void init_acia_vecs(void);

/* some exception vectors */

#if CONF_WITH_ATARI_VIDEO
void int_hbl(void);
#endif
void int_vbl(void);
void int_linea(void);
void int_timerc(void);

void biostrap(void);
void xbiostrap(void);
#ifndef __m68k__
void gemtrap(void);    /* ARM's own VDI trap dispatcher, bios/arch/arm/vectorsasm.S */
#endif

void just_rte(void);
void just_rts(void);

#if CONF_WITH_BUS_ERROR
long check_read_byte(long);
#endif


/* */
LONG default_etv_critic(WORD err,WORD dev);
void int_illegal(void);
void int_priv(void);
void int_unimpint(void);

#ifdef __arm__
#define trap_save_area 0 /* not used on arm */
#else
extern WORD trap_save_area[];
#endif

/* 680x0 exception vectors */
#ifdef __arm__
volatile PFVOID *vector_address(ULONG address);
#define VEC_AT(address) (*vector_address(address))
#else
#define VEC_AT(address) (*(volatile PFVOID *)(address))
#endif
#define VEC_ILLEGAL VEC_AT(0x10) /* illegal instruction vector */
#define VEC_DIVNULL VEC_AT(0x14) /* division by zero exception vector */
#define VEC_PRIVLGE VEC_AT(0x20) /* privilege exception vector */
#define VEC_LINEA   VEC_AT(0x28) /* LineA exception vector */
#define VEC_LEVEL1  VEC_AT(0x64) /* Level 1 interrupt vector */
#define VEC_LEVEL2  VEC_AT(0x68) /* Level 2 interrupt vector */
#define VEC_LEVEL3  VEC_AT(0x6c) /* Level 3 interrupt vector */
#define VEC_LEVEL4  VEC_AT(0x70) /* Level 4 interrupt vector */
#define VEC_LEVEL5  VEC_AT(0x74) /* Level 5 interrupt vector */
#define VEC_LEVEL6  VEC_AT(0x78) /* Level 6 interrupt vector */
#define VEC_LEVEL7  VEC_AT(0x7c) /* Level 7 interrupt (not maskable) */
#define VEC_TRAP1   VEC_AT(0x84) /* TRAP #1 exception vector */
#define VEC_TRAP2   VEC_AT(0x88) /* TRAP #2 exception vector */
#define VEC_TRAP13  VEC_AT(0xb4) /* TRAP #13 exception vector */
#define VEC_TRAP14  VEC_AT(0xb8) /* TRAP #14 exception vector */
#define VEC_UNIMPINT VEC_AT(0xf4) /* unimplemented integer instruction exception vector */

/* MFP interrupt vectors */
#define VEC_MFP6   (*(volatile PFVOID*)0x118) /* MFP level 6 interrupt vector */

#if CONF_WITH_SCC
/* SCC interrupt vectors, default addresses */
#define VEC_SCCB_TBE (*(volatile PFVOID*)0x180) /* Channel B, transmit buffer empty */
#define VEC_SCCB_EXT (*(volatile PFVOID*)0x188) /* Channel B, external status change */
#define VEC_SCCB_RXA (*(volatile PFVOID*)0x190) /* Channel B, receive character available */
#define VEC_SCCB_SRC (*(volatile PFVOID*)0x198) /* Channel B, special receive condition */

#define VEC_SCCA_TBE (*(volatile PFVOID*)0x1a0) /* Channel A, transmit buffer empty */
#define VEC_SCCA_EXT (*(volatile PFVOID*)0x1a8) /* Channel A, external status change */
#define VEC_SCCA_RXA (*(volatile PFVOID*)0x1b0) /* Channel A, receive character available */
#define VEC_SCCA_SRC (*(volatile PFVOID*)0x1b8) /* Channel A, special receive condition */
#endif

/* Atari hardware interrupt mapping */
#define VEC_HBL     VEC_LEVEL2                /* HBL interrupt vector */
#define VEC_VBL     VEC_LEVEL4                /* VBL interrupt vector */
#define VEC_ACIA    VEC_MFP6                  /* Keyboard/MIDI interrupt vector */

/* OS exception mapping */
#define VEC_GEM     VEC_TRAP2                 /* GEM trap exception vector */
#define VEC_BIOS    VEC_TRAP13                /* BIOS trap exception vector */
#define VEC_XBIOS   VEC_TRAP14                /* XBIOS trap exception vector */

/* Non-Atari hardware vectors */
#if !CONF_WITH_MFP
extern void (*vector_5ms)(void);              /* 200 Hz system timer */
#endif

/*
 * VBL source seam for the machine-independent ARM int_timerc()
 * (bios/arch/arm/vectors.c): normally int_vbl() itself, faked off the
 * every-4th-tick 50 Hz as before, but a machine with a real vsync
 * interrupt can point this at its own handler instead, so it drives
 * VBL and int_timerc()'s fake becomes a fallback.  Only ARM machines
 * define and use this; m68k's int_timerc (bios/arch/m68k/vectors.S)
 * always calls int_vbl() directly.
 */
extern void (*timer_vbl_hook)(void);

/* protect d2/a2 when calling external user-supplied code */
#ifdef __m68k__
LONG protect_v(LONG (*func)(void));
LONG protect_w(LONG (*func)(WORD), WORD);
LONG protect_ww(LONG (*func)(void), WORD, WORD);
LONG protect_wlwwwl(LONG (*func)(void), WORD, LONG, WORD, WORD, WORD, LONG);
#elif defined (__arm__)

/* We assume ARM developers follow the eabi so the folllowing are simple pass-throughs */

static inline LONG protect_v(LONG (*func)(void))
{
    return func();
}
static inline LONG protect_w(LONG (*func)(WORD), WORD a)
{
    return func(a);
}
static inline LONG protect_ww(LONG (*func)(void), WORD a, WORD b)
{
    return ((LONG (*)(WORD, WORD))func)(a, b);
}
static inline LONG protect_wlwwwl(LONG (*func)(void), WORD a, LONG b, WORD c, WORD d, WORD e, LONG f)
{
    return ((LONG (*)(WORD, LONG, WORD, WORD, WORD, LONG))func)(a,b,c,d,e,f);
}
#endif

/* interrupt handlers in vectors.S */
#if CONF_WITH_MFP_RS232
void mfp_rs232_rx_interrupt(void);
void mfp_rs232_tx_interrupt(void);
#endif

#if CONF_WITH_TT_MFP
void mfp_tt_rx_interrupt(void);
void mfp_tt_tx_interrupt(void);
#endif

#if CONF_WITH_SCC
void scca_rx_interrupt(void);
void scca_tx_interrupt(void);
void scca_es_interrupt(void);
void sccb_rx_interrupt(void);
void sccb_tx_interrupt(void);
void sccb_es_interrupt(void);
#endif

#endif /* VECTORS_H */
