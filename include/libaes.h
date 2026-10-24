/* Initial native libaes.so surface. The GEM API migrates in later slices. */
#ifndef LIBAES_H
#define LIBAES_H
#include "cuis.h"
#include "rectfunc.h"
LONG aes_session_open(CUIS_SESSION_INFO *info);
LONG aes_session_query(CUIS_SESSION_INFO *info);
LONG aes_session_close(void);
#endif
