/* Convert GEMDOS trap arguments to the internal Pclone entry contract. */
#include "clone.h"

long clone_dispatch_native(const long *frame)
{
    /* Native frames have one machine-width slot per argument. Preserve the
     * whole slot so xclone can reject values outside the 32-bit user ABI. */
    return xclone((uint64_t)(unsigned long)frame[1],
                  (uint64_t)(unsigned long)frame[2]);
}

long clone_dispatch_m68k(const int16_t *frame)
{
    uint32_t address = ((uint32_t)(uint16_t)frame[1] << 16) | (uint16_t)frame[2];
    uint32_t size = ((uint32_t)(uint16_t)frame[3] << 16) | (uint16_t)frame[4];
    return xclone(address, size);
}
