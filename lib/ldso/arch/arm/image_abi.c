/* Initial ARM ELF image admission policy (#693). */
#include "image.h"

/* ELF32 EM_ARM, little endian, 4 KiB pages, bounded anonymous image span;
 * EABI version 5 with the hard-float ABI, the one every program and library of
 * the system is built for (the VFP registers of a process are saved by the
 * kernel, and a soft-float object would be a different calling convention). */
const DL_IMAGE_ABI dl_image_abi = { 40, 1, 4096, 0x0e000000, 0x05000400, 0x05000400 };
