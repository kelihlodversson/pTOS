/* Loaded application: deliberately linked without libptos-vdi.a. */
#include "vdipb.h"
#include "gemerror.h"

int vdi_roundtrip_checks(WORD inherited_handle, BOOL fault_after_open);

static QUAD call(ULONG fn, UQUAD a, UQUAD b, UQUAD c, UQUAD d)
{
    register UQUAD rax __asm__("rax") = (1ULL << 32) | fn;
    register UQUAD rdi __asm__("rdi") = a;
    register UQUAD rsi __asm__("rsi") = b;
    register UQUAD rdx __asm__("rdx") = c;
    register UQUAD r10 __asm__("r10") = d;

    __asm__ volatile ("syscall" : "+r"(rax)
                      : "r"(rdi), "r"(rsi), "r"(rdx), "r"(r10)
                      : "rcx", "r11", "memory", "cc");
    return (QUAD)rax;
}

void x32_probe_main(UQUAD basepage, UQUAD type, UQUAD sp, ULONG cs, ULONG ss);
void x32_probe_main(UQUAD basepage, UQUAD type, UQUAD sp, ULONG cs, ULONG ss)
{
    const UBYTE *bp = (const UBYTE *)(uintptr_t)basepage;
    char mode = bp[0x80];
    int rc;

    (void)type;
    (void)ss;
    if ((cs & 3) != 3 || ((sp + 8) & 15))
        rc = 1;
    else if (vdi((VDIPB *)0) != EIMBA)
        rc = 2;
    else if (mode == 'w') {
        /* A function-pointer import uses GLOB_DAT as well as PLT calls. */
#ifndef VDI_IMPORT_PLT
        *(volatile UBYTE *)(uintptr_t)vdi = 0;
#endif
        rc = 3;
    } else
        rc = vdi_roundtrip_checks(0, mode == 'f');
    call(0x4c, rc, 0, 0, 0);
    for (;;)
        ;
}
