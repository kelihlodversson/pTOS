/* Ordinary native interpreter fixture: no emulator or binfmt-private ABI. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mint/osbind.h>
#include <mint/basepage.h>

long _stksize = 16384;

static int failure(int line)
{
    fprintf(stderr, "binfmt interpreter: FAIL at %d\n", line);
    return 1;
}
#define CHECK(c) do { if (!(c)) return failure(__LINE__); } while (0)

int main(int argc, char **argv)
{
    const char *kind = getenv("CASE"), *boot = getenv("BOOT"), *fd = getenv("FD");
    char native[64], cwd[128], *relative;
    int start, i;
    CHECK(kind && boot && fd && getenv("DEMO") && !strcmp(getenv("DEMO"), "present"));
    sprintf(native, "%c:\\BIN\\INTERP.TOS", *boot);
    CHECK(Dgetdrv() == 0 && Dgetpath(cwd, *boot - 'A' + 1) == 0);
    relative = cwd;
    while (*relative == '\\') relative++;
    CHECK(!strcmp(relative, "SUB"));
    if (!strcmp(kind, "NATIVE")) {
        CHECK(argc == 4 && !strcmp(argv[0], "original-name"));
        start = 1;
    } else {
        CHECK(!strcmp(argv[0], native));
        if (!strcmp(kind, "SHEBANG")) {
            CHECK(argc == 6 && !strcmp(argv[1], "-s  --literal 'quotes'"));
            start = 3;
        } else if (!strcmp(kind, "CHAIN")) {
            CHECK(argc == 7 && strstr(argv[1], "FOREIGN.TOS") &&
                  !strcmp(argv[2], "-s  --literal 'quotes'") && strstr(argv[3], "CHAIN.TOS"));
            start = 4;
        } else if (!strcmp(kind, "DEPTH4")) {
            CHECK(argc == 8);
            for (i = 1; i <= 4; i++) {
                char name[16];
                sprintf(name, "D%d.TOS", 4 - i);
                CHECK(strstr(argv[i], name));
            }
            start = 5;
        } else if (!strcmp(kind, "CLASSIC")) {
            CHECK(argc == 6 && !strcmp(argv[2], "one") && !strcmp(argv[3], "two words") &&
                  !*argv[4] && !strcmp(argv[5], "it's"));
            start = 0;
        } else if (!strcmp(kind, "LONG")) {
            CHECK(argc == 3 && strlen(argv[2]) == 1000 && (unsigned char)_base->p_cmdlin[0] == 127);
            for (i = 0; i < 1000; i++) CHECK(argv[2][i] == 'a');
            start = 0;
        } else {
            CHECK((!strcmp(kind, "FORMAT") || !strcmp(kind, "ELF") || !strcmp(kind, "PRG")) && argc == 5);
            start = 2;
        }
        i = !strcmp(kind, "SHEBANG") ? 2 : 1;
        CHECK(argv[i][0] == *boot && argv[i][1] == ':');
    }
    if (start) CHECK(!strcmp(argv[start], "two words") && !*argv[start + 1] && !strcmp(argv[start + 2], "quote'arg"));
    CHECK(Fwrite((short)atoi(fd), 5, "child") == 5);
    return 37;
}
