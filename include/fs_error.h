/* Loaded disk-error continuation: CPU-owned while executing, saved with its
 * suspended kernel continuation. No PROCESS identity owns this jump target. */
#ifndef FS_ERROR_H
#define FS_ERROR_H
#include <setjmp.h>
typedef struct {
    jmp_buf jump;
    long code, rw;
    int drive;
} FS_ERROR_STATE;
FS_ERROR_STATE *fs_error_current(void);
#define errbuf (fs_error_current()->jump)
#define errcode (fs_error_current()->code)
#define rwerr (fs_error_current()->rw)
#define errdrv (fs_error_current()->drive)
#endif
