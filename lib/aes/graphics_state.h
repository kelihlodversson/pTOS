/* Retained workstation and cursor state for one userspace AES context. */
#ifndef LIBAES_GRAPHICS_STATE_H
#define LIBAES_GRAPHICS_STATE_H
#include "libaes.h"
#include "layout.h"
#include "style.h"
typedef struct {
    WORD handle, wbox, mouse_depth;
    LAYOUT_METRICS metrics;
    MFORM current, previous, saved;
    BOOL cursor_initialized;
    AES_3D_STYLE style;
} AES_GRAPHICS_STATE;
#endif
