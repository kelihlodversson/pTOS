/*
 * sd.h - header for SD/MMC card routines
 *
 * Copyright (C) 2013-2016 The EmuTOS development team
 *
 * Authors:
 *  RFB   Roger Burrows
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */
#ifndef _RASPI_EMCC_H
#define _RASPI_EMCC_H

#include <portab.h>

#if CONF_WITH_RASPI_EMMC

/* driver functions */
void raspi_emmc_init(void);
LONG raspi_emmc_ioctl(UWORD drv,UWORD ctrl,void *arg);
LONG raspi_emmc_rw(WORD rw,LONG sector,WORD count,UBYTE *buf,WORD dev);

void raspi_act_led_on(void);
void raspi_act_led_off(void);

#endif /* CONF_WITH_SDMMC */

#endif /* _SD_H */
