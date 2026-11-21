/* Shared snapshot ABI, compile-only under real user toolchains. */
#include "process_identity.h"
#include "cuis.h"
typedef struct { char prefix; PROCESS_IDENTITY value; } IDENTITY_ALIGNMENT_PROBE;
typedef char pointer_width[sizeof(void *)==4 ? 1 : -1];
typedef char alignment[offsetof(IDENTITY_ALIGNMENT_PROBE,value)==4 ? 1 : -1];
typedef char stride[sizeof(PROCESS_IDENTITY[2])==32 ? 1 : -1];
typedef char signed_pid[((LONG)-1)<0 ? 1 : -1];
