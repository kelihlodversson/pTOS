/* Test-parent policy over the ordinary public PROCESS wait interface. */
#ifndef TEST_GEM_PROCESS_H
#define TEST_GEM_PROCESS_H
#include <stdint.h>
static int32_t gem_wait_child(short pid)
{
    register uint64_t ax __asm__("rax") = (1ULL << 32) | 0x13a;
    __asm__ volatile ("syscall" : "+a"(ax) : "D"((uint64_t)(unsigned short)pid), "S"(1ULL), "d"(0ULL)
                      : "rcx","r11","memory","cc");
    return (int32_t)ax;
}
#endif
