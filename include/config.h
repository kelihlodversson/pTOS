/*
 * config.h - settings derived from the build configuration
 *
 * Copyright (C) 2001-2017 The EmuTOS development team
 *
 * Authors:
 *  MAD     Martin Doering
 *  LVL     Laurent Vogel
 *  VRI     Vincent Rivière
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

/*
 * The configurable options themselves are described in the Kconfig files
 * and are selected with "make menuconfig".  The result of that is
 * obj/autoconf.h, included below.
 *
 * What is left in this file is everything that is not a user choice: the
 * settings that follow mechanically from the configuration, the fixed
 * system limits, and a few sanity checks.
 */

#ifndef CONFIG_H
#define CONFIG_H

#include "autoconf.h"

/*
 * Offset of a ramtos TEXT symbol defined in obj/ramtos.h
 */
#if EMUTOS_LIVES_IN_RAM
#define OFFSETOF(x) (x - ADR_TEXT)
#endif

/*
 * Where in low memory the 68030 PMMU tree is built.  Unless you really
 * understand the implications, don't change this value!
 */
#if CONF_WITH_68030_PMMU
#define PMMUTREE_ADDRESS_68030 0x700
#endif

/*
 * Value written at the bottom of the desktop stack to detect overflows.
 */
#if CONF_DEBUG_DESK_STACK
#define STACK_MARKER 0xdeadbeef
#endif

/*
 * Path to the user cursor file.
 */
#if CONF_WITH_LOADABLE_CURSORS
#define CURSOR_RSC_NAME "A:\\EMUCURS.RSC"
#endif

/*
 * Determine if kprintf() is available.  It is, as soon as there is
 * somewhere to send its output to.
 */
#if CONF_WITH_UAE || DETECT_NATIVE_FEATURES || STONX_NATIVE_PRINT \
    || CONSOLE_DEBUG_PRINT || RS232_DEBUG_PRINT || SCC_DEBUG_PRINT \
    || COLDFIRE_DEBUG_PRINT || MIDI_DEBUG_PRINT
# define HAS_KPRINTF 1
#else
# define HAS_KPRINTF 0
#endif

/*
 * System configuration definitions
 */
/*
 * Maximum number of windows (the desktop itself counts as 1 window)
 *
 * Later AES versions support more windows; like upstream, we raise
 * the limit from 8 to 16 when the AES is configured above version
 * 0x0320 (TOS 2.06/3.06).
 */
#if (AES_VERSION > 0x0320)
# define NUM_WIN 16
#else
# define NUM_WIN 8
#endif

#define NUM_ACCS 6              /* maximum number of desk accessory   */
                                /* files (.ACC) that will be loaded   */
                                /* AND the maximum number of desk     */
                                /* accessory slots available (one     */
                                /* slot per mn_register() call)       */

#define BLKDEVNUM 26                    /* number of block devices supported: A: ... Z: */
#define INF_FILE_NAME "A:\\EMUDESK.INF" /* path to saved desktop file */
#define ICON_RSC_NAME "A:\\EMUICON.RSC" /* path to user icon file */

/*
 * Maximum lengths for pathname, filename, and filename components
 */
#define LEN_ZPATH 114                   /* max path length, incl drive */
#define LEN_ZFNAME 13                   /* max fname length, incl '\' separator */
#define LEN_ZNODE 8                     /* max node length */
#define LEN_ZEXT 3                      /* max extension length */
#define MAXPATHLEN (LEN_ZPATH+LEN_ZFNAME+1) /* convenient shorthand */

/*
 * Maximum coordinate supported (must fit in WORD)
 */
#define MAX_COORDINATE  (10000)         /* arbitrary, could be 32767 */

/*
 * Default keyboard auto-repeat settings: values are units of 20 msec
 */
#define KB_INITIAL  15          /* initial delay i.e. 300 msec */
#define KB_REPEAT   2           /* ticks between repeats, i.e. 40 msec */

/*
 * Sanity checks
 *
 * Most of these combinations are already impossible to select, because
 * the Kconfig dependencies rule them out.  They are kept here as a second
 * line of defence, mostly for hand-edited .config files.
 */

#if EMUTOS_LIVES_IN_RAM
# if DIAGNOSTIC_CARTRIDGE
#  error DIAGNOSTIC_CARTRIDGE is incompatible with EMUTOS_LIVES_IN_RAM.
# endif
#endif

#if !DETECT_NATIVE_FEATURES
# if CONF_WITH_ARANYM
#  error CONF_WITH_ARANYM requires DETECT_NATIVE_FEATURES.
# endif
#endif

#if !CONF_WITH_ADVANCED_CPU
# if CONF_WITH_68030_PMMU
#  error CONF_WITH_68030_PMMU requires CONF_WITH_ADVANCED_CPU.
# endif
# if CONF_WITH_APOLLO_68080
#  error CONF_WITH_APOLLO_68080 requires CONF_WITH_ADVANCED_CPU.
# endif
#endif

