/*
 * cmdnls.c - the language of the standalone EmuCON
 *
 * The standalone EmuCON of a multi-language build (cli/Makefile, NLS=1)
 * carries the translations of the messages of cli/ for every language of
 * po/LINGUAS, in the tables that "bug make -S" writes (ml/cli-langs.c), and
 * picks one when it starts: the one named by the LANG variable of its
 * environment, which the kernel sets to the language of the system.  A
 * message in a language it does not have is English.
 *
 * This is util/nls.c, for a program that has no emutos.h.
 *
 * Copyright (C) 2001-2019 The EmuTOS Development Team
 * Copyright (C) 2026 The pTOS development team
 *
 * This file is distributed under the GPL, version 2 or at your
 * option any later version.  See doc/license.txt for details.
 */

#include "cmd.h"

#ifdef CLI_NLS

#include "langs.h"

extern char const nls_key_strings[];
extern nls_key_offset const msg_en_offsets[];

static const nls_key_offset *nls_offsets = msg_en_offsets;
static const char *nls_translations = nls_key_strings;

/*
 * LANG is "ll", optionally followed by "_CC" and ".charset": the language of
 * the kernel (doc/nls.txt).  pTOS names Greek "gr", ISO 639 "el"; English has
 * no translation table.
 */
void nls_set_lang_env(const char *value)
{
    char name[3];
    const struct lang_info *lang;

    name[0] = value[0];
    name[1] = value[0] ? value[1] : '\0';
    name[2] = '\0';
    if (!strcmp(name, "el"))
        strcpy(name, "gr");

    for (lang = langs; lang->name[0]; lang++)
    {
        if (!strcmp(name, lang->name))
        {
            nls_offsets = lang->offsets;
            nls_translations = lang->translations;
            return;
        }
    }
}

const char *etos_gettext(unsigned long msgid)
{
    nls_key_offset offset;

    if (msgid == 0)
        return nls_key_strings;

    /* a string that is not marked with N_() is a pointer */
    if ((msgid & NLS_MSGID_FLAG) != NLS_MSGID_FLAG)
        return (const char *)msgid;
    msgid &= ~NLS_MSGID_FLAG;

    offset = nls_offsets[msgid - 1];
    if (offset == 0)    /* no translation: the original string */
        return nls_key_strings + msg_en_offsets[msgid - 1];
    return nls_translations + offset;
}

#endif /* CLI_NLS */
