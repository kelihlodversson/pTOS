/* Private per-call modal form adapters. */
#ifndef LIBAES_MODAL_CONTEXT_H
#define LIBAES_MODAL_CONTEXT_H
#include "gem.h"
#include "layout.h"
typedef struct {
    WORD *global;
    LONG session;
    BOOL ok, display, mouse, keyboard;
    LAYOUT_METRICS metrics;
} MODAL_CONTEXT;
extern MODAL_CONTEXT *aes_modal;
WORD aes_form_do_core(OBJECT *tree, WORD start);
BOOL aes_modal_enter(void);
void aes_modal_leave(void);
WORD aes_modal_wait(WORD *results);
WORD aes_modal_edit(OBJECT *tree, WORD object, WORD key, WORD *index, WORD kind);
WORD aes_modal_keybd(OBJECT *tree, WORD object, WORD *key, WORD *next);
WORD aes_modal_button(OBJECT *tree, WORD object, WORD clicks, WORD *next);
WORD aes_form_button_checked(OBJECT *tree, WORD object, WORD clicks, WORD *next, WORD *global, BOOL *ok);
WORD aes_form_keybd_checked(OBJECT *tree, WORD object, WORD proposed, WORD key, WORD *next, WORD *remaining, WORD *global, BOOL *ok);
#endif
