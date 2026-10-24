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
int app_main(uint32_t argc, const char *const *argv, const char *const *envp, uint32_t finalizer)
{
    if (argc != 1 || !argv[0] || argv[1] || !envp[0]) return 90;
    if (startup_log != 9123 || dynvalue != 9 || dynbss != 3 || answer() != 23) return 91;
    /* Main and interpreter RELRO are sealed too, not just Mmap-backed DSOs. */
    if (dl_syscall(0x17d, (uintptr_t)&pre, 4, 1, 0, 0, 0) ||
        dl_syscall(0x17d, (uintptr_t)&pre, 4, 0, 0, 0, 0) >= 0 ||
        dl_syscall(0x17d, (uintptr_t)&dynvalue, 4, 0, 0, 0, 0) ||
        dl_syscall(0x17d, finalizer, 1, 0, 0, 0, 0) >= 0 ||
        dl_syscall(GEMDOS_MPROT, 0x7ffff000, 4096, PROT_READ, 0, 0, 0) >= 0 ||
        dl_syscall(GEMDOS_MPROT, 0x700000, 4096, PROT_READ, 0, 0, 0) >= 0 ||
        dl_syscall(GEMDOS_MPROT, (uintptr_t)&pre & ~4095U, 4096,
                   PROT_WRITE | PROT_EXEC, 0, 0, 0) >= 0) return 94;
    return 77;
}
int app_after_fini(void)
{
    return startup_log == 9123456 && dynvalue == -7 ? 0 : 92;
}
