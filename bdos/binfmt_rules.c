/*
 * binfmt_rules.c - bounded PTOS.INI and interpreter-header parsing (#944)
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

static int lower(int c)
{
    return c >= 'A' && c <= 'Z' ? c + 'a' - 'A' : c;
}

static BOOL equal(const char *a, const char *b)
{
    while (*a && lower((UBYTE)*a) == lower((UBYTE)*b)) {
        a++;
        b++;
    }
    return lower((UBYTE)*a) == lower((UBYTE)*b);
}

static BOOL space(int c)
{
    return c == ' ' || c == '\t';
}

static BOOL has_nul(const UBYTE *bytes, ULONG length)
{
    ULONG i;
    for (i = 0; i < length; i++) if (!bytes[i]) return TRUE;
    return FALSE;
}

static char *trim(char *s)
{
    char *end;
    while (space(*s)) s++;
    end = s + strlen(s);
    while (end > s && space(end[-1])) end--;
    *end = 0;
    return s;
}

static void diagnostic(BINFMT_PARSER *p, ULONG line, const char *message)
{
    if (p->diagnostic) p->diagnostic(line, message, p->argument);
}

static void invalid(BINFMT_PARSER *p, ULONG line, const char *message)
{
    if (p->current) p->current->valid = FALSE;
    diagnostic(p, line, message);
}

LONG binfmt_absolute(WORD bootdrive, const char *path, char *result)
{
    ULONG prefix = 0, i, length = strlen(path);
    if (path[0] == '/' || path[0] == '\\') {
        if (bootdrive < 0 || bootdrive >= 26) return EPLFMT;
        result[0] = 'A' + bootdrive;
        result[1] = ':';
        prefix = 2;
    } else if (!length || lower((UBYTE)path[0]) < 'a' || lower((UBYTE)path[0]) > 'z' ||
               path[1] != ':' || (path[2] != '/' && path[2] != '\\')) {
        return EPLFMT;
    }
    if (length >= BINFMT_INTERPRETER - prefix) return ERANGE;
    for (i = 0; i <= length; i++) result[prefix + i] = path[i] == '/' ? '\\' : path[i];
    return E_OK;
}

void binfmt_parser_init(BINFMT_PARSER *p, BINFMT_REGISTRY *registry,
                       WORD bootdrive, BINFMT_DIAGNOSTIC report, void *argument)
{
    memset(registry, 0, sizeof(*registry));
    registry->bootdrive = bootdrive;
    memset(p, 0, sizeof(*p));
    p->registry = registry;
    p->diagnostic = report;
    p->argument = argument;
}

enum { KEY_ENABLED = 1, KEY_OFFSET = 2, KEY_MAGIC = 4, KEY_MASK = 8, KEY_INTERPRETER = 16 };

void binfmt_config_end(BINFMT_PARSER *p)
{
    BINFMT_RULE *r = p->current;
    if (!r) return;
    if ((r->seen & (KEY_MAGIC | KEY_INTERPRETER)) != (KEY_MAGIC | KEY_INTERPRETER))
        invalid(p, p->section_line, "handler requires magic and interpreter");
    if ((r->seen & KEY_MASK) && r->mask_length != r->length)
        invalid(p, p->section_line, "mask length differs from magic");
    if (r->offset > BINFMT_HEADER || r->length > BINFMT_HEADER - r->offset)
        invalid(p, p->section_line, "match extends beyond header limit");
    if (!(r->seen & KEY_MASK)) memset(r->mask, 0xff, sizeof(r->mask));
    p->current = NULL;
}

static int hex(int c)
{
    c = lower(c);
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

static BOOL bytes(const char *s, UBYTE *out, UWORD *count)
{
    ULONG n = strlen(s), i;
    if (!n || (n & 1) || n > BINFMT_MAGIC * 2) return FALSE;
    for (i = 0; i < n; i += 2) {
        int a = hex((UBYTE)s[i]), b = hex((UBYTE)s[i + 1]);
        if (a < 0 || b < 0) return FALSE;
        out[i / 2] = (a << 4) | b;
    }
    *count = n / 2;
    return TRUE;
}

static BOOL number(const char *s, ULONG *out)
{
    ULONG value = 0, base = 10;
    if (s[0] == '0' && lower((UBYTE)s[1]) == 'x') { base = 16; s += 2; }
    if (!*s) return FALSE;
    while (*s) {
        int digit = hex((UBYTE)*s++);
        if (digit < 0 || (ULONG)digit >= base || value > (0xffffffffUL - digit) / base) return FALSE;
        value = value * base + digit;
    }
    *out = value;
    return TRUE;
}

static void section(BINFMT_PARSER *p, char *s, ULONG line)
{
    char *name, *end;
    ULONG i, n;
    BINFMT_RULE *r;
    binfmt_config_end(p);
    p->section = -1;       /* a bad header never reuses the preceding section */
    p->key_count = 0;
    end = strchr(s, ']');
    if (!end || *trim(end + 1)) { diagnostic(p, line, "malformed section header"); return; }
    *end = 0;
    name = trim(s + 1);
    n = strlen(name);
    if (!n) { diagnostic(p, line, "empty section name"); return; }
    if (n < 7 || lower(name[0]) != 'b' || lower(name[1]) != 'i' || lower(name[2]) != 'n' ||
        lower(name[3]) != 'f' || lower(name[4]) != 'm' || lower(name[5]) != 't' || name[6] != '.') {
        p->section = 1;   /* reserved, unknown namespace */
        return;
    }
    name += 7;
    n -= 7;
    if (!n || n >= BINFMT_NAME) { diagnostic(p, line, "invalid handler name"); return; }
    for (i = 0; i < n; i++) {
        int c = lower((UBYTE)name[i]);
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) {
            diagnostic(p, line, "invalid handler name"); return;
        }
    }
    for (i = 0; i < p->registry->count; i++)
        if (equal(name, p->registry->rules[i].name)) {
            diagnostic(p, line, "duplicate handler name"); return;
        }
    if (p->registry->count == BINFMT_RULES) { diagnostic(p, line, "too many handlers"); return; }
    r = &p->registry->rules[p->registry->count++];
    strcpy(r->name, name);
    r->enabled = r->valid = TRUE;
    p->current = r;
    p->section = 2;
    p->section_line = line;
}

