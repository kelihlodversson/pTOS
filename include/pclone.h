/* pTOS process-clone GEMDOS extension. */
#ifndef PCLONE_H
#define PCLONE_H

/* Reserved for Pclone; the call is not available until its implementation is
 * enabled.  This number is in the GEMDOS namespace (trap #1), not XBIOS. */
#define GEMDOS_PCLONE 0x190

/* Pclone receives the structure address and its size as separate arguments.
 * Future fields are appended. Pointer members use the application's native
 * 32-bit ABI, including on x32; they are not kernel-width integers. */
#include <stdint.h>

struct ptos_clone_args {
    uint32_t flags;
    void (*entry)(void *);
    void *argument;
    void *stack_base;
    uint32_t stack_size;
    void *tls;
    int32_t *child_tid;
};

#endif
