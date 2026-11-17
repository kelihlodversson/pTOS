/* A file selector owns all of its writable resource and search state. */
#ifndef LIBAES_FSEL_CONTEXT_H
#define LIBAES_FSEL_CONTEXT_H
#include "emutos.h"
#include "libaes.h"
#include "bdosdefs.h"
#include "layout.h"
#include "gem_rsc.h"
typedef struct aes_fsel_call {
    AESGLOBAL *global;
    WORD handle;
    LONG session;
    BOOL ok, dial;
    OBJECT objects[RS_NOBS], *trees[1];
    TEDINFO teds[RS_NTED];
    char text[RS_TEXT_BYTES], mask[LEN_ZPATH+1];
    DTA dta;
    GRECT screen, clip, bounds, center;
    LAYOUT_METRICS metrics;
    char *names;
    LONG *list, count, drives;
} FSEL_CONTEXT;
void aes_fsel_start(FSEL_CONTEXT *aes_fsel);
WORD aes_fsel_core(FSEL_CONTEXT *aes_fsel, char *path, char *selection, WORD *button, char *title);
void aes_fsel_draw(FSEL_CONTEXT *aes_fsel, OBJECT *tree, WORD object, WORD depth);
void aes_fsel_change(FSEL_CONTEXT *aes_fsel, OBJECT *tree, WORD object, UWORD state, WORD redraw);
void aes_fsel_rect(FSEL_CONTEXT *aes_fsel, OBJECT *tree, WORD object, GRECT *rect);
void aes_fsel_clip(FSEL_CONTEXT *aes_fsel, const GRECT *rect);
void aes_fsel_get_clip(FSEL_CONTEXT *aes_fsel, GRECT *rect);
void aes_fsel_pointer(FSEL_CONTEXT *aes_fsel, WORD *x, WORD *y);
void aes_fsel_cursor(FSEL_CONTEXT *aes_fsel, WORD mode);
WORD aes_fsel_form(FSEL_CONTEXT *aes_fsel, OBJECT *tree, WORD start);
WORD aes_fsel_dial(FSEL_CONTEXT *aes_fsel, WORD kind, GRECT *small, GRECT *large);
WORD aes_fsel_slide(FSEL_CONTEXT *aes_fsel, OBJECT *tree, WORD parent, WORD child, WORD vertical);
WORD aes_fsel_error(FSEL_CONTEXT *aes_fsel, WORD error);
WORD aes_fsel_show(FSEL_CONTEXT *aes_fsel, WORD string, WORD *number, WORD button);
WORD aes_fsel_drive(FSEL_CONTEXT *aes_fsel);
void *aes_fsel_get_dta(FSEL_CONTEXT *aes_fsel);
void aes_fsel_set_dta(FSEL_CONTEXT *aes_fsel, void *dta);
WORD aes_fsel_first(FSEL_CONTEXT *aes_fsel, char *path, WORD attributes);
WORD aes_fsel_next(FSEL_CONTEXT *aes_fsel);
void aes_fsel_copy_screen(FSEL_CONTEXT *aes_fsel, WORD sx, WORD sy, WORD dx, WORD dy, WORD width, WORD height);
#endif
