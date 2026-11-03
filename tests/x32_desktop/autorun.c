/* Autorun fixture: its relative file must resolve beside the executable. */
#include "ptos.h"

static long gemdos(unsigned function, unsigned long long a, unsigned long long b)
{
    register unsigned long long ax __asm__("rax") = (1ULL << 32) | function;
    __asm__ volatile ("syscall" : "+a"(ax) : "D"(a), "S"(b)
                      : "rcx", "r11", "memory", "cc");
    return (long)ax;
}

static void record(const char *text)
{
    while (*text) {
        register unsigned long long ax __asm__("rax") = (13ULL << 32) | 3;
        __asm__ volatile ("syscall" : "+a"(ax) : "D"(1ULL), "S"((unsigned long long)*text++)
                          : "rcx", "r11", "memory", "cc");
    }
}

int main(void)
{
    long handle = gemdos(0x3d, (uintptr_t)"AUTORUN.DAT", 0);
    if (handle < 0) {
        record("x32 autorun relative file FAIL\r\n");
        return 98;
    }
    (void)gemdos(0x3e, handle, 0);
    record("x32 autorun relative file PASS\r\n");
    return 37;
}
