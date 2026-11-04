/* Decode a kernel-owned Pclone ABI snapshot. */
#include "clone_args.h"
#include "pclone.h"

void clone_decode_v1(CLONE_ARGS *out, const CLONE_USER_V1 *snapshot)
{
    out->version = snapshot->version;
    out->flags = snapshot->flags;
    out->entry = snapshot->entry;
    out->argument = snapshot->argument;
    out->stack_base = snapshot->stack_base;
    out->stack_size = snapshot->stack_size;
    out->tls = snapshot->tls;
    out->child_tid = snapshot->child_tid;
}

enum clone_copy_result clone_copy_v1(CLONE_ARGS *out, uint64_t address,
                                    uint64_t size,
                                    int (*copy)(void *, uint64_t, void *, uint32_t),
                                    void *context)
{
    CLONE_USER_V1 snapshot;
    CLONE_ARGS decoded;

    if (size != sizeof(snapshot)) return CLONE_COPY_BAD_SIZE;
    if (!address || address > UINT64_C(0x100000000) - sizeof(snapshot))
        return CLONE_COPY_BAD_ADDRESS;
    if (!copy(context, address, &snapshot, sizeof(snapshot)))
        return CLONE_COPY_BAD_ADDRESS;
    if (snapshot.version != PTOS_CLONE_VERSION_1) return CLONE_COPY_BAD_VERSION;
    clone_decode_v1(&decoded, &snapshot);
    *out = decoded;
    return CLONE_COPY_OK;
}
