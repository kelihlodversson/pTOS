/*
 * bootargs.h - country/keyboard override parsed from the ARM boot
 * command line, for machines with CONF_MULTILANG but no NVRAM to store
 * the setting in (see bios/country.c's detect_akp()).
 *
 * Copyright (C) 2026 The pTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#ifndef BOOTARGS_H
#define BOOTARGS_H

#if CONF_WITH_BOOTARGS_LANG

/*
 * Look for a "ptos.lang=xx" parameter in the ARM boot loader's command
 * line -- an ATAG_CMDLINE record, or an FDT /chosen/bootargs property,
 * whichever arm_boot_regs.atags actually points to (see
 * include/arch/arm/arm_boot.h) -- and return the matching COUNTRY_xx
 * code from ctrycodes.h.  Returns -1 if there is no boot command line,
 * or it does not name a country code recognized in country.mk.
 */
int bootargs_get_country(void);

#endif /* CONF_WITH_BOOTARGS_LANG */

#endif /* BOOTARGS_H */
