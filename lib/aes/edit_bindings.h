/* Caller-local bindings for the original GEM text editor. */
#ifndef LIBAES_EDIT_BINDINGS_H
#define LIBAES_EDIT_BINDINGS_H
#include "render_bindings.h"
#define edblk (aes_painter->edit)
#define ob_edit aes_edit_core
#define ob_draw aes_edit_draw
#define ob_actxywh aes_edit_rect

WORD aes_edit_core(PAINTER *aes_painter, OBJECT *tree, WORD object, WORD key, WORD *index, WORD kind);
#endif
