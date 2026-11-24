/*
 * launch_args.c - kernel-owned launch arguments
 *
 * Copyright (C) 2026 The pTOS development team.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "emutos.h"
#include "bdosdefs.h"
#include "gemerror.h"
#include "kheap.h"
#include "kproc.h"
#include "launch_args.h"
#include "procmem.h"

#define LAUNCH_ADDRESS_MAX 0xffffffffULL

LONG launch_copy_user_string(UQUAD address, char *buffer, ULONG capacity,
                             BOOL environment, ULONG *used)
{
    ULONG i;

    if (!address || address > LAUNCH_ADDRESS_MAX)
        return EIMBA;
    for (i = 0; i < capacity;) {
        UQUAD source = address + i;
        ULONG count = USER_PAGE_SIZE - (source & (USER_PAGE_SIZE - 1));
        ULONG end;
        LONG error;

        if (source > LAUNCH_ADDRESS_MAX)
            return EIMBA;
        if (count > 64)
            count = 64;
        if (count > capacity - i)
            count = capacity - i;
        /* A short string may end before the end of the readable range.
         * Check a page at a time, and retry narrow reads in that last range. */
        error = kproc_copy_from_user_checked(buffer + i, (VADDR)source, count);
        if (error != E_OK && count > 1) {
            count = 1;
            error = kproc_copy_from_user_checked(buffer + i, (VADDR)source, count);
        }
        if (error != E_OK)
            return error;
        end = i + count;
        for (; i < end; i++) {
            if (!buffer[i] && (!environment || (i && !buffer[i-1]))) {
                if (used)
                    *used = i + 1;
                return E_OK;
            }
        }
    }
    return ERANGE;
}

typedef struct {
    PEXECVE_ARGS args;
    const char *entries[PEXECVE_MAX_ENTRIES + 2];
    char path[PEXECVE_PATH_MAX];
    char strings[PEXECVE_ARG_MAX];
} PEXECVE_STORAGE;

typedef struct {
    char *next_string;
    ULONG bytes_left;
    ULONG entries_left;
} VECTOR_COPY;

static BOOL valid_slot(UQUAD address)
{
    return address && address <= LAUNCH_ADDRESS_MAX - (sizeof(ULONG) - 1);
}

static LONG copy_vector(VECTOR_COPY *copy, UQUAD address, const char **vector,
                        ULONG *count)
{
    ULONG i;

    for (i = 0; ; i++, address += sizeof(ULONG)) {
        ULONG string_address;
        ULONG used;
        LONG error;

        if (!valid_slot(address))
            return EIMBA;
        /* ULONG is 32 bits even on the LP64 x86-64 kernel. The wire
         * element uses the caller's native byte order on all three ports. */
        error = kproc_copy_from_user_checked(&string_address, (VADDR)address,
                                             sizeof(string_address));
        if (error != E_OK)
            return error;
        if (!string_address) {
            vector[i] = NULL;
            *count = i;
            return E_OK;
        }
        if (!copy->entries_left || copy->bytes_left <= sizeof(string_address))
            return ERANGE;
        copy->entries_left--;
        copy->bytes_left -= sizeof(string_address);
        error = launch_copy_user_string(string_address, copy->next_string,
                                        copy->bytes_left, FALSE, &used);
        if (error != E_OK)
            return error;
        vector[i] = copy->next_string;
        copy->next_string += used;
        copy->bytes_left -= used;
    }
}

LONG pexecve_args_capture(WORD mode, UQUAD path, UQUAD argv, UQUAD envp,
                          PEXECVE_ARGS **out)
{
    PEXECVE_STORAGE *storage;
    VECTOR_COPY copy;
    const char **environment;
    LONG error;

    if (mode != PE_LOADGO && mode != PE_ASYNC_LOADGO && mode != PE_EXEC_REPLACE)
        return EINVFN;
    if (!out || *out)
        return EINVAL;
    if (!path || path > LAUNCH_ADDRESS_MAX || !valid_slot(argv) || !valid_slot(envp))
        return EIMBA;
    storage = kalloc(sizeof(*storage));
    if (!storage)
        return ENSMEM;
    error = launch_copy_user_string(path, storage->path, sizeof(storage->path),
                                    FALSE, NULL);
    if (error != E_OK)
        goto fail;
    if (!storage->path[0]) {
        error = EFILNF;
        goto fail;
    }
    copy.next_string = storage->strings;
    copy.bytes_left = PEXECVE_ARG_MAX - 2 * sizeof(ULONG);
    copy.entries_left = PEXECVE_MAX_ENTRIES;
    error = copy_vector(&copy, argv, storage->entries, &storage->args.argc);
    if (error != E_OK)
        goto fail;
    environment = storage->entries + storage->args.argc + 1;
    error = copy_vector(&copy, envp, environment, &storage->args.envc);
    if (error != E_OK)
        goto fail;
    storage->args.path = storage->path;
    storage->args.argv = storage->entries;
    storage->args.envp = environment;
    *out = &storage->args;
    return E_OK;

fail:
    kfree(storage);
    return error;
}

void pexecve_args_release(PEXECVE_ARGS **args)
{
    PEXECVE_ARGS *owned = *args;

    *args = NULL;
    kfree(owned);
}
