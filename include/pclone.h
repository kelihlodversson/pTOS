/* pTOS process-clone GEMDOS extension. */
#ifndef PCLONE_H
#define PCLONE_H

/* Reserved for Pclone; the call is not available until its implementation is
 * enabled.  This number is in the GEMDOS namespace (trap #1), not XBIOS. */
#define GEMDOS_PCLONE 0x190

/* Version 1 argument layout.  Pclone receives the structure address and its
 * size as separate arguments.  Pointer members use the application's native
 * 32-bit ABI, including on x32; they are not kernel-width integers. */
#include <stdint.h>
#define PTOS_CLONE_VERSION_1 1U

struct ptos_clone_args {
    uint32_t version;
    uint32_t flags;
    void (*entry)(void *);
    void *argument;
    void *stack_base;
    uint32_t stack_size;
    void *tls;
    int32_t *child_tid;
};

#endif
