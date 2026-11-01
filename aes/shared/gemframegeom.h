/* Native frame/work margins with explicit box/mode metrics and wide results. */
#ifndef GEMFRAMEGEOM_H
#define GEMFRAMEGEOM_H
#include "portab.h"
typedef struct { LONG left, top, right, bottom; } WM_FRAME_INSETS;
BOOL wm_frame_insets(UWORD kind, WORD wbox, WORD hbox, BOOL three_d, WORD screen_width, WORD screen_height, WM_FRAME_INSETS *out);
typedef struct { LONG min_w, min_h, max_w, max_h; } WM_FRAME_LIMITS;
BOOL wm_frame_limits(UWORD kind, LONG x, LONG y, WORD wbox, WORD hbox,
                      WORD screen_width, WORD screen_height, WM_FRAME_LIMITS *out);
WORD wm_arrow_action(WORD object);
#endif
