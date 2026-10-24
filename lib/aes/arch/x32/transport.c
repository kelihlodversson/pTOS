/* x32 syscall transport; the shared session/geometry code is portable. */
#include "../../transport.h"
LONG cuis_call(const CUIS_SESSION_REQUEST *request, CUIS_SESSION_INFO *info)
{
    register UQUAD ax __asm__("rax") = ((UQUAD)CUIS_SERVICE_CLASS << 32) | CUIS_SESSION;
    register UQUAD di __asm__("rdi") = (uintptr_t)request;
    register UQUAD si __asm__("rsi") = sizeof(*request);
    register UQUAD dx __asm__("rdx") = (uintptr_t)info;
    register UQUAD r10 __asm__("r10") = info ? sizeof(*info) : 0;
    __asm__ volatile ("syscall" : "+a"(ax)
        : "r"(di), "r"(si), "r"(dx), "r"(r10)
        : "rcx", "r11", "memory", "cc");
    return (LONG)ax;
}
