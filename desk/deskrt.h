/*
 * deskrt.h - what EmuDesk gets from its surroundings
 *
 * Linked into the kernel, EmuDesk reads the state of the AES, of the VDI and
 * of the BIOS from their variables and calls their internal helpers.  As a
 * program (CONF_EXTERNAL_DESK) it asks for it: desk/deskrt.c answers through
 * the AES, the VDI and Ssystem().  The desk_*() names below are the same
 * thing both ways, so that desk/ does not need a conditional at each place.
 *
 * Copyright (C) 2026 The pTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef DESKRT_H
#define DESKRT_H

#if CONF_EXTERNAL_DESK

void desk_rt_init(void);                /* once the AES is up (graf_handle()) */
UBYTE desk_bootflags(void);             /* BOOTFLAG_* of include/biosext.h */
BOOL desk_rez_changeable(void);         /* the resolution can be changed */
BOOL desk_has_alt_ram(void);            /* there is alternative (TT) RAM */
LONG desk_idt(void);                    /* the _IDT cookie, 0 if there is none */
void desk_set_ceh(BOOL enable);         /* the critical error handler shows alerts */
void desk_set_background(UBYTE patcol); /* the pattern and colour of the AES desktop */
WORD desk_mouse_buttons(void);          /* bit 0 left, bit 1 right */
BOOL desk_cache_exists(void);           /* desk_set_cache() has an effect */
void desk_set_cache(WORD enable);       /* the CPU caches on or off */
BOOL desk_can_shutdown(void);           /* Shutdown() can power off or reset */
BOOL desk_has_videl(void);              /* a Falcon VIDEL */
BOOL desk_has_tt_shifter(void);         /* a TT shifter */
WORD desk_videl_mode(void);             /* the VIDEL mode, 0 if there is none */
WORD desk_floppy_type(void);            /* 0 if DD, 1 if HD (the _FDC cookie) */

/*
 * the helpers of the AES and the VDI that the desktop uses (aes/gemgraf.c,
 * declared in aesext.h), on a virtual workstation of its own
 */
#include "aesext.h"

#else /* in the kernel: the variables and the functions of the AES and the BIOS */

#include "biosext.h"
#include "has.h"
#include "aesext.h"
#include "lineavars.h"

#define desk_rt_init()          do { } while (0)
#define desk_bootflags()        (bootflags)
#define desk_rez_changeable()   (rez_changeable())
#define desk_has_alt_ram()      (has_alt_ram != 0)
#define desk_idt()              (Supexec((LONG)get_idt_cookie))
#define desk_set_ceh(e)         (enable_ceh = (e))
#define desk_set_background(p)  (set_aes_background(p))
#define desk_mouse_buttons()    (linea_vars.MOUSE_BT)
#define desk_cache_exists()     (cache_exists())
#define desk_can_shutdown()     (can_shutdown())
#define desk_has_videl()        (has_videl)
#define desk_has_tt_shifter()   (has_tt_shifter)
#define desk_videl_mode()       (get_videl_mode())
#define desk_floppy_type()      (Supexec((LONG)get_floppy_type))

#endif /* CONF_EXTERNAL_DESK */

#endif /* DESKRT_H */
