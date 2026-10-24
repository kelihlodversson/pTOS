/* Validate the bootstrap without needing a host x32 execution environment. */
#define _GNU_SOURCE
#include <sys/mman.h>
#include <stdint.h>
#include <string.h>
#include "runtime.h"

static void put(unsigned char *p, uint32_t value)
{
    memcpy(p, &value, 4);
}
int main(void)
{
    unsigned char *p = mmap((void *)0x0f000000, 8192, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    uint32_t stack[] = { 0, 0, 0, 7, 0x0f000000, 0, 0 };
    uint32_t value32;
    uint64_t value64;
    int rc = 1;
    if (p == MAP_FAILED) return 10;
    put(p, 0x464c457f); p[4] = 1; p[5] = 1;
    put(p + 28, 52); p[44] = 2;
    put(p + 52, 1); put(p + 52 + 20, 8192); put(p + 52 + 24, 6);
    put(p + 84, 2); put(p + 84 + 8, 0x200); put(p + 84 + 16, 32);
    put(p + 0x200, 7); put(p + 0x204, 0x300);
    put(p + 0x208, 8); put(p + 0x20c, 36);
    put(p + 0x210, 9); put(p + 0x214, 12);
    put(p + 0x300, 0x400); put(p + 0x304, 8); put(p + 0x308, 0x1000);
    put(p + 0x30c, 0x408); put(p + 0x310, 38); put(p + 0x314, (uint32_t)-4096);
    put(p + 0x318, 0x420); put(p + 0x31c, 0x101);
    memset(p + 0x400, 0xa5, 40);
    if (!dl_bootstrap(stack)) goto done;
    memcpy(&value64, p + 0x400, 8);
    if (value64 != UINT64_C(0xa5a5a5a5a5a5a5a5)) goto done;
    /* Correcting the final record permits both 32- and 64-bit relative writes. */
    put(p + 0x31c, 0);
    if (dl_bootstrap(stack)) goto done;
    memcpy(&value32, p + 0x400, 4); memcpy(&value64, p + 0x408, 8);
    if (value32 != 0x0f001000 || value64 != UINT64_C(0x0efff000)) goto done;
    /* A metadata destination and overlapping destination are both refused. */
    put(p + 0x300, 0x200);
    if (!dl_bootstrap(stack)) goto done;
    put(p + 0x300, 0x408);
    if (!dl_bootstrap(stack)) goto done;
    rc = 0;
done:
    munmap(p, 8192);
    return rc;
}
