#include "ptos.h"
#include <stdlib.h>
#include <string.h>
/* Exercise the shared exports rather than libcmini header inlines. */
#undef strlen
#undef strcmp
extern void client_exit(void);
extern int64_t client_pexec(const char *path, const char *tail, const char *env);
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
    if (!strcmp(argv[1], "nest")) {
        static const char tail[] = { 6, 'r', 'e', 't', 'u', 'r', 'n', 0 };
        static const char env[] = "CHECK=660\0\0";
        uint32_t basepage = ptos_basepage;
        if (client_pexec(argv[0], tail, env) != 77 || lifecycle != 3 ||
            ptos_basepage != basepage || strcmp(getenv("CHECK"), "660")) _Exit(103);
        return 77;
    }
    if (!strcmp(argv[1], "_Exit")) _Exit(89);
    if (!strcmp(argv[1], "exit")) client_exit();
    if (strcmp(argv[1], "return")) return 89;
    return 77;
}
