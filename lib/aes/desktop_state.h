/* Retained window-zero metadata and screen storage for one AES context. */
#ifndef LIBAES_DESKTOP_STATE_H
#define LIBAES_DESKTOP_STATE_H
#include "libaes.h"
typedef struct {
    OBJECT *tree;
    WORD root;
    BOOL installed, painting;
    struct desktop_region *regions;
    void *screen_buffer;
    LONG screen_bytes;
} AES_DESKTOP_STATE;
#endif
