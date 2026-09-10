/*
 * kprint.h - BIOS console routines & debug macros
 *
 * Copyright (C) 2001-2020 The EmuTOS development team
 *
 * Authors:
 *  MAD     Martin Doering
 *  LVL     Laurent Vogel
 *  VRI     Vincent Rivière
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef KPRINT_H
#define KPRINT_H

/* console output */
int cprintf(const char *RESTRICT fmt, ...) PRINTF_STYLE;

/* native debugging output */
int kprintf(const char *RESTRICT fmt, ...) PRINTF_STYLE;

/* output done both through kprintf and cprintf */
int kcprintf(const char *RESTRICT fmt, ...) PRINTF_STYLE;

/* assert stuff */
#if CONF_WITH_ASSERT
extern void doassert(const char *, long, const char *, const char *);
#define assert(a) if(!(a)) { doassert(__FILE__, __LINE__, __FUNCTION__, #a); }
#else
#define assert(a) NULL_FUNCTION()
#endif

/* KINFO(()) outputs to the debugger, if kprintf() is available */
#if HAS_KPRINTF
#define KINFO(args) kprintf args
#else
#define KINFO(args) NULL_FUNCTION()
#endif

/* KDEBUG(()) may call KINFO(()) when locally enabled */
#ifdef ENABLE_KDEBUG
#define KDEBUG(args) KINFO(args)
#else
#define KDEBUG(args) NULL_FUNCTION()
#endif

/* A handy macro used when debugging */
#ifdef __GNUC__
#define HERE kprintf("HERE %s:%s:%d\n", __FILE__, __FUNCTION__, __LINE__);
#else
#define HERE kprintf("HERE %s:%d\n", __FILE__, __LINE__);
#endif

#endif /* KPRINT_H */
