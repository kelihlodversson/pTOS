/* Native six-scalar GEMDOS transport (svc 1: r0 the function, r1-r6 the
 * arguments), independent of any libc (#693).  The arguments are 32-bit. */
#include "runtime.h"

int64_t dl_syscall(uint32_t fn, uint64_t a, uint64_t b, uint64_t c,
                    uint64_t d, uint64_t e, uint64_t f)
{
    register uint32_t r0 __asm__("r0") = fn;
    register uint32_t r1 __asm__("r1") = (uint32_t)a;
    register uint32_t r2 __asm__("r2") = (uint32_t)b;
    register uint32_t r3 __asm__("r3") = (uint32_t)c;
    register uint32_t r4 __asm__("r4") = (uint32_t)d;
    register uint32_t r5 __asm__("r5") = (uint32_t)e;
    register uint32_t r6 __asm__("r6") = (uint32_t)f;

    __asm__ volatile ("svc 1"
        : "+r"(r0)
        : "r"(r1), "r"(r2), "r"(r3), "r"(r4), "r"(r5), "r"(r6)
        : "r12", "lr", "memory", "cc");
    return (int64_t)(int32_t)r0;
}
