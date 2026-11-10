/*
 * binfmt.c - boot configuration and bounded executable redirection (#944)
 * Copyright (C) 2026 The pTOS development team.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "emutos.h"
#include "fs.h"
#include "mem.h"
#include "proc.h"
#include "bdosstub.h"
#include "gemerror.h"
#include "kheap.h"
#include "tosvars.h"
#include "binfmt.h"
#include "string.h"

static BINFMT_REGISTRY registry;

static void *allocate(ULONG size)
{
#if CONF_WITH_KHEAP
    return kalloc(size);
#else
    return xmxalloc(size, MX_PREFTTRAM);
#endif
}

static void release(void *p)
{
#if CONF_WITH_KHEAP
    kfree(p);
#else
    if (p) xmfree(p);
#endif
}

static void report(ULONG line, const char *message, void *argument)
{
    (void)argument;
    kcprintf("PTOS.INI:%lu: %s\n", (unsigned long)line, message);
}

void binfmt_boot_init(void)
{
    struct config_work {
        BINFMT_PARSER parser;
        char line[BINFMT_LINE + 2];
        UBYTE input[256];
    } *work;
    char path[] = "A:\\PTOS.INI";
    LONG handle, rc, closed;
    ULONG line = 1, used = 0, total = 0, i;
    BOOL overlong = FALSE;

    memset(&registry, 0, sizeof(registry));
    registry.bootdrive = bootdev;
    if (bootdev < 0 || bootdev >= 26) {
        kcprintf("PTOS.INI: invalid boot drive\n");
        return;
    }
    path[0] += bootdev;
    handle = kdos_fopen((long)path, 0);
    if (handle == EFILNF || handle == EPTHNF) return;
    if (handle < 0) {
        kcprintf("PTOS.INI: cannot open boot configuration (%ld)\n", (long)handle);
        return;
    }
    work = allocate(sizeof(*work));
    rc = work ? E_OK : ENSMEM;
    if (!work) goto done;
    binfmt_parser_init(&work->parser, &registry, bootdev, report, NULL);
    for (;;) {
        rc = kdos_fread(handle, sizeof(work->input), (long)work->input);
        if (rc <= 0) break;
        for (i = 0; i < (ULONG)rc; i++) {
            int c = work->input[i];
            if (++total > 65536UL) { rc = ERANGE; goto done; }
            if (c == '\n') {
                work->line[used] = 0;
                binfmt_config_line(&work->parser, work->line, used, overlong, line++);
                used = 0; overlong = FALSE;
            } else if (used < BINFMT_LINE + 1) work->line[used++] = c;
            else overlong = TRUE;
        }
    }
    if (rc == E_OK) {
        if (used || overlong) {
            work->line[used] = 0;
            binfmt_config_line(&work->parser, work->line, used, overlong, line);
        }
        binfmt_config_end(&work->parser);
    }
done:
    closed = kdos_fclose(handle);
    if (rc >= 0 && closed < 0) rc = closed;
    if (rc < 0) {
        registry.count = 0;
        kcprintf("PTOS.INI: configuration discarded (%ld)\n", (long)rc);
    }
    release(work);
}

static LONG read_header(const char *path, UBYTE *header, DISK_ERROR *error)
{
    LONG handle = DISK_CALL_ERROR(xopen((char *)path, 0), error), rc, closed;
    ULONG size = 0;
    if (handle < 0) return handle;
    while (size < BINFMT_HEADER) {
        rc = DISK_CALL_ERROR(xread(handle, BINFMT_HEADER - size, header + size), error);
        if (rc < 0) break;
        if (!rc) break;
        if ((ULONG)rc > BINFMT_HEADER - size) { rc = EPLFMT; break; }
        size += rc;
    }
    closed = kdos_fclose_error(handle, error);
    if (rc < 0) return rc;
    return closed < 0 ? closed : (LONG)size;
}

static BOOL same_path(const char *a, const char *b)
{
    for (;;) {
        int x = (UBYTE)*a++, y = (UBYTE)*b++;
        if (x == '/') x = '\\';
        if (y == '/') y = '\\';
        if (x >= 'A' && x <= 'Z') x += 'a' - 'A';
        if (y >= 'A' && y <= 'Z') y += 'a' - 'A';
        if (x != y) return FALSE;
        if (!x) return TRUE;
    }
}

void binfmt_release_environment(BINFMT_LAUNCH *launch)
{
    release(launch->environment);
    launch->environment = NULL;
    launch->path = NULL;
}

void binfmt_release(BINFMT_LAUNCH *launch)
{
    binfmt_release_environment(launch);
    release(launch->tail);
    launch->tail = NULL;
}

LONG binfmt_load_header(char *path, char *tail, char *environment,
                       PGMHDR01 *header, FH *handle, BINFMT_LAUNCH *launch)
{
    struct redirect_work {
        UBYTE header[BINFMT_HEADER];
        char visited[BINFMT_DEPTH + 1][BINFMT_PATH];
        BINFMT_SELECTION selection;
    } *work = NULL;
    LONG rc;
    DISK_ERROR error = {0};
    unsigned int depth = 0, i;

    memset(launch, 0, sizeof(*launch));
    for (;;) {
        rc = kpgmhdrld_error(path, header, handle, &error);
        if (error.code || rc != EPLFMT || !kpgm_can_interpret()) break;
        if (!work) {
            if (strlen(path) >= BINFMT_PATH) { rc = ERANGE; break; }
            work = allocate(sizeof(*work));
            if (!work) { rc = ENSMEM; break; }
            strcpy(work->visited[0], path);
        }
        rc = read_header(path, work->header, &error);
        if (error.code || rc < 0) break;
        rc = binfmt_select(&registry, work->header, rc, &work->selection);
        if (rc) { if (rc > 0) rc = EPLFMT; break; }
        if (depth == BINFMT_DEPTH) { rc = EPLFMT; break; }
        for (i = 0; i <= depth; i++)
            if (same_path(work->selection.interpreter, work->visited[i])) break;
        if (i <= depth) { rc = EPLFMT; break; }
        if (!launch->tail) {
            launch->tail = allocate(128);
            if (!launch->tail) { rc = ENSMEM; break; }
        }
        {
            char *next = allocate(BINFMT_ENV_WORK);
            char *argv;
            if (!next) { rc = ENSMEM; break; }
            rc = binfmt_arguments(environment, tail, &work->selection, path, next, launch->tail);
            if (rc) { release(next); break; }
            binfmt_release_environment(launch);
            launch->environment = next;
            /* The builder always supplies canonical ARGV followed by argv[0].
             * Keep the pathname there so alloc_env() copies it for the loader. */
            for (argv = next; *argv && strncmp(argv, "ARGV=", 5); argv += strlen(argv) + 1);
            launch->path = argv + strlen(argv) + 1;
        }
        path = launch->path; tail = launch->tail; environment = launch->environment;
        strcpy(work->visited[++depth], path);
    }
    if (error.code) rc = error.code;
    release(work);
    if (rc) binfmt_release(launch);
    DISK_RETHROW(error);       /* osif() remounts/retries E_CHNG after cleanup */
    return rc;
}
