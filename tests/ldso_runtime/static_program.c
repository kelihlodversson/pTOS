/* Probe the standard static ELF entry on the actual target. */
#include "runtime.h"
#include "../../include/pexecve.h"
#include "../../include/gemerror.h"

static volatile uint32_t bss;

static int equal(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

int app_main(uint32_t argc, const char **argv, const char **envp, uintptr_t finalizer);

static int32_t pexecve(unsigned int mode, const char *path,
                       const char *const *argv, const char *const *envp)
{
    return dl_syscall(GEMDOS_PEXECVE, mode, (uintptr_t)path, (uintptr_t)argv,
                      (uintptr_t)envp, 0, 0);
}

static int32_t process_id(void)
{
    return dl_syscall(0x10b, 0, 0, 0, 0, 0, 0);
}

static int32_t wait_child(int32_t pid, unsigned int flags)
{
    return dl_syscall(0x13a, pid, flags, 0, 0, 0, 0);
}

#if defined(__m68k__)
static int32_t pexec(unsigned int mode, const char *path,
                     const char *tail, const char *environment)
{
    return dl_syscall(0x4b, mode, (uintptr_t)path, (uintptr_t)tail,
                      (uintptr_t)environment, 0, 0);
}

#if PTOS_TEST_EXEC_REPLACE
static int legacy_replace(const char *path)
{
    char environment[] = "KEY=two words\0FLAG=\0ARGV=NULL:1\0legacy name\0"
                         "placeholder\0two words\0literal\"quote\0" "000000\0";
    char *digits = environment + sizeof(environment) - sizeof("000000\0");
    uint32_t pid = process_id();
    unsigned int i;

    for (i = 6; i > 0; i--) {
        digits[i - 1] = '0' + pid % 10;
        pid /= 10;
    }
    (void)pexec(200, path, "\177", environment);
    return 96;
}
#endif

static int test_legacy_launches(const char *path)
{
    char tail[] = "\0\"\" \"two words\" 'literal\"quote'";
    const char environment[] = "KEY=two words\0FLAG=\0ARGV=\0unused\0";
    const char extended[] = "KEY=two words\0FLAG=\0ARGV=NULL:1\0legacy name\0"
                            "placeholder\0two words\0literal\"quote\0";
    int32_t pid;

    tail[0] = sizeof(tail) - 2;
    if (pexec(0, path, tail, environment) != 79 ||
        pexec(0, path, "\177", "FLAG=\0") != EPLFMT)
        return 94;
    pid = pexec(100, path, "\177", extended);
    if (pid <= 0 || (uint32_t)wait_child(pid, 0) != ((uint32_t)pid << 16 | 79U) ||
        wait_child(pid, 1) != EFILNF)
        return 95;
#if PTOS_TEST_EXEC_REPLACE
    {
        const char *const argv[] = {"check-legacy-exec", path, 0};
        const char *const envp[] = {0};

        if (pexecve(0, path, argv, envp) != 78)
            return 96;
    }
#endif
    return 0;
}
#endif

static int test_syscalls(const char *path)
{
    char arg[] = "two words", variable[] = "KEY=two words";
    const char *argv[] = {"custom name", "", arg, "literal\"quote", 0, 0};
    const char *envp[] = {variable, "", "FLAG=", 0};
    int32_t pid, status, self = process_id();

    if (self <= 0 || pexecve(3, 0, 0, 0) != EINVFN ||
        pexecve(0, path, 0, envp) != EIMBA || pexecve(100, path, argv, 0) != EIMBA)
        return 86;
    if (pexecve(0, path, argv, envp) != 79 || wait_child(-1, 1) != EFILNF)
        return 87;
#if defined(__m68k__)
    status = test_legacy_launches(path);
    if (status)
        return status;
#endif
    {
#if defined(__x86_64__)
        char dynamic_path[] = "A:\\SOAPP.TOS";
#else
        char dynamic_path[] = "A:\\LDSOAPP.ELF";
#endif
        const char *const dynamic_argv[] = {"vector shared", "", arg, 0};

        dynamic_path[0] = path[0];
        if (pexecve(0, dynamic_path, dynamic_argv, envp) != 76)
            return 93;
    }
#if PTOS_TEST_PRG
    {
        char prg_path[] = "A:\\VEC.PRG";
        const char *const prg_env[] = {variable, "FLAG=", 0};
        const char *const classic_argv[] = {"", "one", "two", 0};

        prg_path[0] = path[0];
        status = pexecve(0, prg_path, argv, envp);
        if (status != ERANGE)
            return 110 - status;
        status = pexecve(0, prg_path, argv, prg_env);
        if (status != 77)
            return status < 0 ? 160 - status : status;
        status = pexecve(0, prg_path, classic_argv, prg_env);
        if (status != 78)
            return status < 0 ? 160 - status : status;
    }
#endif
    pid = pexecve(100, path, argv, envp);
    if (pid <= 0 || pid == self)
        return 88;
    arg[0] = 'T';
    variable[0] = 'k';
    status = wait_child(pid, 0);
    if ((uint32_t)status != ((uint32_t)pid << 16 | 79U) || wait_child(pid, 1) != EFILNF)
        return 89;
#if PTOS_TEST_EXEC_REPLACE
    {
        char expected[6];
        unsigned int i;
        uint32_t value = self;
        /* Capture succeeds but the executable is absent. The caller must
         * stay runnable under its original PID and a retry must succeed. */
        if (pexecve(200, "NOSUCH.ELF", argv, envp) != EFILNF || process_id() != self)
            return 90;
        expected[5] = 0;
        for (i = 5; i > 0; i--) {
            expected[i - 1] = '0' + value % 10;
            value /= 10;
        }
        arg[0] = 't';
        variable[0] = 'K';
        argv[4] = expected;
        (void)pexecve(200, path, argv, envp);
        return 91;
    }
#else
    return pexecve(200, 0, 0, 0) == EINVFN ? 78 : 90;
#endif
}

int app_main(uint32_t argc, const char **argv, const char **envp, uintptr_t finalizer)
{
    const uint32_t *aux, *headers, *basepage;
    uint32_t phdr = 0, phent = 0, phnum = 0, entry = 0, base = 1, pd = 0, execfn = 0, bias = 0;
    unsigned int i, envc = 3;

    if (argc == 2 && equal(argv[0], "check-syscalls"))
        return test_syscalls(argv[1]);
#if defined(__m68k__) && PTOS_TEST_EXEC_REPLACE
    if (argc == 2 && equal(argv[0], "check-legacy-exec"))
        return legacy_replace(argv[1]);
#endif
    if (bss || finalizer || (argc != 4 && argc != 5) || argv[argc] ||
        argv[1][0] || !equal(argv[2], "two words") || !equal(argv[3], "literal\"quote"))
        return 81;
    if (equal(argv[0], "custom name")) {
        if (!envp[1] || envp[1][0])
            return 82;
#if defined(__m68k__)
    } else if (equal(argv[0], "legacy name") ||
               (argv[0][0] >= 'A' && argv[0][0] <= 'Z' && equal(argv[0] + 1, ":\\STKVEC.ELF"))) {
        envc = 2;
#endif
    } else {
        return 81;
    }
    if (!envp[0] || !envp[envc - 1] || envp[envc] ||
        !equal(envp[0], "KEY=two words") || !equal(envp[envc - 1], "FLAG="))
        return 82;
    for (aux = (const uint32_t *)(envp + envc + 1), i = 0; i < 20 && *aux; aux += 2, i++) {
        switch (aux[0]) {
        case 3: phdr = aux[1]; break;
        case 4: phent = aux[1]; break;
        case 5: phnum = aux[1]; break;
        case 7: base = aux[1]; break;
        case 9: entry = aux[1]; break;
        case 31: execfn = aux[1]; break;
        case DL_AT_PTOS_BASEPAGE: pd = aux[1]; break;
        case DL_AT_PTOS_LOADBIAS: bias = aux[1]; break;
        }
    }
    if (i == 20 || aux[1] || !phdr || phent != 32 || !phnum || phnum > 16 ||
        base || !entry || !pd || !execfn)
        return 83;
    basepage = (const uint32_t *)(uintptr_t)pd;
    if (basepage[0] != pd || basepage[2] != entry ||
        !equal((const char *)(uintptr_t)(execfn + 1), ":\\STKVEC.ELF"))
        return 84;
    for (headers = (const uint32_t *)(uintptr_t)phdr, i = 0; i < phnum; headers += 8, i++) {
        uint32_t first = headers[2] + bias;
        if (headers[0] == 1 && (headers[6] & 1) && entry >= first && entry - first < headers[5]) {
            if (argc == 5) {
                const char *digits = argv[4];
                uint32_t pid = 0;
                while (*digits >= '0' && *digits <= '9')
                    pid = pid * 10 + *digits++ - '0';
                return !*digits && pid && pid == (uint32_t)process_id() ? 78 : 92;
            }
            return 79;
        }
    }
    return 85;
}
