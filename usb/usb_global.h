/*
 * Modified for the FreeMiNT USB subsystem by David Galvez. 2010 - 2011
 *
 * XaAES - XaAES Ain't the AES (c) 1992 - 1998 C.Graham
 *                                 1999 - 2003 H.Robbers
 *                                        2004 F.Naumann & O.Skancke
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This file is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with XaAES; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef _usb_global_h
#define _usb_global_h
#include "config.h"
#include "portab.h"
#include "string.h"
#include "kprint.h"
#include "endian.h"
#include "biosdefs.h"
#include "gemerror.h"
#include "../bios/cookie.h"

#include "../bdos/fs.h"
#include "../bdos/mem.h"

#ifdef MACHINE_RPI
#include "raspi_io.h"
#include "../bios/raspi_int.h"
#endif

#ifndef MACHINE_RPI
static inline ULONG phys_to_bus(ULONG phys)
{
	return phys;
}

static inline ULONG bus_to_phys(ULONG bus)
{
	return bus;
}
#endif

typedef char Path[MAXPATHLEN];

/*
 * debug section
 */

# define ALERT(x)       KINFO(x)
# define DEBUG(x)       KDEBUG(x)

#ifdef MACHINE_RPI
#define mdelay(x) 	raspi_delay_ms(x)
#define udelay(x) 	raspi_delay_us(x)
#define get_timer(x) raspi_get_timer(x)
#else
#error "The USB driver can currently only be compiled on RPI"
#endif

#define min3(x,y,z) ((x)<(y)?((x)<(z)?(x):(z)):((y)<(z)?(y):(z)))

/* cookie jar definition
 */

#define _USB 0x5f555342L

#endif /* _global_h */
