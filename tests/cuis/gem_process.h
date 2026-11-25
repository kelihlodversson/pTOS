/* Test-parent policy over the ordinary public PROCESS wait interface. */
#ifndef TEST_GEM_PROCESS_H
#define TEST_GEM_PROCESS_H
#include <stdint.h>
static inline int32_t gem_wait_child_flags(short pid, unsigned flags)
{
    register uint64_t ax __asm__("rax") = (1ULL << 32) | 0x13a;
    __asm__ volatile ("syscall" : "+a"(ax) : "D"((uint64_t)(unsigned short)pid), "S"((uint64_t)flags), "d"(0ULL)
                      : "rcx","r11","memory","cc");
    return (int32_t)ax;
}
static inline int32_t gem_wait_child(short pid)
{
    return gem_wait_child_flags(pid,1);
}
#endif
