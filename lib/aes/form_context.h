/* Per-call form gesture state; callbacks can nest ordinary GEM operations. */
#ifndef LIBAES_FORM_CONTEXT_H
#define LIBAES_FORM_CONTEXT_H
#include "gem.h"
#include "layout.h"
typedef struct {
    short *global;
    LONG session;
    BOOL ok, grabbed, have_metrics, full_screen, have_release, released_inside;
    LAYOUT_METRICS metrics;
    GRECT screen;
} FORM_CONTEXT;
extern FORM_CONTEXT *aes_form_context;
WORD aes_form_button_core(OBJECT *tree, WORD object, WORD clicks, WORD *next);
WORD aes_form_watch_core(OBJECT *tree, WORD object, WORD in_state, WORD out_state);
WORD aes_form_watch(OBJECT *tree, WORD object, WORD in_state, WORD out_state);
void aes_form_clip(const GRECT *clip);
void aes_form_rect(OBJECT *tree, WORD object, GRECT *rect);
UWORD aes_form_flags(OBJECT *tree, WORD object, WORD *flags);
void aes_form_change(OBJECT *tree, WORD object, UWORD state, WORD redraw);
BOOL aes_form_stilldn(BOOL outside, WORD x, WORD y, WORD width, WORD height);
WORD aes_form_release_wait(WORD clicks, UWORD mask, UWORD state, WORD *results);
#endif
