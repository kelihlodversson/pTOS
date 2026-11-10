/* pTOS process-clone GEMDOS extension. */
#ifndef PCLONE_H
#define PCLONE_H

/* Pclone is configuration-gated. Functional shared-VM PROCESS/THREAD startup
 * additionally requires a supported managed scheduler context. This number
 * is in the GEMDOS namespace (trap #1), not XBIOS. */
#define GEMDOS_PCLONE 0x190

/* Pclone receives the structure address and its size as separate arguments.
 * Future fields are appended. Pointer members use the application's native
 * 32-bit ABI, including on x32; they are not kernel-width integers. */
#include <stdint.h>

#define CLONE_VM                    (1U << 0) /* share ASPACE; new PROCESS unless SAME_PROCESS */
#define CLONE_SAME_PROCESS          (1U << 1) /* requires VM; resource-copy bits invalid */
#define CLONE_COPY_STANDARD_HANDLES (1U << 2)
#define CLONE_COPY_CWD              (1U << 3)
#define PTOS_CLONE_FLAG_MASK        15U
#define PTOS_CLONE_ARGS_MIN_SIZE    28U
#define PTOS_CLONE_ARGS_PID_SIZE    32U

/* Success returns a positive int32_t TID; child_pid is an independent PID,
 * optional even for NEW_PROCESS. Sizes 28..31 omit child_pid; >=32 includes it.
 * Unknown tail bytes are ignored after range arithmetic validation. No version
 * field. Unknown flags/non-NULL TLS fail EINVAL; private/unmanaged/unavailable
 * policies fail EINVFN after argument validation, without output/publication.
 * Outputs are native-aligned writable words, disjoint from each other and the
 * startup stack/executable pages. They are initial snapshots, not exit stores.
 * The void entry's normal return terminates that THREAD with status zero. */
struct ptos_clone_args {
    uint32_t flags;             /* CLONE_* policy bits */
    void (*entry)(void *);
    void *argument;
    void *stack_base;
    uint32_t stack_size;
    void *tls;                 /* reserved: must be NULL; TLS is independent follow-up #908 */
    int32_t *child_tid;
    int32_t *child_pid;         /* optional append-only output, present at size >= 32 */
};

#endif
