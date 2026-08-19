/*
 * tosvars.c - storage for the ARM subset of the TOS system variables
 *
 * On m68k, tosvars.ld gives every one of these a fixed low-memory
 * address, for compatibility with real TOS software that reads them
 * directly. ARM has no such software to be compatible with, and
 * Ssystem() (bdos/ssystem.c) now gives ARM programs a documented way
 * to reach the same values without a fixed address at all -- so on
 * ARM these are just ordinary global variables, wherever the linker
 * puts them. See #219.
 *
 * This covers exactly the system variables bios/tosvars.h declares
 * (i.e. the ones used by C code); a handful of others that aren't
 * declared there (con_state, themd, bufl, pun_ptr, vbclock) already
 * have -- or, for vbclock, now get -- real storage next to their one
 * user instead.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#include "portab.h"
#include "tosvars.h"

void (*etv_timer)(int);
LONG (*etv_critic)(WORD err, WORD dev);
void (*etv_term)(void);

UBYTE *phystop;
UBYTE *membot;
UBYTE *memtop;

volatile WORD flock;
WORD seekrate;
WORD timer_ms;
WORD fverify;
WORD bootdev;
BYTE defshiftmod;
BYTE sshiftmod;

UBYTE *v_bas_ad;
volatile WORD vblsem;
WORD nvbls;
LONG *vblqueue;
const UWORD *colorptr;
volatile LONG frclock;

void (*hdv_init)(void);
LONG (*hdv_bpb)(WORD dev);
LONG (*hdv_rw)(WORD rw, UBYTE *buf, WORD cnt, WORD recnr, WORD dev, LONG lrecnr);
LONG (*hdv_boot)(void);
LONG (*hdv_mediach)(WORD dev);

WORD cmdload;
BYTE conterm;
LONG savptr;
WORD nflops;
WORD save_row;
volatile LONG hz_200;
LONG drvbits;
UBYTE *dskbufp;
WORD dumpflg;

LONG sysbase;
UBYTE *end_os;
void (*exec_os)(void) NORETURN;
void (*dump_vec)(void);
void (*prt_stat)(void);
void (*prt_vec)(void);
void (*aux_stat)(void);
void (*aux_vec)(void);

LONG *p_cookies;

void (*bell_hook)(void);
void (*kcl_hook)(void);

LONG (*bconstat_vec[8])(void);
LONG (*bconin_vec[8])(void);
LONG (*bcostat_vec[8])(void);
LONG (*bconout_vec[8])(WORD, WORD);
LONG vbl_list[8];
