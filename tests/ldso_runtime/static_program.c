/* Probe the standard static ELF entry on the actual target. */
#include "startup.h"

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

int app_main(uint32_t argc, const char **argv, const char **envp, uintptr_t finalizer)
{
    const uint32_t *aux, *headers, *basepage;
    uint32_t phdr = 0, phent = 0, phnum = 0, entry = 0, base = 1, pd = 0, execfn = 0, bias = 0;
    unsigned int i;

    if (bss || finalizer || argc != 4 || argv[4] || !equal(argv[0], "custom name") ||
        argv[1][0] || !equal(argv[2], "two words") || !equal(argv[3], "literal\"quote"))
        return 81;
    if (!envp[0] || !envp[1] || !envp[2] || envp[3] ||
        !equal(envp[0], "KEY=two words") || envp[1][0] || !equal(envp[2], "FLAG="))
        return 82;
    for (aux = (const uint32_t *)(envp + 4), i = 0; i < 20 && *aux; aux += 2, i++) {
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
        if (headers[0] == 1 && (headers[6] & 1) && entry >= first && entry - first < headers[5])
            return 79;
    }
    return 85;
}
