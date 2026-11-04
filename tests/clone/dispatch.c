/* Verify native-slot and packed m68k trap argument decoding. */
#include <assert.h>
#include <stdint.h>
#include "clone.h"

static uint64_t got_address, got_size;
long xclone(uint64_t address, uint64_t size)
{
    got_address = address;
    got_size = size;
    return -123;
}

int main(void)
{
    long native[] = { 0x190, (long)0x87654321UL, (long)0xfedcba98UL };
    int16_t m68k[] = { 0x190, (int16_t)0x8765, 0x4321,
                       (int16_t)0xfedc, (int16_t)0xba98 };
    assert(clone_dispatch_native(native) == -123);
    assert(got_address == UINT64_C(0x87654321));
    assert(got_size == UINT64_C(0xfedcba98));
    assert(clone_dispatch_m68k(m68k) == -123);
    assert(got_address == UINT64_C(0x87654321));
    assert(got_size == UINT64_C(0xfedcba98));
#if __SIZEOF_LONG__ > 4
    native[1] = (long)UINT64_C(0x100000000);
    native[2] = (long)UINT64_C(0x10000001c);
    assert(clone_dispatch_native(native) == -123);
    assert(got_address == UINT64_C(0x100000000));
    assert(got_size == UINT64_C(0x10000001c));
#endif
    return 0;
}
