/* Experimental native CUIS wire ABI. No GEM arrays or kernel pointers. */
#ifndef CUIS_H
#define CUIS_H
#include "portab.h"

#define CUIS_SERVICE_CLASS 0x103UL
#define CUIS_SESSION 0UL
#define CUIS_VERSION 1UL
#define CUIS_SESSION_ATTACH 0UL
#define CUIS_SESSION_QUERY 1UL
#define CUIS_SESSION_DETACH 2UL
#define CUIS_FEATURE_SESSION 1UL

typedef struct {
    ULONG size, version, operation, handle;
    ULONG flags, reserved[3];
} CUIS_SESSION_REQUEST;

typedef struct {
    ULONG size, version, handle, features;
    ULONG reserved[4];
} CUIS_SESSION_INFO;

typedef char cuis_request_size[(sizeof(CUIS_SESSION_REQUEST) == 32) ? 1 : -1];
typedef char cuis_info_size[(sizeof(CUIS_SESSION_INFO) == 32) ? 1 : -1];
#endif
