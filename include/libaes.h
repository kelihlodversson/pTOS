/* Initial native libaes.so surface. The GEM API migrates in later slices. */
#ifndef LIBAES_H
#define LIBAES_H
#include "cuis.h"
#include "rectfunc.h"
#include "obdefs.h"
LONG aes_session_open(void);
LONG aes_session_query(LONG handle);
LONG aes_session_close(LONG handle);
WORD objc_add(OBJECT *tree, WORD parent, WORD child);
WORD objc_delete(OBJECT *tree, WORD object);
WORD objc_order(OBJECT *tree, WORD object, WORD position);
WORD objc_offset(OBJECT *tree, WORD object, WORD *x, WORD *y);
LONG aes_window_create(LONG x, LONG y, LONG width, LONG height);
LONG aes_window_configure(LONG handle, LONG x, LONG y, LONG width, LONG height);
LONG aes_window_destroy(LONG handle);
LONG aes_window_show(LONG handle);
LONG aes_window_hide(LONG handle);
LONG aes_window_raise(LONG handle);
LONG aes_window_lower(LONG handle);
LONG aes_window_redraw(LONG handle);
LONG aes_update_begin(LONG window);
LONG aes_update_end(LONG window);
LONG aes_window_clip(LONG window, ULONG index, LONG *x, LONG *y, LONG *width, LONG *height);
LONG aes_window_focus(LONG window);
LONG aes_window_focused(LONG window);
LONG aes_window_unfocus(LONG window);
LONG aes_key_read(LONG window, ULONG *key);
LONG aes_key_wait(LONG window, LONG timeout_ms);
#endif
