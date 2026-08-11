/*
 * bootargs.c - country/keyboard override parsed from the ARM boot
 * command line
 *
 * Copyright (C) 2026 The EmuTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

/*
 * Machines built with CONF_MULTILANG pick their country/keyboard/font at
 * run time (see bios/country.c).  Atari hardware reads the choice from
 * NVRAM (CONF_WITH_NVRAM); the ARM machines have none, so this file reads
 * it from the boot loader's command line instead, via whichever of the
 * two boot-info formats the 32-bit ARM Linux boot protocol allows:
 *
 * - a classic ATAG list (a linear list of {size,tag,data...} records
 *   ending in ATAG_NONE), found e.g. on the Raspberry Pi under QEMU when
 *   booted with "-kernel kernel.img -append '...'" (verified empirically:
 *   QEMU's raspi1ap/raspi2b machines build an ATAG_CORE/ATAG_MEM/
 *   ATAG_CMDLINE list for a raw kernel image; loading the same kernel as
 *   an ELF, or booting with -bios as documented for this OS, gets no boot
 *   info passed at all -- r0-r2 come back zero, see arm_boot.h);
 *
 * - a flattened device tree (FDT), with the command line in its
 *   /chosen/bootargs property.  This is the only format QEMU's ARM
 *   "virt" machine (-M virt) uses -- it has no ATAG support -- and is
 *   also what real Raspberry Pi firmware passes by default nowadays.
 *
 * Real hardware always uses one or the other, never both, and hands
 * arm_boot_regs.atags pointing at whichever it is; the magic number at
 * that address says which.  Both are read-only, walked once at boot,
 * with no assumption that either format was actually used -- a null or
 * unrecognized pointer just means no override.
 */

#include "config.h"

#if CONF_WITH_BOOTARGS_LANG

#include "portab.h"
#include "arm_boot.h"
#include "ctrycodes.h"
#include "string.h"
#include "bootargs.h"

/* ---- ATAG list (Linux Documentation/arch/arm/booting.rst) ---- */

#define ATAG_NONE       0x00000000UL
#define ATAG_CORE       0x54410001UL
#define ATAG_CMDLINE    0x54410009UL
#define ATAG_MAX_TAGS   256     /* safety net against a corrupt/bogus list */

struct atag_header {
    ULONG size;     /* size of this tag in 4-byte words, header included */
    ULONG tag;
};

/* Walk a classic ATAG list looking for ATAG_CMDLINE.  Returns the NUL
 * terminated command line string, or NULL if there is none. */
static const char *atag_find_cmdline(const struct atag_header *tag)
{
    int i;

    for (i = 0; i < ATAG_MAX_TAGS; i++)
    {
        if (tag->size == 0)
            break;
        if (tag->tag == ATAG_CMDLINE)
            return (const char *)(tag + 1);
        tag = (const struct atag_header *)((const ULONG *)tag + tag->size);
    }
    return NULL;
}

/* ---- Flattened device tree (devicetree.org, "Devicetree Specification") ---- */

#define FDT_MAGIC       0xd00dfeedUL
#define FDT_BEGIN_NODE  0x00000001UL
#define FDT_END_NODE    0x00000002UL
#define FDT_PROP        0x00000003UL
#define FDT_NOP         0x00000004UL
#define FDT_END         0x00000009UL

struct fdt_header {
    ULONG magic;
    ULONG totalsize;
    ULONG off_dt_struct;
    ULONG off_dt_strings;
    ULONG off_mem_rsvmap;
    ULONG version;
    ULONG last_comp_version;
    ULONG boot_cpuid_phys;
    ULONG size_dt_strings;
    ULONG size_dt_struct;
};

/* Every field of an FDT is big-endian, regardless of the CPU. */
static ULONG fdt32_to_cpu(ULONG v)
{
    return ((v & 0x000000ffUL) << 24) | ((v & 0x0000ff00UL) << 8)
         | ((v & 0x00ff0000UL) >> 8)  | ((v & 0xff000000UL) >> 24);
}

/* Walk a flattened device tree's structure block looking for a
 * "bootargs" property (expected under /chosen, but this looks for it
 * anywhere, which is simpler and harmless).  Returns the NUL terminated
 * command line string, or NULL if there is none. */
