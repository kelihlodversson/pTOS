/* Initial standard ELF32 stack, independent of host pointers (#660). */
#ifndef LDSO_STARTUP_H
#define LDSO_STARTUP_H
#include "dynamic.h"
#include "launch.h"
#define DL_START_CAPACITY 131072
#define DL_AT_PTOS_BASEPAGE 0x5000
#define DL_AT_PTOS_BOOTDRIVE 0x5001
#define DL_AT_PTOS_LOADBIAS 0x5002

typedef struct {
    uint32_t phdr, phnum, entry, base, pagesz, basepage, bootdrive, load_bias;
} DL_START_INFO;

typedef struct {
    const char *const *argv, *const *envp;
    uint32_t argc, envc;
} DL_START_VECTORS;

/* Construct argc/argv/envp/auxv and private strings in a caller-owned buffer
 * representing [top-capacity,top). The vector inputs are already validated,
 * with a NUL-terminated string in each of their counted entries. Arguments
 * and environment entries are copied literally, including empty strings.
 * AT_EXECFN comes from filename. The optional launch record is copied into
 * the stack and reachable via AT_PTOS_LAUNCH. All inputs are immutable and
 * disjoint from the buffer and outputs. RSP is 16-byte aligned. On error,
 * outputs are zero and buffer contents are unspecified. No allocation. */
int dl_start_stack(unsigned char *buffer, uint32_t capacity, uint32_t top,
                   const char *filename, uint32_t filenamesize,
                   const DL_START_VECTORS *vectors, const DL_START_INFO *info,
                   const DL_LAUNCH *launch, uint32_t *rsp, uint32_t *used);
#endif
