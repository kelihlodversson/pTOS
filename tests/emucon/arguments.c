/*
 * Verify the vectors inherited through the standalone EmuCON command editor.
 * Copyright (C) 2026 The pTOS development team.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <mint/osbind.h>
#include <stdlib.h>
#include <string.h>

#define PASS "native EmuCON argv/envp/CWD: PASS\r\n"
#define FAIL "native EmuCON argv/envp/CWD: FAIL\r\n"

int main(int argc, char **argv)
{
    static const char digits[] = "0123456789";
    char cwd[256];
    const char *lang = getenv("LANG");
    unsigned int i;

    if (argc != 5 || argv[argc] || strcmp(argv[0],"cliargs") ||
        strcmp(argv[1],"two words") || argv[2][0] || strcmp(argv[3],"a>b") ||
        strlen(argv[4]) != 130 || !lang || !*lang ||
        Dgetdrv() != 2 || Dgetpath(cwd,0) < 0 || strcmp(cwd,"\\"))
        goto fail;
    for (i = 0; i < 130; i++)
        if (argv[4][i] != digits[i % 10])
            goto fail;
    if (Fwrite(1,sizeof(PASS)-1,PASS) != sizeof(PASS)-1)
        goto fail;
    return 0;

fail:
    (void)Fwrite(2,sizeof(FAIL)-1,FAIL);
    return 1;
}