void binfmt_config_line(BINFMT_PARSER *p, char *line, ULONG length, BOOL overlong, ULONG lineno)
{
    char *s, *value, *key;
    BINFMT_RULE *r;
    int i;
    if (length && line[length - 1] == '\r') line[--length] = 0;
    overlong = overlong || length > BINFMT_LINE;
    s = line;
    while (space(*s)) s++;
    if (overlong || has_nul((const UBYTE *)line, length)) {
        if (*s == '[') { binfmt_config_end(p); p->section = -1; }
        invalid(p, lineno, overlong ? "line too long" : "NUL byte in configuration");
        return;
    }
    s = trim(s);
    if (!*s || *s == ';' || *s == '#') return;
    if (*s == '[') { section(p, s, lineno); return; }
    if (p->section == 1) return;
    if (p->section != 2 || !p->current) { diagnostic(p, lineno, "setting outside a section"); return; }
    r = p->current;
    value = strchr(s, '=');
    if (!value) { invalid(p, lineno, "expected key = value"); return; }
    *value++ = 0;
    key = trim(s); value = trim(value);
    if (!*key || strlen(key) >= BINFMT_KEY || p->key_count == BINFMT_KEYS) {
        invalid(p, lineno, "invalid or excessive handler keys"); return;
    }
    for (i = 0; i < p->key_count; i++)
        if (equal(key, p->keys[i])) { invalid(p, lineno, "duplicate handler key"); return; }
    strcpy(p->keys[p->key_count++], key);
    if (equal(key, "enabled")) {
        r->seen |= KEY_ENABLED;
        if (!equal(value, "true") && !equal(value, "false")) invalid(p, lineno, "enabled must be true or false");
        else r->enabled = equal(value, "true");
    } else if (equal(key, "offset")) {
        r->seen |= KEY_OFFSET;
        if (!number(value, &r->offset)) invalid(p, lineno, "invalid or overflowing offset");
    } else if (equal(key, "magic")) {
        r->seen |= KEY_MAGIC;
        if (!bytes(value, r->magic, &r->length)) invalid(p, lineno, "invalid magic bytes");
    } else if (equal(key, "mask")) {
        r->seen |= KEY_MASK;
        if (!bytes(value, r->mask, &r->mask_length)) invalid(p, lineno, "invalid mask bytes");
    } else if (equal(key, "interpreter")) {
        r->seen |= KEY_INTERPRETER;
        if (binfmt_absolute(p->registry->bootdrive, value, r->interpreter)) invalid(p, lineno, "interpreter must be a bounded absolute path");
    } else diagnostic(p, lineno, "unknown handler key");
}

static LONG shebang(const BINFMT_REGISTRY *r, const UBYTE *data, ULONG length, BINFMT_SELECTION *out)
{
    ULONG n = 0, start, end;
    char path[BINFMT_SHEBANG + 1];
    while (n < length && data[n] != '\n') n++;
    if (n > BINFMT_SHEBANG && !(n == BINFMT_SHEBANG + 1 && data[n - 1] == '\r')) return EPLFMT;
    if (n && data[n - 1] == '\r') n--;
    if (has_nul(data, n)) return EPLFMT;
    start = 2;
    while (start < n && space(data[start])) start++;
    end = start;
    while (end < n && !space(data[end])) end++;
    if (end == start) return EPLFMT;
    memcpy(path, data + start, end - start); path[end - start] = 0;
    if (binfmt_absolute(r->bootdrive, path, out->interpreter)) return EPLFMT;
    while (end < n && space(data[end])) end++;
    while (n > end && space(data[n - 1])) n--;
    out->has_argument = n > end;
    memcpy(out->argument, data + end, n - end); out->argument[n - end] = 0;
    return E_OK;
}

LONG binfmt_select(const BINFMT_REGISTRY *r, const UBYTE *data, ULONG length, BINFMT_SELECTION *out)
{
    UWORD i, j;
    memset(out, 0, sizeof(*out));
    if (length >= 2 && data[0] == '#' && data[1] == '!') return shebang(r, data, length, out);
    for (i = 0; i < r->count; i++) {
        const BINFMT_RULE *rule = &r->rules[i];
        if (!rule->valid || !rule->enabled || rule->offset > length || rule->length > length - rule->offset) continue;
        for (j = 0; j < rule->length; j++)
            if ((data[rule->offset + j] & rule->mask[j]) != (rule->magic[j] & rule->mask[j])) break;
        if (j != rule->length) continue;
        strcpy(out->interpreter, rule->interpreter);
        return E_OK;
    }
    return 1;                       /* no interpreter selected */
}
