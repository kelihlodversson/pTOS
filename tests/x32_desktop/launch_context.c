/* TOS launch fixture: report its current directory and require console input. */
#include "ptos.h"

static long call(unsigned function, unsigned long long a, unsigned long long b)
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
    long handle = call(0x3d, (uintptr_t)"TOP.MRK", 0);
    if (handle >= 0) {
        (void)call(0x3e, handle, 0);
        record("x32 launch directory TOP\r\n");
    } else {
        handle = call(0x3d, (uintptr_t)"APP.MRK", 0);
        if (handle < 0) {
            record("x32 launch directory FAIL\r\n");
            return 98;
        }
        (void)call(0x3e, handle, 0);
        record("x32 launch directory APP\r\n");
    }
    record("x32 launch console READY\r\n");
    if ((call(7, 0, 0) & 255) != 'q')
        return 98;
    record("x32 launch console INPUT PASS\r\n");
    return 37;
}
