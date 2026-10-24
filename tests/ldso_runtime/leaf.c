/* Transitive dependency with root-executable references. */
extern int startup_log;
/* A dedicated read-only PT_LOAD is shortened to one file byte by the
 * fixture generator, leaving partial-page BSS filled from nonzero file data. */
const volatile unsigned char readonly_bss[32]
    __attribute__((section(".partial_bss"))) = { 0xa5, 0xff, 0xff, 0xff };
int leaf(void) { return 7; }
__attribute__((constructor)) static void init(void)
{
    unsigned int i;
    if (readonly_bss[0] != 0xa5) { startup_log = -1; return; }
    for (i = 1; i < sizeof(readonly_bss); i++)
        if (readonly_bss[i]) { startup_log = -1; return; }
    startup_log = startup_log * 10 + 1;
}
__attribute__((destructor)) static void fini(void) { startup_log = startup_log * 10 + 6; }
