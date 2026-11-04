/* Standard ELF32 argc/argv/envp/auxv and private mapping probe (#660). */
#include <stdint.h>
#include "../../lib/ldso/launch.h"
extern unsigned char _start[] __attribute__((visibility("hidden")));

static int equal(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}

uint32_t interp_check(const uint32_t *stack, uint64_t finalizer);
uint32_t interp_check(const uint32_t *stack, uint64_t finalizer)
{
    uint32_t argc = stack[0], i, at, phdr = 0, phnum = 0, entry = 0, base = 0;
    uint32_t pagesz = 0, execfn = 0, basepage = 0, phent = 0, bias = UINT32_MAX, launch = 0;
    const uint32_t *v = stack + 1;
    int extended;

    if (((uintptr_t)stack & 15) || finalizer || argc < 3 || argc > 4)
        return 1;
    extended = equal((const char *)(uintptr_t)v[0], "custom-name");
    if ((!extended && argc != 4) || (extended && argc != 3) ||
        !equal((const char *)(uintptr_t)v[1], "hello world") || v[argc])
        return 2;
    if ((!extended && !equal((const char *)(uintptr_t)v[2], "")) ||
        !equal((const char *)(uintptr_t)v[argc - 1], "C:\\tmp"))
        return 3;
    v += argc + 1;
    if (!v[0] || !equal((const char *)(uintptr_t)v[0], "CHECK=660") ||
        !v[1] || !equal((const char *)(uintptr_t)v[1], "SECOND=value") || v[2])
        return 4;
    v += 3;
    for (i = 0; i < 12; i++, v += 2) {
        at = v[0];
        if (!at) break;
        switch (at) {
        case 3: phdr = v[1]; break;
        case 4: phent = v[1]; break;
        case 5: phnum = v[1]; break;
        case 6: pagesz = v[1]; break;
        case 7: base = v[1]; break;
        case 9: entry = v[1]; break;
        case 31: execfn = v[1]; break;
        case 0x5000: basepage = v[1]; break;
        case 0x5001: if (v[1] >= 26) return 5; break;
        case 0x5002: bias = v[1]; break;
        case DL_AT_PTOS_LAUNCH: launch = v[1]; break;
        default: return 5;
        }
    }
    if (i == 12 || bias == UINT32_MAX || pagesz != 4096 || phent != 32 || !phnum || phnum > 128 ||
        base < 0xf000000 || base >= 0xfff0000 ||
        (uintptr_t)_start < base || (uintptr_t)_start >= 0xfff0000 ||
        entry < 0x400000 || entry >= 0x800000 || !basepage ||
        !execfn || (!equal((const char *)(uintptr_t)execfn, "C:\\DYNTEST.TOS") &&
                   !equal((const char *)(uintptr_t)execfn, "C:\\DYNPIE.TOS")) ||
        phdr < 0x400000 || phdr >= 0x800000 - phnum * 32)
        return 6;
    if (launch) {
        const DL_LAUNCH *saved = (const DL_LAUNCH *)(uintptr_t)launch;
        static const char short_tail[] = "\"hello world\" \"\" C:\\tmp";
        if ((launch & 3) || launch < (uintptr_t)stack ||
            launch > 0x80000000U-sizeof(*saved) || saved->ready || !saved->environment ||
            saved->environment_size < 2 || saved->environment_size > 32768) return 9;
        if (extended) {
            if (saved->tail[0] != 127) return 9;
        } else {
            if (saved->tail[0] != sizeof(short_tail)-1) return 9;
            for (i = 0; i < sizeof(short_tail)-1; i++)
                if (saved->tail[i+1] != (unsigned char)short_tail[i]) return 9;
        }
    }
    for (i = 0; i < phnum; i++) {
        const uint32_t *ph = (const uint32_t *)(uintptr_t)(phdr + i * 32);
        if (ph[0] == 6 && (phdr < ph[2] || bias != phdr - ph[2]))
            return 7;
    }
    for (i = 0; i < phnum; i++) {
        const uint32_t *ph = (const uint32_t *)(uintptr_t)(phdr + i * 32);
        if (ph[0] == 1 && (ph[6] & 1) && entry >= ph[2] + bias && entry - ph[2] - bias < ph[5])
            return entry;
    }
    return 7;
}
