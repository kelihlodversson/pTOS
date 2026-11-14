/* Initial m68k ELF image admission policy (#694). */
#include "image.h"

/* ELF32 EM_68K, big endian, 4 KiB pages, bounded anonymous image span.
 * GNU/Linux emits zero e_flags; the pTOS SDK marks the classic m68k family
 * with EF_M68K_M68000. Both run on our 68020+ backend. Refuse other flags. */
const DL_IMAGE_ABI dl_image_abi = { 4, 2, 4096, 0x0e000000, 0, 0x01000000 };
