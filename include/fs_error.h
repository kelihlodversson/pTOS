/* Loaded disk-error continuation: CPU-owned while executing, saved with its
 * suspended kernel continuation. No PROCESS identity owns this jump target. */
#ifndef FS_ERROR_H
#define FS_ERROR_H
/* Respect an already selected kernel jump ABI. Hosted fixtures may instead
 * select libc's ABI through their setjmp shim before including this header. */
#ifndef SETJMP_H
#include <setjmp.h>
#endif
typedef struct {
    jmp_buf jump;
    long code, rw;
    int drive;
} FS_ERROR_STATE;
#if CONF_WITH_NATIVE_SCHED
FS_ERROR_STATE *fs_error_current(void);
#else
/* Classic kernels have no CPU selection. Keep every access a direct constant
 * address, rather than charging small ROMs for out-of-line accessors. */
extern FS_ERROR_STATE fs_legacy_error;
#define fs_error_current() (&fs_legacy_error)
#endif
/* Non-MMU classic kernels retain their historical jump-only nested scope;
 * MMU execution must preserve error metadata across retained continuations. */
#if CONF_WITH_USER_ASPACE
typedef FS_ERROR_STATE FS_ERROR_SCOPE;
#define fs_error_scope_save(s) ((s)=*fs_error_current())
#define fs_error_scope_restore(s) (*fs_error_current()=(s))
#define fs_error_scope_jump(s) ((s).jump)
#else
typedef jmp_buf FS_ERROR_SCOPE;
#define fs_error_scope_save(s) memcpy((s),fs_error_current()->jump,sizeof(jmp_buf))
#define fs_error_scope_restore(s) memcpy(fs_error_current()->jump,(s),sizeof(jmp_buf))
#define fs_error_scope_jump(s) (s)
#endif
#define errbuf (fs_error_current()->jump)
#define errcode (fs_error_current()->code)
#define rwerr (fs_error_current()->rw)
#define errdrv (fs_error_current()->drive)
#endif
