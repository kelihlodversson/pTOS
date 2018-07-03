#ifndef MFORM_H
#define MFORM_H 1

/* Mouse / structure */
typedef struct mform
{
        WORD    mf_xhot;
        WORD    mf_yhot;
        WORD    mf_nplanes;
        WORD    mf_bg;          /* mask colour index */
        WORD    mf_fg;          /* data colour index */
        UWORD   mf_mask[16];
        UWORD   mf_data[16];
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
