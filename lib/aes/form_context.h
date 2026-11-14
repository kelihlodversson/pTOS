/* Per-call form gesture state; callbacks can nest ordinary GEM operations. */
#ifndef LIBAES_FORM_CONTEXT_H
#define LIBAES_FORM_CONTEXT_H
#include "libaes.h"
#include "layout.h"
typedef struct aes_form_call {
    AESGLOBAL *global;
    LONG session;
    BOOL ok, grabbed, overlay, have_metrics, full_screen, have_release, released_inside;
    LAYOUT_METRICS metrics;
    GRECT screen;
} FORM_CONTEXT;
BOOL aes_form_bounds(OBJECT *tree, WORD object, GRECT *rect);
WORD aes_form_button_core(FORM_CONTEXT *aes_form_context, OBJECT *tree, WORD object, WORD clicks, WORD *next);
WORD aes_form_watch_core(FORM_CONTEXT *aes_form_context, OBJECT *tree, WORD object, WORD in_state, WORD out_state);
WORD aes_form_watch(FORM_CONTEXT *aes_form_context, OBJECT *tree, WORD object, WORD in_state, WORD out_state);
void aes_form_clip(FORM_CONTEXT *aes_form_context, const GRECT *clip);
void aes_form_rect(FORM_CONTEXT *aes_form_context, OBJECT *tree, WORD object, GRECT *rect);
UWORD aes_form_flags(OBJECT *tree, WORD object, WORD *flags);
void aes_form_change(FORM_CONTEXT *aes_form_context, OBJECT *tree, WORD object, UWORD state, WORD redraw);
BOOL aes_form_stilldn(FORM_CONTEXT *aes_form_context, BOOL outside, WORD x, WORD y, WORD width, WORD height);
WORD aes_form_release_wait(FORM_CONTEXT *aes_form_context, WORD clicks, UWORD mask, UWORD state, WORD *results);
#endif
