#include "ptos.h"
#include <stdlib.h>
#include <string.h>
/* Exercise the shared exports rather than libcmini header inlines. */
#undef strlen
#undef strcmp
extern void client_exit(void);
int lifecycle;
static int late_registered, late_calls;
static void handler(void)
{
    if (lifecycle != 3 || late_calls != 1) _Exit(91);
    lifecycle = 4;
}
__attribute__((constructor)) static void init(void)
{
    if (lifecycle != 1 || atexit(handler)) _Exit(92);
    lifecycle = 2;
}
__attribute__((destructor)) static void fini(void)
{
    if (lifecycle != 5) _Exit(93);
    lifecycle = 6;
}
static void late_handler(void)
{
    if (lifecycle != 3 || late_calls) _Exit(101);
    late_calls++;
}
static void extra_handler(void)
{
    if (lifecycle != 3) _Exit(98);
    if (!late_registered) {
        late_registered = 1;
        if (atexit(late_handler)) _Exit(102);
    }
}
int main(int argc, char **argv, char **envp)
{
    int i;
    if (argc != 2 || !argv[0] || argv[2] || envp != environ ||
        !getenv("CHECK") || lifecycle != 2) return 90;
    /* The two constructors already registered two of the 32 slots. */
    for (i = 0; i < 30; i++) if (atexit(extra_handler)) return 99;
    if (!atexit(extra_handler) || !atexit(0)) return 100;
    lifecycle = 3;
    if (!strcmp(argv[1], "_Exit")) _Exit(89);
    if (!strcmp(argv[1], "exit")) client_exit();
    if (strcmp(argv[1], "return")) return 89;
    return 77;
}
