/* Kernel file-reference close and residual ownership; no public syscall ABI. */
#ifndef FS_CLOSE_H
#define FS_CLOSE_H
#include "portab.h"
#if CONF_WITH_USER_ASPACE
#define FS_CLOSE_RETAINED 0
#define FS_CLOSE_CONSUMED 1
#define FS_CLOSE_TRANSFERRED 2
#define FS_CLOSE_NOT_OWNED 3
#define FS_CLEANUP_PENDING 1
#define FS_CLEANUP_COMPLETE 2
#define FS_CLEANUP_INVALIDATED 3

typedef struct { UQUAD incarnation, reference; WORD handle; } FS_CLEANUP_TOKEN;
struct kproc;
/* Trusted MOVE-ONLY live owner, not a user capability or residual ticket.
 * Initialize to zero. Never copy an owned record; storage must survive cleanup. */
typedef struct { UQUAD incarnation; WORD handle; BOOL owned, closing; } FS_FILE_REFERENCE;
#define FS_FILE_REFERENCE_INIT { 0, 0, FALSE, FALSE }
LONG fs_reference_acquire(struct kproc *source, WORD descriptor,
                          UQUAD expected_incarnation, FS_FILE_REFERENCE *out);

typedef struct {
    LONG error, last_error;
    UWORD disposition;
    BOOL disk_error;
    int drive;
    LONG rw;
    FS_CLEANUP_TOKEN token;
} FS_CLOSE_REPORT;
typedef struct {
    FS_CLEANUP_TOKEN token;
    ULONG references; /* 0/1 for this transfer, never a reusable batch right */
    ULONG pending_references; /* aggregate pending count for the incarnation */
    LONG first_error, last_error;
    UWORD state;
} FS_CLEANUP_STATUS;
/* Caller owns one live reference. A failure consumes it only if reported;
 * disposal may transfer a retained reference to kernel SFT ownership. All
 * callbacks/I/O run outside caller leaf/IRQ/GUI exclusion. No scheduler queue. */
LONG fs_close_bounded(WORD handle, BOOL dispose, FS_CLOSE_REPORT *report);
LONG fs_close_once(WORD handle, BOOL cleanup, FS_CLOSE_REPORT *report);
/* RETAINED leaves owned set; CONSUMED/TRANSFERRED clear it even on error.
 * Revoked/stale incarnations clear ownership without touching replacement. */
LONG fs_reference_close(FS_FILE_REFERENCE *reference, BOOL dispose, FS_CLOSE_REPORT *report);
LONG fs_cleanup_snapshot(const FS_CLEANUP_TOKEN *token, FS_CLEANUP_STATUS *out);
LONG fs_cleanup_retry(const FS_CLEANUP_TOKEN *token, FS_CLOSE_REPORT *report);
/* Slot allocation/invalidation and access checks; no allocation or callbacks. */
BOOL fs_sft_claim(int slot);
BOOL fs_sft_accessible(int slot);
BOOL fs_sft_busy(int slot);
void fs_sft_invalidated(int slot);
#if CONF_WITH_X86_64_MEMTEST
void fs_close_test_exhaust(WORD handle);
#endif
long fs_close_backend(WORD handle, volatile BOOL *consumed);
#endif
#endif
