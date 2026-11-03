/* A real console child: no AES registration, standard GEMDOS I/O. */
#include "ptos.h"
static unsigned long long call(unsigned function, unsigned long long argument)
{
    register unsigned long long ax __asm__("rax") = (1ULL << 32) | function;
    __asm__ volatile ("syscall" : "+a"(ax) : "D"(argument) : "rcx","r11","memory","cc");
    return ax;
}
static void record(const char *event)
{
    char text[100], digits[12];
    const char *prefix = "x32 console ";
    unsigned used = 0, n = 0, pid = (unsigned)call(0x10b,0), i;
    while (*prefix) text[used++] = *prefix++;
    while (*event) text[used++] = *event++;
    text[used++] = ' ';
    do { digits[n++] = '0' + pid%10; pid /= 10; } while (pid);
    while (n) text[used++] = digits[--n];
    text[used++] = '\r'; text[used++] = '\n';
    for (i = 0; i < used; i++) {
        register unsigned long long ax __asm__("rax") = (13ULL << 32) | 3;
        __asm__ volatile ("syscall" : "+a"(ax) : "D"(1ULL),"S"((unsigned long long)text[i])
                          : "rcx","r11","memory","cc");
    }
}
int main(int argc, char **argv)
{
    unsigned key;
    if (argc > 1) {
        const char *expected = "param", *actual = argv[1];
        while (*expected && *actual == *expected) { actual++; expected++; }
        if (*actual || *expected) return 99;
        record("PARAMETERS PASS");
    }
    (void)call(9,(uintptr_t)"Resident EmultiDesk console child\r\nPress Q to return to the desktop.\r\n");
    record("READY");
    key = (unsigned)call(7,0) & 255;
    record("INPUT PASS");
    if (key == 'f') { record("fault READY"); __asm__ volatile ("ud2"); }
    return key == 'q' ? 37 : 98;
}
