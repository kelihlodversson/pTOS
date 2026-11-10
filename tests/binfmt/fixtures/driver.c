/* Native boot driver, testing public Pexec and inherited launch state. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mint/osbind.h>
#include <mint/basepage.h>

long _stksize = 16384;
static char environment[32768], large[1001];
static int boot;
static long shared;

static char *append(char *at, const char *s)
{
    size_t n = strlen(s) + 1;
    memcpy(at, s, n);
    return at + n;
}

static void make_environment(const char *kind, int classic)
{
    char value[80], *at = environment;
    at = append(at, "DEMO=present");
    sprintf(value, "CASE=%s", kind); at = append(at, value);
    sprintf(value, "BOOT=%c", 'A' + boot); at = append(at, value);
    sprintf(value, "FD=%ld", shared); at = append(at, value);
    if (!classic) {
        at = append(at, !strcmp(kind, "LONG") ? "ARGV=" : "ARGV=NULL:2");
        at = append(at, "original-name");
        if (!strcmp(kind, "LONG")) at = append(at, large);
        else {
            at = append(at, "two words"); at = append(at, " "); at = append(at, "quote'arg");
        }
    }
    *at++ = 0; *at = 0;
}

static int run(const char *name, const char *kind, int mode, int classic, long expected, int writes)
{
    char path[80], tail[128] = {127, 0};
    long result, before = Fseek(0, (short)shared, 2);
    BASEPAGE *bp;
    make_environment(kind, classic);
    sprintf(path, "%c:\\%s", 'A' + boot, name);
    if (classic) {
        const char *text = classic == 1 ? "one 'two words' '' 'it''s'" : "one \"two words\" \"\" \"it's\"";
        tail[0] = strlen(text); strcpy(tail + 1, text);
    }
    result = Pexec(mode == 6 ? 3 : 0, path, tail, environment);
    if (mode == 6 && result > 0) {
        bp = (BASEPAGE *)result;
        result = Pexec(6, "", bp, 0);
    }
    if (result != expected || Fseek(0, (short)shared, 2) != before + (writes ? 5 : 0)) {
        printf("binfmt boot: FAIL %s mode %d returned %ld, expected %ld\n", name, mode, result, expected);
        return 1;
    }
    return 0;
}

int main(void)
{
    char path[80];
    long handle;
    int i, mode;
    puts("binfmt boot: BEGIN");
    boot = Dgetdrv();
    if (boot < 0 || boot >= 26) goto failed;
    memset(large, 'a', 1000); large[1000] = 0;
    /* Configuration was read before this executable: poison its on-disk
     * copy and use another working directory/drive for every launch. */
    sprintf(path, "%c:\\PTOS.INI", 'A' + boot);
    handle = Fcreate(path, 0);
    if (handle < 0 || Fwrite((short)handle, 7, "invalid") != 7 || Fclose((short)handle)) goto failed;
    sprintf(path, "%c:\\SUB", 'A' + boot);
    if (Dsetpath(path)) goto failed;
    sprintf(path, "%c:\\SUB\\SHARED.TMP", 'A' + boot);
    shared = Fcreate(path, 0);
    if (shared < 0) goto failed;
    (void)Dsetdrv(0);
    for (mode = 0; mode <= 6; mode += 6) {
        if (run("SUB\\DEMO.FMT", "FORMAT", mode, 0, 37, 1) ||
            run("SUB\\SIMPLE.TOS", "SHEBANG", mode, 0, 37, 1) ||
            run("SUB\\CHAIN.TOS", "CHAIN", mode, 0, 37, 1) ||
            run("SUB\\D0.TOS", "DEPTH4", mode, 0, 37, 1) ||
            run("SUB\\FOREIGN.ELF", "ELF", mode, 0, 37, 1) ||
            run("SUB\\DEMO.FMT", "CLASSIC", mode, 1, 37, 1) ||
            run("SUB\\DEMO.FMT", "CLASSIC", mode, 2, 37, 1) ||
            run("SUB\\DEMO.FMT", "LONG", mode, 0, 37, 1) ||
            run("BIN\\INTERP.TOS", "NATIVE", mode, 0, 37, 1)) goto failed;
#if defined(__m68k__)
        if (run("SUB\\RAW.TOS", "PRG", mode, 0, 37, 0)) goto failed;
#else
        if (run("SUB\\RAW.TOS", "PRG", mode, 0, 37, 1)) goto failed;
#endif
    }
    for (i = 0; i < 12; i++) {
        if (run("SUB\\MISSING.TOS", "ERROR", 0, 0, -33, 0) ||
            run("SUB\\BAD.TOS", "ERROR", 0, 0, -66, 0) ||
            run("SUB\\RELATIVE.TOS", "ERROR", 0, 0, -66, 0) ||
            run("SUB\\LONG.TOS", "ERROR", 0, 0, -66, 0) ||
            run("SUB\\LOOPA.TOS", "ERROR", 0, 0, -66, 0) ||
            run("SUB\\L0.TOS", "ERROR", 0, 0, -66, 0) ||
            run("SUB\\UNKNOWN.TOS", "ERROR", 0, 0, -66, 0) ||
            run("SUB\\NATBAD.ELF", "ERROR", 0, 0, -66, 0) ||
            run("SUB\\NOEXIST.TOS", "ERROR", 0, 0, -33, 0) ||
            run("SUB\\SHORT.TOS", "ERROR", 0, 0, -66, 0)) goto failed;
    }
    if (Fclose((short)shared)) goto failed;
    puts("binfmt boot: PASS");
    return 0;
failed:
    puts("binfmt boot: FAIL");
    return 1;
}
