/*
 * Role selection for test fixtures shared by native ELF and classic PRG.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef TEST_ENTRY_ARGS_H
#define TEST_ENTRY_ARGS_H

#include <mint/basepage.h>

static inline const char *test_entry_argument(int argc, char **argv)
{
#ifdef TEST_NATIVE_ELF_ENTRY
    return argc > 1 ? argv[1] : "";
#else
    (void)argc;
    (void)argv;
    return (const char *)_base->p_cmdlin + 1;
#endif
}

#endif
