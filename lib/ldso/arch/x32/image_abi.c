/* Initial x32 ELF image admission policy (#660). */
#include "image.h"

/* ELF32 EM_X86_64, little endian, 4 KiB pages, bounded anonymous image span. */
const DL_IMAGE_ABI dl_x32_image_abi = { 62, 1, 4096, 0x0e000000 };
