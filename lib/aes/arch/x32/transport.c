/* x32 syscall transport; the shared session/geometry code is portable. */
#include "../../transport.h"
LONG cuis_call(ULONG function, LONG handle)
{
    register UQUAD ax __asm__("rax") = ((UQUAD)CUIS_SERVICE_CLASS << 32) | function;
    register UQUAD di __asm__("rdi") = (QUAD)handle;
    __asm__ volatile ("syscall" : "+a"(ax) : "r"(di)
        : "rcx", "r11", "memory", "cc");
    return (LONG)ax;
}
