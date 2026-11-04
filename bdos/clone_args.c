/* Decode a kernel-owned Pclone ABI snapshot. */
#include "clone_args.h"

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
