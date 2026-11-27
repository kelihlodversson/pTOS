/*
 * Verify the vectors inherited through the EmuCON command editor.
 * Copyright (C) 2026 The pTOS development team.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <mint/osbind.h>
#include <stdlib.h>
#include <string.h>

#define PASS "native EmuCON argv/envp/CWD: PASS\r\n"
#define FAIL "native EmuCON argv/envp/CWD: FAIL\r\n"

static int fail(const char *reason)
{
    (void)Fwrite(2,strlen(reason),reason);
    (void)Fwrite(2,sizeof(FAIL)-1,FAIL);
    return 1;
}

int main(int argc, char **argv)
{
    static const char digits[] = "0123456789";
    char cwd[256];
    const char *lang = getenv("LANG");
    const char *path = getenv("PATH");
    unsigned int i;

    if (argc != 5 || argv[argc] || strcmp(argv[0],"cliargs") ||
        strcmp(argv[1],"two wordssuffix") || argv[2][0] || strcmp(argv[3],"a>b"))
        return fail("unexpected argv\r\n");
    if (strlen(argv[4]) != 130)
        return fail("truncated argument\r\n");
    for (i = 0; i < 130; i++)
        if (argv[4][i] != digits[i % 10])
            return fail("incorrect long argument\r\n");
    if (!lang || !*lang)
        return fail("missing inherited LANG\r\n");
    if (!path || strcasecmp(path,"C:\\"))
        return fail("incorrect inherited PATH\r\n");
    if (Dgetdrv() != 2 || Dgetpath(cwd,0) < 0 ||
        strcasecmp(cwd + (cwd[0] == '\\'),"tests"))
        return fail("incorrect inherited CWD\r\n");
    if (Fseek(0,1,1) != 0 || Fwrite(1,sizeof(PASS)-1,PASS) != sizeof(PASS)-1)
        return fail("stdout is not the redirected file\r\n");
    return 0;
}
