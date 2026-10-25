/* Native CUIS syscall numbers. Arguments and results are scalars. */
#ifndef CUIS_H
#define CUIS_H
#include "portab.h"

#define CUIS_SERVICE_CLASS 0x103UL
#define CUIS_SESSION_START 0UL
#define CUIS_SESSION_QUERY 1UL
#define CUIS_SESSION_END 2UL
#define CUIS_WINDOW_CREATE 3UL
#define CUIS_WINDOW_CONFIGURE 4UL
#define CUIS_WINDOW_DESTROY 5UL
#define CUIS_WINDOW_SHOW 6UL
#define CUIS_WINDOW_HIDE 7UL
#define CUIS_WINDOW_RAISE 8UL
#define CUIS_WINDOW_LOWER 9UL
#define CUIS_WINDOW_REDRAW 10UL
/* Bound each session's kernel allocations independently of available RAM. */
#define CUIS_WINDOWS_PER_SESSION 64UL
#endif
