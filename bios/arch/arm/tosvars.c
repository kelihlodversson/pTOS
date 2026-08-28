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
#include "biosdefs.h"
#include "cookie.h"
#include "tosvars.h"
#include "header.h"
#include "asm.h"

void (*etv_timer)(int);
LONG (*etv_critic)(WORD err, WORD dev);
void (*etv_term)(void);
void (*swv_vec)(void);

UBYTE *phystop;
UBYTE *membot;
UBYTE *memtop;

volatile WORD flock;
WORD seekrate;
WORD timer_ms;
WORD fverify;
WORD bootdev;
UBYTE defshiftmod;
UBYTE sshiftmod;

UBYTE *v_bas_ad;
volatile WORD vblsem;
WORD nvbls;
PFVOID *vblqueue;
const UWORD *colorptr;
volatile LONG frclock;

void (*hdv_init)(void);
LONG (*hdv_bpb)(WORD dev);
LONG (*hdv_rw)(WORD rw, UBYTE *buf, WORD cnt, WORD recnr, WORD dev, LONG lrecnr);
LONG (*hdv_boot)(void);
LONG (*hdv_mediach)(WORD dev);

WORD cmdload;
UBYTE conterm;
LONG savptr;
WORD nflops;
WORD save_row;
volatile LONG hz_200;
LONG drvbits;
UBYTE *dskbufp;
WORD dumpflg;

/*
 * unlike m68k, this isn't overlaid on any real ROM header memory -- ARM
 * has no software depending on the exact byte layout of OSHEADER, so this
 * is just an ordinary struct populated with the same build-time values
 * bios/arch/m68k/startup.S embeds in its own header.
 */
const OSHEADER os_header = {
    0,                                          /* os_entry (unused on ARM) */
    0,                                          /* os_version */
    just_rts,                                   /* reseth */
    (OSHEADER *)&os_header,                     /* os_beg: ABI field is non-const */
    NULL,                                       /* os_end */
    NULL,                                       /* os_rsvl */
    NULL,                                       /* os_magic */
    OS_DATE,                                    /* os_date */
#if CONF_MULTILANG
    OS_CONF_MULTILANG,                          /* os_conf */
#else
    (OS_COUNTRY << 1) + OS_PAL,                 /* os_conf */
#endif
    OS_DOSDATE,                                 /* os_dosdate */
    NULL,                                       /* os_root */
    NULL,                                       /* os_kbshift */
    NULL,                                       /* os_run */
    0,                                          /* os_dummy */
};

const OSHEADER *sysbase;
UBYTE *end_os;
PRG_ENTRY *exec_os;
void (*dump_vec)(void);
void (*prt_stat)(void);
void (*prt_vec)(void);
void (*aux_stat)(void);
void (*aux_vec)(void);

struct cookie *p_cookies;

void (*bell_hook)(void);
void (*kcl_hook)(void);

LONG (*bconstat_vec[8])(void);
LONG (*bconin_vec[8])(void);
LONG (*bcostat_vec[8])(void);
LONG (*bconout_vec[8])(WORD, WORD);
LONG vbl_list[8];

/* on m68k this is patched externally in the OSXH header (see startup.S);
 * ARM has no such header, so the boot delay is simply always disabled. */
UBYTE osxhbootdelay;
