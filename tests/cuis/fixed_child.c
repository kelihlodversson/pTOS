/* Fixed-address ET_EXEC without PT_INTERP or retained relocation sections. */
static volatile unsigned long value = 0x2468;
static volatile unsigned char zero[4096];
void _start(void) __attribute__((noreturn));
void _start(void)
{
    register unsigned long long result __asm__("rax") = (1ULL << 32) | 0x4c;
    register unsigned long long status __asm__("rdi") = value+zero[4095];
    __asm__ volatile ("syscall" : "+a"(result) : "r"(status) : "rcx","r11","memory","cc");
    for (;;) __asm__ volatile ("ud2");
}
