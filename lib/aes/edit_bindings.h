/* Caller-local bindings for the original GEM text editor. */
#ifndef LIBAES_EDIT_BINDINGS_H
#define LIBAES_EDIT_BINDINGS_H
#include "render_bindings.h"
#define edblk (aes_painter->edit)
#define ob_edit aes_edit_core
#define ob_draw aes_edit_draw
#define ob_actxywh aes_form_rect
void aes_form_rect(OBJECT *tree, WORD object, GRECT *rect);
WORD aes_edit_core(OBJECT *tree, WORD object, WORD key, WORD *index, WORD kind);
#endif
