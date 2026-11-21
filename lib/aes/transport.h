#ifndef LIBAES_TRANSPORT_H
#define LIBAES_TRANSPORT_H
#include "cuis.h"
LONG cuis_call(ULONG function, LONG handle);
LONG cuis_call5(ULONG function, LONG a, LONG b, LONG c, LONG d, LONG e);
LONG cuis_call6(ULONG function, UQUAD a, UQUAD b, UQUAD c, UQUAD d, UQUAD e, UQUAD f);
LONG aes_graphics_call(ULONG function, UQUAD a, UQUAD b, UQUAD c, UQUAD d);
UQUAD aes_gemdos_call(ULONG function, UQUAD a, UQUAD b, UQUAD c);
UQUAD aes_gemdos_call4(ULONG function, UQUAD a, UQUAD b, UQUAD c, UQUAD d);
UQUAD aes_memory_call(ULONG function, UQUAD argument);
LONG aes_process_call(ULONG function);
LONG aes_drive_map(void);
/* Generic Ssystem(S_GETLVAL) exposes the authoritative 200-Hz system clock.
 * Keep the libaes deadline ring independent of GUI registration. */
static inline LONG aes_clock_ticks(void)
{
    return (LONG)((ULONG)aes_gemdos_call(0x154, 0x000a, 0x4ba, 0) & 0x7fffffffUL);
}
#endif
