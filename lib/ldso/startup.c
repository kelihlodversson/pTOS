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

int dl_start_stack(unsigned char *buffer, uint32_t capacity, uint32_t top,
                   const char *filename, uint32_t filenamesize,
                   const DL_START_VECTORS *vectors, const DL_START_INFO *info,
                   const DL_LAUNCH *launch, uint32_t *rsp, uint32_t *used)
{
    STACK s;
    uint32_t n, execfn, i, at;
    uint32_t aux[24], auxwords = 22, record = 0;
    int status;

    *rsp = *used = 0;
    if (!buffer || !filename || !vectors || !info || capacity > DL_START_CAPACITY ||
        capacity < 128 || capacity > top || !filenamesize || filenamesize > 1024 ||
        !vectors->argv || !vectors->envp)
        return DL_BAD_ELF;
    n = length(filename, filenamesize);
    if (!n || n == filenamesize)
        return DL_BAD_ELF;

    for (i = 0; i < capacity; i++)
        buffer[i] = 0;
    s.buffer = buffer; s.front = 0; s.back = capacity; s.base = top - capacity;
    if (!word(&s, vectors->argc) || !string(&s, filename, n, &execfn))
        return DL_NO_SPACE;
    if (launch) {
        uint32_t padding = (s.base + s.back) & 3U;
        if (s.back - s.front < sizeof(*launch) + padding)
            return DL_NO_SPACE;
        s.back -= sizeof(*launch) + padding;
        record = s.base + s.back;
        for (i = 0; i < sizeof(*launch); i++)
            buffer[s.back + i] = ((const unsigned char *)launch)[i];
    }
    status = vector(&s, vectors->argv, vectors->argc);
    if (status != DL_OK)
        return status;
    status = vector(&s, vectors->envp, vectors->envc);
    if (status != DL_OK)
        return status;

    aux[0] = 3; aux[1] = info->phdr;
    aux[2] = 4; aux[3] = 32;
    aux[4] = 5; aux[5] = info->phnum;
    aux[6] = 6; aux[7] = info->pagesz;
    aux[8] = 7; aux[9] = info->base;
    aux[10] = 9; aux[11] = info->entry;
    aux[12] = 31; aux[13] = execfn;
    aux[14] = DL_AT_PTOS_BASEPAGE; aux[15] = info->basepage;
    aux[16] = DL_AT_PTOS_BOOTDRIVE; aux[17] = info->bootdrive;
    aux[18] = DL_AT_PTOS_LOADBIAS; aux[19] = info->load_bias;
    if (record) {
        aux[20] = DL_AT_PTOS_LAUNCH; aux[21] = record;
        auxwords += 2;
    }
    aux[auxwords-2] = aux[auxwords-1] = 0;
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
