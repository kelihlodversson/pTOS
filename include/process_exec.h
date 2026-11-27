/* Native PROCESS-owned launch and observation transport. */
#ifndef PROCESS_EXEC_H
#define PROCESS_EXEC_H
#include "process_completion.h"
#define PROCESS_EXEC_SSYSTEM (-9)
#define PROCESS_EXEC_PREPARE 0UL
#define PROCESS_EXEC_COMMIT 1UL
#define PROCESS_EXEC_SNAPSHOT 2UL
#define PROCESS_EXEC_CLOSE 3UL
#define PROCESS_EXEC_CANCEL_DEFERRED 4UL
#define PROCESS_EXEC_DEFER 5UL
#define PROCESS_EXEC_START 6UL
#define PROCESS_EXEC_ASYNC 0UL
#define PROCESS_EXEC_DEFER_REPLACEMENT 1UL
#define PROCESS_EXEC_VECTOR_ARGS 1UL
#define PROCESS_EXEC_ACCESSORY 2UL
/* Size is a separate Ssystem argument. All current user ABIs have 32-bit
 * addresses; kernel decoding zero-extends these fields, never kernel pointers.
 * PREPARE supplies path/tail/env/CWD and policy. DEFER and START supply
 * the same launch inputs with zero policy and handle. With VECTOR_ARGS, the
 * tail and environment slots carry argv and envp, respectively. classic_tail
 * is an optional address of a 128-byte short tail for legacy shel_read.
 * ACCESSORY requests ELF accessory startup on PREPARE or START with vectors.
 * Other operations supply only operation/handle. Reserved fields must be zero.
 * PREPARE returns a positive owned handle as its scalar syscall result, COMMIT
 * returns PID and keeps the handle as observer. Neither operation copies out.
 * CANCEL_DEFERRED has no handle/arguments: cancel the calling PROCESS's
 * unpublished successor, if any. Idempotent; never cancel a running PROCESS.
 * DEFER prepares and commits an unobserved successor in one call, returning
 * zero on success. The operation closes its temporary right on every path.
 * START similarly launches an unobserved asynchronous child and returns its PID.
 * A failed replacement PREPARE/COMMIT must preserve the previous successor.
 * CLOSE consumes a valid owned handle once, including when it returns a
 * filesystem cleanup error. The right cannot be retried: residual references
 * belong to the SFT service. Closing a committed observer never cancels its
 * successor; cancellation is explicit.
 * SNAPSHOT alone copies a retryable non-consuming snapshot; a copy fault may
 * partially modify user memory but cannot consume the handle/completion. */
typedef struct {
    ULONG operation, handle, policy, flags;
    ULONG path, tail, environment, directory;
    PROCESS_COMPLETION completion;
    ULONG classic_tail, reserved;
} __attribute__((aligned(4))) PROCESS_EXEC_REQUEST;
typedef char PROCESS_EXEC_REQUEST_LAYOUT[
    sizeof(PROCESS_EXEC_REQUEST)==72 && __alignof__(PROCESS_EXEC_REQUEST)==4 &&
    offsetof(PROCESS_EXEC_REQUEST,operation)==0 && offsetof(PROCESS_EXEC_REQUEST,handle)==4 &&
    offsetof(PROCESS_EXEC_REQUEST,policy)==8 && offsetof(PROCESS_EXEC_REQUEST,flags)==12 &&
    offsetof(PROCESS_EXEC_REQUEST,path)==16 && offsetof(PROCESS_EXEC_REQUEST,tail)==20 &&
    offsetof(PROCESS_EXEC_REQUEST,environment)==24 && offsetof(PROCESS_EXEC_REQUEST,directory)==28 &&
    offsetof(PROCESS_EXEC_REQUEST,completion)==32 &&
    offsetof(PROCESS_EXEC_REQUEST,classic_tail)==64 && offsetof(PROCESS_EXEC_REQUEST,reserved)==68 ? 1 : -1];
#endif
