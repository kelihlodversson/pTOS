/*
 * startup.c - bounded ELF32 initial stack from argument vectors
 * Copyright (C) 2026 The pTOS development team.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "startup.h"

typedef struct {
    unsigned char *buffer;
    uint32_t front, back, base;
} STACK;

static uint32_t length(const char *s, uint32_t limit)
{
    uint32_t i;
    for (i = 0; i < limit && s[i]; i++)
        ;
    return i;
}

static int word(STACK *s, uint32_t value)
{
    uint32_t i;
    if (s->back - s->front < 4)
        return 0;
    for (i = 0; i < 4; i++)
        s->buffer[s->front++] = ((unsigned char *)&value)[i];
    return 1;
}

static int string(STACK *s, const char *text, uint32_t n, uint32_t *address)
{
    uint32_t i;
    if (n >= s->back - s->front)
        return 0;
    s->back -= n + 1;
    for (i = 0; i < n; i++)
        s->buffer[s->back + i] = text[i];
    s->buffer[s->back + n] = 0;
    *address = s->base + s->back;
    return 1;
}

static int vector(STACK *s, const char *const *entries, uint32_t count)
{
    uint32_t i, limit, address;

    for (i = 0; i < count; i++) {
        if (!entries[i])
            return DL_BAD_ELF;
        limit = s->back - s->front;
        if (!string(s, entries[i], length(entries[i], limit), &address) ||
            !word(s, address))
            return DL_NO_SPACE;
    }
    return word(s, 0) ? DL_OK : DL_NO_SPACE;
}

static int aligned_copy(STACK *s, const void *data, uint32_t bytes, uint32_t *address)
{
    uint32_t i, padding = (s->base + s->back - bytes) & 3U;

    if (bytes > s->back - s->front || padding > s->back - s->front - bytes)
        return 0;
    s->back -= bytes + padding;
    for (i = 0; i < bytes; i++)
        s->buffer[s->back + i] = ((const unsigned char *)data)[i];
    *address = s->base + s->back;
    return 1;
}

/* Reserve the saved slots at the back (the front is for live argc/argv).
 * Strings are then added below the slots, disjoint from the live vectors. */
static int save_vector(STACK *s, const char *const *entries, uint32_t count,
                       uint32_t *address)
{
    uint32_t i, at, value, bytes;

    if (!entries || count >= (s->back - s->front) / 4)
        return DL_NO_SPACE;
    bytes = (count + 1) * 4;
    at = ((s->base + s->back - bytes) & ~3U) - s->base;
    if (at < s->front || at > s->back)
        return DL_NO_SPACE;
    *address = s->base + at;
    s->back = at;
    for (i = 0; i < count; i++, at += 4) {
        uint32_t j, limit = s->back - s->front;
        if (!entries[i])
            return DL_BAD_ELF;
        if (!string(s, entries[i], length(entries[i], limit), &value))
            return DL_NO_SPACE;
        for (j = 0; j < 4; j++)
            s->buffer[at + j] = ((const unsigned char *)&value)[j];
    }
    return DL_OK; /* slots were zeroed, including the terminator */
}

