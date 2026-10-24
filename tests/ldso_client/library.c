#include "ptos.h"
#include <stdlib.h>
#include <string.h>
/* Exercise the shared exports rather than libcmini header inlines. */
#undef strlen
#undef strcmp
extern int lifecycle;

static void handler(void)
{
    if (lifecycle != 4) _Exit(94);
    lifecycle = 5;
}
__attribute__((constructor)) static void init(void)
{
    size_t (*volatile length)(const char *) = strlen;
    int (*volatile compare)(const char *, const char *) = strcmp;
    const char *value = getenv("CHECK");
    if (!value || compare(value, "660") || length(value) != 3 || !ptos_basepage ||
        atexit(handler)) _Exit(95);
    lifecycle = 1;
}
static void destructor_handler(void)
{
    static const char message[] = "x32 dynamic CRT lifecycle: PASS\r\n";
    if (lifecycle != 7) _Exit(98);
    lifecycle = 8;
    if (ptos_write(1, message, sizeof(message) - 1) != sizeof(message) - 1) _Exit(97);
}
__attribute__((destructor)) static void fini(void)
{
    if (lifecycle != 6) _Exit(96);
    lifecycle = 7;
    if (atexit(destructor_handler)) _Exit(99);
}
void client_exit(void)
{
    exit(88);
}
