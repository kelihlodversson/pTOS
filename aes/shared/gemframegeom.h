/* Native frame/work margins with explicit box/mode metrics and wide results. */
#ifndef GEMFRAMEGEOM_H
#define GEMFRAMEGEOM_H
#include "portab.h"
typedef struct { LONG left, top, right, bottom; } WM_FRAME_INSETS;
BOOL wm_frame_insets(UWORD kind, WORD wbox, WORD hbox, BOOL three_d, WORD screen_width, WORD screen_height, WM_FRAME_INSETS *out);
#endif
