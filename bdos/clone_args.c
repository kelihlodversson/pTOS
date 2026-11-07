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
    out->child_pid = (int32_t *)(uintptr_t)snapshot->child_pid;
}

enum clone_copy_result clone_copy_v1(CLONE_ARGS *out, uint64_t address,
                                    uint64_t size,
                                    int (*copy)(void *, uint64_t, void *, uint32_t),
                                    void *context)
{
    CLONE_USER_V1 snapshot = { 0 };
    uint32_t known = size >= PTOS_CLONE_ARGS_PID_SIZE ? PTOS_CLONE_ARGS_PID_SIZE : PTOS_CLONE_ARGS_MIN_SIZE;
    CLONE_ARGS decoded;

    if (size < PTOS_CLONE_ARGS_MIN_SIZE || size > UINT32_MAX) return CLONE_COPY_BAD_SIZE;
    if (!address || address > UINT64_C(0x100000000) - size)
        return CLONE_COPY_BAD_ADDRESS;
    if (!copy(context, address, &snapshot, known))
        return CLONE_COPY_BAD_ADDRESS;
    clone_decode_v1(&decoded, &snapshot);
    *out = decoded;
    return CLONE_COPY_OK;
}

enum clone_validate_result clone_validate_v1(const CLONE_ARGS *args,
    const CLONE_ABI *abi, int (*access)(void *, uintptr_t, uint32_t, int), void *context)
{
    uintptr_t entry = (uintptr_t)args->entry;
    uintptr_t stack = (uintptr_t)args->stack_base;
    uintptr_t tls = (uintptr_t)args->tls;
    uintptr_t tid = (uintptr_t)args->child_tid;
    uintptr_t pid = (uintptr_t)args->child_pid;

    if ((args->flags & ~PTOS_CLONE_FLAG_MASK) ||
        ((args->flags & CLONE_SAME_PROCESS) && (!(args->flags & CLONE_VM) ||
          (args->flags & (CLONE_COPY_STANDARD_HANDLES | CLONE_COPY_CWD)))) ||
        tls || args->stack_size < abi->minimum_stack) return CLONE_INVALID;
    if (!entry || !stack || stack > UINT32_MAX ||
        (uint64_t)stack + args->stack_size > UINT64_C(0x100000000) ||
        entry > UINT32_MAX || tls > UINT32_MAX || tid > UINT32_MAX || pid > UINT32_MAX)
        return CLONE_BAD_MEMORY;
    if ((tid && (tid & (__alignof__(int32_t) - 1))) ||
        (pid && (pid & (__alignof__(int32_t) - 1))) ||
        (stack & (abi->stack_alignment - 1)) ||
        (args->stack_size & (abi->stack_alignment - 1)) ||
        (entry & (abi->entry_alignment - 1))) return CLONE_INVALID;
    if (!access(context, entry, 1, CLONE_ACCESS_EXEC) ||
        !access(context, stack, args->stack_size, CLONE_ACCESS_WRITE) ||
        (tid && !access(context, tid, sizeof(int32_t), CLONE_ACCESS_WRITE)) ||
        (pid && !access(context, pid, sizeof(int32_t), CLONE_ACCESS_WRITE)))
        return CLONE_BAD_MEMORY;
    return CLONE_VALID;
}