static const char *fdt_find_bootargs(const struct fdt_header *fdt)
{
    const UBYTE *cur, *end, *strings_base;

    if (fdt32_to_cpu(fdt->magic) != FDT_MAGIC)
        return NULL;

    cur = (const UBYTE *)fdt + fdt32_to_cpu(fdt->off_dt_struct);
    end = (const UBYTE *)fdt + fdt32_to_cpu(fdt->totalsize);
    strings_base = (const UBYTE *)fdt + fdt32_to_cpu(fdt->off_dt_strings);

    while (cur + 4 <= end)
    {
        ULONG token = fdt32_to_cpu(*(const ULONG *)cur);
        cur += 4;

        switch (token)
        {
        case FDT_BEGIN_NODE:
            /* NUL terminated unit name, then pad to a 4 byte boundary */
            while (cur < end && *cur)
                cur++;
            cur++;
            cur = (const UBYTE *)(((ULONG)cur + 3) & ~3UL);
            break;

        case FDT_END_NODE:
        case FDT_NOP:
            break;

        case FDT_PROP:
        {
            ULONG len, nameoff;
            const char *propname;

            if (cur + 8 > end)
                return NULL;
            len = fdt32_to_cpu(*(const ULONG *)cur);
            cur += 4;
            nameoff = fdt32_to_cpu(*(const ULONG *)cur);
            cur += 4;
            propname = (const char *)(strings_base + nameoff);

            if (strcmp(propname, "bootargs") == 0)
                return (const char *)cur;

            cur += len;
            cur = (const UBYTE *)(((ULONG)cur + 3) & ~3UL);
            break;
        }

        case FDT_END:
            return NULL;

        default:
            /* malformed structure block: bail out rather than loop
             * forever on a token we don't recognize */
            return NULL;
        }
    }
    return NULL;
}

/* ---- command line parsing ---- */

/* Two letter country codes recognized on the command line.  Must match
 * COUNTRIES in country.mk: every multi-language image supports exactly
 * this set of countries at run time (see bios/ctables.h). */
static const struct {
    const char *name;
    int country;
} country_names[] = {
    { "us", COUNTRY_US },
    { "de", COUNTRY_DE },
    { "fr", COUNTRY_FR },
    { "cz", COUNTRY_CZ },
    { "gr", COUNTRY_GR },
    { "es", COUNTRY_ES },
    { "fi", COUNTRY_FI },
    { "sg", COUNTRY_SG },
    { "ru", COUNTRY_RU },
    { "it", COUNTRY_IT },
    { "uk", COUNTRY_UK },
    { "no", COUNTRY_NO },
    { "se", COUNTRY_SE },
};

#define LANG_PARAM      "ptos.lang="
#define LANG_PARAM_LEN  (sizeof(LANG_PARAM) - 1)

/* Find "ptos.lang=xx" in a kernel-style, space separated command line
 * and return the matching COUNTRY_xx code, or -1 if the parameter is
 * absent or does not name a recognized country. */
static int parse_lang_param(const char *cmdline)
{
    const char *p = cmdline;

    if (!p)
        return -1;

    while (*p)
    {
        if (strncmp(p, LANG_PARAM, LANG_PARAM_LEN) == 0)
        {
            const char *value = p + LANG_PARAM_LEN;
            size_t i;

            for (i = 0; i < ARRAY_SIZE(country_names); i++)
            {
                const char *name = country_names[i].name;
                size_t namelen = strlen(name);

                if (strncasecmp(value, name, namelen) == 0
                    && (value[namelen] == '\0' || value[namelen] == ' '))
                {
                    return country_names[i].country;
                }
            }
            return -1;
        }

        /* skip to the next whitespace separated token */
        while (*p && *p != ' ')
            p++;
        while (*p == ' ')
            p++;
    }
    return -1;
}

int bootargs_get_country(void)
{
    ULONG ptr = arm_boot_regs.atags;
    const ULONG *fdt_magic;
    const struct atag_header *first_tag;
    const char *cmdline;

    if (!ptr)
        return -1;

    /* An FDT starts with a magic word; an ATAG list starts with the
     * ATAG_CORE record's {size,tag} header, tag second -- check that,
     * not the size word first. */
    fdt_magic = (const ULONG *)ptr;
    first_tag = (const struct atag_header *)ptr;
    if (fdt32_to_cpu(*fdt_magic) == FDT_MAGIC)
        cmdline = fdt_find_bootargs((const struct fdt_header *)ptr);
    else if (first_tag->tag == ATAG_CORE)
        cmdline = atag_find_cmdline(first_tag);
    else
        cmdline = NULL;

    return parse_lang_param(cmdline);
}

#endif /* CONF_WITH_BOOTARGS_LANG */
