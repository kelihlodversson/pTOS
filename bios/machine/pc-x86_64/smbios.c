/*
 * smbios.c - x86-64 machine name from the firmware's SMBIOS tables
 *
 * Copyright (C) 2025-2026 The pTOS development team.
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "config.h"
#ifndef MACHINE_PC_X86_64
#error This file must only be compiled for the pc-x86_64 machine
#endif

#include "portab.h"
#include "efi.h"
#include "string.h"
#include "smbios.h"

/* SMBIOS_TABLE_GUID / SMBIOS3_TABLE_GUID (UEFI spec 4.6) */
#define SMBIOS_TABLE_GUID \
    { 0xEB9D2D31, 0x2D88, 0x11d3, { 0x9A, 0x16, 0x00, 0x90, 0x27, 0x3F, 0xC1, 0x4D } }
#define SMBIOS3_TABLE_GUID \
    { 0xF2FD1544, 0x9794, 0x4a2c, { 0x99, 0x2E, 0xE5, 0xBB, 0xCF, 0x20, 0xE3, 0x94 } }

/* SMBIOS structure types and the type 1 string indexes used (DSP0134) */
#define SMBIOS_TYPE_SYSTEM      1
#define SMBIOS_TYPE_END         127
#define SMBIOS_SYS_MANUFACTURER 4
#define SMBIOS_SYS_PRODUCT      5

/* Sanity bound on a structure table, so a corrupt entry point cannot send
 * the walk through arbitrary memory. */
#define SMBIOS_TABLE_MAX        (1024 * 1024)

static char smbios_name[X86_64_SMBIOS_NAME_MAX + 1];

static BOOL guid_equal(const EFI_GUID *a, const EFI_GUID *b)
{
    int i;

    if (a->Data1 != b->Data1 || a->Data2 != b->Data2 || a->Data3 != b->Data3)
        return FALSE;
    for (i = 0; i < 8; i++)
        if (a->Data4[i] != b->Data4[i])
            return FALSE;
    return TRUE;
}

static BOOL anchor_is(const UBYTE *p, const char *anchor)
{
    while (*anchor)
        if (*p++ != (UBYTE)*anchor++)
            return FALSE;
    return TRUE;
}

/* little-endian field reads: the entry point is packed, so no alignment */
static ULONG rd16(const UBYTE *p)
{
    return p[0] | ((ULONG)p[1] << 8);
}

static ULONG rd32(const UBYTE *p)
{
    return rd16(p) | (rd16(p + 2) << 16);
}

static UQUAD rd64(const UBYTE *p)
{
    return rd32(p) | ((UQUAD)rd32(p + 4) << 32);
}

/* TRUE if the n bytes at p add up to 0 (mod 256), as the spec requires */
static BOOL checksum_ok(const UBYTE *p, ULONG n)
{
    UBYTE sum = 0;

    while (n--)
        sum += *p++;
    return sum == 0;
}

/*
 * Locate the structure table from an SMBIOS 3.0 ("_SM3_") or 2.x ("_SM_")
 * entry point.  The table address is only trusted once the entry point has
 * proved itself: its anchor, its own length and checksum, and for 2.x the
 * intermediate "_DMI_" anchor and checksum.  Returns FALSE, and leaves the
 * outputs alone, for anything else -- a damaged entry point must end in "no
 * SMBIOS name", not a walk through whatever memory its address points at.
 */