#if !CONF_WITH_YM2149
# if CONF_WITH_FDC
#  error CONF_WITH_FDC requires CONF_WITH_YM2149.
# endif
#endif

#if !CONF_WITH_ALT_RAM
# if CONF_WITH_STATIC_ALT_RAM
#  error CONF_WITH_STATIC_ALT_RAM requires CONF_WITH_ALT_RAM.
# endif
# if CONF_WITH_TTRAM
#  error CONF_WITH_TTRAM requires CONF_WITH_ALT_RAM.
# endif
#endif

#ifndef STATIC_ALT_RAM_ADDRESS
# if CONF_WITH_STATIC_ALT_RAM
#  error CONF_WITH_STATIC_ALT_RAM requires STATIC_ALT_RAM_ADDRESS.
# endif
# ifdef STATIC_ALT_RAM_SIZE
#  error STATIC_ALT_RAM_SIZE requires STATIC_ALT_RAM_ADDRESS.
# endif
#endif

#if !CONF_WITH_MFP
# if CONF_WITH_MFP_RS232
#  error CONF_WITH_MFP_RS232 requires CONF_WITH_MFP.
# endif
# if CONF_WITH_PRINTER_PORT
#  error CONF_WITH_PRINTER_PORT requires CONF_WITH_MFP.
# endif
# if CONF_WITH_FDC
#  error CONF_WITH_FDC requires CONF_WITH_MFP.
# endif
# if CONF_WITH_IKBD_ACIA
#  error CONF_WITH_IKBD_ACIA requires CONF_WITH_MFP.
# endif
# if CONF_WITH_MIDI_ACIA
#  error CONF_WITH_MIDI_ACIA requires CONF_WITH_MFP.
# endif
# if CONF_WITH_SCSI
#  error CONF_WITH_SCSI requires CONF_WITH_MFP.
# endif
#endif

#if !CONF_WITH_ATARI_VIDEO
# if CONF_WITH_STE_SHIFTER
#  error CONF_WITH_STE_SHIFTER requires CONF_WITH_ATARI_VIDEO.
# endif
# if CONF_WITH_TT_SHIFTER
#  error CONF_WITH_TT_SHIFTER requires CONF_WITH_ATARI_VIDEO.
# endif
# if CONF_WITH_VIDEL
#  error CONF_WITH_VIDEL requires CONF_WITH_ATARI_VIDEO.
# endif
#endif

#if !CONF_WITH_SCC
# if SCC_DEBUG_PRINT
#  error SCC_DEBUG_PRINT requires CONF_WITH_SCC.
# endif
#endif

#if !CONF_WITH_COLDFIRE_RS232
# if COLDFIRE_DEBUG_PRINT
#  error COLDFIRE_DEBUG_PRINT requires CONF_WITH_COLDFIRE_RS232.
# endif
#endif

#if !CONF_SERIAL_CONSOLE
# if CONF_SERIAL_CONSOLE_ANSI
#  error CONF_SERIAL_CONSOLE_ANSI requires CONF_SERIAL_CONSOLE.
# endif
#endif

#if !defined(MACHINE_FIREBEE) && !defined(MACHINE_M548X)
# if CONF_WITH_BAS_MEMORY_MAP
#  error CONF_WITH_BAS_MEMORY_MAP requires MACHINE_FIREBEE or MACHINE_M548X.
# endif
# if CONF_WITH_FLEXCAN
#  error CONF_WITH_FLEXCAN requires MACHINE_FIREBEE or MACHINE_M548X.
# endif
#endif

#ifndef MACHINE_AMIGA
# if CONF_WITH_UAE
#  error CONF_WITH_UAE requires MACHINE_AMIGA.
# endif
# if CONF_WITH_AROS
#  error CONF_WITH_AROS requires MACHINE_AMIGA.
# endif
#endif

#if (CONSOLE_DEBUG_PRINT + RS232_DEBUG_PRINT + SCC_DEBUG_PRINT + COLDFIRE_DEBUG_PRINT + MIDI_DEBUG_PRINT) > 1
# error Only one of CONSOLE_DEBUG_PRINT, RS232_DEBUG_PRINT, SCC_DEBUG_PRINT, COLDFIRE_DEBUG_PRINT or MIDI_DEBUG_PRINT must be set to 1.
#endif

#if !CONF_WITH_ACSI
# if CONF_WITH_ICDRTC
#  error CONF_WITH_ICDRTC requires CONF_WITH_ACSI
# endif
#endif

#if !CONF_WITH_DMASOUND
# if CONF_WITH_XBIOS_SOUND
#  error CONF_WITH_XBIOS_SOUND requires CONF_WITH_DMASOUND.
# endif
#endif

#endif /* CONFIG_H */
