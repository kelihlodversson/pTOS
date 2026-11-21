/* Native boot driver, testing public Pexec and inherited launch state. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mint/osbind.h>
#include <mint/basepage.h>
#ifdef PROCESS_DIRECTORY_TEST
#include <mint/mintbind.h>
#include "../../../include/process_exec.h"
#endif

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
        int plain = !strcmp(kind, "SHORT") || !strcmp(kind, "SHORTCHAIN");
        int quoted = !strcmp(kind, "QUOTED");
        at = append(at, plain || quoted || !strcmp(kind, "LONG") ? "ARGV=" : "ARGV=NULL:2");
        at = append(at, "original-name");
        if (!strcmp(kind, "LONG")) at = append(at, large);
        else if (plain) at = append(at, "plain");
        else if (quoted) {
            at = append(at, "it's"); at = append(at, "literal\"quote");
        }
        else {
            at = append(at, "two words"); at = append(at, " "); at = append(at, "quote'arg");
        }
    }
    *at++ = 0; *at = 0;
}

#ifdef PROCESS_DIRECTORY_TEST
static int process_directory(void)
{
    PROCESS_EXEC_REQUEST request = {0};
    char directory[16], tail[] = {0, 0};
    long handle, pid, status;
    unsigned int mode;
    sprintf(directory, "%c:\\BIN", 'A' + boot);
    if (Ssystem(-1, PROCESS_EXEC_SSYSTEM, 0L) || Dsetdrv(boot) < 0) return 1;
    make_environment("PROCESSDIR", 1);
    for (mode = 0; mode < 2; mode++) {
        memset(&request, 0, sizeof(request));
        request.operation = mode ? PROCESS_EXEC_PREPARE : PROCESS_EXEC_START;
        request.path = (ULONG)(uintptr_t)"DEMO.FMT";
        request.tail = (ULONG)(uintptr_t)tail;
        request.environment = (ULONG)(uintptr_t)environment;
        request.directory = (ULONG)(uintptr_t)directory;
        pid = Ssystem(PROCESS_EXEC_SSYSTEM, (long)&request, sizeof(request));
        if (mode && pid > 0) {
            handle = pid;
            memset(&request, 0, sizeof(request));
            request.operation = PROCESS_EXEC_COMMIT;
            request.handle = (ULONG)handle;
            pid = Ssystem(PROCESS_EXEC_SSYSTEM, (long)&request, sizeof(request));
            request.operation = PROCESS_EXEC_CLOSE;
            if (Ssystem(PROCESS_EXEC_SSYSTEM, (long)&request, sizeof(request))) return 1;
        }
        status = pid > 0 ? Pwaitpid((short)pid, 0, 0) : pid;
        if (pid <= 0 || (unsigned long)status != ((unsigned long)pid << 16 | 37)) {
            printf("binfmt boot: FAIL PROCESS %s directory returned %ld, wait %ld\n",
                   mode ? "PREPARE" : "START", pid, status);
            return 1;
        }
    }
    (void)Dsetdrv(0);
    return 0;
}
#endif

static int run(const char *name, const char *kind, int mode, int classic, long expected, int writes)
{
    char path[80], tail[128] = {127, 0};
    long result, before = Fseek(0, (short)shared, 2);
    BASEPAGE *bp;
    make_environment(kind, classic == 1 || classic == 2);
    sprintf(path, "%c:\\%s", 'A' + boot, name);
    if (classic) {
        const char *text = classic == 3 ? "fresh tail" :
            classic == 1 ? "one 'two words' '' 'it''s'" : "one \"two words\" \"\" \"it's\"";
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

#if defined(__m68k__) || !defined(STAGED_UNCHECKED)
/* #1018: mode 3 names an executable and stages a 68k PRG only; everything
 * that would stage an ELF (a foreign ELF, an interpreter) is EPLFMT before
 * anything is allocated, and MMU ARM/x32 have no mode 3 at all. */
