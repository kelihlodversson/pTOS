/* Private per-call modal form adapters. */
#ifndef LIBAES_MODAL_CONTEXT_H
#define LIBAES_MODAL_CONTEXT_H
#include "libaes.h"
#include "layout.h"
typedef struct aes_modal_call {
    AESGLOBAL *global;
    LONG session;
    BOOL ok, display, mouse, keyboard;
    LAYOUT_METRICS metrics;
} MODAL_CONTEXT;
WORD aes_form_do_core(MODAL_CONTEXT *aes_modal, OBJECT *tree, WORD start);
BOOL aes_modal_enter(MODAL_CONTEXT *aes_modal);
void aes_modal_leave(MODAL_CONTEXT *aes_modal);
WORD aes_modal_wait(MODAL_CONTEXT *aes_modal, WORD *results);
WORD aes_modal_edit(MODAL_CONTEXT *aes_modal, OBJECT *tree, WORD object, WORD key, WORD *index, WORD kind);
WORD aes_modal_keybd(MODAL_CONTEXT *aes_modal, OBJECT *tree, WORD object, WORD *key, WORD *next);
WORD aes_modal_button(MODAL_CONTEXT *aes_modal, OBJECT *tree, WORD object, WORD clicks, WORD *next);
WORD aes_form_button_checked(OBJECT *tree, WORD object, WORD clicks, WORD *next, AESGLOBAL *global, BOOL *ok);
WORD aes_form_keybd_checked(OBJECT *tree, WORD object, WORD proposed, WORD key, WORD *next, WORD *remaining, AESGLOBAL *global, BOOL *ok);
#endif
