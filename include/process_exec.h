/* DRAFT public transport layout for #907 review. No Ssystem handler is enabled
 * by this header; kernels currently return EINVFN for the proposed mode. */
#ifndef PROCESS_EXEC_H
#define PROCESS_EXEC_H
#include "process_completion.h"
/* Agreed Ssystem selector -9 remains test-gated, never advertised. */
#define PROCESS_EXEC_PREPARE 0UL
#define PROCESS_EXEC_COMMIT 1UL
#define PROCESS_EXEC_SNAPSHOT 2UL
#define PROCESS_EXEC_CLOSE 3UL
#define PROCESS_EXEC_CANCEL_DEFERRED 4UL
#define PROCESS_EXEC_ASYNC 0UL
#define PROCESS_EXEC_DEFER_REPLACEMENT 1UL
/* Size is a separate Ssystem argument. All current user ABIs have 32-bit
 * addresses; kernel decoding zero-extends these fields, never kernel pointers.
 * PREPARE supplies path/tail/env/CWD and policy; other operations supply only
 * operation/handle and zero other inputs. Reserved fields must be zero.
 * PREPARE returns a positive owned handle as its scalar syscall result, COMMIT
 * returns PID and keeps the handle as observer. Neither operation copies out.
 * CANCEL_DEFERRED has no handle/arguments: cancel the calling PROCESS's
 * unpublished successor, if any. Idempotent; never cancel a running PROCESS.
 * A failed replacement PREPARE/COMMIT must preserve the previous successor.
 * CLOSE consumes a valid right even when cleanup returns an error; retry is
 * EIHNDL, not another release. Residual file ownership belongs to the SFT
 * service. A committed observer CLOSE never cancels execution.
 * SNAPSHOT alone copies a retryable non-consuming snapshot; a copy fault may
 * partially modify user memory but cannot consume the handle/completion. */
typedef struct {
    ULONG operation, handle, policy, reserved;
    ULONG path, tail, environment, directory;
    PROCESS_COMPLETION completion;
    ULONG reserved_tail[2];
} __attribute__((aligned(4))) PROCESS_EXEC_REQUEST;
typedef char PROCESS_EXEC_REQUEST_LAYOUT[
    sizeof(PROCESS_EXEC_REQUEST)==72 && __alignof__(PROCESS_EXEC_REQUEST)==4 &&
    offsetof(PROCESS_EXEC_REQUEST,operation)==0 && offsetof(PROCESS_EXEC_REQUEST,handle)==4 &&
    offsetof(PROCESS_EXEC_REQUEST,policy)==8 && offsetof(PROCESS_EXEC_REQUEST,reserved)==12 &&
    offsetof(PROCESS_EXEC_REQUEST,path)==16 && offsetof(PROCESS_EXEC_REQUEST,tail)==20 &&
    offsetof(PROCESS_EXEC_REQUEST,environment)==24 && offsetof(PROCESS_EXEC_REQUEST,directory)==28 &&
    offsetof(PROCESS_EXEC_REQUEST,completion)==32 &&
    offsetof(PROCESS_EXEC_REQUEST,reserved_tail)==64 ? 1 : -1];
#endif
