/* Initial native libaes.so surface. The GEM API migrates in later slices. */
#ifndef LIBAES_H
#define LIBAES_H
#include "cuis.h"
#include "rectfunc.h"
LONG aes_session_open(void);
LONG aes_session_query(LONG handle);
LONG aes_session_close(LONG handle);
#endif