static int staged(const char *name, long expected)
{
    char path[80];
    long result;
    sprintf(path, "%c:\\%s", 'A' + boot, name);
    result = Pexec(3, path, "", 0);
    if (expected > 0 ? result <= 0 : result != expected) {
        printf("binfmt boot: FAIL %s mode 3 returned %ld, expected %ld\n", name, result, expected);
        return 1;
    }
    if (result > 0) {               /* a staged PRG: discard it unlaunched */
        BASEPAGE *bp = (BASEPAGE *)result;
        void *env = bp->p_env;
        if (Mfree(bp) || Mfree(env)) return 1;
    }
    return 0;
}

#endif

#if defined(__m68k__)
/* What a script stages is its interpreter: allowed only if that is a PRG. */
static long interpreted_expectation(void)
{
    char path[80];
    unsigned char magic[2] = {0, 0};
    long handle;
    sprintf(path, "%c:\\BIN\\INTERP.TOS", 'A' + boot);
    handle = Fopen(path, 0);
    if (handle < 0) return 0;
    if (Fread((short)handle, 2, magic) != 2) magic[0] = 0;
    (void)Fclose((short)handle);
    return magic[0] == 0x60 && magic[1] == 0x1a ? 1 : -66;
}
#endif

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
#if defined(STAGED_UNCHECKED)
    /* non-MMU ARM (to be deprecated) keeps the staged modes unchecked */
    for (mode = 0; mode <= 6; mode += 6)
#else
    mode = 0;
#endif
    {
        if (run("SUB\\DEMO.FMT", "FORMAT", mode, 0, 37, 1) ||
            run("SUB\\SIMPLE.TOS", "SHEBANG", mode, 0, 37, 1) ||
            run("SUB\\CHAIN.TOS", "CHAIN", mode, 0, 37, 1) ||
            run("SUB\\D0.TOS", "DEPTH4", mode, 0, 37, 1) ||
            run("SUB\\FOREIGN.ELF", "ELF", mode, 0, 37, 1) ||
            run("SUB\\DEMO.FMT", "CLASSIC", mode, 1, 37, 1) ||
            run("SUB\\DEMO.FMT", "CLASSIC", mode, 2, 37, 1) ||
            run("SUB\\DEMO.FMT", "STALE", mode, 3, 37, 1) ||
            run("SUB\\DEMO.FMT", "QUOTED", mode, 0, 37, 1) ||
            run("SUB\\DEMO.FMT", "SHORT", mode, 0, 37, 1) ||
            run("SUB\\D0.TOS", "SHORTCHAIN", mode, 0, 37, 1) ||
            run("SUB\\DEMO.FMT", "LONG", mode, 0, 37, 1) ||
            run("BIN\\INTERP.TOS", "NATIVE", mode, 0, 37, 1)) goto failed;
#if defined(__m68k__)
        if (run("SUB\\RAW.TOS", "PRG", mode, 0, 37, 0)) goto failed;
#else
        if (run("SUB\\RAW.TOS", "PRG", mode, 0, 37, 1)) goto failed;
#endif
    }
#if defined(__m68k__)
    /* the classic staged launch of a real PRG, then the refused ELF stagings */
    if (run("SUB\\RAW.TOS", "PRG", 6, 0, 37, 0) || !interpreted_expectation() ||
        staged("SUB\\DEMO.FMT", interpreted_expectation()) ||
        staged("SUB\\SIMPLE.TOS", interpreted_expectation()) ||
        staged("BIN\\INTERP.TOS", interpreted_expectation())) goto failed;
#elif !defined(STAGED_UNCHECKED)
    if (staged("SUB\\RAW.TOS", -32) || staged("SUB\\DEMO.FMT", -32)) goto failed;
#endif
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
#ifdef PROCESS_DIRECTORY_TEST
    if (process_directory()) goto failed;
    puts("binfmt PROCESS directory: PASS");
#endif
    if (Fclose((short)shared)) goto failed;
    puts("binfmt boot: PASS");
    return 0;
failed:
    puts("binfmt boot: FAIL");
    return 1;
}
