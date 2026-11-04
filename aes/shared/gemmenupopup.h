/* Internal popup interaction, derived from the original GEM menu tracker.
 * Copyright (C) 2026 The pTOS development team. GPL version 2 or later. */
#ifndef GEMMENUPOPUP_H
#define GEMMENUPOPUP_H
#include "portab.h"
#include "obdefs.h"
#include "grect.h"

/* Geometry, input and drawing belong to the caller's active display context.
 * Event results use GEM MU_M1/MU_BUTTON bits and the usual four mouse words. */
typedef struct {
    BOOL (*draw)(void *context, OBJECT *tree, WORD root);
    BOOL (*change)(void *context, OBJECT *tree, WORD item, UWORD state);
    BOOL (*pointer)(void *context, WORD *x, WORD *y);
    WORD (*find)(void *context, OBJECT *tree, WORD root, WORD x, WORD y);
    BOOL (*rect)(void *context, OBJECT *tree, WORD item, GRECT *rect);
    WORD (*wait)(void *context, BOOL outside, const GRECT *rect, WORD *mouse);
} POPUP_TRACK_OPS;

/* start is an immediate child of root. Input/tree admission, background save,
 * display/mouse ownership and final release waits belong to the adapter.
 * Returns NIL for cancellation/error; ok distinguishes the two outcomes. */
WORD mn_popup_track(OBJECT *tree, WORD root, WORD start, const POPUP_TRACK_OPS *ops,
                     void *context, WORD *modifiers, BOOL *ok);
#endif
