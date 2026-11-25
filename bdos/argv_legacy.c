/*
 * argv_legacy.c - checked conversion of legacy launch arguments
 * Copyright (C) 2026 The pTOS development team.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "argv_legacy.h"

static uint32_t length(const unsigned char *s, uint32_t limit)
{
    uint32_t i;
    for (i = 0; i < limit && s[i]; i++)
        ;
    return i;
}

static int starts_with(const unsigned char *s, uint32_t n, const char *prefix)
{
    for (; *prefix; n--, s++, prefix++)
        if (!n || *s != (unsigned char)*prefix)
            return 0;
    return 1;
}

static int classic_args(char *text, const char **argv, uint32_t capacity,
                        uint32_t *count, const unsigned char *tail)
{
    uint32_t pos = 1;

    while (pos <= tail[0]) {
        unsigned char quote = 0;
        while (pos <= tail[0] && (tail[pos] == ' ' || tail[pos] == '\t'))
            pos++;
        if (pos > tail[0])
            break;
        if (*count >= capacity)
            return DL_NO_SPACE;
        argv[(*count)++] = text;
        while (pos <= tail[0]) {
            unsigned char c = tail[pos++];
            if (!c)
                return DL_BAD_ELF;
            if (quote) {
                if (c == quote)
                    quote = 0;
                else
                    *text++ = c;
            } else if (c == '\'' || c == '"') {
                quote = c;
            } else if (c == ' ' || c == '\t') {
                break;
            } else {
                *text++ = c;
            }
        }
        if (quote)
            return DL_BAD_ELF;
        *text++ = 0;
    }
    return DL_OK;
}

static int mark_empty(const char **argv, uint32_t argc,
                      const unsigned char *digits, const unsigned char *end)
{
    while (digits < end) {
        uint32_t index = 0;
        if (*digits < '0' || *digits > '9')
            return DL_BAD_ELF;
        do {
            if (index > argc / 10)
                return DL_BAD_ELF;
            index = index * 10 + *digits++ - '0';
            if (index >= argc)
                return DL_BAD_ELF;
        } while (digits < end && *digits >= '0' && *digits <= '9');
        argv[index] = "";
        if (digits < end && (*digits++ != ',' || digits == end))
            return DL_BAD_ELF;
    }
    return DL_OK;
}

int legacy_start_vectors(LEGACY_START_ARGS *out, const char *filename,
                         const unsigned char *tail, uint32_t tailsize,
                         const unsigned char *env, uint32_t envsize,
                         const char **slots, uint32_t capacity)
{
    uint32_t pos, n, envc = 0, argc = 0;
    const unsigned char *digits = 0, *end = 0;
    uint32_t extended = 0;
    const char **argv;
    int status;

    if (!out || !filename || !tail || !tailsize || !env || envsize < 2 ||
        envsize > LEGACY_START_ENV_MAX || !slots)
        return DL_BAD_ELF;
    for (pos = 0; pos + 1 < envsize; pos++)
        if (!env[pos] && !env[pos + 1])
            break;
    if (pos + 1 == envsize || (!env[0] && env[1]))
        return DL_BAD_ELF;
    envsize = pos + 2;

    for (pos = 0; env[pos]; pos += n + 1) {
        n = length(env + pos, envsize - pos);
        if (starts_with(env + pos, n, "ARGV=")) {
            if (tail[0] == 127) {
                if (n != 5 && !starts_with(env + pos + 5, n - 5, "NULL:"))
                    return DL_UNSUPPORTED;
                if (n > 5) {
                    digits = env + pos + 10;
                    end = env + pos + n;
                }
                extended = pos + n + 1;
            }
            break;
        }
        if (envc == capacity)
            return DL_NO_SPACE;
        slots[envc++] = (const char *)(env + pos);
    }
    if (capacity - envc < 2)
        return DL_NO_SPACE;
    slots[envc] = 0;
    argv = slots + envc + 1;
    capacity -= envc + 2;
    if (tail[0] == 127) {
        if (!extended || !env[extended])
            return DL_BAD_ELF;
        for (pos = extended; env[pos]; pos += n + 1) {
            if (argc == capacity)
                return DL_NO_SPACE;
            n = length(env + pos, envsize - pos);
            argv[argc++] = (const char *)(env + pos);
        }
        if (digits) {
            status = mark_empty(argv, argc, digits, end);
            if (status != DL_OK)
                return status;
        }
    } else {
        if (tail[0] > 126 || (uint32_t)tail[0] + 1 > tailsize)
            return DL_BAD_ELF;
        if (!capacity)
            return DL_NO_SPACE;
        argv[argc++] = filename;
        status = classic_args(out->text, argv, capacity, &argc, tail);
        if (status != DL_OK)
            return status;
    }
    argv[argc] = 0;
    out->vectors.argv = argv;
    out->vectors.argc = argc;
    out->vectors.envp = slots;
    out->vectors.envc = envc;
    out->vectors.original = 0;
    out->vectors.classic_tail = 0;
    return DL_OK;
}
