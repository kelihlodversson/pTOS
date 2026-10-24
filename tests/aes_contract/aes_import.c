/* Loaded consumer: imports both AES and VDI, never a kernel code address. */
#include "aes_client.h"

int aes_roundtrip_checks(const char *tail, const char *child_path);
void x32_probe_main(UQUAD basepage, UQUAD type, UQUAD sp, ULONG cs, ULONG ss);
void x32_probe_main(UQUAD basepage, UQUAD type, UQUAD sp, ULONG cs, ULONG ss)
{
    const char *tail = (const char *)(uintptr_t)(basepage + 0x80);
    register UQUAD rax __asm__("rax");
    register UQUAD rdi __asm__("rdi");
    register UQUAD rsi __asm__("rsi") = 0;
    register UQUAD rdx __asm__("rdx") = 0;
    register UQUAD r10 __asm__("r10") = 0;
    int result;

    (void)type;
    (void)ss;
    if (tail[0] == 'w')
        *(volatile UBYTE *)(uintptr_t)aes_appl_exit = 0;
    result = ((cs & 3) == 3 && !((sp + 8) & 15)) ?
        aes_roundtrip_checks(tail, "AESIMP.TOS") : 1;
    rax = (1ULL << 32) | 0x4c;
    rdi = result;
    __asm__ volatile ("syscall" : "+r"(rax)
                      : "r"(rdi), "r"(rsi), "r"(rdx), "r"(r10)
                      : "rcx", "r11", "memory", "cc");
    for (;;)
        ;
}
