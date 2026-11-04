/* Decode a kernel-owned Pclone ABI snapshot. */
#include "clone_args.h"

void clone_decode_v1(CLONE_ARGS *out, const CLONE_USER_V1 *snapshot)
{
    out->flags = snapshot->flags;
    out->entry = (void (*)(void *))(uintptr_t)snapshot->entry;
    out->argument = (void *)(uintptr_t)snapshot->argument;
    out->stack_base = (void *)(uintptr_t)snapshot->stack_base;
    out->stack_size = snapshot->stack_size;
    out->tls = (void *)(uintptr_t)snapshot->tls;
    out->child_tid = (int32_t *)(uintptr_t)snapshot->child_tid;
}

enum clone_copy_result clone_copy_v1(CLONE_ARGS *out, uint64_t address,
                                    uint64_t size,
                                    int (*copy)(void *, uint64_t, void *, uint32_t),
                                    void *context)
{
    CLONE_USER_V1 snapshot;
    CLONE_ARGS decoded;

    if (size < sizeof(snapshot) || size > UINT32_MAX) return CLONE_COPY_BAD_SIZE;
    if (!address || address > UINT64_C(0x100000000) - size)
        return CLONE_COPY_BAD_ADDRESS;
    if (!copy(context, address, &snapshot, sizeof(snapshot)))
        return CLONE_COPY_BAD_ADDRESS;
    clone_decode_v1(&decoded, &snapshot);
    *out = decoded;
    return CLONE_COPY_OK;
}

enum clone_validate_result clone_validate_v1(const CLONE_ARGS *args,
    int (*access)(void *, uintptr_t, uint32_t, int), void *context)
{
    uintptr_t entry = (uintptr_t)args->entry;
    uintptr_t stack = (uintptr_t)args->stack_base;
    uintptr_t tls = (uintptr_t)args->tls;
    uintptr_t tid = (uintptr_t)args->child_tid;

    /* No flag combinations are exposed by the unsupported backend. */
    if (args->flags || !args->stack_size) return CLONE_INVALID;
    if (!entry || !stack || stack > UINT32_MAX ||
        (uint64_t)stack + args->stack_size > UINT64_C(0x100000000) ||
        entry > UINT32_MAX || tls > UINT32_MAX || tid > UINT32_MAX)
        return CLONE_BAD_MEMORY;
    if ((tid && (tid & (__alignof__(int32_t) - 1))) ||
        (stack & (__alignof__(uint32_t) - 1))) return CLONE_INVALID;
    if (!access(context, entry, 1, CLONE_ACCESS_EXEC) ||
        !access(context, stack, args->stack_size, CLONE_ACCESS_WRITE) ||
        (tls && !access(context, tls, 1, CLONE_ACCESS_WRITE)) ||
        (tid && !access(context, tid, sizeof(int32_t), CLONE_ACCESS_WRITE)))
        return CLONE_BAD_MEMORY;
    return CLONE_VALID;
}
