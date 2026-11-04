#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "clone_args.h"

struct fixture { CLONE_USER_V1 wire; int calls, readable; uint64_t address; };
static int copy_from_user(void *context, uint64_t address, void *dst, uint32_t size)
{
    struct fixture *fixture = context;
    fixture->calls++;
    if (!fixture->readable || address != fixture->address || size != sizeof(fixture->wire))
        return 0;
    memcpy(dst, &fixture->wire, size);
    return 1;
}

int main(void)
{
    CLONE_USER_V1 wire = { 0x1234, 0x80000001U, 0xffffffffU,
                           0x80000000U, 0x4321, 0xf1234567U, 0x87654321U };
    CLONE_ARGS decoded;
    clone_decode_v1(&decoded, &wire);
    assert(decoded.flags == 0x1234 && decoded.stack_size == 0x4321);
    assert((uintptr_t)decoded.entry == UINT32_C(0x80000001));
    assert((uintptr_t)decoded.argument == UINT32_C(0xffffffff));
    assert((uintptr_t)decoded.stack_base == UINT32_C(0x80000000));
    assert((uintptr_t)decoded.tls == UINT32_C(0xf1234567));
    assert((uintptr_t)decoded.child_tid == UINT32_C(0x87654321));
    {
        struct fixture f = { { 0 }, 0, 1, UINT64_C(0x80000000) };
        CLONE_ARGS before;
        f.wire = wire;
        memset(&decoded, 0xa5, sizeof(decoded));
        before = decoded;
        assert(clone_copy_v1(&decoded, f.address, 27, copy_from_user, &f) == CLONE_COPY_BAD_SIZE);
        assert(clone_copy_v1(&decoded, f.address, UINT64_C(0x100000000), copy_from_user, &f) == CLONE_COPY_BAD_SIZE);
        assert(clone_copy_v1(&decoded, 0, 28, copy_from_user, &f) == CLONE_COPY_BAD_ADDRESS);
        assert(clone_copy_v1(&decoded, UINT64_C(0xfffffff0), 28, copy_from_user, &f) == CLONE_COPY_BAD_ADDRESS);
        assert(clone_copy_v1(&decoded, UINT64_C(0xffffffe4), 32, copy_from_user, &f) == CLONE_COPY_BAD_ADDRESS);
        assert(clone_copy_v1(&decoded, UINT64_C(0x100000000), 28, copy_from_user, &f) == CLONE_COPY_BAD_ADDRESS);
        assert(f.calls == 0 && memcmp(&decoded, &before, sizeof(decoded)) == 0);
        f.readable = 0;
        assert(clone_copy_v1(&decoded, f.address, 28, copy_from_user, &f) == CLONE_COPY_BAD_ADDRESS);
        assert(f.calls == 1 && memcmp(&decoded, &before, sizeof(decoded)) == 0);
        f.readable = 1;
        assert(clone_copy_v1(&decoded, f.address, 28, copy_from_user, &f) == CLONE_COPY_OK);
        assert(f.calls == 2 && (uintptr_t)decoded.entry == UINT32_C(0x80000001));
        assert((uintptr_t)decoded.child_tid == UINT32_C(0x87654321));
        /* A later caller may append fields; only the known prefix is read. */
        assert(clone_copy_v1(&decoded, f.address, 36, copy_from_user, &f) == CLONE_COPY_OK);
        assert(f.calls == 3 && (uintptr_t)decoded.entry == UINT32_C(0x80000001));
        f.address = UINT64_C(0xffffffe4);
        assert(clone_copy_v1(&decoded, f.address, 28, copy_from_user, &f) == CLONE_COPY_OK);
        assert(f.calls == 4);
    }
    return 0;
}
