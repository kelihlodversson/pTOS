/*
 * startup.c - bounded GEMDOS argument conversion and ELF32 initial stack
 * Copyright (C) 2026 The pTOS development team.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "startup.h"

typedef struct {
    unsigned char *buffer;
    uint32_t front, back, base;
} STACK;

static uint32_t length(const unsigned char *s, uint32_t limit)
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

static int string(STACK *s, const unsigned char *text, uint32_t n, uint32_t *address)
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

static int argument(STACK *s, const unsigned char *text, uint32_t n)
{
    uint32_t address;
    return string(s, text, n, &address) && word(s, address);
}

int dl_start_stack(unsigned char *buffer, uint32_t capacity, uint32_t top,
                    const char *filename, uint32_t filenamesize,
                    const unsigned char *tail, uint32_t tailsize,
                    const unsigned char *env, uint32_t envsize,
                    const DL_START_INFO *info, uint32_t *rsp, uint32_t *used)
{
    STACK s;
    uint32_t pos, n, argc = 0, envend, argstart = 0, execfn, i, at, vectors;
    uint32_t aux[22];
    unsigned char token[128], quote;

    *rsp = *used = 0;
    if (!buffer || !filename || !tail || !env || !info || capacity > DL_START_CAPACITY ||
        capacity < 128 || capacity > top || !filenamesize || filenamesize > 1024 ||
        !tailsize || envsize < 2 || envsize > 32768)
        return DL_BAD_ELF;
    n = length((const unsigned char *)filename, filenamesize);
    if (!n || n == filenamesize)
        return DL_BAD_ELF;
    /* Even an unused ARGV payload must end inside the bounded environment. */
    for (i = 0; i + 1 < envsize; i++)
        if (!env[i] && !env[i + 1])
            break;
    if (i + 1 == envsize)
        return DL_BAD_ELF;
    for (i = 0; i < capacity; i++)
        buffer[i] = 0;
    s.buffer = buffer; s.front = 0; s.back = capacity; s.base = top - capacity;
    if (!word(&s, 0) || !string(&s, (const unsigned char *)filename, n, &execfn))
        return DL_NO_SPACE;
    /* Locate the end of ordinary environment entries and the extended argv. */
    pos = 0; envend = 0;
    while (pos < envsize && env[pos]) {
        n = length(env + pos, envsize - pos);
        if (n == envsize - pos)
            return DL_BAD_ELF;
        if (n >= 5 && env[pos] == 'A' && env[pos+1] == 'R' && env[pos+2] == 'G' &&
            env[pos+3] == 'V' && env[pos+4] == '=') {
            envend = pos;
            if (tail[0] == 127) {
                if (n != 5)
                    return DL_UNSUPPORTED;
                argstart = pos + n + 1;
            }
            break;
        }
        pos += n + 1;
        envend = pos;
    }
    if (pos >= envsize || (!pos && !env[0] && env[1]))
        return DL_BAD_ELF;
    if (tail[0] == 127) {
        pos = argstart;
        if (!pos || pos >= envsize || !env[pos])
            return DL_BAD_ELF;
        while (pos < envsize && env[pos]) {
            n = length(env + pos, envsize - pos);
            if (n == envsize - pos)
                return DL_BAD_ELF;
            if (!argument(&s, env + pos, n))
                return DL_NO_SPACE;
            argc++;
            pos += n + 1;
        }
        if (pos >= envsize)
            return DL_BAD_ELF;
    } else {
        if (tail[0] > 126 || (uint32_t)tail[0] + 1 > tailsize || !word(&s, execfn))
            return DL_BAD_ELF;
        argc = 1;
        pos = 1;
        while (pos <= tail[0]) {
            while (pos <= tail[0] && (tail[pos] == ' ' || tail[pos] == '\t'))
                pos++;
            if (pos > tail[0])
                break;
            n = 0; quote = 0;
            while (pos <= tail[0]) {
                unsigned char c = tail[pos++];
                if (!c)
                    return DL_BAD_ELF;
                if (quote) {
                    if (c == quote) quote = 0;
                    else token[n++] = c;
                } else if (c == '\'' || c == '"') {
                    quote = c;
                } else if (c == ' ' || c == '\t') {
                    break;
                } else {
                    token[n++] = c;
                }
            }
            if (quote)
                return DL_BAD_ELF;
            if (!argument(&s, token, n))
                return DL_NO_SPACE;
            argc++;
        }
    }
    if (!word(&s, 0))
        return DL_NO_SPACE;
    for (pos = 0; pos < envend; pos += n + 1) {
        n = length(env + pos, envend - pos);
        if (!argument(&s, env + pos, n))
            return DL_NO_SPACE;
    }
    if (!word(&s, 0))
        return DL_NO_SPACE;
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
    aux[20] = aux[21] = 0;
    for (i = 0; i < sizeof(aux) / sizeof(aux[0]); i++)
        if (!word(&s, aux[i]))
            return DL_NO_SPACE;
    for (i = 0; i < 4; i++)
        buffer[i] = ((unsigned char *)&argc)[i];
    vectors = s.front;
    if (s.back - vectors < 15)
        return DL_NO_SPACE;
    at = ((s.base + s.back - vectors) & ~15U) - s.base;
    /* memmove: vector source and final location may overlap. */
    for (i = vectors; i; i--)
        buffer[at + i - 1] = buffer[i - 1];
    *rsp = s.base + at;
    *used = top - *rsp;
    return DL_OK;
}
