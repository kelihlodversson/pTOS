/* Fixed-width syscall registers; never call a supervisor C entry point. */
#ifndef DESK_X32_SYSCALL_H
#define DESK_X32_SYSCALL_H
static __inline__ long desk_syscall(unsigned trap, unsigned fn,
                                    long a, long b, long c, long d)
{
    register unsigned long long rax __asm__("rax") = (trap << 32) | fn;
    register long long rdi __asm__("rdi") = a;
    register long long rsi __asm__("rsi") = b;
    register long long rdx __asm__("rdx") = c;
    register long long r10 __asm__("r10") = d;

    __asm__ volatile ("syscall" : "+r"(rax)
                      : "r"(rdi), "r"(rsi), "r"(rdx), "r"(r10)
                      : "rcx", "r11", "memory", "cc");
    return (long)rax;
}
#endif
