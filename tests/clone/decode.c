#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "clone_args.h"

struct fixture { CLONE_USER_V1 wire; int calls, readable; uint64_t address; };
static int copy_from_user(void *context, uint64_t address, void *dst, uint32_t size)
{
    struct fixture *fixture = context;
    fixture->calls++;
    if (!fixture->readable || address != fixture->address || (size != 28 && size != 32))
        return 0;
    memcpy(dst, &fixture->wire, size);
    return 1;
}

static int access_user(void *context, uintptr_t address, uint32_t size, int kind)
{
    int *calls = context;
    (*calls)++;
    if (kind == CLONE_ACCESS_EXEC) return address == 0x1000 && size == 1;
    if (kind != CLONE_ACCESS_WRITE) return 0;
    return (address == 0x2000 && size == 256) ||
           (address == 0x3000 && size == 1) ||
           (address == 0x4000 && size == 4);
}

int main(void)
{
    CLONE_USER_V1 wire = { 0x1234, 0x80000001U, 0xffffffffU,
                           0x80000000U, 0x4321, 0xf1234567U, 0x87654321U, 0xfedcba98U };
    CLONE_ARGS decoded;
    clone_decode_v1(&decoded, &wire);
    assert(decoded.flags == 0x1234 && decoded.stack_size == 0x4321);
    assert((uintptr_t)decoded.entry == UINT32_C(0x80000001));
    assert((uintptr_t)decoded.argument == UINT32_C(0xffffffff));
    assert((uintptr_t)decoded.stack_base == UINT32_C(0x80000000));
    assert((uintptr_t)decoded.tls == UINT32_C(0xf1234567));
    assert((uintptr_t)decoded.child_tid == UINT32_C(0x87654321));
    assert((uintptr_t)decoded.child_pid == UINT32_C(0xfedcba98));
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
        assert(!decoded.child_pid);
        { unsigned n; for (n=29; n<=31; n++) {
            assert(clone_copy_v1(&decoded, f.address, n, copy_from_user, &f) == CLONE_COPY_OK && !decoded.child_pid);
        } }
        /* A later caller may append fields; only the known prefix is read. */
        assert(clone_copy_v1(&decoded, f.address, 36, copy_from_user, &f) == CLONE_COPY_OK);
        assert(f.calls == 6 && (uintptr_t)decoded.entry == UINT32_C(0x80000001));
        assert((uintptr_t)decoded.child_pid == UINT32_C(0xfedcba98));
        f.address = UINT64_C(0xffffffe4);
        assert(clone_copy_v1(&decoded, f.address, 28, copy_from_user, &f) == CLONE_COPY_OK);
        assert(f.calls == 7);
    }
    {
        CLONE_ARGS a = { 0 };
        int calls = 0;
        const CLONE_ABI abi = { 16, 256, 1 };
        a.entry = (void (*)(void *))(uintptr_t)0x1000;
        a.stack_base = (void *)(uintptr_t)0x2000;
        a.stack_size = 256;
        a.tls = 0;
        a.child_tid = (int32_t *)(uintptr_t)0x4000;
        a.argument = (void *)(uintptr_t)0xffffffffU; /* opaque, not dereferenced */
        assert(clone_validate_v1(&a, &abi, access_user, &calls) == CLONE_VALID && calls == 3);
        a.flags = 0x80000000U; calls = 0;
        assert(clone_validate_v1(&a, &abi, access_user, &calls) == CLONE_INVALID && !calls);
        a.flags = CLONE_SAME_PROCESS; calls = 0;
        assert(clone_validate_v1(&a,&abi,access_user,&calls) == CLONE_INVALID && !calls);
        a.flags = CLONE_VM | CLONE_SAME_PROCESS | CLONE_COPY_CWD;
        assert(clone_validate_v1(&a,&abi,access_user,&calls) == CLONE_INVALID && !calls);
        a.flags = CLONE_VM | CLONE_COPY_CWD | CLONE_COPY_STANDARD_HANDLES;
        assert(clone_validate_v1(&a,&abi,access_user,&calls) == CLONE_VALID);
        a.child_pid = (int32_t *)(uintptr_t)0x4000; calls=0;
        assert(clone_validate_v1(&a,&abi,access_user,&calls) == CLONE_VALID && calls==4);
        a.child_pid = (int32_t *)(uintptr_t)0x4001; calls=0;
        assert(clone_validate_v1(&a,&abi,access_user,&calls) == CLONE_INVALID && !calls);
        a.child_pid = 0;
        a.flags = 0; a.stack_size = 0;
        assert(clone_validate_v1(&a, &abi, access_user, &calls) == CLONE_INVALID && !calls);
        a.stack_size = 256; a.stack_base = (void *)(uintptr_t)0xfffffffcU;
        assert(clone_validate_v1(&a, &abi, access_user, &calls) == CLONE_BAD_MEMORY && !calls);
        a.stack_base = (void *)(uintptr_t)0x2001;
        assert(clone_validate_v1(&a, &abi, access_user, &calls) == CLONE_INVALID && !calls);
        a.stack_base = (void *)(uintptr_t)0x2000; a.child_tid = (int32_t *)(uintptr_t)0x4001;
        assert(clone_validate_v1(&a, &abi, access_user, &calls) == CLONE_INVALID && !calls);
        a.child_tid = (int32_t *)(uintptr_t)0x4000; a.entry = (void (*)(void *))(uintptr_t)0x5000;
        assert(clone_validate_v1(&a, &abi, access_user, &calls) == CLONE_BAD_MEMORY && calls == 1);
    }
    return 0;
}