int dl_start_stack(unsigned char *buffer, uint32_t capacity, uint32_t top,
                   const char *filename, uint32_t filenamesize,
                   const DL_START_VECTORS *vectors, const DL_START_INFO *info,
                   const DL_LAUNCH *launch, uint32_t *rsp, uint32_t *used)
{
    STACK s;
    uint32_t n, execfn, i, at, phdr;
    uint32_t aux[28], auxwords = 20, record = 0, snapshot = 0, guest = 0;
    int status;

    *rsp = *used = 0;
    if (!buffer || !filename || !vectors || !info || capacity > DL_START_CAPACITY ||
        capacity < 128 || capacity > top || !filenamesize || filenamesize > 1024 ||
        !vectors->argv || !vectors->envp || info->phent < 32 ||
        (info->phdr_copy && (info->phdr || !info->phnum)))
        return DL_BAD_ELF;
    n = length(filename, filenamesize);
    if (!n || n == filenamesize)
        return DL_BAD_ELF;

    for (i = 0; i < capacity; i++)
        buffer[i] = 0;
    s.buffer = buffer; s.front = 0; s.back = capacity; s.base = top - capacity;
    if (!word(&s, vectors->argc) || !string(&s, filename, n, &execfn))
        return DL_NO_SPACE;
    phdr = info->phdr;
    if (info->phdr_copy && (info->phnum > capacity / info->phent ||
        !aligned_copy(&s, info->phdr_copy, info->phnum * info->phent, &phdr)))
        return DL_NO_SPACE;
    if (launch || vectors->original) {
        DL_LAUNCH_VECTORS saved;
        const DL_START_VECTORS *original = vectors->original ? vectors->original : vectors;
        const char *path = info->launch_path ? info->launch_path : filename;
        uint32_t path_chars = info->launch_path ? length(path, 1024) : n;

        if (!path_chars || path_chars == 1024)
            return DL_BAD_ELF;
        if ((launch && !aligned_copy(&s, launch, sizeof(*launch), &record)) ||
            !string(&s, path, path_chars, &saved.path))
            return DL_NO_SPACE;
        status = save_vector(&s, original->argv, original->argc, &saved.argv);
        if (status != DL_OK)
            return status;
        status = save_vector(&s, original->envp, original->envc, &saved.envp);
        if (status != DL_OK)
            return status;
        if (!aligned_copy(&s, &saved, sizeof(saved), &snapshot))
            return DL_NO_SPACE;
        if (vectors->direct_redirect) {
            DL_GUEST_LAUNCH metadata = {0};
            const unsigned char *slot = buffer + (saved.argv - s.base);
            unsigned char *argv0 = (unsigned char *)&metadata.argv0;

            for (i = 0; i < sizeof(metadata.argv0); i++)
                argv0[i] = slot[i];
            if (original->guest_tail && !aligned_copy(&s, original->guest_tail,
                    DL_LAUNCH_TAIL, &metadata.tail))
                return DL_NO_SPACE;
            if (original->guest_environment) {
                metadata.environment_size = original->guest_environment_size;
                if (!aligned_copy(&s, original->guest_environment,
                                  metadata.environment_size, &metadata.environment))
                    return DL_NO_SPACE;
            }
            if (!aligned_copy(&s, &metadata, sizeof(metadata), &guest))
                return DL_NO_SPACE;
        }
    }
    status = vector(&s, vectors->argv, vectors->argc);
    if (status != DL_OK)
        return status;
    status = vector(&s, vectors->envp, vectors->envc);
    if (status != DL_OK)
        return status;

    aux[0] = 3; aux[1] = phdr;
    aux[2] = 4; aux[3] = info->phent;
    aux[4] = 5; aux[5] = info->phnum;
    aux[6] = 6; aux[7] = info->pagesz;
    aux[8] = 7; aux[9] = info->base;
    aux[10] = 9; aux[11] = info->entry;
    aux[12] = 31; aux[13] = execfn;
    aux[14] = DL_AT_PTOS_BASEPAGE; aux[15] = info->basepage;
    aux[16] = DL_AT_PTOS_BOOTDRIVE; aux[17] = info->bootdrive;
    aux[18] = DL_AT_PTOS_LOADBIAS; aux[19] = info->load_bias;
    if (record) {
        aux[auxwords++] = DL_AT_PTOS_LAUNCH; aux[auxwords++] = record;
    }
    if (snapshot) {
        aux[auxwords++] = DL_AT_PTOS_LAUNCH_VECTORS; aux[auxwords++] = snapshot;
    }
    if (guest) {
        aux[auxwords++] = DL_AT_PTOS_GUEST_LAUNCH; aux[auxwords++] = guest;
    }
    aux[auxwords++] = 0; aux[auxwords++] = 0;
    for (i = 0; i < auxwords; i++)
        if (!word(&s, aux[i]))
            return DL_NO_SPACE;
    if (s.back - s.front < 15)
        return DL_NO_SPACE;
    at = ((s.base + s.back - s.front) & ~15U) - s.base;
    /* memmove: vector source and final location may overlap. */
    for (i = s.front; i; i--)
        buffer[at + i - 1] = buffer[i - 1];
    *rsp = s.base + at;
    *used = top - *rsp;
    return DL_OK;
}
