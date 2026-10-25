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
#endif
