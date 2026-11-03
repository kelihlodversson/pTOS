/* Explicit launch type overrides the program's filename extension. */
#include <gem.h>
#include "ptos.h"
#define VERIFY(c) do { if (!(c)) return 0x6a00 | __LINE__; } while (0)
static char startup_directory[128];
static int startup_ok;
static void directory_preinit(unsigned argc, char **argv, char **envp)
{
    register uint64_t ax __asm__("rax") = (1ULL << 32) | 0x47;
    (void)argc; (void)argv; (void)envp;
    __asm__ volatile ("syscall" : "+a"(ax) : "D"((uint64_t)(uintptr_t)startup_directory),"S"(0ULL)
                      : "rcx","r11","memory","cc");
    startup_ok = (int32_t)ax == 0;
}
__attribute__((used,section(".preinit_array")))
static void (*const preinit)(unsigned,char **,char **) = directory_preinit;
static int equal(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}
int main(int argc, char **argv)
{
    short window;
    VERIFY(argc == 2 && gl_apid == -1 && aes_global[2] == -1 && startup_ok);
    VERIFY(equal(startup_directory,argv[1][1] == 'l' ? "\\GEMSYS\\LIB" : argv[1][1] == 'i' ? "\\GEMSYS" : "\\APPS"));
    if (argv[1][2] == 'e') VERIFY(environ[0] && equal(environ[0],"DIRECTORY_TEST=present") && !environ[1]);
    if (argv[1][0] == 'g') {
        VERIFY(appl_init() > 0);
        window = wind_create(0,50,50,100,80);
        VERIFY(window > 0 && wind_open(window,50,50,100,80));
        VERIFY(wind_close(window) && wind_delete(window) && appl_exit());
    } else {
        static const char text[] = "x32 explicit TOS launch: RUNNING\r\n";
        register unsigned long long ax __asm__("rax") = (1ULL << 32) | 9;
        __asm__ volatile ("syscall" : "+a"(ax) : "D"((unsigned long long)(uintptr_t)text)
                          : "rcx","r11","memory","cc");
        if (argv[1][0] == 'f') __asm__ volatile ("ud2");
        VERIFY(argv[1][0] == 't');
    }
    return 37;
}
