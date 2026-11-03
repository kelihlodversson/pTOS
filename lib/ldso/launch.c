/* Capture process launch names before any ELF application hooks run. */
#include "runtime.h"

/* The optional record was reserved by the startup stack builder. Its tail
 * is already independent of the default DTA in the writable basepage. */
void dl_capture_launch(DL_LAUNCH *launch, const char *name)
{
    char cwd[DL_LAUNCH_PATH];
    uint32_t n = 0, at = 0, i, prefix = 0;
    int64_t drive;
    if (!launch) return;
    launch->ready = 0;
    if (!name) return;
    while (n < DL_LAUNCH_PATH && name[n]) n++;
    if (!n || n == DL_LAUNCH_PATH) return;
    drive = dl_syscall(0x19,0,0,0,0,0,0);
    if (n >= 2 && name[1] == ':') {
        drive = name[0] >= 'a' && name[0] <= 'z' ? name[0]-'a' : name[0]-'A';
        prefix = 2;
    }
    if (drive < 0 || drive > 25) return;
    launch->command[at++] = 'A'+drive; launch->command[at++] = ':';
    if (name[prefix] != '\\' && name[prefix] != '/') {
        if (dl_syscall(0x47,(uintptr_t)cwd,drive+1,0,0,0,0) != 0) return;
        for (i = 0; i < sizeof(cwd) && cwd[i]; i++) ;
        if (i == sizeof(cwd)) return;
        if (!i || (cwd[0] != '\\' && cwd[0] != '/')) launch->command[at++] = '\\';
        for (i = 0; cwd[i]; i++) {
            if (at+1 >= DL_LAUNCH_PATH) return;
            launch->command[at++] = cwd[i] == '/' ? '\\' : cwd[i];
        }
        if (launch->command[at-1] != '\\') {
            if (at+1 >= DL_LAUNCH_PATH) return;
            launch->command[at++] = '\\';
        }
    }
    for (i = prefix; i < n; i++) {
        if (at+1 >= DL_LAUNCH_PATH) return;
        launch->command[at++] = name[i] == '/' ? '\\' : name[i];
    }
    launch->command[at] = 0;
    launch->ready = 1;
}

