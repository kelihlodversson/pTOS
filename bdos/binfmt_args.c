/*
 * binfmt_args.c - lossless interpreter argv/environment construction (#944)
 * Copyright (C) 2026 The pTOS development team.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifdef BINFMT_HOST_TEST
#include <string.h>
#else
#include "emutos.h"
#include "string.h"
#endif
#include "binfmt.h"
#include "gemerror.h"

typedef struct {
    const char *environment, *arguments, *tail;
    ULONG regular_size, argc;
    UBYTE *empty;
} ARGUMENTS;

typedef LONG (*ARGUMENT_VISITOR)(void *context, const char *s, ULONG size, ULONG index);

static BOOL whitespace(int c)
{
    return c == ' ' || (c >= '\t' && c <= '\r');
}

static LONG argument_source(ARGUMENTS *a, const char *env, const char *tail, UBYTE *bitmap)
{
    ULONG size, i;
    const char *s, *metadata = NULL;
    memset(a, 0, sizeof(*a));
    memset(bitmap, 0, BINFMT_ARGC / 8);
    a->environment = env; a->tail = tail; a->empty = bitmap;
    for (size = 0; size + 1 < BINFMT_ENV; size++)
        if (!env[size] && !env[size + 1]) break;
    if (size + 1 == BINFMT_ENV) return ERANGE;
    size += 2;
    s = env;
    while (*s) {
        if (!strncmp(s, "ARGV=", 5)) {
            metadata = s + 5;
            a->arguments = s + strlen(s) + 1;
            break;
        }
        s += strlen(s) + 1;
    }
    a->regular_size = s - env;
    if (!a->arguments) return E_OK;
    s = a->arguments;
    while (*s) {
        if (++a->argc > BINFMT_ARGC) return ERANGE;
        s += strlen(s) + 1;
        if ((ULONG)(s - env) >= size) return EINVAL;
    }
    if (!a->argc) return EINVAL;
    if (strncmp(metadata, "NULL:", 5)) return E_OK;
    metadata += 5;
    do {
        if (*metadata < '0' || *metadata > '9') return EINVAL;
        i = 0;
        while (*metadata >= '0' && *metadata <= '9') {
            if (i > (BINFMT_ARGC - 1 - (*metadata - '0')) / 10) return ERANGE;
            i = i * 10 + *metadata++ - '0';
        }
        if (i >= a->argc) return EINVAL;
        bitmap[i / 8] |= 1U << (i & 7);
        if (!*metadata) break;
        if (*metadata++ != ',' || !*metadata) return EINVAL;
    } while (TRUE);
    return E_OK;
}

static LONG visit_arguments(const ARGUMENTS *a, const BINFMT_SELECTION *selection,
                            const char *program, ARGUMENT_VISITOR visit, void *context)
{
    ULONG index = 0, i, n, at;
    LONG rc;
    const char *s;
    char token[128];
    rc = visit(context, selection->interpreter, strlen(selection->interpreter), index++);
    if (rc) return rc;
    if (selection->has_argument) {
        rc = visit(context, selection->argument, strlen(selection->argument), index++);
        if (rc) return rc;
    }
    rc = visit(context, program, strlen(program), index++);
    if (rc) return rc;
    if (a->arguments) {
        s = a->arguments + strlen(a->arguments) + 1; /* replace original argv[0] */
        for (i = 1; i < a->argc; i++) {
            n = (a->empty[i / 8] & (1U << (i & 7))) ? 0 : strlen(s);
            rc = visit(context, s, n, index++);
            if (rc) return rc;
            s += strlen(s) + 1;
        }
        return E_OK;
    }
    /* The classic tail is bounded by its length byte AND its first NUL.
     * Never read beyond the NUL validated by the user-copy boundary. */
    n = (UBYTE)a->tail[0];
    if (n > 127) return ERANGE;
    s = a->tail + 1;
    for (i = 0; i < n; i++) if (!s[i]) break;
    n = i;
    at = 0;
    while (at < n) {
        ULONG used = 0;
        int quote = 0;
        while (at < n && whitespace((UBYTE)s[at])) at++;
        if (at == n) break;
        while (at < n) {
            int c = (UBYTE)s[at++];
            if (quote && c == quote) {
                if (at < n && s[at] == quote) at++;
                else { quote = 0; continue; }
            } else if (!quote && (c == '\'' || c == '"')) {
                quote = c; continue;
            } else if (!quote && whitespace(c)) break;
            token[used++] = c;
        }
        if (quote) return EINVAL;
        rc = visit(context, token, used, index++);
        if (rc) return rc;
    }
    return E_OK;
}

