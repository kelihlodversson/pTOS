#include <assert.h>
#include <stdint.h>
#include "clone_args.h"

int main(void)
{
    CLONE_USER_V1 wire = { 1, 0x1234, 0x80000001U, 0xffffffffU,
                           0x80000000U, 0x4321, 0xf1234567U, 0x87654321U };
    CLONE_ARGS decoded;
    clone_decode_v1(&decoded, &wire);
    assert(decoded.version == 1 && decoded.flags == 0x1234 && decoded.stack_size == 0x4321);
    assert(decoded.entry == UINT64_C(0x80000001));
    assert(decoded.argument == UINT64_C(0xffffffff));
    assert(decoded.stack_base == UINT64_C(0x80000000));
    assert(decoded.tls == UINT64_C(0xf1234567));
    assert(decoded.child_tid == UINT64_C(0x87654321));
    return 0;
}
