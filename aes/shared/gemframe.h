/* Caller-owned state for the native 3D window frame builder. */
#ifndef GEMFRAME_H
#define GEMFRAME_H
#include "portab.h"
#include "obdefs.h"
#include "gemframeids.h"
typedef struct {
    OBJECT tree[NUM_ELEM];
    TEDINFO name, info;
} WM_FRAME;
typedef struct {
    GRECT border;
    UWORD kind;
    BOOL topped;
    WORD wbox, hbox, adjusted_width, adjusted_height;
    WORD hslide, vslide, hslsiz, vslsiz;
    char *name, *info;
    const WORD *top_colours, *bottom_colours;
} WM_FRAME_STATE;
void wm_frame_init(WM_FRAME *frame);
void wm_frame_build(OBJECT *tree, TEDINFO *name, TEDINFO *info, const WM_FRAME_STATE *state);
#endif
