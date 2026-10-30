#ifndef MFORM_H
#define MFORM_H 1

/* Mouse / structure */
#if defined(AES_GEM_TYPES)
typedef struct mouse_form
#else
typedef struct mform
#endif
{
        WORD    mf_xhot;
        WORD    mf_yhot;
        WORD    mf_nplanes;
#if defined(AES_GEM_TYPES)
        /* GEMlib names these two native VDI colour words in this order.
         * Public records pass through unchanged; the kernel/VDI spelling
         * below retains its original mask/data interpretation. */
        WORD    mf_fg;
        WORD    mf_bg;
#else
        WORD    mf_bg;          /* mask colour index */
        WORD    mf_fg;          /* data colour index */
#endif
#if defined(AES_GEM_TYPES)
        WORD    mf_mask[16];
        WORD    mf_data[16];
#else
        UWORD   mf_mask[16];
        UWORD   mf_data[16];
#endif
} MFORM;

/* Mouse / sprite structure */
typedef struct Mcdb_ Mcdb;
struct Mcdb_ {
        WORD    xhot;
        WORD    yhot;
        WORD    planes;
        WORD    bg_col;
        WORD    fg_col;
        UWORD   maskdata[32];   /* mask & data are interleaved */
};

#endif
