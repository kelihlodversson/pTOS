/* A file selector owns all of its writable resource and search state. */
#ifndef LIBAES_FSEL_CONTEXT_H
#define LIBAES_FSEL_CONTEXT_H
#include "emutos.h"
#include "gem.h"
#include "bdosdefs.h"
#include "layout.h"
#include "gem_rsc.h"
typedef struct {
    WORD *global, handle;
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
extern FSEL_CONTEXT *aes_fsel;
void aes_fsel_start(void);
WORD aes_fsel_core(char *path, char *selection, WORD *button, char *title);
void aes_fsel_draw(OBJECT *tree, WORD object, WORD depth);
void aes_fsel_change(OBJECT *tree, WORD object, UWORD state, WORD redraw);
void aes_fsel_rect(OBJECT *tree, WORD object, GRECT *rect);
void aes_fsel_clip(const GRECT *rect);
void aes_fsel_get_clip(GRECT *rect);
void aes_fsel_pointer(WORD *x, WORD *y);
void aes_fsel_cursor(WORD mode);
WORD aes_fsel_form(OBJECT *tree, WORD start);
WORD aes_fsel_dial(WORD kind, GRECT *small, GRECT *large);
WORD aes_fsel_slide(OBJECT *tree, WORD parent, WORD child, WORD vertical);
WORD aes_fsel_error(WORD error);
WORD aes_fsel_show(WORD string, WORD *number, WORD button);
WORD aes_fsel_drive(void);
void *aes_fsel_get_dta(void);
void aes_fsel_set_dta(void *dta);
WORD aes_fsel_first(char *path, WORD attributes);
WORD aes_fsel_next(void);
void aes_fsel_copy_screen(WORD sx, WORD sy, WORD dx, WORD dy, WORD width, WORD height);
#endif
