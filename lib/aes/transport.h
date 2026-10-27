#ifndef LIBAES_TRANSPORT_H
#define LIBAES_TRANSPORT_H
#include "cuis.h"
LONG cuis_call(ULONG function, LONG handle);
LONG cuis_call5(ULONG function, LONG a, LONG b, LONG c, LONG d, LONG e);
LONG cuis_call6(ULONG function, UQUAD a, UQUAD b, UQUAD c, UQUAD d, UQUAD e, UQUAD f);
LONG aes_graphics_call(ULONG function, UQUAD a, UQUAD b, UQUAD c, UQUAD d);
UQUAD aes_gemdos_call(ULONG function, UQUAD a, UQUAD b, UQUAD c);
UQUAD aes_memory_call(ULONG function, UQUAD argument);
LONG aes_process_call(ULONG function);
#endif