static BOOL find_table(const UBYTE *ep, BOOL is_v3, const UBYTE **table, ULONG *size)
{
    const UBYTE *t;
    ULONG sz;

    if (is_v3) {
        /* entry point structure: 24 bytes, length in byte 6 (DSP0134 5.2.2) */
        if (!anchor_is(ep, "_SM3_") || ep[6] < 0x18 || !checksum_ok(ep, ep[6]))
            return FALSE;
        sz = rd32(ep + 0x0C);
        t = (const UBYTE *)(uintptr_t)rd64(ep + 0x10);
    } else {
        /* entry point structure: 31 bytes, length in byte 5, with an
         * intermediate structure in bytes 0x10-0x1E (DSP0134 5.2.1) */
        if (!anchor_is(ep, "_SM_") || ep[5] < 0x1F || !checksum_ok(ep, ep[5]))
            return FALSE;
        if (!anchor_is(ep + 0x10, "_DMI_") || !checksum_ok(ep + 0x10, 15))
            return FALSE;
        sz = rd16(ep + 0x16);
        t = (const UBYTE *)(uintptr_t)rd32(ep + 0x18);
    }
    if (t == NULL || sz == 0 || sz > SMBIOS_TABLE_MAX ||
        (uintptr_t)t + sz < (uintptr_t)t)
        return FALSE;

    /* only on success: callers stop scanning as soon as *table is set */
    *table = t;
    *size = sz;
    return TRUE;
}

/*
 * The string 'index' (1-based; 0 means none) of the structure at 's',
 * whose formatted area is s[1] bytes long and is followed by the strings.
 * Returns NULL if there is no such string inside [s, end).
 */
static const char *get_string(const UBYTE *s, const UBYTE *end, UBYTE index)
{
    const char *p = (const char *)(s + s[1]);
    const char *limit = (const char *)end;

    if (index == 0)
        return NULL;
    while (p < limit && *p) {
        if (--index == 0)
            return p;
        while (p < limit && *p)
            p++;
        p++;
    }
    return NULL;
}

static BOOL starts_with_nocase(const char *s, const char *prefix)
{
    for (; *prefix; s++, prefix++) {
        char c = *s;
        char d = *prefix;

        if (c >= 'A' && c <= 'Z')
            c += 'a' - 'A';
        if (d >= 'A' && d <= 'Z')
            d += 'a' - 'A';
        if (c != d)
            return FALSE;
    }
    return TRUE;
}

/* Firmware fills fields it knows nothing about with strings like these. */
static BOOL is_placeholder(const char *s)
{
    static const char *const junk[] = {
        "to be filled", "default string", "system product name",
        "system manufacturer", "system version", "not specified",
        "not applicable", "unknown", "o.e.m", "none"
    };
    int i;

    for (i = 0; i < (int)(sizeof(junk) / sizeof(junk[0])); i++)
        if (starts_with_nocase(s, junk[i]))
            return TRUE;
    return FALSE;
}

/* Whether the n-character word w equals the vn-character word v, any case */
static BOOL same_word(const char *w, int n, const char *v, int vn)
{
    int i;

    if (n != vn)
        return FALSE;
    for (i = 0; i < n; i++) {
        char c = w[i];
        char d = v[i];

        if (c >= 'A' && c <= 'Z')
            c += 'a' - 'A';
        if (d >= 'A' && d <= 'Z')
            d += 'a' - 'A';
        if (c != d)
            return FALSE;
    }
    return TRUE;
}

/* Corporate suffixes that only take up room in the welcome screen column. */
static BOOL is_filler(const char *w, int n)
{
    static const char *const filler[] = {
        "inc", "inc.", "corp", "corp.", "corporation", "co.", "co.,", "co",
        "ltd", "ltd.", "ltd.,", "gmbh", "company"
    };
    int i;

    for (i = 0; i < (int)(sizeof(filler) / sizeof(filler[0])); i++)
        if (same_word(w, n, filler[i], strlen(filler[i])))
            return TRUE;
    return FALSE;
}

/*
 * Append the printable words of 's' to smbios_name, space separated,
 * until one no longer fits.  Parenthesised text such as QEMU's
 * "(i440FX + PIIX, 1996)", corporate suffixes such as "Inc." and a
 * repetition of the name's first word (a product "Dell XPS 13" after the
 * manufacturer "Dell") are dropped.  If 'first_only', stop after the first
 * word ("Micro-Star International Co., Ltd." -> "Micro-Star").
 * Returns FALSE once the name is full.
 */
