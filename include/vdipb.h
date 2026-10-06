#ifndef VDIPB_H
#define VDIPB_H 1

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
#if defined(__arm__) || defined(__x86_64__)
    WORD pad;
#endif
    void *ptr1;
    void *ptr2;
#if !defined(__arm__) && !defined(__x86_64__)
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

#endif /* VDIPB_H */
