/* Optional process-local launch snapshot, prepared before ELF preinitializers. */
#ifndef LDSO_LAUNCH_H
#define LDSO_LAUNCH_H
#include "dynamic.h"
#define DL_AT_PTOS_LAUNCH 0x5003
#define DL_AT_PTOS_LAUNCH_VECTORS 0x5004
#define DL_LAUNCH_PATH 260U
#define DL_LAUNCH_TAIL 128U
typedef struct {
    uint32_t ready, environment, environment_size;
    char command[DL_LAUNCH_PATH];
    unsigned char tail[DL_LAUNCH_TAIL];
} DL_LAUNCH;

/* ELF32 pointers to the original request, independent of the live vectors
 * and any interpreter prefix. This tag distinguishes it from old kernels. */
typedef struct dl_launch_vectors {
    uint32_t path, argv, envp;
} DL_LAUNCH_VECTORS;
#endif
