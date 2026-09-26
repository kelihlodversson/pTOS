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

#if defined(__arm__) || defined(__x86_64__)
#define trap_save_area 0 /* not used on arm/x86-64 */
#else
extern WORD trap_save_area[];
#endif

/* 680x0 exception vectors */
#ifdef __arm__
volatile PFVOID *vector_address(ULONG address);
#define VEC_AT(address) (*vector_address(address))
#elif defined(__x86_64__)
/* This whole table is a fixed, historical 32-bit-per-slot layout (see
 * #351): setexc()'s generic path (bios/bios.c) reads and writes every
 * one of these cells -- and every other Setexc()-addressable vector
 * number, e.g. 0x8c/4=0x23, right next to VEC_GEM/VEC_TRAP2 at 0x88 --
 * as a plain 4-byte LONG. A native x86-64 function pointer is 8 bytes;
 * storing one directly through a PFVOID-typed cell, as the generic
 * fallback below does, spans two adjacent 4-byte slots and corrupts
 * whatever real, distinct GEMDOS vector lives in the next one. Every
 * VEC_LEVEL1..7/VEC_DIVNULL/VEC_GEM/VEC_BIOS/VEC_XBIOS write vecs_init()
 * (bios.c) does on this arch hits exactly this hazard, since they are
 * all spaced only 4 bytes apart. Keep the cell 4 bytes wide here too --
 * see SET_VEC() below for the matching write side. */
#define VEC_AT(address) (*(volatile LONG *)(address))
#else
#define VEC_AT(address) (*(volatile PFVOID *)(address))
#endif

/*
 * SET_VEC(cell, fn): the only safe way to store a function pointer into
 * a VEC_AT() cell from shared (m68k/ARM/x86-64) code such as bios.c's
 * vecs_init(). On m68k/ARM this is a plain assignment (cell is already
 * a native, correctly-sized PFVOID slot there). On x86-64, cell is a
 * 4-byte LONG (see VEC_AT's own comment above): this arch's own trap
 * dispatch never reads these particular cells back either way
 * (bios/arch/x86_64/trap.c's own comment on why bios_init() writing
 * VEC_GEM/VEC_BIOS/VEC_XBIOS is "pure unread data" here), so storing
 * only the pointer's low 32 bits is safe -- a stray Setexc()-based
 * read-back still gets *something* plausible-looking rather than
 * always-zero, matching every other slot's own convention, without
 * ever touching the next slot the way a full 8-byte store would. */
#if defined(__x86_64__)
#define SET_VEC(cell, fn) ((cell) = (LONG)(long)(fn))
#else
#define SET_VEC(cell, fn) ((cell) = (fn))
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
#elif defined (__arm__) || defined(__x86_64__)

/* We assume ARM/x86-64 developers follow their platform's own standard
 * calling convention (AAPCS / SysV x86-64), so the following are simple
 * pass-throughs on either: neither needs the m68k d2/a2-preservation
 * trick above (no such caller-saved-vs-callee-saved mismatch exists to
 * protect against). */

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
/*
 * `long b`, not portab.h's always-32-bit LONG: lrwabs() (bios/bios.c)
 * passes a genuine buffer pointer through this slot, and hdv_rw's real
 * signature (tosvars.h) already declares it `UBYTE *`. `long` matches
 * that pointer's real width on every arch this branch serves (32 bits on
 * ARM's ILP32, same as LONG there; 64 bits on x86-64's LP64, where a
 * LONG-sized slot would truncate it before the real driver ever saw it
 * -- #350's review).
 */
static inline LONG protect_wlwwwl(LONG (*func)(void), WORD a, long b, WORD c, WORD d, WORD e, LONG f)
{
    return ((LONG (*)(WORD, long, WORD, WORD, WORD, LONG))func)(a,b,c,d,e,f);
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