typedef struct {
    char *storage, *tail;
    ULONG used, tail_size;
    BOOL nulls, quoted;
} WRITER;

static LONG append(WRITER *w, const char *s, ULONG n)
{
    if (n > BINFMT_ENV - w->used) return ERANGE;
    memcpy(w->storage + w->used, s, n); w->used += n;
    return E_OK;
}

static LONG empty_indices(void *context, const char *s, ULONG n, ULONG index)
{
    WRITER *w = context;
    ULONG i;
    if (!n) {
        char digits[11];
        ULONG count = 0, value = index;
        LONG rc = append(w, w->nulls ? "," : "NULL:", w->nulls ? 1 : 5);
        if (rc) return rc;
        do { digits[count++] = '0' + value % 10; value /= 10; } while (value);
        for (i = 0; i < count / 2; i++) {
            char c = digits[i]; digits[i] = digits[count - i - 1]; digits[count - i - 1] = c;
        }
        rc = append(w, digits, count);
        if (rc) return rc;
        w->nulls = w->quoted = TRUE;
    }
    if (index) for (i = 0; i < n; i++)
        if (whitespace((UBYTE)s[i]) || s[i] == '\'') w->quoted = TRUE;
    return E_OK;
}

static void tail_byte(WRITER *w, char c)
{
    if (w->tail_size < 126) w->tail[w->tail_size + 1] = c;
    w->tail_size++;
}

static LONG write_argument(void *context, const char *s, ULONG n, ULONG index)
{
    WRITER *w = context;
    ULONG i;
    LONG rc = append(w, n ? s : " ", n ? n : 1);
    if (!rc) rc = append(w, "", 1);
    if (rc || !index) return rc;
    if (index > 1) tail_byte(w, ' ');
    if (w->quoted) tail_byte(w, '\'');
    for (i = 0; i < n; i++) {
        tail_byte(w, s[i]);
        if (w->quoted && s[i] == '\'') tail_byte(w, '\'');
    }
    if (w->quoted) tail_byte(w, '\'');
    return E_OK;
}

LONG binfmt_arguments(const char *environment, const char *tail,
                      const BINFMT_SELECTION *selection, const char *program,
                      char *storage, char output_tail[128])
{
    ARGUMENTS args;
    WRITER writer;
    LONG rc;
    rc = argument_source(&args, environment, tail, (UBYTE *)storage + BINFMT_ENV + 2);
    if (rc) return rc;
    memset(&writer, 0, sizeof(writer));
    memset(output_tail, 0, 128);
    writer.storage = storage; writer.tail = output_tail;
    rc = append(&writer, environment, args.regular_size);
    if (!rc) rc = append(&writer, "ARGV=", 5);
    if (!rc) rc = visit_arguments(&args, selection, program, empty_indices, &writer);
    if (!rc) rc = append(&writer, "", 1);
    if (!rc) rc = visit_arguments(&args, selection, program, write_argument, &writer);
    if (!rc) rc = append(&writer, "", 1);
    if (rc) return rc;
    storage[writer.used] = 0;        /* alloc_env() may copy an even-size pad */
    output_tail[0] = writer.nulls || writer.tail_size > 126 ? 127 : (char)writer.tail_size;
    return E_OK;
}
