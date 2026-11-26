/* Copyright (C) 2026 The pTOS development team.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "emutos.h"
#include "exec_path.h"
#include "bdosstub.h"
#include "gemerror.h"
#include "string.h"

LONG exec_qualify_path(const char *path, char *out, ULONG capacity, DISK_ERROR *error)
{
    char directory[MAXPATHLEN];
    const char *cwd = directory;
    WORD drive = run->p_curdrv;
    ULONG used = 3, n, i;
    LONG rc;

    if (capacity < 4) return ERANGE;
    if (path[0] && path[1] == ':') {
        int c = (UBYTE)path[0];
        if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
        drive = c - 'A'; path += 2;
    }
    if (drive < 0 || drive >= 26) return EDRIVE;
    out[0] = 'A' + drive; out[1] = ':'; out[2] = '\\';
    if (*path == '\\' || *path == '/') path++;
    else {
        rc = DISK_CALL_ERROR(xgetdir(directory, drive + 1), error);
        if (rc < 0) return rc;
        while (*cwd == '\\' || *cwd == '/') cwd++;
        n = strlen(cwd);
        if (n >= capacity - used - 1) return ERANGE;
        memcpy(out + used, cwd, n); used += n;
        if (out[used - 1] != '\\' && out[used - 1] != '/') out[used++] = '\\';
    }
    n = strlen(path);
    if (n >= capacity - used) return ERANGE;
    memcpy(out + used, path, n + 1);
    for (i = 2; i < used + n; i++) if (out[i] == '/') out[i] = '\\';
    return E_OK;
}
