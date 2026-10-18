#ifndef VDIPB_H
#define VDIPB_H 1

#include "config.h"

#if defined(X32_USERLAND) || (defined(__x86_64__) && defined(__ILP32__))
#include "vdi_user.h"
typedef VDI_USER_CONTROL VDICONTROL;
typedef VDI_USER_PB VDIPB;
#else

/* Internal/trusted VDI parameters, and the unchanged ARM/m68k interface.
 * The LP64 kernel representation must never overlay a ring-3 x32 block. */

/* VDI control parameters */
typedef struct
{
    WORD code;
    WORD nptsin;
    WORD nptsout;
    WORD nintin;
    WORD nintout;
    WORD subcode;
    WORD handle;
#if CONF_WITH_NATIVE_ABI
    WORD pad;
#endif
    void *ptr1;
    void *ptr2;
#if !CONF_WITH_NATIVE_ABI
    WORD filler; /* make it 12 WORDs long, as documented */
#endif
} VDICONTROL;

typedef struct
{
   VDICONTROL *contrl;  /* Pointer to contrl array */
   WORD  *intin;     /* Pointer to intin array  */
   WORD  *ptsin;     /* Pointer to ptsin array  */
   WORD  *intout;    /* Pointer to intout array */
   WORD  *ptsout;    /* Pointer to ptsout array */
} VDIPB;

#endif /* x32 user ABI */

#endif /* VDIPB_H */
