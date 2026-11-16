/* Caller-local desktop tree, region iterator and compatibility buffer. */
#ifndef LIBAES_DESKTOP_H
#define LIBAES_DESKTOP_H
#include "libaes.h"
BOOL aes_desktop_registered(AESGLOBAL *global);
void aes_desktop_reset(AESGLOBAL *global);
WORD aes_desktop_set(OBJECT *tree, WORD root, AESGLOBAL *global);
WORD aes_desktop_get(OBJECT **tree, WORD *root, AESGLOBAL *global);
BOOL aes_desktop_geometry(GRECT *rect, BOOL work, AESGLOBAL *global);
WORD aes_desktop_region(BOOL first, GRECT *rect, AESGLOBAL *global);
LONG aes_desktop_dispatch(AESGLOBAL *global);
void *aes_screen_buffer(LONG *bytes, AESGLOBAL *global);
#endif
