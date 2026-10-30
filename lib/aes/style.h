/* Caller-local bindings of the native GEM 3D display preferences. */
#ifndef LIBAES_STYLE_H
#define LIBAES_STYLE_H
#include "portab.h"
typedef struct {
    WORD indicator_move, indicator_change, activator_move, activator_change;
    WORD indicator_colour, activator_colour, background;
    BOOL ready;
} AES_3D_STYLE;
extern AES_3D_STYLE aes_3d_style;
#endif
