/* x32 syscall transport; the shared session/geometry code is portable. */
#include "../../transport.h"
LONG aes_process_call(ULONG function)
{
    register UQUAD ax __asm__("rax") = (1ULL << 32) | function;
    __asm__ volatile ("syscall" : "+a"(ax) :: "rcx", "r11", "memory", "cc");
    return (LONG)ax;
}
LONG cuis_call(ULONG function, LONG handle)
{
    register UQUAD ax __asm__("rax") = ((UQUAD)CUIS_SERVICE_CLASS << 32) | function;
    register UQUAD di __asm__("rdi") = (QUAD)handle;
    __asm__ volatile ("syscall" : "+a"(ax) : "r"(di)
        : "rcx", "r11", "memory", "cc");
    return (LONG)ax;
}

LONG cuis_call5(ULONG function, LONG a, LONG b, LONG c, LONG d, LONG e)
{
    register UQUAD ax __asm__("rax") = ((UQUAD)CUIS_SERVICE_CLASS << 32) | function;
    register UQUAD di __asm__("rdi") = (QUAD)a;
    register UQUAD si __asm__("rsi") = (QUAD)b;
    register UQUAD dx __asm__("rdx") = (QUAD)c;
    register UQUAD r10 __asm__("r10") = (QUAD)d;
    register UQUAD r8 __asm__("r8") = (QUAD)e;
    __asm__ volatile ("syscall" : "+a"(ax)
        : "r"(di), "r"(si), "r"(dx), "r"(r10), "r"(r8)
        : "rcx", "r11", "memory", "cc");
    return (LONG)ax;
}

LONG cuis_call6(ULONG function, UQUAD a, UQUAD b, UQUAD c, UQUAD d, UQUAD e, UQUAD f)
{
    register UQUAD ax __asm__("rax") = ((UQUAD)CUIS_SERVICE_CLASS << 32) | function;
    register UQUAD di __asm__("rdi") = a, si __asm__("rsi") = b;
    register UQUAD dx __asm__("rdx") = c, r10 __asm__("r10") = d;
    register UQUAD r8 __asm__("r8") = e, r9 __asm__("r9") = f;
    __asm__ volatile ("syscall" : "+a"(ax)
        : "r"(di), "r"(si), "r"(dx), "r"(r10), "r"(r8), "r"(r9)
        : "rcx", "r11", "memory", "cc");
    return (LONG)ax;
}
