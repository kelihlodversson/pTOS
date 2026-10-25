/* Initial m68k ELF image admission policy (#694). */
#include "image.h"

/* ELF32 EM_68K, big endian, 4 KiB pages, bounded anonymous image span.  The
 * e_flags of the GNU/Linux compiler for the 68020 and later are 0 (the MiNT
 * compilers set the CPU in them, and cannot make PIC objects at all). */
const DL_IMAGE_ABI dl_image_abi = { 4, 2, 4096, 0x0e000000, 0, 0 };
