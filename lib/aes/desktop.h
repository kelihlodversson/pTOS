/* Caller-local desktop tree, region iterator and compatibility buffer. */
#ifndef LIBAES_DESKTOP_H
#define LIBAES_DESKTOP_H
#include "gem.h"
BOOL aes_desktop_registered(void);
void aes_desktop_reset(void);
WORD aes_desktop_set(OBJECT *tree, WORD root, WORD *global);
WORD aes_desktop_get(OBJECT **tree, WORD *root, WORD *global);
BOOL aes_desktop_geometry(GRECT *rect, BOOL work, WORD *global);
WORD aes_desktop_region(BOOL first, GRECT *rect, WORD *global);
LONG aes_desktop_dispatch(WORD *global);
void *aes_screen_buffer(LONG *bytes, WORD *global);
#endif