static BOOL add_words(const char *s, BOOL first_only)
{
    int len = 0;
    int depth = 0;
    int first_len = 0;
    int n;

    if (is_placeholder(s))
        return TRUE;

    while (smbios_name[len])
        len++;
    while (smbios_name[first_len] && smbios_name[first_len] != ' ')
        first_len++;

    while (*s) {
        while (*s == ' ')
            s++;
        for (n = 0; s[n] && s[n] != ' '; n++)
            ;
        if (n == 0)
            break;
        if (*s == '(')
            depth++;
        if (depth == 0 && !is_filler(s, n) &&
            !(first_len && same_word(s, n, smbios_name, first_len))) {
            int i;

            if (len + (len != 0) + n > X86_64_SMBIOS_NAME_MAX)
                return FALSE;
            if (len)
                smbios_name[len++] = ' ';
            for (i = 0; i < n; i++)
                smbios_name[len++] = (s[i] >= 0x20 && s[i] < 0x7f && s[i] != '%') ? s[i] : '?';
            smbios_name[len] = '\0';
            if (first_only)
                return TRUE;
        }
        if (s[n - 1] == ')' && depth)
            depth--;
        s += n;
    }
    return TRUE;
}

static void read_system_info(const UBYTE *table, ULONG size)
{
    const UBYTE *s = table;
    const UBYTE *end = table + size;

    while (s + 4 <= end && s[0] != SMBIOS_TYPE_END) {
        const UBYTE *next;

        /* corrupt: shorter than its own header, or no room left for the
         * (at least empty, double NUL terminated) string set behind it */
        if (s[1] < 4 || (UQUAD)(end - s) < (UQUAD)s[1] + 2)
            return;

        /* the string set after the formatted area ends with a double NUL */
        next = s + s[1];
        while (next + 1 < end && (next[0] || next[1]))
            next++;
        if (next + 1 >= end)    /* ran off the table without a terminator */
            return;
        next += 2;

        if (s[0] == SMBIOS_TYPE_SYSTEM && s[1] > SMBIOS_SYS_PRODUCT) {
            const char *maker = get_string(s, next, s[SMBIOS_SYS_MANUFACTURER]);
            const char *product = get_string(s, next, s[SMBIOS_SYS_PRODUCT]);

            if (maker)
                add_words(maker, TRUE);
            if (product)
                add_words(product, FALSE);
            return;
        }
        s = next;
    }
}

void x86_64_smbios_probe(void *system_table)
{
    const EFI_SYSTEM_TABLE *st = (const EFI_SYSTEM_TABLE *)system_table;
    EFI_GUID smbios3 = SMBIOS3_TABLE_GUID;
    EFI_GUID smbios = SMBIOS_TABLE_GUID;
    const UBYTE *table = NULL;
    ULONG size = 0;
    UQUAD i;

    smbios_name[0] = '\0';

    /* prefer the 64-bit entry point where the firmware offers both */
    for (i = 0; i < st->NumberOfTableEntries && !table; i++) {
        const EFI_CONFIGURATION_TABLE *ct = &st->ConfigurationTable[i];

        if (guid_equal(&ct->VendorGuid, &smbios3))
            find_table((const UBYTE *)ct->VendorTable, TRUE, &table, &size);
    }
    for (i = 0; i < st->NumberOfTableEntries && !table; i++) {
        const EFI_CONFIGURATION_TABLE *ct = &st->ConfigurationTable[i];

        if (guid_equal(&ct->VendorGuid, &smbios))
            find_table((const UBYTE *)ct->VendorTable, FALSE, &table, &size);
    }

    if (table)
        read_system_info(table, size);
}

const char *x86_64_smbios_machine_name(void)
{
    return smbios_name[0] ? smbios_name : NULL;
}
