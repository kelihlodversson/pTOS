/* Private bindings for the shared native box animations. */
#ifndef LIBAES_ANIMATION_H
#define LIBAES_ANIMATION_H
#include "libaes.h"
#include "render_context.h"
#define AES_ANIMATION_MOVE 4
WORD aes_form_animation(WORD kind, const GRECT *small, const GRECT *large, AESGLOBAL *global);
void aes_animation_scale(PAINTER *aes_painter, WORD xdist, WORD ydist, WORD *count, WORD *xstep, WORD *ystep);
#endif
