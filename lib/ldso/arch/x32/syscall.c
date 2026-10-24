/* Native six-scalar GEMDOS transport, independent of any libc (#660). */
#include "runtime.h"
int64_t dl_syscall(uint32_t fn, uint64_t a, uint64_t b, uint64_t c,
                    uint64_t d, uint64_t e, uint64_t f)
{
    register uint64_t ax __asm__("rax") = ((uint64_t)1 << 32) | fn;
    register uint64_t di __asm__("rdi") = a, si __asm__("rsi") = b, dx __asm__("rdx") = c;
    register uint64_t r10 __asm__("r10") = d, r8 __asm__("r8") = e, r9 __asm__("r9") = f;
    __asm__ volatile ("syscall" : "+a"(ax)
        : "r"(di), "r"(si), "r"(dx), "r"(r10), "r"(r8), "r"(r9)
        : "rcx", "r11", "memory");
    return (int64_t)ax;
}
