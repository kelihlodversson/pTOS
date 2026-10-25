/* Private x32 VDI client image; shared with its preprocessed linker script. */
#ifndef X32VDI_IMAGE_H
#define X32VDI_IMAGE_H

#define X32_VDI_IMAGE_BASE 0x0ffe0000
#define X32_VDI_IMAGE_SIZE 0x00010000

#ifndef __ASSEMBLER__
#include "config.h"
#include "portab.h"
#if CONF_WITH_X32_VDI
#include "procmem.h"
BOOL x32_vdi_image_load(ASPACE *as);
ULONG x32_vdi_image_entry(void);
#endif
#endif
#endif
