#include <stdint.h>
#include "../../lib/ldso/runtime.h"
#include "../../include/mman.h"
extern int answer(void), dynvalue, dynbss;
int startup_log;
int leaf(void) { return 11; }             /* executable interposes library leaf */
static void preinit(void) { startup_log = 9; }
__attribute__((used, section(".preinit_array"))) static void (*const pre)(void) = preinit;
__attribute__((constructor)) static void init(void)
{
    startup_log = startup_log * 10 + 3;
    dynbss = 3;
}
__attribute__((destructor)) static void fini(void) { startup_log = startup_log * 10 + 4; }
#ifdef LDSO_TEST_SHARING
/*
 * Two live processes: this one launches another copy of itself ("child N"),
 * which must see the clean pages of the libraries as shared with this one
 * (Ssystem(S_MEMINFO): the count of pages mapped from another address space
 * has grown since N), while the writable data of both stay their own.
 */
#define S_MEMINFO ((uint32_t)-3)
#define SS_CROSS 6                      /* the 7th long of struct mem_info */
static uint32_t cross_shares(void)
{
    uint32_t info[7] = { 0 };

    if (dl_syscall(0x154, S_MEMINFO, (uintptr_t)info, sizeof(info), 0, 0, 0) != sizeof(info))
        return UINT32_MAX;
    return info[SS_CROSS];
}
static uint32_t number(const char *text)
{
    uint32_t value = 0;

    while (*text >= '0' && *text <= '9')
        value = value * 10 + (uint32_t)(*text++ - '0');
    return value;
}
static int child(const char *const *argv)
{
    /* not this process's own copy of the parent's writes */
    if (startup_log != 9123 || dynvalue != 9 || dynbss != 3 || answer() != 23) return 98;
    return cross_shares() > number(argv[2]) ? 78 : 99;
}
static int launch_child(void)
{
    char tail[24] = { 0 };
    uint32_t n = cross_shares(), digits = 0, i;
    char text[12];

    if (n == UINT32_MAX) return 95;
    do text[digits++] = (char)('0' + n % 10); while (n /= 10);
    tail[1] = 'c'; tail[2] = 'h'; tail[3] = 'i'; tail[4] = 'l'; tail[5] = 'd'; tail[6] = ' ';
    for (i = 0; i < digits; i++) tail[7 + i] = text[digits - 1 - i];
    tail[0] = (char)(6 + digits);
    dynvalue = 123;                     /* ... which the child must not see */
    if (dl_syscall(0x4b, 0, (uintptr_t)"C:\\LDSOAPP.ELF", (uintptr_t)tail, 0, 0, 0) != 78) return 96;
    return dynvalue == 123 ? 0 : 97;
}
#endif
int app_main(uint32_t argc, const char *const *argv, const char *const *envp, uint32_t finalizer)
{
#ifdef LDSO_TEST_SHARING
    if (argc == 3 && argv[1] && argv[1][0] == 'c' && argv[2]) return child(argv);
#endif
    if (argc != 1 || !argv[0] || argv[1] || !envp[0]) return 90;
    if (startup_log != 9123 || dynvalue != 9 || dynbss != 3 || answer() != 23) return 91;
    /* Main and interpreter RELRO are sealed too, not just Mmap-backed DSOs
     * (where the linker can make a RELRO: not the bare-metal ARM one). */
#ifdef LDSO_TEST_NO_RELRO
#define PRE_SEALED 0
#else
#define PRE_SEALED (dl_syscall(0x17d, (uintptr_t)&pre, 4, 0, 0, 0, 0) >= 0)
#endif
    if (dl_syscall(0x17d, (uintptr_t)&pre, 4, 1, 0, 0, 0) ||
        PRE_SEALED ||
        dl_syscall(0x17d, (uintptr_t)&dynvalue, 4, 0, 0, 0, 0) ||
        dl_syscall(0x17d, finalizer, 1, 0, 0, 0, 0) >= 0 ||
        dl_syscall(GEMDOS_MPROT, 0x7ffff000, 4096, PROT_READ, 0, 0, 0) >= 0 ||
        dl_syscall(GEMDOS_MPROT, 0x700000, 4096, PROT_READ, 0, 0, 0) >= 0 ||
        dl_syscall(GEMDOS_MPROT, (uintptr_t)&pre & ~4095U, 4096,
                   PROT_WRITE | PROT_EXEC, 0, 0, 0) >= 0) return 94;
#ifdef LDSO_TEST_SHARING
    {
        int rc = launch_child();

        if (rc) return rc;
    }
#endif
    return 77;
}
int app_after_fini(void)
{
    return startup_log == 9123456 && dynvalue == -7 ? 0 : 92;
}
