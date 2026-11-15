/* Retained menu-bar metadata and delivery state for one AES context. */
#ifndef LIBAES_MENU_STATE_H
#define LIBAES_MENU_STATE_H
#include "libaes.h"
#include "layout.h"
#include "../../aes/shared/gemmenubar.h"
typedef struct {
    OBJECT *tree;
    MENU_BAR_ENTRY *entries;
    WORD count, pairs, height;
    LAYOUT_METRICS metrics;
    BOOL busy, pending;
    LONG recipient;
    WORD message[8];
} AES_MENU_STATE;
#endif
